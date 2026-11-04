# Issue 14 — procedural crouch/prone presentation

## Decisions and implementation

- Pure runtime C++, no new animation/Blueprint assets, plugin or module dependencies.
  Template source and template assets are untouched.
- `UBrCharacterAnimInstance` owns GC-visible loaded assets; its custom proxy copies
  posture, velocity and developer values on the game thread in `PreUpdate`.
  Worker evaluation reads only that snapshot, the required bone container and immutable
  engine animation assets. No actor/settings access from the worker.
- The engine's existing unarmed `BS_Idle_Walk_Run` is sampled directly, preserving
  directional idle/walk/run (direction X, speed Y, confirmed via UE Python).
  It does not evaluate/copy the template ABP's event graph.
- Crouch retains the locomotion cycle, lowers pelvis 65 cm and moves it forward
  22 cm at default capsule height, with 12° hip lean plus 38° additional spine lean.
  Analytic hip/knee chains retain sampled foot height/cycle and rotation, adding
  8 cm lateral foot spacing per side and an outward/forward knee pole ratio of 1.2.
  The resulting deep, wide squat exposes bent knees beyond the torso at 65° instead
  of hiding them underneath an almost-standing silhouette. No bone stretching/scaling.
- Prone uses idle as its base and rotates the pelvis -90° around component X:
  mannequin +Y is facing, so the head goes forward and the face down. Pelvis height
  is 24 cm. Analytic limb targets spread knees/elbows and point arms forward; opposing
  sinusoidal arm/leg targets scale with velocity. Phase stops at rest (idle breathing
  remains). No physical prone room/wall test.
- Component-space edits are converted back to local poses and blended with normalized
  smoothstep posture weights (0.25 s crouch, 0.4 s prone, including exits; interrupted
  blends start from their current weights). Bone names are cached on required-bone
  changes; missing skeleton bones warn at initialization, missing LOD chains skip.
- Whole-mesh transform is **not** rotated for prone. `ApplyMeshPose`,
  `CacheInitialMeshOffset`, native crouch base-height restoration, life presentation
  and additive hit reaction remain independent. Downed/dead still switch out of this
  anim instance and use their original back-lying transform.
- Actual weapon inspection found a capsule-root attachment, **not** a hand socket.
  The cosmetic block now attaches to `hand_r`; game-thread post-evaluation keeps its
  X-long barrel along smoothed character facing, independent of mannequin hand axes.
  No shot/throw origin, collision, ammo or server combat logic changes.
- Posture uses persistent replicated CMC state on simulated proxies (owners still use
  prediction); no animation replication/RPC or gameplay authority changes.
  First update starts at current posture; hidden/URO gaps catch up the blend with world
  elapsed time. Mesh uses URO and `OnlyTickPoseWhenRendered`.
- Fog reveal explicitly calls `PrepareForFogReveal` **before** exposing primitives:
  complete any prior parallel task, request a posture-target snap, bypass URO for one
  zero-delta synchronous animation update/bone refresh, restore URO, then unhide.
  This also refreshes the hand-attached weapon. Already-visible characters are not
  re-evaluated by fog; hidden evaluation remains visibility-gated. Life mesh transforms
  are untouched. Unlike match/screenshot fixtures, the fog regression uses production
  visibility/URO policies and proves bone finalization occurred while still hidden.
- Per-character sample caches reserve once. Pose scratch uses Unreal's animation
  memstack; no project-owned per-frame heap buffers, traces, new actor Tick or timers.
  This is not a measured 60-player performance claim.

## Developer settings / future animations

`Config/DefaultGame.ini` and the settings CDO now select
`/Script/BrGame.BrCharacterAnimInstance` as `DefaultCharacterAnimClass`.
`PostureLocomotion` defaults to the existing unarmed blendspace.
`CrouchPoseBlendTime` = 0.25 s; `PronePoseBlendTime` = 0.4 s (clamped 0–2 s).
All six main crouch silhouette amounts above are developer settings under
`Character|Posture`: `CrouchPelvisDrop`, `CrouchPelvisForward`, `CrouchHipLean`,
`CrouchTorsoLean`, `CrouchKneeOutward`, `CrouchFootSpread`. Pelvis drop scales with
capsule reduction relative to the default 96 cm standing / 60 cm crouching half-heights.

Optional `CrouchIdle`, `CrouchMove`, `ProneIdle`, `ProneMove` accept non-additive
full-body **in-place** sequences or blendspaces on the exact character skeleton.
Author these with feet/body at the intended ground plane, without root travel.
Blendspace axes must match the existing convention (direction degrees X, speed cm/s Y).
The idle/move crossfade scales over 0–35 cm/s and smooths over 0.2 s.
When both slots are supplied, they fully replace the procedural posture; with one slot
supplied the other end falls back procedurally. Missing/invalid optional assets warn
and use procedural targets; missing/incompatible base locomotion logs an error.
No actual crouch/prone animation assets currently exist; those slots stay empty.

## Repeatable commands

Before **every** UBT build, reject any running `UnrealEditor*` or `LiveCodingConsole`
process. No user's editor is terminated. Smoke scripts only stop PIDs they launch.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" .\BrGame.uproject "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -log -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
.\Scripts\RunShotSmoke.ps1 -Posture
.\Scripts\RunShotSmoke.ps1 -Posture -PostureOrthoWidth 1200 -RunLabel Min
.\Scripts\RunShotSmoke.ps1 -Posture -PostureOrthoWidth 4000 -RunLabel Max
```

`BrGame.Posture.*` covers target selection/life precedence, exact blend timing,
interrupted weight bounds, real Manny crouch foot world Z vs capsule bottom
(idle and moving), prone head height/horizontal facing at yaw 0/37/125 and speed
0/120, finite bounded bone offsets, and unchanged downed base/anim-mode isolation.
The real-mesh fixture explicitly updates component-to-world after animation refresh;
without the normal component tick a stale cached transform invalidates measurements.
At 65°, the real-mesh test also requires forward camera-plane head shift >=18 cm,
shoulder midpoint shift >=8 cm and each knee outside torso half-width by >=6 cm.
Measured: **21.4 cm / 15.7 cm**, knee half-width **47.2 cm** vs torso **18.4 cm**.
These world-unit projected metrics are independent of orthographic zoom.

`BrGame.Posture.FogRevealFirstPoseProductionTick` establishes standing, then repeats
hide/change/reveal for prone, crouch and standing using the production fog helper.
It checks hidden ticks retain cached bones, forces URO skip flags, and checks target
bones immediately on reveal with NO subsequent tick/refresh. A bone-finalization
delegate proves evaluation precedes unhide; another visible update performs no
evaluation. Production policies remain unchanged. First head Z (mesh space) measured
**18.2 / 65.0 / 159.7 cm** respectively.

Match smoke now holds revived crouch/prone/standing for 1.8 s each instead of
changing all three in one service call. Listen host **and** late-joining non-owner
Client3 must each report CROUCH/PRONE head-height PASS twice (one per round), in
addition to sustained DOWNED/REVIVED base-offset assertions. Crouch head maximum
135 cm, prone maximum 45 cm above capsule bottom, minimum 5 cm.
The fixture sends a gated, development-only owning-client intent RPC, then uses the
normal predicted CMC move path. A first full regression run timed out on both new
low-pose markers because server-only fixture requests were immediately overwritten
by the owner's standing movement packets; that fixture error is corrected.

## Screenshot inspection

`RunShotSmoke.ps1 -Posture` captures five postures from both the owner gameplay
camera and an actual separate remote observer process, via the same orthographic
view target (pitch 65°, yaw 37°, 1280×720). The default capture now uses actual default
width **2500**, with separate **1200 minimum** / **4000 maximum** width runs.
Output: `Saved/Screenshots/Posture/{standing,crouch_idle,crouch_move,prone_idle,prone_move}_{gameplay,observer}.png`.
Minimum/maximum runs append `Min` / `Max` before `.png`; 30 captures regenerated.
The lower character is the test subject; the upper standing character is the observer.
Observer HUD still describes its own standing pawn, not the viewed subject.

The ten default captures plus standing/crouch-idle/crouch-move in both roles at
both min/max zoom (22 raw frames total) were opened and compared:
- Standing: tall body/longer shadow, unarmed base stance, weapon on right hand.
- Crouch idle: obvious broad, deep squat versus the narrow standing body; both bent
  knees project outside the torso, with a lowered/forward head and sloping back.
  Feet stay grounded and the right-hand weapon stays forward. The minimum-width
  close view makes the hip/knee bends particularly clear.
- Crouch moving: retains that wide bent silhouette with asymmetric leg steps and
  arm swing. Remote captures match, with independent animation phase. At maximum
  width characters are small, but the crouch's lateral knee silhouette remains
  visibly wider than standing; fine joint detail naturally diminishes.
- Prone idle: long face-down/back-visible silhouette on ground, head toward screen-up,
  bent/outward elbows and knees, forward right-hand weapon; distinct from back-lying downed.
- Prone moving: opposing raised/reaching arm and tucked/extended leg visibly change
  from idle; body stays flat and facing forward.
- Remote versions show matching low/flat silhouettes and weapon position, with
  expected independent animation phase differences in crouch walk.

Earlier screenshot attempts correctly failed because the remote standing capture
was missed and the observer used AActor's inactive-camera perspective fallback.
The fixture now waits longer for initial relevancy, reasserts its view target through
ClientRestart, and activates the actual orthographic spectator camera before capturing.
An initial automation attempt also exposed the test fixture's stale world transform;
that fixture was fixed, not the height thresholds relaxed.

## Remaining manual checks

- Physical input/rebinding and rapid reversals; camera pitch 30/90, arbitrary yaw/zoom;
  backwards/strafe crouch, ramps/stairs/slopes, animation LOD transitions.
- Network emulation/loss, late join already prone, real rendered fog destruction/
  re-reveal and spectator cycling. The production-policy first-reveal CPU regression
  is automated; no GPU first-frame capture or physical spectator playtest is claimed.
- Low-posture bullet/grenade jerk and down/revive/eliminate across all transitions;
  all weapon cosmetic sizes. The placeholder gun block is not a rigged firearm.
- Profile 60 rendered versus fog-hidden characters with animation insights/URO;
  no 60-character benchmark has been run.
- Real optional animations need authoring and editor playtest if desired, but none
  are required for this implementation. No Blueprint/map/UI asset work is required.

## Final review-fix validation results

2026-11-04, UE 5.8.4 / Win64 Development Editor:

- Both P2 findings addressed: stronger developer-tunable crouch silhouette and
  synchronous posture-target evaluation before fog unhide. The original 50 cm /
  22° crouch passed height tests but failed visual readability; the new camera-plane
  metrics and multi-zoom captures cover that gap. Elapsed-time catch-up alone was
  insufficient for the cached first visible frame, hence the explicit reveal boundary.
- Guarded full UBT editor build using the exact command above: **Succeeded**, exit
  **0**, **zero compiler warnings**, **3.99 s**. No editor/LiveCoding process was
  running. An earlier C4458 local-name shadow was fixed, not suppressed.
- Required standalone `UnrealEditor-Cmd.exe ... Automation RunTests BrGame; Quit`:
  exit **0**, **88/88 success**, **0 failures**, including four `BrGame.Posture.*`
  tests. Automation completed **08:16:51 UTC** in `Saved/Logs/BrGame.log`.
  Crouch head **65.9 cm**, feet **8.6 / 8.2 cm** above capsule bottom; projected
  silhouette and production fog first-pose measurements are documented above.
- Immediately afterward, `.\Scripts\RunAllTests.ps1 -SkipBuild`: exit **0**,
  **14/14 PASS**, including another **88/88 automation**, both fog modes and both
  two-round match/non-owner posture checks:
  Automation-BrGame, Flow, Flow-Grouped, Flow-Zone, Flow-DisconnectGrace,
  Flow-LvlTopDown, Maps-Selection, Maps-Random, Fog, Fog-Grouped, Session,
  Match-AllVsAll, Match-Grouped, Menu. Match steps **150 / 152 s**.
  Logs: **`Saved/Logs/RunAll/20261104-091657/`**.
- Before final build/automation/regression, the three screenshot commands above each
  exited **0**, **10/10 captures** at widths **2500 / 1200 / 4000**. Inspection is
  described above (22 frames actually opened, not a claim that all 30 were opened).
  Logs: `Saved/Logs/MP/Shot/`, `ShotMin/`, `ShotMax/`.
- Validation commands ran serially; stopped after all 14 steps passed. No required
  asset/editor work or blockers. Remaining exploratory/manual checks are listed above.

Review-fix changed paths specifically: `BrCharacter.cpp/.h`,
`BrCharacterAnimInstance.cpp/.h`, `BrDeveloperSettings.h`, `BrFogComponent.cpp/.h`,
`BrCharacterMovementComponent.h` (test friendship), `BrPostureShot.cpp`,
`Tests/BrPostureVisualTest.cpp`, `Scripts/RunShotSmoke.ps1`, this document and
`Issues.md` point 14. Full original implementation manifest follows below.

## Original implementation validation (superseded by review fixes)

2026-11-04, UE 5.8.4 / Win64 Development Editor, no Live Coding:

- Final full UBT `BrGameEditor Win64 Development -Project=... -WaitMutex`:
  **Succeeded**, exit 0, **zero compiler warnings**, 4.70 s. All prior editor/smoke
  processes had exited before building. Earlier compile errors (function placement
  and a controller member-name shadow in the dev fixture) were fixed, not suppressed.
- Required `UnrealEditor-Cmd.exe ... "-ExecCmds=Automation RunTests BrGame; Quit"
  -unattended -nullrhi -nosound -log -nosteam`: exit **0**, **87/87 success,
  0 failures**, including all three `BrGame.Posture.*` tests.
  `Saved/Logs/BrGame.log` (automation end 07:51:27 UTC).
  Real mesh: crouch head **101.8 cm** vs standing **160.0 cm**; feet **8.6/8.2 cm**
  above capsule bottom. Prone head **17.4–19.2 cm** across tested speeds/yaws.
- `.\Scripts\RunAllTests.ps1 -SkipBuild`: exit **0**, **14/14 PASS**:
  Automation-BrGame (87 tests), Flow, Flow-Grouped, Flow-Zone,
  Flow-DisconnectGrace, Flow-LvlTopDown, Maps-Selection, Maps-Random, Fog,
  Fog-Grouped, Session, Match-AllVsAll, Match-Grouped, Menu.
  Logs: **`Saved/Logs/RunAll/20261104-085131/`**, including per-step output
  and copied smoke logs. Match steps completed in 156/155 s. Both host and Client3
  passed CROUCH/PRONE non-owner assertions twice, alongside DOWNED/REVIVED,
  sprint, damage-feedback, audio and existing combat/respawn checks.
- Focused `.\Scripts\RunMatchSmoke.ps1 -RunLabel PostureFix`: exit **0**,
  two rounds PASS. Example host/observer crouch head **100.73/100.72 cm**,
  prone **18.88/18.86 cm**. Logs `Saved/Logs/MP/MatchPostureFix/`.
- Final `.\Scripts\RunShotSmoke.ps1 -Posture`: exit **0**, **10/10** captures,
  all opened/inspected as described above; logs `Saved/Logs/MP/Shot/`.
  Initial labelled captures are diagnostic leftovers, not the final evidence.

The final build, standalone automation command, then full regression were run
serially. Validation stopped after all 14 steps passed. No packaging/60-player
benchmark/physical-input playtest is claimed.

## Changed paths

- `Config/DefaultGame.ini`
- `Source/BrGame/Core/BrCharacter.cpp`, `BrCharacter.h`
- `Source/BrGame/Core/BrFogComponent.cpp`, `BrFogComponent.h`
- `Source/BrGame/Core/BrCharacterAnimInstance.cpp`, `BrCharacterAnimInstance.h` (new)
- `Source/BrGame/Core/BrPostureVisual.h`, `BrPostureShot.cpp` (new)
- `Source/BrGame/Core/BrDeveloperSettings.cpp`, `BrDeveloperSettings.h`
- `Source/BrGame/Core/BrCharacterMovementComponent.h` (test friendship only)
- `Source/BrGame/Core/BrGameMode.h`, `BrPlayerController.h`, `BrMatchSmoke.cpp`, `BrShotSmoke.cpp`
- `Source/BrGame/Tests/BrPostureVisualTest.cpp` (new)
- `Scripts/RunMatchSmoke.ps1`, `Scripts/RunShotSmoke.ps1`
- `Scripts/Content/InspectPostureAssets.py` (new read-only UE Python inspection)
- `Scripts/PostureVisualValidation.md` (new), `Docs/PlaytestChecklist.md`,
  `GameDescription.md`, `Issues.md` (only Issue 14 status changed here;
  pre-existing issue-list edits preserved).
