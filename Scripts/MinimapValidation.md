# Issue 2: minimap / full map

Status: implemented; full regression passed; manual multiplayer playtest pending.
Latest rule: **all-vs-all never shows enemy markers**, even when visible.

## Approach / cost

ABrHUD owns one nonreplicated orthographic SceneCaptureComponent2D and RGBA8
background target per local view, captured once when the match world is ready.
Unlit SceneColor, no post-process/atmosphere/fog/shadows: stable cartographic
colors across maps without depending on their lighting/exposure. A show-only
list includes visible Static-mobility WorldStatic primitives from nonreplicated,
non-pawn actors within the playable XY area. Replicated gameplay actors (pawns,
pickups, grenades, corpses, zone rings) cannot enter it. Movable geometry is not
captured. BrMinimapExclude on an actor/component explicitly opts out.
Bounds use ABrZoneBounds::FindPlayableBounds (authored, nav, geometry fallback).

The circular minimap rotates with camera yaw; full map is north-up (+X) to keep
the whole area fitted and navigation stable. A yellow camera-direction line on
the self marker gives orientation. Both share the same draw/filter path.
M is player-mappable IA_Map in IMC_BrDefault / DA_BrInputConfig; M/Esc closes.
Controller bindings also work while spectating. No pause/input-mode change.

Fog uses the existing active-view R8 visible / RG32f exploration targets and
exact world-XY bounds. No copies, readbacks or extra exploration targets.
Currently visible terrain is clear, explored terrain dimmed, unknown black.
Enemy markers borrow the exact cached 25Hz local fog actor-hide decision,
without new LOS traces or server grace/hysteresis. Group switching fails closed
until the target group masks are ready; inactive history follows existing fog
policy. Friends use group colors, downed friends pulse a red cross, self has a
white arrow; no pickup markers. Zone rings overlay fog; off-minimap zone-center
direction uses a green edge arrow.

Authority verified by source inspection: BrCharacter::IsNetRelevantFor returns
BrVisibilitySubsystem::Relevant directly during active fog, before Super's
distance cull. Relevant returns true for friends/selected spectator target.
Therefore distant group friends are already relevant regardless of
NetCullDistanceSquared. No replication/relevancy changes were necessary.

Developer settings (Minimap): MinimapSize=220 reference pixels (viewport-height
scaled), MinimapRadius=6000 cm, MinimapTextureSize=1024, FullMapViewportFraction=0.8,
MinimapMarkerSize=5 reference pixels, MinimapMaterial. UE has no pixel EUnit;
pixel properties use explicit display names, distances use Units=cm.
Default added texture payload: **4 MiB/local view**, plus the unchanged fixed
36 MiB fog masks. One Canvas material tile/frame, 3 texture samples/map pixel
(image + visible + exploration), analytic zone rings and O(local characters)
marker work. No added Tick, per-frame capture, enemy-position replication or
per-frame traces. Not a frame-time/high-player-count benchmark.

## Changed paths

- Source/BrGame/Core/BrMinimapRules.h, BrMinimapHUD.cpp (new)
- Source/BrGame/Core/BrHUD.{h,cpp}, BrFogComponent.{h,cpp},
  BrPlayerController.{h,cpp}, BrInputConfig.h, BrDeveloperSettings.h
- Source/BrGame/Core/BrGameMode.h, BrShotSmoke.cpp (dev-only screenshot fixture)
- Source/BrGame/Tests/BrMinimapTest.cpp (new)
- Scripts/Content/create_minimap_assets.py (new), create_input_assets.py
- Scripts/RunShotSmoke.ps1 (-Minimap single-process mode), this document
- Content/BrGame/Minimap/M_Minimap.uasset (generated), Input/Actions/IA_Map.uasset
  (generated), Input/{IMC_BrDefault,DA_BrInputConfig}.uasset (updated)
- GameDescription.md (Minimap + M), Docs/PlaytestChecklist.md (map checks),
  Issues.md (issue 2 status only)

No template/map binary edits, new folders/dependencies/plugins or config changes.
DefaultGame.ini already cooks /Game/BrGame and both match maps.

## Executed validation

UE: C:\Program Files\Epic Games\UE_5.8, project root as working directory.
Editor/LiveCoding process checks precede every build/editor launch; no user
process was stopped. Commands:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
# Each asset command run twice, serially:
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\create_input_assets.py" -unattended -nullrhi -nosound -nosteam
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\create_minimap_assets.py" -unattended -nullrhi -nosound -nosteam
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame; Quit' -unattended -nullrhi -nosound -nosteam -log "-abslog=$PWD\Saved\Logs\MinimapAutomation.log"
& .\Scripts\RunShotSmoke.ps1 -Minimap -RunLabel Final
```

Build succeeded without source warnings. Asset passes exit 0 (expected -nosteam
warning only). Automation: 49/49 successes, TEST COMPLETE exit 0, including
BrGame.Minimap.{Transform,MarkerFilter,FullMapFit}. Early compilation failures
(pixel unit metadata, unsupported capture clip properties, fixture placement)
and one material-script connection failure were corrected before successful
validation.

Single-process real-RHI screenshot fixture on L_Town: PASS, 5 PNGs inspected in
Saved/Screenshots/Minimap/{grouped,zone_edge,full_map,all_vs_all,all_vs_all_full}Final.png.
Camera yaw 37 degrees: street orientation matches minimap; full map fits,
terrain/coverage align, visited street dims, unknown stays black. Grouped
Enemy outside own radius (about 30m) appears through the teammate's
vision; downed cross, current/next ring and zone-edge arrow are distinct.
All-vs-all enemy within own radius (7m) remains visible in the main view but
has no marker on either map. Logs: Saved/Logs/MP/ShotFinal/Host.log.

## Remaining / limitations

- No remaining asset-generation or Blueprint/editor wiring work.
- At initial handoff, full `.\Scripts\RunAllTests.ps1 -SkipBuild` **not run**: its multiplayer smokes
  launch concurrent UnrealEditor game processes, conflicting with the requested
  single-editor-process rule. Full automated BrGame tests were run separately.
  Allow concurrent -game instances (not interactive editors/Live Coding) to run
  that unchanged regression script.
- The screenshot fixture is local with AI-controller contestants, not proof of
  remote relevancy, spectator switching or input rebinding. These need the
  updated manual checklist/multiplayer regression. Lvl_TopDown rendering has not
  been visually retested in this pass; bounds/capture path is map-independent.
- Background is a load-time static chart: streamed/new/destroyed static geometry
  after capture is not refreshed, nor are movable doors/dynamic world actors.
  High-player-count load and extreme DPI/resolution are not benchmarked.

## Review follow-up: fixed Escape dismissal (2026-11-03)

BrPlayerController.cpp now binds fixed Escape/IE_Pressed to CloseMap with
bConsumeInput=false, independently of rebindable Br.Menu. Closing a closed map
is inert. MenuAction, console handling and input modes are unchanged.

With concurrent game processes explicitly allowed for the regression script:
- Same BrGameEditor Build.bat command above: succeeded, no source warnings.
- Same automation command with filter BrGame.Minimap and log
  Saved/Logs/MinimapEscapeAutomation.log: 3/3 passed, exit 0.
- `.\Scripts\RunAllTests.ps1 -SkipBuild`: exit 0, all 11 steps PASS (49/49
  automation tests plus all 10 multiplayer/session smoke cases).
  Logs: Saved/Logs/RunAll/20261103-121557.

Commands ran serially, with editor/LiveCoding safety checks before each launch.
No asset/editor work required. Manual rebinding + Escape/console interaction
playtest was not performed; pure minimap tests do not exercise physical keys.
