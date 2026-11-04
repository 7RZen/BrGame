# Issue 9 — slide

## Decisions

- Press rebindable Slide, default Left Ctrl; C remains crouch. No queued slide on
  ineligible input. Threshold is current weapon-adjusted **run** speed, not base
  unarmed run. Forward cone shares `SprintForwardHalfAngle` (50°).
- Lock initial horizontal velocity direction (not facing); mouse can turn freely.
  `SlideSpeedMultiplier=1.2`, `SlideDuration=0.8 s`, final crouch speed. Default
  unarmed sprint: 936→120 cm/s, 422.4 cm; run: 720→120, 336 cm. Thus “4–5 m”
  applies to sprint, not slower run/weapon-penalized starts.
- `SlideCooldown=1 s` from **exit**, also on every early cancellation. No stamina
  cost; sprint cancels and ordinary stamina regeneration continues.
- Crouch capsule and actual Crouching posture throughout (both sides of hit chance
  and target damage). Finish crouched; press Shift again after exit to stand/sprint.
- Entry waives the sprint-to-fire deadline entirely. New fire/throw is immediately
  eligible, with normal authoritative checks. Already queued pre-slide actions
  retain their existing timer (at most the previous sprint recovery), not restarted.
- Existing movement/posture validation cancels an unstuck hold when sliding moves
  the pawn. Slide does not freeze or relocate an unstuck pawn.

## Implementation

- Existing walking CMC owns collision, stepping, ramps, floor and falling. Slide
  overrides only `CalcVelocity`, integrating the linear curve over native physics
  substeps with trapezoidal distance and ignoring steering/friction. Ends after
  duration, wall velocity below `SlideWallSpeedFraction=0.5`, leaving ground,
  downed, prone request or input block. Native crouch must succeed before activation.
- No compressed flag is stolen: Custom_0/1 remain sprint/prone, Custom_2/3 still
  report actual posture. Custom network move data adds press/input-block bits and
  end slide state for comparison only. Server derives speed/direction/eligibility
  itself using the move's original control yaw, even across the combat aim fence.
  A client input-block bit can restrict, never bypass authoritative phase/life.
- Saved moves retain request, initial/end state and historical yaw. Combining is
  disabled at requests, during slide and during cooldown. Correction snapshots
  accompany native adjustment timestamps; replay starts from authority state,
  never stale saved remaining/cooldown. Remaining time/cooldown use accepted move
  deltas, so timestamp resets need no deadline rebasing. Diagnostic start move
  timestamp is serialized but grants no permission. Protocol 11→12.
- Simulated proxies receive persistent SkipOwner slide gait plus existing posture/
  CMC location/velocity. Native animation proxy snapshots gait/settings on game
  thread; worker pose uses no actor/settings access. `SlidePoseBlendTime=.08 s`,
  lean-back pelvis/extended legs/left hand down; existing fog snap applies to slide.
- Generated original scrape `SW_Slide`, developer `SlideSound` and 1000 cm hearing
  range, existing authoritative anonymous batching/footstep concurrency. Owner
  predicts once, replay emits no sound, nonowners only hear batches. Cadence is
  suppressed both at timer gate and callback during slide. HUD gait says Slide.
- Input commandlet generates IA_Slide/Br.Slide and saves mapping/config. Existing
  rebind UI discovers the player-mappable row without a second binding system.
  No templates, plugins, dependencies, engine edits or binary hand edits.

## Validation

Commands run serially (results appended below). Every build is guarded with
`Get-Process UnrealEditor*,LiveCodingConsole -ErrorAction SilentlyContinue`;
no user's editor is terminated.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\create_input_assets.py" -unattended -nullrhi -nosound -nosteam
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\create_sound_assets.py" -unattended -nullrhi -nosound -nosteam
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -log -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
.\Scripts\RunShotSmoke.ps1 -Slide
```

`BrGame.Slide.EligibilityAndCurve`, `DirectionPostureAndCancels`,
`SavedMoveRoundTripReplay` cover eligibility, input asset/rebind row, curve,
distance integration, direction lock while turning, sprint cancellation, no stamina
charge, immediate combat gate, crouch combat posture, end/cooldown, prone/fall/down,
saved requests, correction serialization, replay idempotence, no stale cooldown
restore, noncombining start and reset-independent cooldown.

Extended Match smoke uses the existing real remote CMC road runner after sprint
exhaustion/fire validation: fresh sprint→slide, measured 390–460 cm distance, crouch
exit, a full-speed eligible press rejected inside cooldown, authority accepts an
actual mid-slide shot. Counts every received correction during 11.75–13 s including
native-rejected responses, not just applied corrections. Runs twice per match mode.

The existing production-policy `BrGame.Posture.FogRevealFirstPoseProductionTick`
also reveals a previously hidden sliding pawn without a subsequent tick: head behind
pelvis, feet forward and left hand below pelvis are asserted on the first bones.
Slide pose foot spread/height and left-hand target are developer settings too.

### Results / iteration history

- Input and sound UE Python commandlets: exit 0, zero errors, expected single
  `-nosteam` warning. Kept only new Slide assets and changed input config/mapping;
  reverted GUID/reimport churn in otherwise unchanged action/sound assets.
- Full UBT Editor target build: **Succeeded, exit 0, zero compiler warnings**,
  final source build **20.54 s**. An earlier build caught two unbraced UE_LOG/else
  macro sites in the smoke fixture; fixed normally, no warning/error suppression.
- Standalone full `Automation RunTests BrGame; Quit`: **92/92 Success, zero
  failures, exit 0**, completed **2026-11-04 10:35:37 UTC** in
  `Saved/Logs/BrGame.log`. This includes the three new Slide tests, packed move
  request/state round trip, response round trip, wall/menu cancels, crouch damage,
  and extended real-mesh fog-first-pose test.
- Initial RunAll: **12/14 PASS**, logs `Saved/Logs/RunAll/20261104-111357/`.
  The new fixture attempted a second sprint with insufficient regenerated energy,
  correctly producing a **336 cm run slide**, not the intended sprint slide.
  Moved that fresh sprint/slide one second later instead of loosening distance.
  Also replaced a coarse sprint service poll's current-stamina assumption with
  authority move-time exhaustion recording: regen can already have resumed by
  the poll. The five-second drain/exhaustion requirement is retained.
- After fixes, `.\Scripts\RunMatchSmoke.ps1 -RunLabel Slide`: **PASS, exit 0**,
  both rounds, zero sprint/slide corrections, sprint→slide **~422.5 cm**, crouched
  exit, cooldown rejection and accepted authoritative mid-slide shots.
- Final `.\Scripts\RunAllTests.ps1 -SkipBuild`: **14/14 PASS, exit 0**,
  logs **`Saved/Logs/RunAll/20261104-113548/`**; automation again **92 successes,
  zero failures**. All four remote sprint slides measured **422.4 cm**, **zero
  corrections**, crouched exits, cooldown rejections and actual mid-slide authority
  shot acceptance. Sprint validation also passed in both modes/both rounds.

| Step | Result | Seconds |
|---|---|---:|
| Automation-BrGame | PASS (92/92) | 24 |
| Flow | PASS | 44 |
| Flow-Grouped | PASS | 43 |
| Flow-Zone | PASS | 47 |
| Flow-DisconnectGrace | PASS | 27 |
| Flow-LvlTopDown | PASS | 41 |
| Maps-Selection | PASS | 41 |
| Maps-Random | PASS | 39 |
| Fog | PASS | 33 |
| Fog-Grouped | PASS | 41 |
| Session | PASS | 72 |
| Match-AllVsAll | PASS | 166 |
| Match-Grouped | PASS | 165 |
| Menu | PASS | 37 |

### Screenshots

```powershell
.\Scripts\RunShotSmoke.ps1 -Slide
.\Scripts\RunShotSmoke.ps1 -Slide -PostureOrthoWidth 1200 -RunLabel Close
```

Both commands passed, exit 0, two captures each. Opened all four PNGs under
`Saved/Screenshots/Slide/`: `slide_gameplay.png`, `slide_observer.png`,
`slide_gameplayClose.png`, `slide_observerClose.png`. Orthographic pitch 65°, yaw
37°, widths 2500/1200, actual separate remote observer process using the same view
target camera. The seated low silhouette has two extended forward legs, leaned-back
torso/head and lowered left arm; close views show the hand/leg shapes clearly.
Initial owner HUD included `Crouching | Slide (modifier ignored)`; the review
follow-up below removes that suffix for slide. Observer HUD correctly describes
its own standing pawn, not the viewed subject. The remote pose matches the owner's shape.
These are real moving slides, not frozen or duration-overridden animation fixtures.
Both captures were regenerated after the final regression using the final compiled
code, and all four replacement images were opened again. Final capture logs:
`Saved/Logs/MP/Shot/`, `Saved/Logs/MP/ShotClose/`. All validation was serial;
no editor/Live Coding process was present for builds, no user's editor was killed,
and no launched game/editor process remained after capture cleanup.

## Changed paths

- `Source/BrGame/Core/BrCharacterMovementComponent.{h,cpp}`, `BrMovement.h`,
  `BrCharacter.{h,cpp}`, `BrCharacterAnimInstance.cpp`, `BrDeveloperSettings.h`,
  `BrInputConfig.h`, `BrHUD.cpp`, `BrAudioRules.h`, `BrAudioSubsystem.cpp`,
  `BrSessionRules.h`, `BrSprintSmoke.cpp`, `BrPostureShot.cpp`, `BrShotSmoke.cpp`.
- `Source/BrGame/Combat/BrInventoryComponent.cpp` (development smoke acceptance log).
- `Source/BrGame/Tests/BrSlideTest.cpp` (new), `BrPostureVisualTest.cpp`.
- `Scripts/Content/create_input_assets.py`, `create_sound_assets.py`,
  `Audio/SW_Slide.wav` (new original placeholder); `Scripts/RunMatchSmoke.ps1`,
  `RunShotSmoke.ps1`, `SlideValidation.md` (new).
- Editor-generated `Content/BrGame/Input/Actions/IA_Slide.uasset`,
  `Input/DA_BrInputConfig.uasset`, `Input/IMC_BrDefault.uasset`,
  `Content/BrGame/Audio/SW_Slide.uasset`.
- `GameDescription.md`, `Docs/PlaytestChecklist.md`, `Issues.md` point 9.
  Preserved pre-existing Issues.md point 4.1 edit.

## Remaining manual checks

- Physical Ctrl/rebinding/persistence/reset, crouch clearance, steep slopes,
  stairs/walls/ledges and arbitrary mouse turns, very low frame rate.
- Latency/jitter/loss, reordered combat/movement, rapid cancellations, down/revive
  and teardown; full rendered fog reveal while sliding/late relevancy.
- Human audio perception/10 m attenuation and concurrency in crowds. NullRHI and
  nosound tests cannot validate physical input, GPU first-reveal or perceived sound.
- No new authored animation/Blueprint/map work is required; placeholder art/audio
  can be replaced via developer settings.

## Independent-review follow-up — replay wire facing and HUD

- Confirmed UE 5.8 `FSavedMove_Character::PostUpdate` overwrites
  `SavedControlRotation` from the live controller in both Record and Replay.
  Native `FCharacterNetworkMoveData::ClientFillNetworkMoveData` subsequently uses
  that field for retransmitted movement, not the preserved prediction-only yaw.
- `FBrSavedMove::PostUpdate` now retains the originally recorded **full control
  rotation** across the native Replay call. Record still samples native control
  rotation normally, and native end location/base/root-motion/state updates remain
  intact. No live controller changes, new wire fields or protocol changes. Both
  sprint and slide retransmit the historical facing used by their prediction.
- Added `BrGame.Slide.ReplayPreservesSprintAndSlideWireFacing`: for sprint and
  slide, forward-valid and backward-invalid starts, Record → turn live yaw/pitch/
  roll → restore pre-start state → Prep/replay/PostUpdate → serialize/decode an
  **OldMove** → actual authority pre-movement eligibility. Repeats with two newer
  live rotations, checks full recorded rotation survives, live rotation stays
  untouched, native quantization tolerance, matching accepted/rejected gaits and
  replay context cleanup. Decoded client end state never grants authority state.
  This is a deterministic replay/wire regression, not packet-loss emulation.
- HUD now suppresses `(modifier ignored)` during slide: **`Crouching | Slide`**.
  Non-slide low-posture labels are unchanged.
- Review-specific changed paths: `Source/BrGame/Core/BrCharacterMovementComponent.cpp`
  and `.h` (test friendship only), `BrHUD.cpp`,
  `Source/BrGame/Tests/BrSlideTest.cpp`, this document. No asset regeneration needed.

Validation uses the exact guarded UBT / full automation / RunAll commands above,
serially. No editor or LiveCodingConsole was running before the full build:
- UBT Editor target: **Succeeded, exit 0, zero compiler warnings, 17.63 s**.
- Standalone full BrGame automation: **93/93 Success, zero failures, exit 0**,
  completed **2026-11-04 10:57:44 UTC**, `Saved/Logs/BrGame.log`.
  The new replay-to-wire authority regression passed.
- `.\Scripts\RunAllTests.ps1 -SkipBuild`: **14/14 PASS, exit 0**,
  logs **`Saved/Logs/RunAll/20261104-115747/`**; automation again **93/93**,
  zero failures. Four remote slides measured **422.4–422.5 cm**, each with zero
  corrections, crouched exit, cooldown rejection and accepted mid-slide shots.

| Review validation step | Result | Seconds |
|---|---|---:|
| Automation-BrGame | PASS (93/93) | 24 |
| Flow | PASS | 39 |
| Flow-Grouped | PASS | 40 |
| Flow-Zone | PASS | 47 |
| Flow-DisconnectGrace | PASS | 28 |
| Flow-LvlTopDown | PASS | 45 |
| Maps-Selection | PASS | 36 |
| Maps-Random | PASS | 37 |
| Fog | PASS | 34 |
| Fog-Grouped | PASS | 43 |
| Session | PASS | 72 |
| Match-AllVsAll | PASS | 161 |
| Match-Grouped | PASS | 159 |
| Menu | PASS | 34 |

After regression, reran both screenshot commands above serially: **PASS, exit 0**,
two captures each. Opened replacement `slide_gameplay.png` and
`slide_gameplayClose.png`: both now visibly read **`Crouching | Slide`**, without
the suffix. Remote captures were regenerated too. Logs remain in
`Saved/Logs/MP/Shot/` and `ShotClose/`; PNGs in `Saved/Screenshots/Slide/`.
No editor/Live Coding process remained after script cleanup. No user's editor was
terminated. No additional asset/editor work or blockers; exploratory latency/loss,
physical input and audio checks above remain manual.
