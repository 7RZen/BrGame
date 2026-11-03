# Issue 8 — relative mouse aiming (UE 5.8)

## Decisions and implementation
- Keyboard/mouse only. `IA_Look` is Axis1D, bound only to raw MouseX, with
  `IGNORE_SETTINGS` and no player-mappable metadata. Y is unused. There is no
  quick-turn binding, cursor aiming, or crosshair. Existing keyboard/button/wheel
  bindings and Enhanced Input profile persistence remain intact.
- Positive X adds clockwise control yaw: `delta X * sensitivity` (degrees per
  raw axis unit, no delta-time multiplier or interpolation). Facing is updated
  immediately; ordinary CharacterMovement move packets carry control yaw to
  authority. Fire/grenade requests send a horizontal aim sampled from the owning
  client's control yaw. The server validates/accepts that aim and commits both
  control yaw and pawn rotation to it before the shot's presentation (after a
  successful launch for grenades). Damage, ammunition and cooldowns remain
  server-authoritative. The earlier statement that RPCs simply consumed the
  server's existing control yaw was incorrect; the independent review identified
  and this follow-up fixes that facing/aim mismatch.
- Downed retains controller-yaw rotation but not movement/fire. For a local
  owner the camera uses control yaw; other view targets use replicated actor
  rotation plus the existing CMC mesh smoothing rotation, removing the cached
  mesh pose rotation. The camera, minimap and positional listener agree on yaw.
- `DefaultMouseSensitivity` defaults to 0.2 degrees/unit, clamped 0.02–2.
  Settings exposes a slider. The existing machine-local `BrAudio` SaveGame now
  writes version 2 with sensitivity alongside both volumes. Version 1 preserves
  volumes and adopts the current developer default. No user save was edited
  for testing; persistence automation uses memory serialization.
- `PlayerScreenOffset` defaults to 0.25, clamped 0–0.4. The pawn origin projects
  at `(0.5, 0.5 + offset)`. Camera evaluation offsets forward horizontally by
  `OrthoWidth / viewportAspect * offset / sin(CameraPitchAngle)`, including the
  currently interpolated zoom width. The view explicitly maintains horizontal
  ortho width and uses the actual viewport aspect ratio.
- UE 5.8's automatic ortho plane correction otherwise recentres projection on
  the pawn. Adaptive planes are calculated during CalcCamera around the optical
  target, then automatic recalculation is disabled in that POV so the subsequent
  projection correction only moves along the view axis. Engine code is unchanged.
  A regression assertion exercises the actual engine projection path, including
  PlayerCameraManager overwriting CameraToViewTarget.
- Cursor/input mode is centralized in `RefreshCursorMode`: GameOnly with a
  consumed capture click in gameplay; GameAndUI/unlocked/visible for menu,
  Settings/rebinding/confirmation, full map, start page, lobby and results.
  Full map now gates gameplay actions and stops held fire/interactions.
  Native Slate deactivation and viewport key flushing handle focus loss; native
  permanent capture consumes the click on recapture. Physical OS focus behavior
  is explicitly still a manual check, not proven by a null-RHI smoke.
- Fog masks/material UVs use world positions/map bounds and friendly pawn vision
  sources, not screen centre. No fog or minimap coordinate rewrite was necessary.
  BrMatchSmoke sets control/actor yaw explicitly and remains compatible.
- Template root BrGame* classes and Variant_* were not edited. No new module
  dependencies, plugins, engine edits, or hand-edited binary assets.

## Changed paths
- `Source/BrGame/Core/BrAimingRules.h` (new)
- `Source/BrGame/Core/BrCharacter.{h,cpp}`
- `Source/BrGame/Core/BrCharacterMovementComponent.{h,cpp}`
- `Source/BrGame/Combat/BrInventoryComponent.{h,cpp}`
- `Source/BrGame/Core/BrGameMode.h` (automation-test friendship only)
- `Source/BrGame/Core/BrDeveloperSettings.h`
- `Source/BrGame/Core/BrInputConfig.h`
- `Source/BrGame/Core/BrGameInstance.{h,cpp}`
- `Source/BrGame/Core/BrAudioSettingsPanel.{h,cpp}`
- `Source/BrGame/Core/BrPlayerController.{h,cpp}`
- `Source/BrGame/Core/BrMenuController.cpp`
- `Source/BrGame/Core/BrHUD.cpp`
- `Source/BrGame/Core/BrShotSmoke.cpp`
- `Source/BrGame/Tests/BrAimingTest.cpp` (new), `BrAudioTest.cpp`
- `Scripts/Content/create_input_assets.py`, `Scripts/RunShotSmoke.ps1`
- `Content/BrGame/Input/Actions/IA_Look.uasset` (new); regenerated
  `IMC_BrDefault.uasset`, `DA_BrInputConfig.uasset` and existing action assets
  in that directory through editor Python.
- `GameDescription.md`, `Docs/PlaytestChecklist.md`, `Issues.md`, this file.

No obsolete camera setting entries were present in DefaultGame.ini or
DefaultEngine.ini. Their other/template settings were preserved.

## Initial implementation commands and results (2026-11-03)
All build/editor/test jobs were serial. Before each build,
`Get-Process UnrealEditor*,LiveCodingConsole -ErrorAction SilentlyContinue`
found no editor/Live Coding process. No user's editor was stopped.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex
```
Final full-target UBT build (not Live Coding): **Succeeded, exit 0, zero compiler
warnings**. Earlier iterations caught and corrected a controller member-shadowing
error and a test float/double overload error. Camera tests also exposed fixture
initialization/local-player requirements; these were corrected without weakening
the assertions.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -run=pythonscript -script="C:\Users\oiste\Projects\Unreal\BrGame\Scripts\Content\create_input_assets.py" -unattended -nullrhi -nosound -nosteam
```
**PASS, exit 0**. UE Python generated/saved and validated 14 actions, 16 unique
player-mappable keys plus non-mappable MouseX Look. Commandlet reported 0 errors
and the expected warning that Steam was disabled by `-nosteam`.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -log -nosteam
```
**PASS, exit 0: 67/67 tests, zero failures** in `Saved/Logs/BrGame.log`,
including all five `BrGame.Aiming.*` tests:
- mouse delta/sensitivity/sign/wrap/frame partitioning;
- forward/back/strafe and diagonal movement;
- 324 combinations of ortho width/aspect/pitch/fraction/yaw;
- real camera and engine projection, simulated visual smoothing, local yaw,
  downed facing and blocked movement;
- sensitivity memory persistence and generated non-mappable X-only input.

```powershell
.\Scripts\RunShotSmoke.ps1 -Aiming -RunLabel Pitch65 -CameraPitchAngle 65
.\Scripts\RunShotSmoke.ps1 -Aiming -RunLabel Pitch30 -CameraPitchAngle 30
.\Scripts\RunShotSmoke.ps1 -Aiming -RunLabel Pitch90 -CameraPitchAngle 90
.\Scripts\RunShotSmoke.ps1 -Menu -RunLabel Aiming
```
**All PASS, exit 0**: 18 aiming PNGs in `Saved/Screenshots/Aiming/`, three menu
PNGs in `Saved/Screenshots/Menu/*Aiming.png`. The aiming fixture explicitly sets
different facing angles instead of relying on a screen cursor. Initial pitch-65
screenshots exposed UE orthographic recentring; these were replaced after the fix.
Inspected corrected `town_streetPitch65`, `forestPitch65`,
`town_streetPitch30`, `fieldPitch90`, and `settings_bindingsAiming`.
Pawn below centre, camera-relative facing, fog/minimap alignment, absent crosshair
and the sensitivity slider/no Look row are visible. Screenshots cannot verify
physical relative-mouse capture or network rotation under latency.

```powershell
.\Scripts\RunAllTests.ps1 -SkipBuild
```
**PASS, exit 0: 12/12 steps**, with 67 automation successes and zero failures.
Logs: `Saved/Logs/RunAll/20261103-191149/` (including per-step stdout/stderr,
`steps/Automation.log` and archived smoke logs).

| Step | Result | Seconds |
|---|---|---:|
| Automation-BrGame | PASS (67 tests) | 18 |
| Flow | PASS | 42 |
| Flow-Grouped | PASS | 42 |
| Flow-Zone | PASS | 46 |
| Flow-DisconnectGrace | PASS | 28 |
| Flow-LvlTopDown | PASS | 42 |
| Fog | PASS | 31 |
| Fog-Grouped | PASS | 39 |
| Session | PASS | 67 |
| Match-AllVsAll | PASS | 94 |
| Match-Grouped | PASS | 97 |
| Menu | PASS | 33 |

Initial validation stopped after all required regression steps passed. The
independent-review follow-up below supersedes that implementation's combat-facing
claim and records its separate validation.

## Independent-review P2 follow-up (2026-11-03)
- Kept existing aim normalization/horizontality, alive/health, weapon/ammo,
  reload, fire-rate and grenade cooldown checks. Rejected actions do not rotate
  the pawn. Failed grenade spawn/launch also does not commit facing.
- Accepted fire/grenade aim is authoritative for server control yaw and actor
  yaw, with `ForceNetUpdate` for ordinary replicated facing. There is deliberately
  no angle-error rejection or turn-speed limit: fast flicks remain allowed.
- Both existing combat RPCs now also carry the current CMC client move timestamp
  as an ordering hint, checked finite, nonnegative and within the CMC reset period
  plus one second of frame overshoot. This hint does not authorize hits/damage or
  bypass native movement timestamp validation.
- A reliable combat RPC may overtake older unreliable/batched moves. Until CMC
  accepts a strictly newer move, their control rotation is replaced by the last
  accepted combat yaw; their movement/acceleration/posture and native timestamp
  validation remain untouched. Timestamp comparison accounts for CMC's periodic
  timestamp reset. A genuinely newer move immediately resumes normal yaw input,
  so there is no timed facing lock, client camera correction or multicast turn.
- Added `BrGame.Aiming.ServerAimSetsFacing`: real accepted fire and grenade RPC
  implementations with aim differing from current facing; checks control/actor
  yaw, accepted ammo/grenade consumption and bullet direction. It then executes
  actual CMC moves to check pre-shot/equal timestamp protection and newer-turn
  recovery for both actions. It also checks rejected fire/grenade cooldowns,
  invalid fire aim, invalid timestamp hints, and reset-boundary ordering.
- Changed paths for this follow-up: `Combat/BrInventoryComponent.{h,cpp}`,
  `Core/BrCharacterMovementComponent.{h,cpp}`, `Core/BrAimingRules.h`,
  `Core/BrGameMode.h` (test-only friendship), `Tests/BrAimingTest.cpp`,
  this document and `Issues.md`. No input/content regeneration is required.

Validation commands are the same full UBT, BrGame automation and
`.\Scripts\RunAllTests.ps1 -SkipBuild` commands shown above, run serially.
The editor/LiveCoding process guard found no running editor before either build.
- UBT: **PASS, exit 0, zero compiler warnings**. An initial build caught three
  test-fixture compile issues (unique-ID overload, fixture admission friendship,
  standard infinity API); corrected before the successful build.
- BrGame automation: **PASS, exit 0, 68/68 tests, zero failures** in
  `Saved/Logs/BrGame.log`, including `BrGame.Aiming.ServerAimSetsFacing`.
- Full regression: **PASS, exit 0, 12/12 steps**, with **68 automation successes,
  zero failures**. Logs: `Saved/Logs/RunAll/20261103-192810/`.

| Follow-up step | Result | Seconds |
|---|---|---:|
| Automation-BrGame | PASS (68 tests) | 18 |
| Flow | PASS | 41 |
| Flow-Grouped | PASS | 40 |
| Flow-Zone | PASS | 43 |
| Flow-DisconnectGrace | PASS | 25 |
| Flow-LvlTopDown | PASS | 39 |
| Fog | PASS | 31 |
| Fog-Grouped | PASS | 37 |
| Session | PASS | 65 |
| Match-AllVsAll | PASS | 104 |
| Match-Grouped | PASS | 97 |
| Menu | PASS | 35 |

Validation stopped after all steps passed. Only the result record and issue
status were updated afterwards; no runtime/test code or assets changed.

No new screenshots were captured for this server-only review fix. Earlier camera/
Settings screenshot checks above are unchanged; actual packet-loss/latency
presentation and extremely fast successive flick shots remain manual checks.

## Remaining manual checks / editor work
No Blueprint/map/animation/UI asset wiring remains; the input assets have been
regenerated with the built reflected LookAction property.

Use the issue-8 section in `Docs/PlaytestChecklist.md`, especially:
- Real mouse input, sensitivity feel/persistence across restart, keyboard/button
  rebinding and console Period/Tilde in gameplay and UI.
- Alt-tab while holding fire/movement; OS cursor release; click-to-recapture with
  no accidental shot, including fullscreen/windowed and after UI confirmation.
- Live host/client downed turning, late spectator target selection and smooth
  remote facing under latency/loss; fire/grenade alignment and listener direction.
- Window resizing/aspect changes and continuous wheel zoom at pitch 30–90;
  fog presentation and clipping in all areas (not just captured fixtures).
