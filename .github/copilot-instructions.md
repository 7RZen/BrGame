# Copilot Instructions — BrGame

Unreal Engine 5.8 C++ project (`BrGame.uproject`, single `Runtime` module `BrGame`). Intended to become a top-down multiplayer battle-royale shooter (see `GameDescription.md` for the design spec: game modes, player states, weapon/damage/hit-chance rules, fog of war, pickups, Steam hosting). The code currently is the UE Top-Down template plus two template "Variants"; the BR gameplay is not implemented yet.

## Build / run

No test suite, linter, or scripts exist in the repo. Builds go through Unreal Build Tool (use the engine install matching `EngineAssociation` 5.8):

```powershell
# Editor target build (replace <UE> with the engine root)
& "<UE>\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex

# Standalone game target
& "<UE>\Engine\Build\BatchFiles\Build.bat" BrGame Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject"
```

Regenerate solution files by right-clicking `BrGame.uproject` → *Generate Visual Studio project files*. Automated tests, if added, run via `UnrealEditor-Cmd.exe BrGame.uproject -ExecCmds="Automation RunTests <Filter>; Quit" -unattended -nullrhe`.

`Binaries/`, `Intermediate/`, `DerivedDataCache/`, `Saved/`, `.vs/` are generated; don't edit them. The directory is not a git repository currently.

## Architecture

- `Source/BrGame/` root: `ABrGameGameMode` (abstract; classes assigned in Blueprint subclasses), `ABrGameCharacter`, `ABrGamePlayerController` — the base top-down template.
- `Variant_TwinStick/` — twin-stick shooter: `ATwinStickCharacter` (move/aim/dash/shoot/AoE), `ATwinStickProjectile`, `ATwinStickAoEAttack`, `ATwinStickPickup`, AI NPCs (`ATwinStickNPC`, `ATwinStickAIController`, `ATwinStickSpawner`) driven by **StateTree** (`TwinStickStateTreeUtility`), and `UTwinStickUI`. Closest match to the target game.
- `Variant_Strategy/` — RTS-style variant (`StrategyPawn/Unit/PlayerController/HUD/UI`, EQS context `EnvQueryContext_MoveGoal`).
- Each variant has its own GameMode/PlayerController; the active setup is determined by Blueprint assets, not C++. `Config/DefaultEngine.ini` sets `GameDefaultMap=/Game/TopDown/Lvl_TopDown` and `GlobalDefaultGameMode=/Game/TopDown/Blueprints/BP_TopDownGameMode`. Variant content lives under `Content/Variant_*` and `Content/TopDown`.
- Plugins enabled: `StateTree`, `GameplayStateTree`, `ModelingToolsEditorMode` (editor only). Online/Steam is not yet enabled (`OnlineSubsystem` is commented out in `BrGame.Build.cs`; Steam also requires enabling `OnlineSubsystemSteam` in the `.uproject`).

## Conventions

- Gameplay classes are `UCLASS(abstract)` with tuning values exposed as `UPROPERTY(EditAnywhere, Category=..., meta=(ClampMin, ClampMax, Units))`; concrete values (input actions, projectile/AoE classes, meshes) are set in Blueprint subclasses, so C++ changes often need matching Blueprint updates in the editor.
- Input uses Enhanced Input (`UInputAction` properties bound in `SetupPlayerInputComponent`).
- Each variant subfolder (including `AI`, `Gameplay`, `UI`) is listed explicitly in `PublicIncludePaths` in `BrGame.Build.cs`; new subfolders need a new entry.
- Renamed-from-template redirects (`TP_TopDown*` → `BrGame*`) live in `DefaultEngine.ini`; keep them (and add new ones) when renaming classes.
- Design requirements from `GameDescription.md`: weapon damage and hit chances must be developer-configurable (not player-configurable); ammo is shared now but should be structured so it can be split per weapon later; pickups/throwables/weapon types should be extensible; no upper player-count limit; host-configurable max players and pickup respawn time.
