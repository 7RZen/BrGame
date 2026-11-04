# Copilot Instructions — BrGame

BrGame is a top-down, player-hosted multiplayer battle-royale shooter. It is an Unreal Engine 5.8 C++ project (`BrGame.uproject`) with a single `Runtime` module, `BrGame`, and targets Win64 with Steam hosting. `GameDescription.md` is the agreed design spec and includes a per-step implementation status table. Keep that table current when you add features. `Issues.md` tracks issues (I) and feature requests (FR). `README.md` is the project overview.

## Build / test / run

Use the engine at `C:\Program Files\Epic Games\UE_5.8` (the scripts hardcode this path). Close the editor, Live Coding and other UnrealEditor processes before you build or run tests. Reflection or layout changes need a full UBT build, because Live Coding is not enough.

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex   # editor
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGame Win64 Development -Project="$PWD\BrGame.uproject"                    # game

# Automation tests (all are named BrGame.*; narrow the filter, e.g. BrGame.Fog)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -ExecCmds="Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -nosteam

.\Scripts\RunAllTests.ps1                      # build + full automation suite + smoke scripts (logs in Saved\Logs\RunAll)
.\Scripts\RunLocalMultiplayer.ps1 -Clients 2   # local listen server + clients on L_Town (-nosteam); -Menu, -Headless
```

- `Scripts\Run*Smoke.ps1` each cover one area: Flow, Match, Shot, Fog, Menu, Audio, Session and Packaged. `RunSteamHostCheck.ps1` covers Steam.
- `Scripts\Content\*.py` scripts create or update assets headlessly. Run them with `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<path> -unattended -nullrhi -nosound -nosteam`. Because they write binary assets, run them serially and never in parallel with builds or tests.
- Crate landing/scatter use shared `FBrPlayableGround` trace/slope/capsule checks independently of navigation readiness. Tagged `BrFogGround` support must block WorldStatic traces, be non-physical/at rest, and span the scatter footprint + 85 cm clearance on both surface axes; small fog-tagged caps are rejected. Zone fallback keeps its legacy policy. Save completed navigation tiles for standalone maps: Dynamic alone does not rebuild an empty loaded Recast actor in UE 5.8. `rebuild_crate_map_navigation.py` repairs Town/Battlefield navigation only; `BrGame.Editor.BuildNavigation` is an editor-only pipeline bridge. `RunCratePlacementCheck.ps1` checks Town/Battlefield/TestArena waves, full loot and TestArena floor heights (repeat with `-Windowed`).
- `Scripts\*Validation.md` files record the decisions, changed paths, commands, results and remaining manual checks for each feature. Add or update one when you implement a feature or issue.
- `Docs\PlaytestChecklist.md` lists the manual checks, and `Docs\AddingMaps.md` explains how to add a map.
- Steam needs a logged-in Steam client and standalone or packaged play, not PIE. Use `-nosteam` to force the Null/LAN subsystem and IP transport.

`Binaries/`, `Intermediate/`, `DerivedDataCache/`, `Saved/` and `.vs/` are generated, so don't edit them.

## Architecture

The BR game lives in four folders under `Source/BrGame/`:

- `Core/` holds the framework classes:
  - `ABrGameMode` (global default) and `BrFlowGameModes` (`ABrMenuGameMode`, `ABrLobbyGameMode`)
  - `ABrGameState`, `ABrPlayerState` and `ABrPlayerController`
  - `UBrGameInstance` and `UBrLocalPlayer`, which are configured in `DefaultEngine.ini`
  - `ABrCharacter`, plus `UBrCharacterMovementComponent` (prediction for sprint, slide and posture) and `UBrCharacterAnimInstance` (procedural posture)
  - `UBrSessionSubsystem` for Steam/Null sessions, privacy and passwords
  - `BrFogComponent` and `BrVisibilitySubsystem` (server-side fog of war), plus `BrSafeZone`
  - `BrHUD` and the menu, flow and settings widgets (native UMG/Slate)
  - `BrAudioSubsystem`, `BrHitFeedbackComponent` and `BrMatchSettings` (host settings)
  - `UBrDeveloperSettings`, the developer tuning shown in *Project Settings → BrGame* and stored in `DefaultGame.ini`
- `Combat/` holds `BrCombatDefinitions` (weapon/grenade data), `BrCombatMath`, `BrHealthComponent`, `BrInventoryComponent` (shared ammo), `BrGrenade` and the damage types.
- `Life/` holds the downed, revive, respawn, death-limit and elimination flow (`BrLifeComponent`), along with `BrCorpse`, `BrSupinePose` and `BrRoundRecord`.
- `Pickups/` holds pickup definitions, pure `FBrCrateRules`, the always-relevant `BrSupplyCrate` and server-only `BrPickupManager` (initial/periodic airdrop waves, scatter and round cleanup). No pickup spawn-point actors or per-slot respawns remain. Host `CrateDropInterval` replaces `PickupRespawnTime`; session protocol is 13. Developer crate tuning/soft definition references live in `DefaultGame.ini`; counts are per `FBrMapEntry` (`InitialCrates` / `CratesPerDrop`, defaults 4 / 2), not global. Same-wave landing centers are spaced by twice the scatter max radius.
- `Tests/` holds the automation tests (`Br*Test.cpp`), roughly one per feature.

Other code and content:

- The root `BrGame*` classes and `Variant_TwinStick/` / `Variant_Strategy/` are leftover UE Top-Down template code. Leave them alone unless a task targets them.
- Content lives under `Content/BrGame/` (Audio, Feedback, Fog, Input, Maps, Minimap, Pickups, Throwables, Town, Weapons, Zone). The maps are `L_MainMenu` (`GameDefaultMap`), `L_Town` (the main match map) and Test Arena. The lobby map list is a developer allowlist.
- Plugins: OnlineSubsystem, OnlineSubsystemNull, OnlineSubsystemSteam, SteamSockets, StateTree, GameplayStateTree, PythonScriptPlugin (editor only) and ModelingToolsEditorMode (editor only). Steam currently uses test App ID 480 (`SteamDevAppId` in `DefaultEngine.ini`).

## Conventions

- **Server-authoritative:** the host decides every shot, hit, damage event and pickup. Clients only predict movement through the custom movement component. Treat client input as untrusted. Do not trust Null/IP identities as Steam identities.
- **Pure rules structs:** gameplay policy goes into header-only structs such as `FBrFogRules`, `FBrLifeRules`, `FBrZoneRules` and `FBrSessionRules`. They have no UObject dependencies, so actors, replication and automation tests can share them. Put new logic there and test it in `Tests/`.
- **Tuning split:** developer tuning goes in `UBrDeveloperSettings` or data assets (`UPROPERTY(Config/EditAnywhere, meta=(ClampMin, ClampMax, Units))`). Players must not be able to change it. Host choices go in `BrMatchSettings`. Machine-local comfort settings (volume, mouse sensitivity, key bindings) are saved locally.
- **Input** uses Enhanced Input with player-rebindable mappings through user settings. Conflicting bindings are rejected, Tilde and Period are reserved, and the Look axis is not rebindable.
- **Include paths:** every source subfolder is listed explicitly in `PublicIncludePaths` in `BrGame.Build.cs`. Add an entry for any new subfolder.
- **Redirects:** the template-rename redirects (`TP_TopDown*` → `BrGame*`) live in `DefaultEngine.ini`. Keep them, and add new ones when you rename classes.
- **Design requirements:** there is no upper player-count limit (host max players, 0 = unlimited). Ammo is shared now but must stay splittable per weapon. Pickup, throwable and weapon types must stay extensible.
