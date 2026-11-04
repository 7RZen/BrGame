# Issue 10 — maps and map selector

## Decisions and implementation

- Project Settings → BrGame → **Maps** owns `Maps` and `DefaultMapId`.
  Removed `MatchMap` and migrated DefaultGame.ini to Town (default) and
  TestArena (`Lvl_TopDown`). No template C++ was changed; no new dependencies,
  plugins, input actions or Tick functions.
- Entries contain stable FName ID, localized name/description, soft world and
  Texture2D references, recommended min/max players. None/Random/path IDs,
  duplicates, missing packages, non-map packages and empty names are rejected
  with warnings. Missing previews are optional: text-only placeholder box.
- The lobby map button cycles Random and valid developer entries; only the
  listen host can use it. Every client reads the replicated selection and sees
  the name, description, square preview and recommendation (never an admission
  limit). Server `ChangeSettings` rejects unknown IDs; no request contains a
  travel destination. Client-supplied resolved IDs are cleared.
- `MapId` (selection) and server-owned `ResolvedMapId` (round outcome) live in
  the existing replicated MatchSettings with its notification. The existing
  GameInstance carry path retains the selection through results and lobby;
  outcome clears on lobby entry. At Start, server Random draws uniformly using
  `FRandomStream::RandRange` over the filtered list. **Random stays Random and
  re-rolls each round**. At match entry, the flow overlay announces `Map: <name>`
  for five local seconds after receiving the outcome; it does not depend on a
  transient multicast. Independent-review correction: a transparent match-only
  root paints only a compact top-centre banner, fading over the last 1.5 s.
  Menu/lobby/results alone keep the full-screen background.
- Session keys **BRMAP** and **BRMAPNAME** advertise the selection, including
  literal Random rather than the previous draw. Existing Null/Steam publication
  signatures automatically republish changes. Browser displays the name.
  Gameplay protocol is now 11 for the changed wire layout; engine version,
  password and authenticated Steam admission checks remain intact.
- Existing `-MatchLevel Lvl_TopDown` flow/fog script interfaces remain supported:
  they now choose TestArena through the same authoritative lobby setting path
  (`-BrSmokeMapId=TestArena`), not a process-local arbitrary path override.
  Flow smoke checks replicated selection/outcome and cleared outcome after
  returning to lobby. Default Flow still exercises Town.

## Preview generation and map assets

Run serially with editor/Live Coding closed:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" Scripts/Content/create_map_previews.py
```

Successful regeneration exit **0**, two 512×512 images inspected:

- `Saved/Screenshots/Maps/Previews/T_Town.png`: green ground, roads, central
  town, forest and compound visible in a full overhead view.
- `Saved/Screenshots/Maps/Previews/T_TestArena.png`: compact blue/grey template
  arena, central ring and surrounding blockers visible.
- Imported as `Content/BrGame/Maps/Previews/T_Town.uasset` and
  `T_TestArena.uasset`, configured in the allowlist. Runtime minimap export
  has zero alpha, so imported textures explicitly remap alpha to opaque.

The script launches bounded real-game captures serially, using the existing
unlit orthographic minimap (512px override, **RHI enabled**), followed by an
editor Python import commandlet. Initial editor-commandlet scene captures
(including `-AllowCommandletRendering`, no `-nullrhi`) returned black images;
those were discarded/replaced, not accepted as previews. System `python` is a
Windows Store alias here; the successful command uses UE's bundled Python.

Explicit `BrFogGround` tags were added via editor Python to broad flat floors.
This saved L_Town, Lvl_TopDown and its two floor external-actor packages. No
capture actors were saved; no map/asset binaries were hand-edited.

### Screenshot inspection

```powershell
.\Scripts\RunShotSmoke.ps1 -Maps
```

Exit **0**, **1/1 screenshot** saved and inspected:
`Saved/Screenshots/Maps/lobby.png` (1280×720).
Shows Town selector, description, recommendation and an opaque square Town
preview above existing lobby settings. The lobby is scrollable; Start and the
player roster are below the fold at 720p. The initial screenshot exposed the
zero-alpha issue; the final screenshot confirms it fixed.

## Automation and regression

Full UBT builds, **not Live Coding**; checked editor/LiveCoding processes before
each build and never stopped a user's editor:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex
```

Final build: **Succeeded, exit 0, zero compiler warnings** (12.84 s).
UHT/reflected layout changes were validated by full builds and fresh editor
processes, not a loaded editor session.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -log -nosteam
```

Final standalone automation: **exit 0, 84 successes, 0 failures** in
`Saved/Logs/BrGame.log`. Baseline was 79; new tests:

| Test | Coverage |
|---|---|
| BrGame.Maps.ListAndValidation | Real ini list, missing/duplicate/reserved/None entries skipped, unknown/path IDs rejected, empty list, deterministic Random draws restricted to valid maps |
| BrGame.Maps.StateCarryAndAdvertisement | Reflected replicated settings/notification, both map fields, GI carry values, shared BRMAP/BRMAPNAME publication helper and Random display |
| BrGame.Maps.CookSettings | Every configured map package explicitly in MapsToCook |
| BrGame.Maps.Validate.Town | Loads PIE Town and checks match prerequisites |
| BrGame.Maps.Validate.TestArena | Loads PIE Test Arena and checks match prerequisites |

Map validator follows the actual code: GameMode spawns the safe-zone actor;
maps need usable bounds rather than a placed safe-zone actor. Players use clear
PlayerStarts, navigation, or grounded spiral fallback. Pickups need clear
BrPickupSpawnPoints **or usable runtime nav** (Test Arena uses nav). Both maps
must have colliding `BrFogGround` ground. Diagnostics name the map and missing
source; the validator builds/waits for PIE navigation. The existing stronger
Town layout/doorway/path/vision test remains unchanged and passes.

The first automation attempt found a test-fixture TArray self-add assertion.
The fixture now copies its duplicate entry before adding; subsequent complete
runs passed. No engine error suppression or dependency changes were used.

```powershell
.\Scripts\RunAllTests.ps1 -SkipBuild
```

An initial full run passed **12/12**, including **84/84** automation:
`Saved/Logs/RunAll/20261104-070904/`.
Final post-preview-fix regression results are recorded below.

Pre-review run: **exit 0, 12/12 steps PASS**, logs
`Saved/Logs/RunAll/20261104-072312/`:

| Step | Result | Seconds |
|---|---|---:|
| Automation-BrGame | PASS (84 successes, 0 failures) | 23 |
| Flow (default Town) | PASS | 42 |
| Flow-Grouped | PASS | 41 |
| Flow-Zone | PASS | 45 |
| Flow-DisconnectGrace | PASS | 27 |
| Flow-LvlTopDown (lobby TestArena selection + carry) | PASS | 42 |
| Fog | PASS | 34 |
| Fog-Grouped | PASS | 41 |
| Session | PASS | 72 |
| Match-AllVsAll | PASS | 138 |
| Match-Grouped | PASS | 139 |
| Menu | PASS | 38 |

## Independent-review fixes (2026-11-04)

### P2: opaque announcement over active gameplay

`UBrFlowWidget::Build(2)` now returns a dedicated transparent `UOverlay` root
with only a content-sized top-centre banner. It never constructs the lobby
border or scrollbox. The banner is non-hit-testable, lasts five seconds from
the locally received resolved map, and fades during its last 1.5 seconds.
Opacity resets when constructing menu/lobby/results. Late joiners get the same
compact announcement without obscuring their world.

```powershell
.\Scripts\RunShotSmoke.ps1 -MapAnnouncement -RunLabel Review
```

Exit **0**, **1/1 screenshot**, requested at logged **MatchAge=2.00 s**:
`Saved/Screenshots/Maps/announcementReview.png`. Inspected the actual 1280×720
image: “Map: Town” appears in a small top-centre rectangle; ground, character,
world objects, health/ammo and minimap are clearly visible. No fullscreen
darkening. This uses the real RHI gameplay viewport with HUD included.

### Test gap: real carry and late lobby replication

Added two cases to the standard `RunAllTests.ps1` multiplayer sequence:

- **Maps-Selection**: `RunFlowSmoke.ps1 -MapSelector`
- **Maps-Random**: `RunFlowSmoke.ps1 -MapSelector -RandomMap`

The fixture uses the real host `ServerSettings` RPC handler **exactly once**
in the initial lobby. It does not repair or reapply the choice on return.
The host changes the selection before any clients connect. Client1 then joins;
the script asserts both the chosen replicated ID with no resolved outcome and
the lobby phase. Only after those assertions does Client2 connect and release
the start gate, so a match snapshot cannot satisfy the late-lobby-join check.

Both fixtures complete **two actual rounds** using authoritative elimination,
production results timers, seamless travel, GameInstance carry, GameMode
initialization and fresh GameState replication. After each round the host and
Client1 must still have the original selection and a cleared outcome in the
lobby. Each match verifies its resolved ID against the developer allowlist
and verifies that the loaded world is that entry's world.

Narrow validation commands above both returned **exit 0 / PASS**:
TestArena → TestArena in both rounds; Random → **Town, then TestArena**.
Random remained Random on both lobby returns. Repeated random outcomes are
also valid; these tests do not require different maps on successive draws.
The existing struct/reflection unit test is supplemental, not evidence of
travel correctness.

Full UBT build (same command documented above), after checking for
UnrealEditor/LiveCoding processes: **exit 0, Succeeded, zero compiler warnings,
17.91 s**. Fresh editor BrGame automation (same command above): **84 successes,
0 failures, exit 0**.

Final review regression:
`.\Scripts\RunAllTests.ps1 -SkipBuild` — **exit 0, 14/14 steps PASS**.
Artifacts: `Saved/Logs/RunAll/20261104-074112/`.

| Step | Result | Seconds |
|---|---|---:|
| Automation-BrGame | PASS (84 successes, 0 failures) | 23 |
| Flow | PASS | 42 |
| Flow-Grouped | PASS | 42 |
| Flow-Zone | PASS | 46 |
| Flow-DisconnectGrace | PASS | 27 |
| Flow-LvlTopDown | PASS | 42 |
| Maps-Selection | PASS (late lobby join + two TestArena rounds/carries) | 38 |
| Maps-Random | PASS (late lobby join + two valid Random rounds/carries) | 38 |
| Fog | PASS | 34 |
| Fog-Grouped | PASS | 42 |
| Session | PASS | 69 |
| Match-AllVsAll | PASS | 139 |
| Match-Grouped | PASS | 137 |
| Menu | PASS | 36 |

Validation ran serially. No user's editor was terminated.

Review-specific changed paths:
`Source/BrGame/Core/BrFlowWidget.h/.cpp`, `BrFlowGameModes.cpp`,
`BrGameMode.h`, `BrGameInstance.h`, `BrShotSmoke.cpp`,
`Scripts/RunFlowSmoke.ps1`, `Scripts/RunShotSmoke.ps1`,
`Scripts/RunAllTests.ps1`, this document and Issues.md point 10.

## Cooking (static verification; no package build requested)

- MapsToCook explicitly includes L_MainMenu, L_Lobby, L_Transition, **L_Town**
  and **Lvl_TopDown**. `BrGame.Maps.CookSettings` passes.
- Both preview references are under `/Game/BrGame/Maps/Previews`, covered by
  existing `DirectoriesToAlwaysCook=/Game/BrGame`.
- New map authoring/cook steps documented in `Docs/AddingMaps.md`; always add
  new map packages to MapsToCook. No claim of packaged-runtime validation.

## Changed paths

- `Config/DefaultGame.ini`
- `Source/BrGame/Core/BrMapEntry.h`, `BrMaps.cpp`, `BrDeveloperSettings.h`,
  `BrMatchSettings.h`, `BrGameMode.cpp`, `BrFlowGameModes.cpp`, `BrGameState.cpp`,
  `BrFlowWidget.h/.cpp`, `BrSessionRules.h`, `BrSessionSubsystem.cpp`,
  `BrHUD.h`, `BrMinimapHUD.cpp`, `BrShotSmoke.cpp`
- `Source/BrGame/Tests/BrMapsTest.cpp`
- `Scripts/Content/create_map_previews.py`, `Scripts/RunFlowSmoke.ps1`,
  `Scripts/RunFogSmoke.ps1`, `Scripts/RunShotSmoke.ps1`, this document
- `Content/BrGame/Maps/Previews/T_Town.uasset`, `T_TestArena.uasset`;
  `Content/BrGame/Maps/L_Town.umap`, `Content/TopDown/Lvl_TopDown.umap`;
  `Content/__ExternalActors__/TopDown/Lvl_TopDown/2/33/SKUR0WWL7JT7C3OXZMWAQS.uasset`
  and `2/BC/O0SDGKASWUILQXBY5MT8BU.uasset`
- `GameDescription.md`, `Docs/PlaytestChecklist.md`, `Docs/AddingMaps.md`,
  `Issues.md` point 10 (preserved its pre-existing wording edit).
- `.github/copilot-instructions.md` did not mention MatchMap; left unchanged.

## Remaining manual checks

- Physical lobby interaction on host/remote, late lobby join, changing choice
  repeatedly until Start, scrolling to Start/roster at different resolutions.
- Random announcement on every client (host screenshot inspected; consecutive
  rounds/retained Random/valid destinations now covered by multiplayer smoke);
  two-machine Steam browser map refresh alongside passwords,
  invites, visibility and version mismatch checks.
- Developer-only removal of preview/invalid map entries/empty map list;
  confirm placeholder, warnings and visible Start refusal. Player counts
  outside recommendation must remain allowed.
- Packaged/cooked play is not run; static cook settings and editor/runtime
  smokes only. No required asset/editor work remains for the configured maps.
