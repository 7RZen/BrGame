# Step 11 — Win64 Development packaging validation

Status: **Win64 Development package and local `-nosteam` runtime flow pass.**
Steam-backed packaged testing on two PCs remains pending.

## Packaging changes

`UProjectPackagingSettings` is read from `Config/DefaultGame.ini` by this UE
5.8 build. The first cook attempt placed these entries in `DefaultEngine.ini`;
the cook completed but omitted `L_Lobby` and `Lvl_TopDown` from the staged
manifest. I moved the settings to the correct `DefaultGame.ini` section and
recooked successfully.

The final settings explicitly cook:

- `/Game/BrGame/Maps/L_MainMenu`
- `/Game/BrGame/Maps/L_Lobby`
- `/Game/BrGame/Maps/L_Transition`
- `/Game/BrGame/Maps/L_Town`
- `/Game/TopDown/Lvl_TopDown` (the retained test map)

`DirectoriesToAlwaysCook` includes `/Game/BrGame` for config-held soft paths
(input config, fog/zone materials, pickup/weapon/throwable data) and
`/Game/Characters/Mannequins` for the configured character mesh/animation.
Final UFS staging manifest contains all five maps and those referenced assets.

No gameplay C++ was changed. `.gitignore` already excludes root `Build/`
artifacts (`git check-ignore -v Build/Package/Windows/BrGame.exe` resolves to
`.gitignore:541`), so it did not need modification.

## Build and cook

Executed from the project directory with `UnrealEditor`, `UnrealEditor-Cmd`,
and `LiveCodingConsole` closed:

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
$Project = "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject"
$Archive = "C:\Users\oiste\Projects\Unreal\BrGame\Build\Package"
& $env:ComSpec /d /s /c "call `"$UE\Engine\Build\BatchFiles\RunUAT.bat`" BuildCookRun -project=`"$Project`" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory=`"$Archive`" -utf8output"
$LASTEXITCODE
```

Result: **exit 0**, `BUILD SUCCESSFUL`, including `BrGame` and `BrGameEditor`
build targets, Win64 Development cook, stage, IoStore/Pak, and archive.
Final cook transcript had no `LogCook` warnings/errors, missing-package or
asset-reference errors, or BrGame/streaming load errors. UE startup also
reported unavailable SDK setup for Android, iOS, Linux, Mac, and tvOS; those
non-target platforms do not block Win64.

Installed build toolchain used:

- Launcher UE **5.8** at `C:\Program Files\Epic Games\UE_5.8`
- Visual Studio 2022 Build Tools MSVC **14.44.35207**
- Windows SDK **10.0.26100.0**

## Artifact

- Copyable distribution folder: `Build\Package\Windows\`
- Launcher: `Build\Package\Windows\BrGame.exe`
- Packaged game binary: `Build\Package\Windows\BrGame\Binaries\Win64\BrGame.exe`
- Content containers: `Build\Package\Windows\BrGame\Content\Paks\`
- Final measured folder: **83 files, 1,116,172,252 bytes (1,064.5 MiB / 1.040 GiB)**.
  The smoke logs used below were redirected under the project `Saved\Logs`.

## Packaged runtime smoke

Repeat with:

```powershell
& .\Scripts\RunPackagedSmoke.ps1 `
  -Packaged "$PWD\Build\Package\Windows\BrGame.exe" `
  -TimeoutSeconds 360
```

Result: **PASS**. The script starts the packaged main menu, then uses the
existing Development-only `-BrFlowSmoke` hook to run a host and two IP clients
with `-nosteam`. The deterministic flow-smoke host starts at `L_Lobby` rather
than clicking the Host widget in the menu; the menu map is separately loaded
and verified first. Logs confirmed:

- `/Game/BrGame/Maps/L_MainMenu` loads.
- Host and two clients reach `L_Lobby`; host travels to `L_Town`.
- Host and both clients spawn `BrCharacter` pawns; late join is admitted.
- Match results appear and all clients return through `L_Transition` to
  `L_Lobby`.
- `LogStreaming` reports load completion for `DA_BrInputConfig`, `M_Fog`,
  `M_ZoneRing`, and `DA_BrPickupTable`; the host also reports fog render
  targets initialized (`Texture=2048`) and `Pickups spawned`.
- No game-asset load failures, null soft-object errors, BrGame errors,
  streaming errors, fatal errors, or assertion failures were found.

Logs:

- `Saved\Logs\MP\Packaged\MainMenu.log`
- `Saved\Logs\MP\Flow\Host.log`
- `Saved\Logs\MP\Flow\Client1.log`
- `Saved\Logs\MP\Flow\Client2.log`

The runtime log does contain UE's optional profiler/instrumentation DLL
messages (`aqProf.dll`, `VtuneApi*.dll`, `WinPixGpuCapturer.dll` absent); none
is a project asset. The match smoke also emitted non-fatal zone-nav retry
warnings and retained its verified playable-ground fallback after the
10-second nav retry budget. The match/results/lobby flow still completed.

## Steam / two-PC validation

No `Steam.exe` process was running during this pass, so a packaged Steam
initialization, App ID 480, or SteamSockets-listen result is **not claimed**.
The local smoke deliberately passed `-nosteam`. The archive contains no
`steam_appid.txt`; `Config/DefaultEngine.ini` still specifies
`SteamDevAppId=480` (unchanged). Keep any Development-only app-id file local
and temporary if the UE/Steam setup on a test PC requires one; do not commit
or distribute it. App ID 480 is Steam's Spacewar test ID, not a production ID.

Two-PC procedure:

1. Copy the **entire** `Build\Package\Windows` folder to the second Win64 PC;
   do not copy only `BrGame.exe`. Keep Steam installed and running on both PCs,
   using two distinct logged-in accounts.
2. Launch `BrGame.exe` from that folder on both PCs **without** `-nosteam`.
   Do not use PIE. Host a public game on PC A; on PC B refresh the browser and
   join. Confirm both can travel into `L_Town` and that the session/browser
   displays the expected game.
3. Check each Development log for `[AppId: 480] Client API initialized 1`,
   successful session callbacks, and `SteamSocketsNetDriver_0 started
   listening`. A successful local Null/IP smoke is not evidence of Steam
   relay, lobby discovery, invites, or cold-start routing.
4. Record whether `steam_appid.txt` is needed by the UE/Steam startup on those
   PCs; if used, it is a local Development test aid only and must not enter
   source control or the distributed folder. Do not change the configured App
   ID for this test.

## Remaining verification

- Steam client initialized with App ID 480 and SteamSockets on a packaged run.
- Actual two-PC Steam lobby/browser/relay join, invites, and manual UI Host
  button interaction.
- Run on a second Windows machine with its graphics/runtime drivers; this
  machine's package ran locally in a windowed Development configuration.
