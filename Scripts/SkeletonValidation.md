# Issue 3 — face-up downed players and corpses

The old `(Pitch=90,Yaw=-90,Roll=0)` rotated around the mannequin's
forward axis and put it on its side. `BrSupinePose` instead maps mesh +Y
(face) to actor +Z and mesh +Z (feet-to-head) to actor +X:
`FRotator(0,90,-90)`. Actor yaw remains the victim's facing.

Both use the same reference-skin grounding: lift by `BoxExtent.Y-Origin.Y`
from the mesh's imported bounds plus 0.5 cm clearance. Character offsets
subtract current capsule half-height; corpse actors spawn at capsule bottom,
including offline-record corpses (the former extra 10 cm spawn lift is removed).
Arms-out pose, dormant replication, no corpse tick/collision remain unchanged.
No assets/template files or reflection layouts changed.

Life presentation already switches to an empty single-node animation for downed
players. There is no downed crawl/help animation to modify: HELP is a pulsing,
root-attached text component. Living procedural prone remains face down, separate
from the corrected life transform. `ApplyMeshPose` still caches the complete base
for CMC and preserves smoothing residuals/hit-reaction composition.
Screenshot review also exposed the old world-fixed HELP text pointing upside down.
Its ground-parallel rotation now follows the local camera's yaw (plus 180° for
TextRender's -Y reading direction), within the existing downed tick. It remains
root-attached, pulsing, and independent of body rotation; no camera yaw replication.

## Validation

Do not build or launch Unreal while any user's editor/LiveCodingConsole is open.
Initial gate: UnrealEditor 19632/33464, LiveCodingConsole 32592; polled once/minute.
Gate cleared at 10:04 on 2026-11-04, before any build/Unreal launch.
No user's process was stopped.

Run serially once clear:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" .\BrGame.uproject "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -log -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
.\Scripts\RunShotSmoke.ps1 -Skeletons -RunLabel Issue3
.\Scripts\RunShotSmoke.ps1 -Skeletons -PostureOrthoWidth 1200 -RunLabel Issue3Min
```

Final results (2026-11-04):
- Full `BrGameEditor Win64 Development` UBT build succeeded, zero compiler warnings.
- Requested `UnrealEditor-Cmd` automation command: exit 0, **89 passed, 0 failed**
  (`Saved/Logs/BrGame.log`). Platform SDK probing prints unavailable non-Windows SDKs;
  Win64 is valid and the actual automation completes successfully.
- `RunAllTests.ps1 -SkipBuild`: exit 0, **14/14 steps PASS**, including 89/89
  automation, AllVsAll and Grouped match DOWNED/REVIVED non-owner pose checks.
  Final logs: `Saved/Logs/RunAll/20261104-102131/`.
- Both final screenshot runs: exit 0, four images each, all **eight inspected**.
  `Saved/Logs/MP/ShotIssue3/` and `ShotIssue3Min/` contain capture logs.
- Initial compile exposed missing private-test friendship (fixed using existing
  convention); initial automation exposed two stale Pitch=90/+12cm expectations
  in the hit-feedback smoothing test (updated to full supine rotation/grounding).
  Full serial build/automation/regression repeated after the final HELP change.
- `git diff --check` and PowerShell AST parsing of `RunShotSmoke.ps1`: no errors.

`BrGame.Life.RealMeshSupineGroundingAndFacing` loads the configured mesh and
checks actual evaluated head/pelvis/body bones for both presentations at four
yaws, including downing from each posture. It derives the head-local face axis
from the reference skeleton's known component +Y face direction, then checks
its evaluated world normal against +Z. It logs that axis, head/pelvis vector,
lowest deforming landmark and skin bounds bottom. Lowest sampled bone must be
0–25 cm above ground; skin bottom must be 0.5 cm above it. Corpse tick/reference
pose and CMC cached full rotation are also checked.
Measured on the configured mannequin: head-local face axis **(0,1,0)**, world face
normal **(0,0,1)** at every tested yaw; head–pelvis horizontal length **66.68 cm**,
vertical difference **-1.62 cm**; lowest sampled body bone **13.90 cm** above ground;
reference skin bottom **0.50 cm**. Both downed and corpse measurements agree.

Existing posture life isolation and match non-owner assertions now check supine
quaternion/ground offset; match additionally requires horizontal head-to-pelvis
alignment with facing and upward face direction, rather than only old Pitch=90.

Screenshot fixture uses a real listen host and separate rendered client observer,
pitch 65°, yaw 37°, default zoom. It downs the host via normal life presentation,
then calls production `LeaveCorpse` and hides the eliminated pawn. Four images
are expected under `Saved/Screenshots/Skeletons/`: downed_gameplay,
downed_observer, corpse_gameplay, corpse_observer (with optional run label).
Inspect chest/face, arm spread, ground contact and HELP visibility; do not treat
capture markers alone as visual verification.

### Inspected screenshots

Under `Saved/Screenshots/Skeletons/`, each stem below has `Issue3.png` (2500 cm
ortho width) and `Issue3Min.png` (1200 cm) versions, at pitch 65° / facing yaw 37°:
- `downed_gameplay`: front chest/face visible, both arms spread symmetrically,
  head toward screen-up/facing; no side-lying silhouette. Grounded body and
  upright, readable yellow HELP countdown. Weapon hidden.
- `downed_observer`: matching face-up pose in a separate non-owning client;
  its own upright HELP presentation. Other upright pawn provides a height/shadow
  comparison (the lying body does not cast a detached standing shadow).
- `corpse_gameplay` / `corpse_observer`: same face-up arms-out reference silhouette
  and orientation, no HELP/weapon; untextured bone-white material appears grey in
  the scene lighting. No visible floating/sinking on the fixture's flat ground.

Remaining manual checks: max zoom, uneven ground, downed turning, late-join
corpse visibility, revive and offline timeout/forfeit. No Blueprint/editor asset
work is required.
