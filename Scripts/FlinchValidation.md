# Issue 7 — hit feedback

## Decisions and implementation
- Developer-only `UBrDeveloperSettings` → HitFeedback contains bullet/grenade camera
  angles/durations, grenade translation, HUD duration/cap/flash, body impulse/duration,
  overlay asset/color/opacity. No player preference or host combat override was added.
- `FBrServerBullet::Origin` captures the authoritative launch position, independently
  of projectile travel and subsequent shooter movement. Native `FBrDamageEvent` carries
  that origin (or grenade `ExplosionCenter`) server-side only.
- `ABrCharacter::TakeDamage` compares health before/after normal damage delegates.
  Only positive bullet/grenade health loss on a still-alive victim emits cosmetics.
  **The downing hit is excluded consistently**, as are already-downed, invulnerable,
  missed-roll, zero-damage and zone events. Friendly fire that damages is included.
- Replicated default `UBrHitFeedbackComponent`: reliable owner-only damage notification
  contains quantized origin, grenade flag and byte damage; no attacker reference/identity.
  Observer reaction uses a small unreliable multicast containing only a quantized unit
  impulse on the victim's actor channel. It has no persistent hit state to replay to
  late joiners. Server origin path has no client-callable server feedback RPC.
- Local view modification happens after normal `CalcCamera`, never on control rotation,
  actor facing, aim or movement. Spectators/unowned view targets are excluded.
  Rotation is a **single camera-local roll about the view axis**: the total orientation
  displacement is bounded by the profile's angle at every camera pitch, not one full
  amplitude each for yaw and roll.
  Repeated hits replace random seeds and refresh the strongest active profile with
  bounded amplitudes, never add impulses. Grenade shake does not scale with distance.
- Canvas edge strips and arcs need no UI assets. World direction projects through the
  rendered camera rotation (yaw, pitch and roll) each frame. Arc placement intersects
  a screen-edge rectangle from the actual projected pawn, not viewport centre.
- Remote impulse is additive to the life mesh pose through `ApplyMeshPose` and
  `CacheInitialMeshOffset`, including simulated proxies. Each reaction update/expiry
  preserves the mesh's translation and quaternion-rotation residual relative to the
  previous cached base before replacing that base. Thus CMC's already-smoothed mesh
  is retained, including frames where CMC skips its visual update. Life/crouch callbacks
  retain the issue 1 absolute downed-pose path. Expiry/downing/visibility loss
  restores base pose and previous overlay. Existing character tick is reused; no new
  component tick, timers, delegates, audio path, plugin or module dependency.
- Server fog relevancy limits remote delivery; `BrFogComponent::LocallyVisible` gates
  receipt and each presentation frame, cancelling pending hits on visibility loss.
  No hidden reaction is queued for reveal. This is visual gating, unlike anonymous
  positional audio, which intentionally travels through fog.

## Material asset
Ran editor Python `Scripts/Content/create_hit_feedback_assets.py`, not system Python.
It inspected `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`: both
`MI_Manny_01_New` and `MI_Manny_02_New` expose `Paint Tint`, `LogoTint`, `Blend Offset`,
but no dedicated hit parameter. Rather than overwrite clothing/team tint, it creates
`Content/BrGame/Feedback/M_HitOverlay.uasset`: unlit translucent red overlay with
`HitColor`/`HitOpacity` parameters. Each pawn owns a dynamic instance; the original
overlay is restored. Missing assets explicitly log `LogBr: Error`.

## Validation commands and results
Engine root: `C:\Program Files\Epic Games\UE_5.8`.
All validation is serial; editor/LiveCoding processes are checked before UBT.
Only script-launched smoke PIDs are stopped.

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\create_hit_feedback_assets.py" -unattended -nullrhi -nosound -nosteam
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -log -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
.\Scripts\RunShotSmoke.ps1 -HitFeedback
```

- Overlay commandlet: exit 0, `HIT FEEDBACK ASSET PASS`. Expected `-nosteam` warning
  and first-run nonexistent-asset lookup warning; no commandlet errors.
- Initial compile errors (logging macro braces, float overloads, shadowing) and an
  uninitialized-local warning were corrected; full UBT target build then succeeded
  with **zero compiler warnings**.
- First automation attempt exposed an incomplete test fixture (no `ULocalPlayer`,
  so the owner gate correctly rejected it). Fixed the fixture, not runtime gating.
- Automation: **71/71 passed**, including `BrGame.HitFeedback.*` (3 tests); exit 0,
  `Saved/Logs/BrGame.log`. Coverage: bounds/profile selection/refresh/cap, unchanged
  control rotation, spectator gate, direction/edge math across yaw/pitch/pawn offsets,
  and damage/visibility eligibility.
- First regression: **12/12 PASS**, exit 0. Automation 71/71; Flow,
  Flow-Grouped, Flow-Zone, Flow-DisconnectGrace, Flow-LvlTopDown, Fog,
  Fog-Grouped, Session, Match-AllVsAll, Match-Grouped, Menu all passed.
  Logs: `Saved/Logs/RunAll/20261103-202032/`.
- Both match smokes passed both rounds' owner and remote observer assertions:
  Stage 1 owner events=1, observer events=0; Stage 2 owner events=2,
  observer events=1. Each owner probe asserts exactly one unexpired indicator
  with origin `(-5000,-6500,100)`.
- First screenshot run: exit 0, three files inspected. Red arcs/edge flash appeared,
  but immediate teleport capture left stale fog on the first frame and captured
  before the remote overlay presentation tick. Corrected **only the screenshot
  fixture**: settle before each hit, capture after the next presentation tick,
  disable motion blur only in this screenshot process. Rebuilt with zero warnings;
  automation again 71/71, exit 0. Final regression/recapture results below.
- Final binary regression: **12/12 PASS**, exit 0, including 71/71 automation
  and both two-round match probes. Logs:
  `Saved/Logs/RunAll/20261103-203252/` (automation log in `steps/Automation.log`).
- Rendering log inspection caught the overlay's missing skeletal-mesh usage flag,
  which null-RHI tests cannot detect. Fixed `used_with_skeletal_mesh` in the
  generator and regenerated the asset: exit 0, zero errors, only the expected
  `-nosteam` warning. No C++ change after the final regression.
- Final `RunShotSmoke.ps1 -HitFeedback`: exit 0, **3/3 screenshots saved and
  inspected**: `Saved/Screenshots/HitFeedback/bullet.png`, `grenade.png`,
  `turned_multi.png`. Settled fog, one arc/red edges on bullet/grenade, two
  correctly repositioned arcs after turning. Red observer mesh overlay is visible
  in grenade/turned_multi; owner retains its normal team color. First bullet
  frame did not catch the short observer tint during initial shader warmup.
  Final Shot logs contain no missing-usage/material/skeletal-mesh warnings or
  `LogBr: Error`. Motion comfort/impulse timing still require live playtesting.

## Network and screenshot fixtures
- `RunMatchSmoke.ps1` now exercises two native server damage events in each of two
  rounds: a remote victim receives exactly one live indicator with the captured origin;
  a remote observer first receives zero reactions while fog-hidden, then one after
  moving into actual LOS/relevancy. Client assertions use local presentation counters,
  not server-side predicted counts. Both grouped/all-vs-all runs include this fixture.
- `RunShotSmoke.ps1 -HitFeedback` requests `bullet`, `grenade`, `turned_multi`
  gameplay-camera screenshots under `Saved/Screenshots/HitFeedback/`.

## Independent-review P2 corrections (2026-11-03)
Both reported issues are corrected and covered by regression assertions, without
changing gameplay/network contracts or assets:

1. **CMC smoothing during body reaction.** `ABrCharacter::Tick` now opts into
   smoothing-preserving pose composition. `ApplyMeshPose` captures
   `MeshRelativeLocation - PreviousBaseTranslation` and
   `MeshRelativeQuat * PreviousBaseQuat.Inverse()` before caching the new base,
   then composes those residuals onto the new base. Reaction refresh/decay/restoration
   therefore cannot replace CMC's smoothed capsule-relative pose or accumulate jerk.
   Prediction offsets/interpolation state are never modified. The absolute
   life/crouch pose callbacks and `CacheInitialMeshOffset` issue 1 fix remain intact.
2. **Combined camera angle.** Removed the full-amplitude world-yaw addition;
   only view-axis roll is applied. This cannot double screen-plane twist at
   pitch 90°. Grenade translation and all existing profile settings are unchanged.

Changed for this review:
`Source/BrGame/Core/BrCharacter.{h,cpp}`,
`Source/BrGame/Core/BrHitFeedbackComponent.{h,cpp}`,
`Source/BrGame/Core/BrCharacterMovementComponent.h` (test friendship only),
`Source/BrGame/Tests/BrHitFeedbackTest.cpp`, this document and `Issues.md` point 7.
No engine/template/asset/editor work was needed.

Tests:
- New `BrGame.HitFeedback.PreservesNetworkSmoothing` drives UE 5.8's actual
  `SmoothClientPosition_UpdateVisuals` in an isolated standalone fixture for
  **linear and exponential** modes, with nonzero translation/rotation offsets.
  It verifies the first impulse, repeated CMC→owner ticks, changing corrections
  and decaying impulses, expiry without another CMC update, untouched prediction data,
  exact cached-base restoration, the next CMC write without a permanent reaction
  offset, and the original downed base height/pitch.
- Extended `BoundedCameraProfilesAndControlRotation`: deterministic ±1 seeds,
  both bullet/grenade amplitudes, pitches 30°/65°/90°. Checks quaternion angular
  distance and screen-right basis angle between unshaken/shaken views against
  the **total** configured amplitude, unchanged view-forward axis and control rotation.

Executed serially using the build/automation/regression commands above:
- Process check before build: no UnrealEditor/UnrealEditor-Cmd/LiveCodingConsole
  running; no user processes stopped.
- Full `BrGameEditor Win64 Development -WaitMutex` UBT build: **PASS**, exit 0,
  **zero compiler warnings** (23 actions, no Live Coding).
- `Automation RunTests BrGame; Quit`: **72/72 PASS**, exit 0; HitFeedback now
  has four tests. Results in `Saved/Logs/BrGame.log`.
- `RunAllTests.ps1 -SkipBuild`: **12/12 PASS**, exit 0. Automation **72/72**;
  Flow, Flow-Grouped, Flow-Zone, Flow-DisconnectGrace, Flow-LvlTopDown, Fog,
  Fog-Grouped, Session, Match-AllVsAll, Match-Grouped and Menu all passed.
  Logs: `Saved/Logs/RunAll/20261103-204958/`. Both two-round match smokes
  retain the non-owner DOWNED/REVIVED mesh-pose assertions as well as hit probes.
- Earlier screenshots above predate these two review corrections; no new screenshot
  or physical latency/comfort validation is claimed for this review.

## Remaining manual checks
`Docs/PlaytestChecklist.md` → Issue 7: actual motion/comfort, moving shooter origin,
mouse turning during feedback, pitch/aspect/zoom combinations, remote smoothing/postures,
fog boundary handoffs, spectator switching, damage exclusions, packet loss and ~60-player
bandwidth. Static screenshots cannot establish motion comfort or network-stress behavior.
No Blueprint/map/animation/UI edit is required. Placeholder art may be replaced through
the developer overlay setting; do not hand-edit binary assets.

## Changed paths
- `Source/BrGame/Core/BrHitFeedbackComponent.{h,cpp}`, `BrHitFeedbackRules.h`,
  `BrHitFeedbackSmoke.cpp` (new).
- `Source/BrGame/Core/BrCharacter.{h,cpp}`, `BrDeveloperSettings.h`, `BrHUD.cpp`.
- Smoke hooks: `Source/BrGame/Core/BrGameMode.h`, `BrPlayerController.h`,
  `BrMatchSmoke.cpp`, `BrShotSmoke.cpp`.
- `Source/BrGame/Combat/BrDamageSource.h`, `BrInventoryComponent.{h,cpp}`,
  `BrGrenade.cpp`.
- `Source/BrGame/Tests/BrHitFeedbackTest.cpp` (new).
- `Scripts/Content/create_hit_feedback_assets.py` (new),
  `Content/BrGame/Feedback/M_HitOverlay.uasset` (editor-generated new asset).
- `Scripts/RunMatchSmoke.ps1`, `Scripts/RunShotSmoke.ps1`,
  `Scripts/FlinchValidation.md` (new), `GameDescription.md`,
  `Docs/PlaytestChecklist.md`, `Issues.md` point 7.
- No template, engine, module/plugin/config, map or Blueprint modifications.
  Existing unrelated `Issues.md` edits were preserved.
