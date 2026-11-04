# Issue 16 — Dropbox / airdrop crates

## Decisions

- Evolve the server-only `ABrPickupManager`; keep `RegisterRoundDrop` and weapon
  payload/lifetime behaviour. No new module, plugin, source directory or Tick
  scheduler. `FBrCrateRules` is header-only policy shared with automation.
- Initial implementation: wave 4, then one per host `CrateDropInterval` (30 s,
  1–3600). Superseded by the per-map follow-up below.
  Replaces `PickupRespawnTime` everywhere; incompatible session protocol 12→13.
- Loot: exactly 1 pistol, AR 1–2, MG chance .5, health/ammo pickup counts 3–8,
  grenades 2–4. Health20/50/100 weights 60/30/10. Developer settings expose
  counts/chance/weights and eight ordered soft definition references. Keep the
  registry and dropped weapon template. The legacy table asset/schema remains
  loadable but has no runtime settings reference and is no longer generated.
- Navigation projection + ground normal + capsule clearance, playable XY
  bounds/current `ABrSafeZone::Circle()`, minimum 300 cm pawn clearance.
  At most 64 candidate attempts by default. Initial navigation readiness waits
  at most 10 seconds. Impossible drops warn/skip, never emergency-place.
- Recheck clearance and **current** zone at touchdown. If a pawn moved onto the
  marker or the circle moved past it, cancel/destroy without collision or damage.
  Final-stage drops continue while a noncollapsed safe landing area exists.
- `ABrSupplyCrate` is always relevant. Its spawn transform is the landing center;
  start server time, height, duration and landed state replicate persistently.
  Only child mesh interpolation ticks locally (no movement replication, no
  dedicated-server interpolation). Initial actor spawn transform supports late
  joiners. Server landing timer enables cover collision; cover expires in 20 s.
- Fall 3000 cm / 4 s; engine cube/cylinder, existing M_Pickup with brown crate /
  yellow landing disc. Existing weapon-pickup wave uses positional explosion
  routing/range/concurrency for touchdown (not owner-only 2D pickup routing).
- Public crate meshes use the existing always-visible fog stencil 1; visibility
  subsystem's explicit channel-closure list only contains characters/grenades.
  Both HUD map modes draw yellow crate squares after fog. Content pickups retain
  their existing presentation.
- Items scatter 500–1000 cm. Up to 48 annulus attempts then 16 closer attempts,
  reproject/trace/capsule-check each and reject projected points outside the
  requested annulus. Keep 100 cm between items. No safe position means warn/skip.
  Items expire in 120 s. Round reset clears items, cover, falling crates and all
  timers, then the cheat can start a fresh wave/interval.

## Changed paths

- Added `Source/BrGame/Pickups/BrCrateRules.h`, `BrSupplyCrate.h/.cpp`,
  `Source/BrGame/Tests/BrCrateTest.cpp`,
  `Scripts/Content/remove_pickup_spawn_points.py`, this record.
- Replaced `Pickups/BrPickupManager.h/.cpp`; removed `BrPickupSpawnPoint.h/.cpp`.
- Updated `Source/BrGame/Core/BrDeveloperSettings.h`, `BrMatchSettings.h`,
  `BrSessionRules.h`, `BrGameMode.cpp`, `BrGameState.cpp`,
  `BrFlowGameModes.cpp`, `BrFlowWidget.h/.cpp`, `BrMinimapHUD.cpp`;
  `Config/DefaultGame.ini`.
- Updated `Source/BrGame/Tests/BrPickupTest.cpp`, `BrMapTest.cpp`,
  `BrMapsTest.cpp`; `Pickups/BrPickupDefinition.h`, `BrPickupRules.h`;
  `Scripts/Content/create_br_map.py`, `dump_br_layout.py`,
  `create_pickup_assets.py`, `Scripts/RunPackagedSmoke.ps1`.
- Editor Python changed only spawn-point actor ownership in `L_Town.umap` and
  `L_BattleField.umap`. Preserved user Battlefield allowlist/cook settings and
  existing preview assets; no preview regeneration or binary hand edits.
- Updated GameDescription, Issues, README, Docs/AddingMaps, copilot instructions.

## Commands and results

All editor/build/test invocations are serial. Initial process check found no
UnrealEditor/LiveCodingConsole. No user process was killed; no commits made.

```powershell
Get-Process UnrealEditor*,LiveCodingConsole -ErrorAction SilentlyContinue
git --no-pager status --short
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\remove_pickup_spawn_points.py" -unattended -nullrhi -nosound -nosteam
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
```

- Migration first attempt failed before changes: Python actors do not expose
  `is_a`. Corrected class filtering and reran successfully **before C++ deletion**:
  Town **339**, Battlefield **330**, Test Arena **0** removed (actual map counts
  differ from the issue's estimates). Commandlet exit 0, zero errors; existing
  Steam-disabled / navigation / crowd warnings.
- After full build/class removal, reran migration: **0 / 0 / 0**, exit 0, zero
  errors and no missing-class warnings. Script remains re-runnable after removal.
- First UBT failed on unsupported TNumericLimits Infinity and an unbraced UE_LOG
  / else macro site. Fixed normally, no suppression. Next full editor build:
  **Succeeded**, exit 0, **zero compiler warnings**, 3.76 s.
- First standalone all-BrGame automation: **97/97 Success**, zero failed tests,
  exit 0, `Saved/Logs/BrGame.log`, completed 2026-11-04 14:01:05 UTC.
  Included all three map validators and the seeded loot/geometry/scheduling tests.

- First RunAll: **14/14 PASS**, exit 0, automation **97/97**; logs
  `Saved/Logs/RunAll/20261104-150151/`. Includes Flow, Grouped, Zone,
  DisconnectGrace, TestArena, map selection/Random, both Fog modes, Session,
  both real multiplayer Match modes, Menu.
- Added real-world `BrGame.Crate.RuntimeLifecycle`: rebuild **Succeeded**, exit 0,
  **zero compiler warnings**, **15.22 s**. Narrow command (same editor arguments,
  `-ExecCmds="Automation RunTests BrGame.Crate; Quit"`): **4/4 Success**, exit 0.
  Actual default four-crate wave landed with **20/20, 23/23, 15/15, 14/14**
  contents and a periodic crate **15/15**, all `Inside=1`, no closer fallback.
- Final standalone all-BrGame automation: **98/98 Success**, zero failures,
  exit 0, completed **2026-11-04 14:16:24 UTC**. The runtime test checks actual
  nav/ground landing, later falling crates, spawned contents, public replication
  flags, cover/item configured lifetimes, actual timer-driven destruction, and
  no crates/items after reset and waiting past pending landing/periodic deadlines.
  Expiry is accelerated to .2 s **after** checking configured default remaining
  lifetimes; a real-time 120 s item-expiry playtest remains manual.
- Final `.\Scripts\RunAllTests.ps1 -SkipBuild`: **14/14 PASS**, exit 0,
  automation **98 successes / zero failures**. Logs:
  **`Saved/Logs/RunAll/20261104-151630/`**.
- Remote evidence in final RunAll's `smoke-logs/Flow-LvlTopDown/Client1.log`:
  `Crate snapshot ... Authority=0 ... Duration=4.00 Landed=0`.
  Late-joining `Client2.log` receives `Crate presentation ... Authority=0 Landed=1`.
  `Maps-Selection/Client1.log` and `Client2.log` receive the same landing/start-time
  snapshot while falling. This verifies network actor/state delivery, not rendered
  fog/marker appearance (headless runs cannot validate pixels).

| Final regression step | Result | Seconds |
|---|---|---:|
| Automation-BrGame | PASS (98/98) | 37 |
| Flow / Flow-Grouped | PASS / PASS | 44 / 44 |
| Flow-Zone / Flow-DisconnectGrace | PASS / PASS | 49 / 30 |
| Flow-LvlTopDown | PASS | 44 |
| Maps-Selection / Maps-Random | PASS / PASS | 38 / 38 |
| Fog / Fog-Grouped | PASS / PASS | 37 / 42 |
| Session | PASS | 67 |
| Match-AllVsAll / Match-Grouped | PASS / PASS | 164 / 166 |
| Menu | PASS | 37 |

Final static checks: no UnrealEditor/LiveCodingConsole processes remain.
`git --no-pager diff --check -- . ':!Issues.md'` found no whitespace errors
(Git emitted LF→CRLF notices). The excluded Issue 16 prose already had two
trailing-space lines in the user's incoming changes; preserved them.
Bare `python` resolved to the unavailable Windows Store alias (exit 9009);
reran successfully with the engine's bundled interpreter (exit 0, **4/4 syntax
PASS**, AST only, no bytecode/assets written):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" -c "import ast, pathlib; files=['Scripts/Content/remove_pickup_spawn_points.py','Scripts/Content/create_br_map.py','Scripts/Content/create_pickup_assets.py','Scripts/Content/dump_br_layout.py']; [ast.parse(pathlib.Path(f).read_text(encoding='utf-8'), filename=f) for f in files]; print('Content script syntax: 4/4 PASS')"
```

## Remaining manual checks

- With rendering/audio enabled: inspect falling crate/ground marker at all camera
  zooms, cover collision/shot blocking, touchdown sound and item readability.
- Two clients/late join: public crate meshes and yellow markers in unexplored
  fog on both map modes, disappearance on expiry/reset, content fog unchanged.
- Crowded and tiny final zones: pawn enters marker during fall, shrink passes
  marker, bounded skip warnings; no damage or displacement from landing.
- Designer loot/health weights and interval edits in lobby; player weapon swaps,
  cheat fresh wave and two-minute uncollected content expiry in a real match.
- Packaged/cooked and two-PC Steam gameplay have not been run for this issue.

## Follow-up — per-map wave density (2026-11-04)

### Decisions and changed paths

- `Source/BrGame/Core/BrMapEntry.h`: designer tooltips and per-map
  `InitialCrates=4`, `CratesPerDrop=2` defaults; bounds 0–100 / 1–100. Removed
  the confusing global `InitialCrates` from `BrDeveloperSettings.h` and config.
- `Config/DefaultGame.ini`: Town **6 / 3**, Battlefield **2 / 1**, Test Arena
  **2 / 1**. Existing map-entry text, including Battlefield description/cook
  settings, otherwise preserved.
- `Source/BrGame/Pickups/BrPickupManager.h/.cpp`: server verifies authoritative
  resolved map against the current world's full package (PIE prefix stripped).
  Direct-world starts resolve from developer map entries, not client `MapId`.
  An unlisted world logs an error and disables crates rather than inventing
  global tuning. Counts are snapshotted for the round and re-resolved on reset.
- `BrCrateRules.h`: interval count clamping and pure wave spacing. Within each
  wave, accepted landing centers stay at least twice the effective scatter max
  apart (default 2000 cm, minimum 170 cm). Each crate still gets the configured
  bounded candidate budget, current-zone/ground/pawn checks and touchdown
  revalidation. Spacing failure warns/skips; logs report requested/placed counts.
- `Source/BrGame/Tests/BrCrateTest.cpp`: seeded spacing/clamp/default/config test;
  real Town lifecycle now asserts six initial crates, three per interval and
  pairwise spacing in both waves. `BrMapsTest.cpp` validates entry count bounds.
- Updated GameDescription, Docs/AddingMaps, copilot instructions and Issue 16
  status. No binary changes or asset scripts are needed. Preserved the
  orchestrator's removal of the obsolete packaged-smoke pickup count check;
  `Scripts/RunPackagedSmoke.ps1` is untouched by this follow-up.
- Host interval, replicated crate state and session settings layout are unchanged;
  protocol remains 13 (these map tuning fields are not replicated match fields).

### Commands and results

Editor/Live Coding process check was empty. Commands run serially:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
```

- Full UBT: **Succeeded**, exit 0, **zero compiler warnings**, **13.17 s**.
- Standalone automation: **99/99 Success**, **zero failures**, exit 0.
  `Saved/Logs/BrGame.log` contains the real Town PIE match:
  - `14:56:41 UTC Crate initial wave: Map=Town Count=6 PerDrop=3`
  - `Crate initial wave placed: Map=Town Count=6 Spawned=6 Spacing=2000`
  - `14:56:47 UTC Crate interval wave placed: Map=Town Count=3 Spawned=3 Spacing=2000`
  The lifecycle fixture uses a 6 s interval so it can count the first periodic
  wave separately from the initial wave before cover expires.
- First regression exposed an obsolete two-map regex in
  `Scripts/RunFlowSmoke.ps1`: the server correctly resolved Random round 2 to
  Battlefield (`Valid=1`), but the script waited for `(Town|TestArena)` and
  timed out. Updated the Random fixture to rely on the existing server-side
  allowlist/world validation and match client phase messages for the selected
  world, instead of hardcoding two maps. Fixed-selection Test Arena assertions
  remain specific. No gameplay validation or error checks were suppressed.
- First regression: **13/14 PASS**, with only the stale Maps-Random assertion
  failing; logs `Saved/Logs/RunAll/20261104-155708/`.
- Additional full UBT builds after placement diagnostics and navigation-readiness
  correction: **Succeeded**, exit 0, **zero compiler warnings**, **4.41 s** and
  **4.32 s**. Final standalone all-BrGame automation on that source: **99/99
  Success**, **zero failures**, exit 0. Town again logged **6/6 initial** and
  **3/3 periodic** placements, spacing 2000 cm (16:03:49 / 16:03:55 UTC).
- Final `.\Scripts\RunAllTests.ps1 -SkipBuild`: **14/14 PASS**, exit 0,
  including automation **99/99**, Maps-Random, both multiplayer Match modes,
  both Fog modes and all other flow/session/menu steps. Logs:
  **`Saved/Logs/RunAll/20261104-170417/`**.

### Standalone navigation blocker (historical; resolved in the next follow-up)

- A separate normal-cadence Town listen-server validation ran for 75 seconds.
  Requested counts were correct (**6 initial / 3 periodic**, default 30 s
  interval), but **zero crates placed**. Every bounded candidate search skipped.
  Evidence: `Saved/Logs/Issue16TownDensity-20261104-165640.log`.
- A 14-second diagnostic run confirmed the navigation API reported a random
  point at `(0,0,0)` without a usable polygon. Evidence:
  `Saved/Logs/Issue16TownPlacementProbe.log`. Engine source confirms
  `UNavigationSystemV1::GetRandomPoint()` can return true solely because NavData
  exists. Readiness and skip diagnostics now additionally require
  `FNavLocation::HasNodeRef()`. Waiting remains bounded; no unsafe placement
  fallback or blocking gameplay-time navigation rebuild was added.
- Final-source standalone check ran 24 seconds after regression completed.
  `Saved/Logs/Issue16TownFinalNavigation.log` confirms
  `navigation unavailable after 10 seconds`, `NavProbe=Unavailable`, and
  `Map=Town Count=6 Spawned=0`. Thus normal standalone placement remains
  **blocked**, despite the passing navigation-built PIE fixture.
- These checks launched the following through PowerShell `Start-Process -PassThru`,
  used a unique `-abslog` path, polled that log, and stopped only the owned
  process ID in `finally`:

  ```powershell
  & "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "$PWD\BrGame.uproject" "/Game/BrGame/Maps/L_Town?listen?game=/Script/BrGame.BrGameMode?Name=CrateDensityHost" -game -unattended -nullrhi -nosound -nosteam -log "-abslog=<absolute log path above>"
  ```

  The intermediate placement probe omitted `?Name=CrateDensityHost`; the first
  and final checks used it. No user-owned editor process was stopped.

- The passing Town lifecycle/map validators explicitly build navigation in PIE;
  they prove wave counts, spacing and lifecycle **with usable navigation**, not
  normal standalone navigation readiness. Root cause of missing standalone
  polygons remains unverified. No map assets or navigation config were changed.

### Remaining checks at that checkpoint (superseded below)

- **Blocker:** inspect/rebuild/save Town runtime navigation or diagnose standalone
  navigation startup, then rerun the normal 30 s listen-server check and confirm
  six initial plus three periodic crates actually land. A rendered/editor
  navigation inspection is required; passing PIE automation alone is insufficient.
- Verify small/final-zone skip frequency feels reasonable with 20 m wave spacing.
- Battlefield's first wave requested 2 but placed 0 in one seamless-travel smoke
  (`Saved/Logs/RunAll/20261104-155708/smoke-logs/Maps-Random/Host.log`); bounded
  ground/nav searches warned/skipped safely. Its map validator passes after
  building navigation. Check Battlefield runtime navigation/startup placement
  in the editor/playtest; this follow-up does not alter that map's content.
- Existing rendered fog/map-marker, sound, packaged and Steam checks above remain
  manual. Any navigation asset correction must go through the editor, preserving
  unrelated Battlefield content.

## Follow-up — standalone ground placement and navigation repair (2026-11-04)

### Decisions and changed paths

- Added `Source/BrGame/Core/BrPlayableGround.h/.cpp`: header-only
  `FBrPlayableGroundRules` policy plus a shared physics adapter. The zone's
  existing fallback now calls this adapter with its existing trace lengths,
  capsule dimensions and WorldStatic channel. Zone/respawn policy is unchanged.
- `Pickups/BrPickupManager.h/.cpp`: no initial navigation wait. Cached playable
  bounds and current circle bound candidate sampling; navigation projection is
  only a preference. Failed/missing nav falls back to the original sampled XY.
  Both paths trace down to the **first** blocking WorldStatic surface, require
  an actor/component `BrFogGround` tag, walkable normal Z >= .7, Pawn-profile
  capsule clearance, 300 cm pawn clearance and same-wave spacing. Untagged roofs
  and cover tops fail; traces never continue through a roof to bless its floor.
  Touchdown rechecks remain. Scatter uses the same nav-independent floor/capsule
  checks. Placement remains bounded (64 crate attempts by default; each item
  at most 64 attempts); success/skip logs include attempt counts and ground path.
- `-BrCrateNavProbe` is opt-in diagnostics only: timer logs real elapsed time,
  world time, valid polygon readiness, runtime generation, **active** tile count,
  build locks/jobs, rebuild settings. It never rebuilds/unlocks navigation and is
  cleared on round reset/teardown (normal games have no diagnostic timer).
- Added editor-only `Core/BrMapNavigationEditor.cpp`: narrowly scoped
  `BrGame.Editor.BuildNavigation` console bridge. It rejects game worlds and
  invokes the engine's synchronous navigation-only Build, not a geometry/light
  rebuild. During a commandlet it first finishes async loading/asset compilation,
  then releases only the editor AsyncLoadLock, which normally waits for idle
  ticker frames unavailable to synchronous Python. Initial/custom/PIE locks are
  not bypassed. No new module/plugin/dependency or reflected gameplay API.
- Added `Scripts/Content/inspect_crate_navigation.py` (read-only map audit) and
  `rebuild_crate_map_navigation.py` (explicit Town/Battlefield nav repair).
  The latter validates projected player starts and fingerprints every non-nav
  actor's identity/class/label/transform/tags before/after rebuilding, refusing
  save on any difference. Only then saves through editor Python.
- `Content/BrGame/Maps/L_Town.umap` and `L_BattleField.umap`: saved completed nav
  tiles via that script. Town **544** / Battlefield **57** non-nav actor
  fingerprints unchanged; **48/48** player starts project in each:
  - Town: `b93434051b1b2864a45b7f8fed6a779043c0290be478f06b70068244af918501`
  - Battlefield: `70139e7fce34ca0d6766fadf7f5de9fac1660a66b44ecd3e85d8984cb45c8a54`
  No map regeneration, preview edits, cook-path changes or binary hand edits.
- `Scripts/Content/create_br_map.py`: future Town generation tags authored
  floors/roads explicitly and builds/verifies navigation before saving, avoiding
  another empty saved mesh. It was **not run** against the user's current layout.
- `Tests/BrCrateTest.cpp`: added slope/static/tag policy and real collision-only
  world tests (navigation explicitly disabled), rejecting roof/cover/wall/missing
  ground. Town lifecycle no longer explicitly builds/waits for navigation.
- Added `Scripts/RunCratePlacementCheck.ps1`: serial standalone Town/Battlefield
  validation, optionally real RHI/windowed; asserts initial/interval counts,
  actual landed/scattered contents, logged clearance checks, no skipped/cancelled
  crates. Every process is owned/tracked/stopped by specific PID in `finally`.
- Updated AddingMaps, GameDescription, copilot instructions and Issue 16 status.
  Existing per-map tuning and packaged-smoke removal remain intact; no commits.

### Navigation root cause and measurements

- Before repair, both saved maps loaded a Dynamic Recast actor (generation 2),
  valid bounds and ForceRebuildOnLoad=true, but **zero active tiles**. Throughout
  all four standalone runs, navigation was **unlocked, idle, not building**.
  It never became ready over 56–59 real runtime seconds (up to 75 world seconds).
  This reproduces with **normal D3D windowed rendering as well as -nullrhi**:
  not a renderer problem, long build, or ten-second timeout.
- UE 5.8 `UNavigationSystemV1::OnWorldInitDone` only requests a game-world initial
  rebuild when a nav data instance `RequiresInitialRebuild()` (set for newly
  spawned data). The loaded actors have that flag false. The editor-mode branch
  rebuilds separately, explaining why explicit PIE/map-validation builds masked
  the empty saved data. `bForceRebuildOnLoad` permits a requested rebuild; it does
  not itself issue the missing request on this loaded instance.
- Controlled standalone diagnostic with `-ExecCmds=RebuildNavigation`:
  **0.05 s** tile build, **630 active Town tiles**, valid polygon ready at the
  next probe (**1.03 real seconds** after manager initialization). Evidence:
  `Saved/Logs/CrateNavigation-ExplicitRebuild.log`. Geometry/generator work;
  startup data/request was missing.
- Root fix: build and **save completed navigation**, retaining Dynamic config.
  Repair builds took Town **.05 s**, Battlefield **.01 s**. Post-repair all four
  standalone runs have valid navigation at the **first probe, 0.00 s relative
  to manager initialization**, with Town **630** / Battlefield **100** active
  tiles. No runtime forced rebuild, global engine/config change, or change to
  zone/respawn selection behavior was needed.

### Commands and actual results

All engine builds, editor scripts, standalone games and tests ran serially.

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\inspect_crate_navigation.py" -unattended -nullrhi -nosound -nosteam
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame.Crate; Quit" -unattended -nullrhi -nosound -nosteam
.\Scripts\RunCratePlacementCheck.ps1
.\Scripts\RunCratePlacementCheck.ps1 -Windowed
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\rebuild_crate_map_navigation.py" -unattended -nullrhi -nosound -nosteam
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGame Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
```

- Audit: exit 0. First fallback editor build: **Succeeded**, **zero compiler
  warnings**, **13.37 s**; focused crate automation **7/7 Success**, exit 0.
- Before nav repair: both standalone commands **2/2 PASS** (four total),
  **28 actual landings / 448 scattered pickups**, zero skipped/cancelled
  crates/items. Thus fallback worked with **no nav polygons**, including scatter.
  Each run lasted 65 real process seconds, using the default 30 s host interval.
  Logs: `CratePlacement-20261104-172521-*-NullRHI.log` and
  `CratePlacement-20261104-172741-*-Windowed.log` under `Saved/Logs`.
- During repair-tool development three attempts refused to save: engine Exec
  didn't route the editor rebuild; the bridge then correctly encountered the
  editor AsyncLoadLock (0x20); finally Python projection required explicit
  `nav_data`/`filter_class` arguments. Corrected the routing/loading phase/API
  usage, with no error suppression or partial saves. Final repair **exit 0**,
  both maps `Unchanged=1 Saved=1`, **48/48 projected starts each**.
- Additional editor builds **4.49 / 4.62 / 3.75 s**, all succeeded with zero
  compiler warnings. The **4.62 s** command added `-NoUBTMakefiles` to force
  discovery of the new editor bridge after the earlier incremental build had
  not picked it up; compile output explicitly includes that file.
- Post-repair standalone results (again 65 real process seconds per map):

  | Run | Initial placed | Each interval placed | Actual landings | Pickups |
  |---|---:|---:|---:|---:|
  | Town -nullrhi | 6/6 | 3/3 | 12 | 210 |
  | Battlefield -nullrhi | 2/2 | 1/1 | 4 | 66 |
  | Town windowed | 6/6 | 3/3 | 9 | 140 |
  | Battlefield windowed | 2/2 | 1/1 | 3 | 42 |

  All **28** landings logged `Inside=1`; each spawn logged tagged static ground,
  clear capsule/pawns and wave spacing. Maximum crate attempts **5** (budget64).
  Zero skipped/cancelled crates or skipped items; **458 pickups** actually
  scattered. Logs: `CratePlacement-20261104-174312-*-NullRHI.log` and
  `CratePlacement-20261104-174541-*-Windowed.log`. One chained windowed invocation
  initially stopped at its process-safety precheck while an owned editor was
  disappearing from enumeration; added bounded teardown polling and reran both
  windowed maps successfully. No user process was killed.
- Standalone **BrGame Win64 Development** build: **Succeeded**, exit 0,
  **zero compiler warnings**, **20.68 s**, validating editor/runtime boundaries.
- Full all-BrGame automation on final source/maps: **101/101 Success**,
  **zero failures**, exit 0; Town logs **6/6 initial, 3/3 periodic** again.
- First full RunAll: **12/14 PASS**, logs
  `Saved/Logs/RunAll/20261104-174928/`. Automation **101/101** and crate/map
  checks passed. Failures were Match-AllVsAll's
  `E2E SLIDE CLIENT FAIL: fire not during slide` and Menu's
  `Cannot save local volume/sensitivity settings`. No assertions were weakened
  and neither smoke script was changed. Strict targeted reruns:
  `.\Scripts\RunMatchSmoke.ps1 -RunLabel CrateRetry` **PASS**, exit 0
  (`Saved/Logs/MP/MatchCrateRetry/`), then `.\Scripts\RunMenuSmoke.ps1`
  **PASS**, exit 0. Full regression rerun unchanged: **14/14 PASS**, exit 0,
  automation **101/101**, logs
  **`Saved/Logs/RunAll/20261104-180452/`**. Both Match modes, Menu,
  Maps-Random, both Fog modes and all flow/session steps passed.
- Final static checks: engine-bundled Python `ast.parse` on `create_br_map.py`,
  `inspect_crate_navigation.py` and `rebuild_crate_map_navigation.py`:
  **3/3 PASS**, without executing the destructive Town generator. PowerShell
  `Parser.ParseFile` on `RunCratePlacementCheck.ps1`: **PASS**.
  `git --no-pager diff --check`: exit 0 (only LF→CRLF notices).
  Editor/Live Coding process count: **0**. Packaged smoke remains untouched;
  no commits.

### Remaining checks

- The standalone zero-crate blocker is resolved. No required navigation authoring
  remains for Town/Battlefield; completed nav is saved in both source maps.
- **Recook/repackage** to ship these changed map/nav assets; an old package will
  not acquire them from an editor build. Packaged gameplay/cook was not run here.
- Real windowed processes ran successfully, but their pixels/audio were not
  visually/audibly inspected. Existing fog-marker/readability/sound, crowded final
  zones, real two-minute expiry and two-PC Steam checks remain manual.

## P2 review follow-up — raised caps and strict loot validation (2026-11-04)

### Fixes and changed paths

- `Source/BrGame/Core/BrPlayableGround.h/.cpp`: fog tags alone no longer qualify
  crate/scatter ground. An opt-in query footprint requires both world-horizontal
  bounds and scaled mesh-local surface axes to cover the scatter footprint plus
  the crate clearance radius. Required half-width is **1085 cm** at defaults
  (1000 cm scatter + 85 cm clearance; full span **21.7 m**). TestArena's tagged
  cylinder is only **350 cm** half-width and fails, regardless of tag or height.
  Checking local surface axes as well prevents yaw rotation from inflating a
  thin prop's AABB; the hit normal chooses the surface plane so a broad floor
  authored on its side is not incorrectly rejected. Slopes/elevated terrain are
  not rejected merely for their world Z. No new map tags, author allowlists or
  extra tuning source are needed. Rejection details are opt-in VeryVerbose logs.
- `Pickups/BrCrateRules.h`: pure `FloorHalfWidth` derives that minimum from the
  existing developer scatter radii. `BrPickupManager.cpp` sets it for crate
  candidate/touchdown **and item scatter** queries, on both nav and trace paths.
- `Tests/BrCrateTest.cpp`: new
  `BrGame.Crate.RaisedTaggedFloorFootprint`; real collision-only world reproduces
  the TestArena-sized tagged cap at Z=210 and rejects it. Checks broad floors on
  all three maps, thin/rotated props, side-authored floor orientation, dimensional
  boundaries, invalid data and at-rest/non-simulating support.
- Supporting actual TestArena floor required fixing an existing compatibility
  problem exposed by the new strict script: it is **Movable / BlockAllDynamic**,
  at rest, while the cylinder is BlockAll/WorldStatic. The old object-type-only
  trace excluded the real floor. Queries now use the same **blocking WorldStatic
  channel** as the zone, accept tagged broad ground collision types at rest
  (<=1 cm/s) with no physics simulation, and reject moving/physical support.
  The actual floor therefore works without changing its asset/profile/tags.
  `Scripts/Content/inspect_crate_navigation.py` now reports transforms, object
  type, profile and mobility to make this audit reproducible.
- **Zone behavior is unchanged:** default `MinFloorHalfWidth=0` opts out and its
  prior trace/slope/capsule policy remains. It **can still accept a walkable raised
  cap**; the collision regression explicitly proves that legacy result at Z=210.
  This is reported rather than silently changing zone policy in a crate fix.
- `Scripts/RunCratePlacementCheck.ps1`: checks **every** landing record, requires
  `Inside=1` and **Spawned == Requested > 0**, rejects unparsed records and fails
  any `Crate loot item skipped` warning. Supports/defaults to Town, Battlefield
  **and TestArena**; TestArena additionally asserts every landing is at its
  known floor height (not above 50 cm, versus the 210 cm cylinder cap).
- Updated GameDescription, AddingMaps and copilot instructions. No binary assets,
  navigation data, previews, config or unrelated gameplay were changed in this
  review follow-up; prior user changes remain intact. No commits.

### Serial commands and actual results

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame.Crate; Quit" -unattended -nullrhi -nosound -nosteam
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\inspect_crate_navigation.py" -unattended -nullrhi -nosound -nosteam
.\Scripts\RunCratePlacementCheck.ps1
.\Scripts\RunCratePlacementCheck.ps1 -Maps TestArena
.\Scripts\RunCratePlacementCheck.ps1 -Maps TestArena -Windowed
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGame Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
```

- Full editor builds **Succeeded**, **zero compiler warnings**, **5.15 / 3.31 /
  4.79 / 5.02 s**; final source is the **5.02 s** build. Intermediate focused
  crate runs **8/8 Success**, exit 0.
- First three-map placement attempt: Town and Battlefield passed; TestArena
  requested 2/1 but placed zero after rejecting the cylinder. Evidence:
  `Saved/Logs/CratePlacement-20261104-182556-TestArena-NullRHI.log`.
  A second TestArena attempt still rejected its Movable/BlockAllDynamic floor:
  `CratePlacement-20261104-183315-TestArena-NullRHI.log`.
  Read-only audits confirmed its actual profile/mobility; the support policy and
  collision fixture were corrected, not the map or the assertions.
  VeryVerbose footprint probe:
  `Saved/Logs/CrateP2-TestArenaFootprintProbe.log` (350/350 cm cylinder vs 1085).
- Corrected TestArena-only run: **PASS**, 4 complete landings,
  `CratePlacement-20261104-183536-TestArena-NullRHI.log`.
- Final default placement script: **3/3 PASS**, exit 0. Additional rendered
  TestArena run: **1/1 PASS**, exit 0. Every run used default host 30 s interval,
  65 real process seconds, with these final results:

  | Run | Initial / periodic placed | Landings | Pickups spawned/requested | All landing Z |
  |---|---|---:|---:|---:|
  | Town -nullrhi | 6/6 / 3/3 | 12 | 197/197 | 0 |
  | Battlefield -nullrhi | 2/2 / 1/1 | 4 | 76/76 | 0 |
  | TestArena -nullrhi | 2/2 / 1/1 | 4 | 61/61 | 0 |
  | TestArena windowed | 2/2 / 1/1 | 3 | 55/55 | 0 |

  **23 actual floor landings, 389/389 pickups**, no skipped loot or
  skipped/cancelled crates. Logs under `Saved/Logs`:
  `CratePlacement-20261104-183649-*-NullRHI.log` and
  `CratePlacement-20261104-184010-TestArena-Windowed.log`.
- Standalone game target: **Succeeded**, exit 0, **zero compiler warnings**,
  **11.43 s**. All-BrGame automation on final source: **102/102 Success**,
  **zero failures**, exit 0, including all **8** crate tests.
- First RunAll: **13/14 PASS**, logs
  `Saved/Logs/RunAll/20261104-184219/`. The Random step exited 0 in under a
  second after printing only its launch PID, without a PASS marker or game log.
  Cause unverified; this is not counted as a pass. No smoke assertions/scripts
  were weakened. Strict `.\Scripts\RunFlowSmoke.ps1 -MapSelector -RandomMap`
  rerun **PASS**, exit 0, including Battlefield and TestArena real rounds.
  Full unchanged RunAll rerun: **14/14 PASS**, exit 0, including
  **102/102 automation**, both Match modes, all map/flow/Fog/session/menu checks.
  Final logs: **`Saved/Logs/RunAll/20261104-185631/`**.
- The final regression's `smoke-logs/Flow-LvlTopDown/Host.log` now has actual
  TestArena landings at **Z=0.000**, **19/19** and **15/15** items (18:00:01 UTC),
  rather than the reviewed cylinder-top landings. Final PowerShell parser and
  audit-Python AST checks **PASS**; `git diff --check` exit 0; **zero**
  editor/Live Coding processes remain.

### Remaining checks

- Existing packaged recook/playtest, visible fog/marker/art, sound, real-time
  expiry and two-PC Steam checks remain manual. This follow-up did not change
  assets; normal windowed TestArena ran but pixels/audio were not inspected.
- Broad tagged floors support crates; small isolated raised platforms intentionally
  do not. This is footprint eligibility, not a global altitude cap or a promise
  that every point on a broad component can fit every crate's entire scatter.
