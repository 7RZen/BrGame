# Issue 16 — Dropbox / airdrop crates

## Decisions

- Evolve the server-only `ABrPickupManager`; keep `RegisterRoundDrop` and weapon
  payload/lifetime behaviour. No new module, plugin, source directory or Tick
  scheduler. `FBrCrateRules` is header-only policy shared with automation.
- Initial wave 4, then one per host `CrateDropInterval` (30 s, 1–3600).
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
