# BrGame

A top-down, player-hosted multiplayer battle-royale shooter built with **Unreal Engine 5.8** (C++ + Blueprint), for Windows with Steam hosting.

The full agreed design, including the implementation status per step, is in [GameDescription.md](GameDescription.md). Known issues and feature requests are tracked in [Issues.md](Issues.md).

Github repository: https://github.com/7RZen/BrGame

## Features

- **Server-authoritative multiplayer.** The host's game is the listen server and resolves every shot, hit and damage event. There is no hard player cap; the host sets max players (0 = unlimited).
- **Steam hosting** (OSSv1 lobbies + SteamSockets relay): server browser, Public / Friends only / Invite only sessions, overlay invites and optional host passwords. Uses test App ID 480 for now. Run with `-nosteam` to use LAN/IP for development.
- **Game flow:** start page, lobby with host settings (map, all-vs-all or grouped mode, groups, friendly fire and more), match, results, then back to the lobby.
- **Combat:** weapons, grenades, shared ammo, developer-configurable hit chance and damage, and directional hit feedback.
- **Life cycle:** players can be downed, revived or respawned. A death limit leads to elimination, which leaves a corpse/skeleton. Players can spectate after elimination.
- **Movement:** tactical sprint with stamina, slide, crouch and prone (procedural posture visuals), and relative-mouse aiming.
- **World:** shrinking safe zone, shared-team fog of war, airdrop loot crates with public minimap/full-map markers and host-configurable interval.
- **Maps:** Town (`L_Town`, 200 m) and Test Arena, picked by the host in the lobby (or Random).
- **UI/audio:** ESC menu (the game is not paused), rebindable keyboard/mouse controls, and audio and mouse-sensitivity settings.

Tuning values are developer-only (*Project Settings → BrGame*, `UBrDeveloperSettings`, data assets). Players cannot change them.

## Default controls

| Action | Key |
|---|---|
| Move | W A S D |
| Tactical sprint | Hold Left Shift |
| Slide | Left Ctrl |
| Crouch / Prone (toggle) | C / Z |
| Fire / Throw grenade / Reload | Left mouse / G / R |
| Pick up / Revive (hold) | E |
| Respawn while downed | Hold Space |
| Zoom / Scoreboard / Full map / Menu | Mouse wheel / Tab / M / Esc |

## Project layout

| Path | Contents |
|---|---|
| `Source/BrGame/Core` | Game mode/state, player controller/state, character and movement, sessions (`BrSessionSubsystem`), fog, safe zone, HUD/menus, audio, developer settings |
| `Source/BrGame/Combat` | Weapon/damage definitions, combat math, health, inventory/ammo, grenades |
| `Source/BrGame/Life` | Downed/revive/respawn/elimination rules, corpses, round records |
| `Source/BrGame/Pickups` | Pickup definitions, crate loot rules, airdrop actor and round drop manager |
| `Source/BrGame/Tests` | Automation tests (`BrGame.*`) |
| `Source/BrGame/Variant_*`, root `BrGame*` classes | Original UE Top-Down template code (TwinStick and Strategy variants), kept for reference |
| `Content/BrGame` | Game content; maps in `Content/BrGame/Maps` (`L_MainMenu`, `L_Town`, ...) |
| `Scripts/` | Test, smoke and local multiplayer PowerShell scripts, plus validation notes for each feature |
| `Docs/` | [Playtest checklist](Docs/PlaytestChecklist.md), [Adding maps](Docs/AddingMaps.md) |

## Building

Requires UE 5.8 (the scripts expect `C:\Program Files\Epic Games\UE_5.8`) and Visual Studio with the C++ game-development workload.

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
```

To generate Visual Studio project files, right-click `BrGame.uproject` and choose *Generate Visual Studio project files*. The game starts at `L_MainMenu`.

## Testing and running

Close the editor before you run these scripts.

```powershell
.\Scripts\RunAllTests.ps1                     # build + all BrGame automation tests + smoke scripts
.\Scripts\RunLocalMultiplayer.ps1 -Clients 2  # local listen server + 2 clients on L_Town (-nosteam)
.\Scripts\RunLocalMultiplayer.ps1 -Menu       # every window opens the start page
```

To run only the automation tests:

```powershell
UnrealEditor-Cmd.exe BrGame.uproject -ExecCmds="Automation RunTests BrGame; Quit" -unattended -nullrhi
```

The `Scripts/Run*Smoke.ps1` scripts cover individual areas. `RunPackagedSmoke.ps1` and `RunSteamHostCheck.ps1` cover packaged and Steam checks.

## Status

Steps 1–11 of the design are implemented, and the automated suite passes. Two-PC Steam validation and the manual playtest ([checklist](Docs/PlaytestChecklist.md)) are still pending. See [GameDescription.md](GameDescription.md#implementation-status).
