# Step 8 validation handoff

Status: Done. Review findings fixed and spectator-channel starvation reproduced,
diagnosed and verified after the fix. Validation below was run on the final C++
build on 2026-11-02, not just against the earlier increment.

## Implementation

Classic replication is active (runtime metadata `iris="0"`).
Character/grenade IsNetRelevantFor overrides use a server LOS/group cache.
Hidden channels are explicitly closed at the visibility update, rather than
changing the driver-wide RelevantTimeout. Radius-edge grace reduces churn;
blocked LOS does not receive grace. Previously received information cannot be
removed from an untrusted client.

Grenades require their own position to be visible, on server and local renderer;
visible throwers no longer reveal unseen grenades. Pickups and corpses are
always relevant. One exploration render target serves the active vision group.
Teammate target changes in the same group retain exploration; changing groups
discards inactive exploration and obstacle history. See the bounded-memory
review fix below (supersedes the original per-group GPU cache).

Client radial LOS fans write visible and persistent last-seen timestamp masks.
A world-XY post-process material renders black/unexplored and fading explored
areas independently of camera yaw/zoom. Friends use custom depth/stencil;
zone rings render after DOF, above the fog. Listen-host hiding changes primitive
presentation, not authoritative actor hidden state.

## Changed source/config/tool paths

- Added Core/BrFogRules.h, Core/BrVisibilitySubsystem.{h,cpp},
  Core/BrFogComponent.{h,cpp}, Core/BrFogSmoke.cpp,
  Tests/BrFogTest.cpp (under Source/BrGame).
- Changed Source/BrGame/Core/BrDeveloperSettings.h, BrGameState.{h,cpp},
  BrCharacter.cpp, BrPlayerController.{h,cpp}, BrCheatManager.{h,cpp},
  BrZoneBounds.{h,cpp}, BrSafeZone.cpp, BrGameMode.h, BrFlowGameModes.cpp.
- Changed Source/BrGame/Combat/BrGrenade.{h,cpp}.
- Changed Source/BrGame/Pickups/BrPickup.cpp and Life/BrCorpse.cpp.
- Changed Config/DefaultEngine.ini and Config/DefaultGame.ini.
- Added Scripts/Content/create_fog_assets.py and Scripts/RunFogSmoke.ps1;
  changed Scripts/Content/create_zone_assets.py and Scripts/RunFlowSmoke.ps1.
- Generated Content/BrGame/Fog/M_Fog.uasset, RT_VisibleMask.uasset,
  RT_ExploredMask.uasset; updated Content/BrGame/Zone/M_ZoneRing.uasset.

## Executed validation

Engine root: C:\Program Files\Epic Games\UE_5.8.
Project root: C:\Users\oiste\Projects\Unreal\BrGame.

Commands (PowerShell, from project root):

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame; Quit' -unattended -nullrhi -nosound -log
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\create_fog_assets.py" -unattended -nullrhi
& Scripts/RunFlowSmoke.ps1
& Scripts/RunFlowSmoke.ps1 -Grouped
& Scripts/RunFlowSmoke.ps1 -Zone
& Scripts/RunFlowSmoke.ps1 -DisconnectGrace
& Scripts/RunFogSmoke.ps1
& Scripts/RunFogSmoke.ps1 -Grouped
& Scripts/RunFogSmoke.ps1 -Windowed
& Scripts/RunFogSmoke.ps1 -Grouped -Windowed
foreach ($Run in 1..10) {
    & Scripts/RunFogSmoke.ps1 -Spectator -RunLabel "Fixed$Run"
    if (!$?) { throw "Spectator repetition $Run failed" }
}
& Scripts/RunFogSmoke.ps1 -Spectator -Windowed -RunLabel WindowedFinal
```

Final editor build: `Result: Succeeded`, no source warnings.
Final automation: 37 successes, `TEST COMPLETE. EXIT CODE: 0`.
Asset commandlet: two consecutive successful idempotence passes during initial
step-8 implementation; no asset-script/material changes in this review fix.
Final default/grouped/zone/disconnect-grace flow runs printed their PASS summaries.
Final headless fog runs verified actual client host-character counts:
all-vs-all 0 -> 1 -> 0; grouped 1 -> 1 -> 1.
Final windowed runs passed real-RHI GPU mask probes in both modes:
`Visible=1.000 Stamp=25.474 Age=0.025` and grouped
`Visible=1.000 Stamp=25.500 Age=0.026` at Stage=5.
Earlier screenshots were inspected for LOS-shaped black fog and visible rings.
Final windowed spectator run also passed all six target probes.
Logs are under Saved/Logs/MP. RunLabel preserves separate repetition logs.

## Spectator root cause and evidence

The original default flow smoke was inadequate: its eliminations entered
results before spectator service. It remains a flow/results regression, not
proof of active-fog spectating.

The new `-Spectator` smoke starts three all-vs-all contestants, freezes and
separates them beyond vision range, verifies enemies absent on the future
spectator, and eliminates only that client. Two opposing contestants stay
alive. Every probe requires active fog and Results=0, checks the authoritative
target PlayerState, direct pawn, PlayerState pawn cache, view target, actual
client actor/channel and NetGUID, and asserts the other enemy absent.
It cycles through the real owning-client ServerCycleSpectator request five times.

Before fix:
- Investigate3 failed initial acquisition:
  `Stage=3 NetGUID=None ExpectedGUID=124 Channel=-1 Cache=None Direct=None Withheld=0 Results=0`.
- Quiet2 server for GUID124:
  `Channel=-1 Closing=0 Initialized=1 ... Relevant=1`; client failed Stage=3.
- Registry1 reproduced reopening starvation: GUID122 opened on channel44,
  closed on switching away, reopened once, closed again, then remained absent
  on switching back. Server registry showed Registered=1, Active=1,
  DormantConnection=0, Initialized=1 and Relevant=1 while NextUpdate advanced;
  client failed Stage=7 with Cache=None Direct=None.
- Five 60-FPS bounded runs passed before the fix, but uncapped runs still failed.
  A frame-rate cap was NOT adopted as the fix.
- Trace1's engine client log correlates GUID124 on channel43, Relevancy cleanup
  at 12:41:13.473, then failed reacquisition at Stage=5.
  Initial high-volume engine logging backlogged the server log writer, so later
  low-volume runs are the authoritative server evidence.

Engine API/source inspection established the scheduling hole:
UE_5.8 Engine/Private/NetDriver.cpp ServerReplicateActors_PrepConnections
throttles listen-server connections. ServerReplicateActors, around lines
6406-6434, preserves bPendingNetUpdate for skipped connections only when a
channel already exists. Newly visible or previously closed stationary actors
have no channel. Their consider-list times can repeatedly land on the other
connection, even though their per-view relevancy returns true.

Fix: world post-net-flush callback retains bPendingNetUpdate only for queued,
missing, currently relevant actor/connection discoveries until a channel
opens. Visibility updates seed character/grenade discoveries; spectator
selection seeds its target even during results. Hidden actors are removed
from the queue. Level-initialization, relevancy, dormancy and bandwidth checks
remain in the normal engine path. No ForceNetRelevant, engine/config throttle
change, global timeout change, or manually created channels. Weak references
and explicit delegate teardown keep this scoped to the owning world.

After fix: ten consecutive uncapped headless matches passed (Fixed1..Fixed10),
60 acquisitions / 50 target switches; plus one windowed match passed.
Example Fixed1 GUID124 lifecycle, all on the same spectator connection:
- 13:25:49.327 discovery opened channel40;
- 13:25:53.666 fog closed channel40;
- 13:25:56.993 discovery reopened channel40;
- 13:26:00.501 fog closed channel40;
- 13:26:04.261 discovery reopened channel49;
- client Stage=7 confirmed GUID124 channel49, Withheld=0 Results=0.

## Remaining manual coverage / risks

No manual Blueprint/map edits are currently required.
Manually check walls/partial cover, shared team vision, rotating camera/zoom,
explored fade, pickups/corpses, hidden HELP/tracers/grenades, spectator switching,
new-round reset, lobby/results without fog, and remote denial of host cheats.
Same-group teammate exploration is preserved; different-group history is
deliberately discarded to bound memory (see review fix below). High-player-count load and
packet-loss stress have not been benchmarked. Optional wall replication smoke
is not implemented; target-switch smoke now covers repeated channel reopening.

## Rendering polish (2026-11-03)

Client rendering only: server relevancy, LOS distances, and client actor hiding
are unchanged. Defaults are now FogTextureSize=2048 (9.77 cm/texel on L_Town)
and FogRayCount=720 (13.1 cm arc spacing at the 1500 cm radius); their supported
maxima are 4096 and 2048. New Fog-category developer setting FogEdgeFeather=30 cm
(0..150) controls an inward-only, bilinear, world-space 3x3 smoothstep feather.
Both masks filter coverage smoothly; explored green stores coverage, with
texel-centered red-channel timestamp reads. Runtime resource clear color AND
initial clear are transparent so untouched exploration remains black.

Only first-hit, placed, non-replicated static WorldStatic StaticMeshActors can
receive obstacle stencils 2..17. These depth-tested object pixels are lit when
seen and fade to the existing DimOpacity using FogDimDelay (16 fade levels).
Weak active-group last-seen history is cleared on group switches; original
custom-depth state is restored on teardown/reset. Authored
custom-depth users are not overwritten. No penetration, floor dilation, pawn,
pickup, or grenade exemption. Friends retain stencil 1; zone rings stay AfterDOF.

Costs (not a frame-time benchmark): 720 client traces/source/update, or 18,000/s
at 25 Hz, versus 6,000/s previously. Post-process uses 19 mask reads/pixel
(9 visible, 9 explored coverage, 1 timestamp), versus 2 previously. Default RT
memory is now a fixed 4 MiB visible + 32 MiB explored (review fix below); known
obstacles add custom-depth draws and an O(known obstacles) timer update, not Tick.
Large grouped-vision/high-player-count loads have not been profiled.

Changed paths: Core/BrFogComponent.{h,cpp}, BrFogRules.h,
BrDeveloperSettings.h, BrShotSmoke.cpp; Tests/BrFogTest.cpp;
Scripts/Content/create_fog_assets.py, RunFogSmoke.ps1, RunShotSmoke.ps1,
this document; generated Content/BrGame/Fog/{M_Fog,RT_VisibleMask,
RT_ExploredMask}.uasset and re-saved Zone/M_ZoneRing.uasset.
No map, template, config, or server visibility changes.

Initial polish validation used UE_5.8 at the engine root above, with editor/Live Coding
process checks before each run. Commands:

```powershell
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
# Run this command twice, serially:
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\create_fog_assets.py" -unattended -nullrhi -nosteam
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame; Quit' -unattended -nullrhi -nosound -log -nosteam
& Scripts/RunFogSmoke.ps1 -RunLabel Final
& Scripts/RunFogSmoke.ps1 -Grouped -RunLabel Final
& Scripts/RunFogSmoke.ps1 -Windowed -RunLabel Final
& Scripts/RunFlowSmoke.ps1
& Scripts/RunShotSmoke.ps1
& Scripts/RunShotSmoke.ps1 -CameraPitchAngle 45 -RunLabel Pitch45
& Scripts/RunShotSmoke.ps1 -CameraPitchAngle 90 -RunLabel Pitch90
```

Initial polish build succeeded without source warnings; both asset passes exit 0 (only
the expected -nosteam warning), 45/45 automation successes / test exit 0.
All final smoke commands exit 0 and print PASS. Windowed GPU probes:
Visible=1.000 Stamp=25.126 Age=-0.000; untouched Stamp=0 Coverage=0.
Logs: Saved/Logs/MP/{FogFinal,FogGroupedFinal,FogWindowedFinal,Flow,
Shot,ShotPitch45,ShotPitch90}. Screenshots: Saved/Screenshots/Step10/
{town_street,town_interior,field,dense,dense_explored,forest}.png and each
name with Pitch45/Pitch90 suffix (18 final captures, process-local ini overrides).
Inspected default areas and tilted/overhead obstacle captures: edges feathered,
tops/sides lit, unseen floor behind walls black, old yard/trunk dimmed in
dense_explored, zone ring above fog, no pitch-dependent mask displacement.

Intermediate builds had two compile errors, corrected before final validation.
Intermediate coverage initialization was caught by screenshot inspection and
fixed; the new windowed untouched-pixel GPU assertion guards that regression.
Some earlier shot launches timed out during engine startup; retries succeeded.
Only owned timeout SDK-validation descendants were cleaned up by specific PID;
no user editor was stopped.

No remaining asset/editor work. Limitation: obstacle knowledge is component-
granular, so a long wall hit at one point shows its entire depth-visible static
component, not its unseen floor or hidden actors. Movable/replicated geometry
and authored custom-depth meshes are deliberately excluded. Fine raster/ray
discretization remains at extreme zoom, not the former large blocky edges.

### Bounded-memory review fix (2026-11-03, latest validation)

Removed ExplorationByGroup. Exactly one runtime exploration allocation is made
in InitializeRendering and reused/cleared on every group change; no inactive GPU
targets or CPU mask copies remain. Obstacle timestamps are also active-group
only. Returning to another group rebuilds vision from its current sources,
without restoring past exploration. Same-group target changes do not clear it.

Formats: Visible=R8 (4 MiB), Explored=RG32f (32 MiB) at default 2048²: **36 MiB
fixed per local view**, independent of survivor/group count (144 MiB at the
4096 setting cap). RG holds full-precision long-round timestamps plus independent
bilinear coverage, rather than four 32-bit channels. Material-default RT assets
are now 1x1 placeholders, not duplicate full-size allocations. Figures are texture
payloads, excluding driver alignment/normal scene resources. No new per-frame
readbacks or extra post-process samples; GPU probes use reduced-format engine
readback APIs only during development smoke.

Obstacle exclusion: BrFogGround component OR actor tag always opts out (available
for future authored/generated ground). Untagged flat/slab-like world bounds also
opt out: height <=50 cm, or height <=200 cm with half-height <=10% of the smaller
horizontal half-extent. Tests cover ground/floor/roads versus upright walls,
low cover, crates and containers. No map-generator, map, template, authority,
relevancy or actor-hiding changes.

Follow-up changed paths: Source/BrGame/Core/BrFogComponent.{h,cpp}, BrFogRules.h,
Source/BrGame/Tests/BrFogTest.cpp, Scripts/Content/create_fog_assets.py, this
document, generated Fog/{M_Fog,RT_VisibleMask,RT_ExploredMask}.uasset and
re-saved Zone/M_ZoneRing.uasset.

Executed the build/asset/automation commands above (asset command twice), then:
```powershell
& Scripts/RunFogSmoke.ps1 -RunLabel Budget
& Scripts/RunFogSmoke.ps1 -Grouped -RunLabel Budget
& Scripts/RunFogSmoke.ps1 -Windowed -RunLabel Budget
& Scripts/RunFlowSmoke.ps1
& Scripts/RunFogSmoke.ps1 -Spectator -Windowed -RunLabel Budget
& Scripts/RunShotSmoke.ps1 -RunLabel Budget
```
Build succeeded without source warnings; asset passes exit 0 (expected -nosteam
warning only); **46/46 tests pass**, test exit 0. All smokes exit 0 / PASS.
Windowed probes: Visible=1.000 Stamp=25.309 Age=0.000, untouched Stamp=0 Coverage=0.
Extra windowed spectator smoke passed five target switches/non-target withholding:
all seven group-reset logs reused TextureRenderTarget2D_1, ResidentTargets=1,
Payload=36.00MiB. No 60-player VRAM benchmark was run; the allocation path itself
is now constant. Logs: Saved/Logs/MP/{FogBudget,FogGroupedBudget,FogWindowedBudget,
Flow,FogSpectatorBudget,ShotBudget}. Recaptured and inspected
Saved/Screenshots/Step10/denseBudget.png and dense_exploredBudget.png: feathered
edges, lit blocking objects, black unseen floor, dimmed old yard, visible ring.
No remaining editor/asset work. Deliberate tradeoff: inactive-group history is lost.
