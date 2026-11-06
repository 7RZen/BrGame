# GitHub #1 — Missing sounds (C++ handoff)

## Implemented decisions

- UE 5.8, Win64, existing BrGame runtime module. Read `.github/copilot-instructions.md` first; retained armor code/config/tests. No content creation, commits, dependencies, plugin changes or generated-file edits.
- Appended sound kinds: Reload=9, DryFire=10, Pain=11, PainArmor=12, VoiceLine=13; NetSerialize accepts through VoiceLine. Session protocol 16.
- Reload starts only on accepted authority reloads, using weapon `ReloadSound`. Reliable owning-controller RPCs send each eligible in-range listener a sound/path, position and opaque uint32 handle. The server retains weak emitter/listener references. Cancellation sends a reliable handle-only stop to the ORIGINAL listeners even if they moved out of range, changed view target or the source lost relevancy. Local weak audio-component handles stop playback; component completion auto-destroys. Successful reload forgets the server handle without stopping natural playback. EndPlay cancels; subsystem teardown stops remaining local reloads. No emitter actor/NetGUID or client-supplied stop request is transmitted.
- Dry fire is authoritative only for empty magazine AND no reserve. It advances the same `NextAllowedFire` fire-rate gate as a shot. Empty magazine with reserve still auto-reloads; no click. Owner and nearby eligible listeners hear the new authority events, without local prediction duplicates.
- Per-victim pain/armor-hit throttle is shared. Zone, rejected damage and downing hits emit neither. Full armor absorption emits PainArmor; any health loss emits Pain. Existing downed scream and armor-break behavior remain intact.
- `CreditDown` uses the existing `CaptureOpponentDowner` policy to exclude self/team/zone paths from voice lines. Only the attacker's active pawn speaks. Server chooses uniformly among unique non-null soft sound paths, excluding that player's last path when >1 line is available. No cooldown.
- Voice preference is saved in `BrAudio` local profile v3, alongside voice volume. Versions 1/2 retain old values and default voice=0 / volume=1. Server-only PlayerState index/history persist across pawns and CopyProperties/OverrideWith. Reliable `ServerSetVoiceIndex` accepts only configured indices; invalid input falls back to 0. Sync occurs on local BeginPlay, OnRep_PlayerState, server possession request, and preference changes.
- Native settings panel adds a Voice slider and “My voice” cycler showing the set DisplayName (unavailable placeholder if absent). Preview is optional and not implemented.
- VoiceLine playback sets audio-component `SoundClassOverride` BEFORE Play, without changing shared sound assets. Only voice lines receive the VoiceSoundClass override; grunts remain effects. Voice volume uses the same sound-mix class override path as effects volume. Missing voice class logs once per instance and falls back to effects; missing/empty voice sets log once per world/path and emit no line.
- New category concurrency budgets are generated at runtime when the configurable concurrency soft reference is empty; no new concurrency assets are required. Explicit missing new-category concurrency assets warn once and use the developer budget.

## Exact new developer config

Under `[/Script/BrGame.BrDeveloperSettings]`, these entries are in `Config/DefaultGame.ini`:

```ini
ReloadVoice=(MaxCount=12,ResolutionRule=StopFarthestThenOldest,Priority=40.000000,Concurrency=None)
DryFireVoice=(MaxCount=12,ResolutionRule=StopFarthestThenOldest,Priority=30.000000,Concurrency=None)
PainVoice=(MaxCount=8,ResolutionRule=StopFarthestThenOldest,Priority=70.000000,Concurrency=None)
VoiceLineVoice=(MaxCount=8,ResolutionRule=StopFarthestThenOldest,Priority=80.000000,Concurrency=None)
ReloadHearingRange=1500.000000
DryFireHearingRange=1500.000000
PainHearingRange=1000.000000
VoiceLineHearingRange=2000.000000
PainSoundMinInterval=0.500000
DryFireSound=/Game/BrGame/Audio/SW_DryFire.SW_DryFire
PainSound=/Game/BrGame/Audio/SW_Pain.SW_Pain
PainArmorSound=/Game/BrGame/Audio/SW_PainArmor.SW_PainArmor
!VoiceSets=ClearArray
+VoiceSets=/Game/BrGame/Audio/Voice/DA_Voice_David.DA_Voice_David
+VoiceSets=/Game/BrGame/Audio/Voice/DA_Voice_Mark.DA_Voice_Mark
+VoiceSets=/Game/BrGame/Audio/Voice/DA_Voice_Zira.DA_Voice_Zira
VoiceSoundClass=/Game/BrGame/Audio/SC_Voice.SC_Voice
```

Ranges are cm; interval is seconds. PainArmor shares PainHearingRange/PainVoice. Local player defaults are VoiceIndex=0 and Voice=1; these are comfort settings, not developer config. `UBrWeaponDefinition::ReloadSound` is an initially empty per-weapon soft sound reference, not a global config entry.

## Content/editor work (not performed)

Create/import non-looping USoundBase assets (normally SoundWave):

- `/Game/BrGame/Audio/SW_DryFire.SW_DryFire`
- `/Game/BrGame/Audio/SW_Pain.SW_Pain`
- `/Game/BrGame/Audio/SW_PainArmor.SW_PainArmor` — deliberately muted compared with normal pain.
- Suggested agreed handoff names for per-weapon assets:
  - `/Game/BrGame/Audio/SW_Reload_Pistol.SW_Reload_Pistol` — light, duration ~1.5 s.
  - `/Game/BrGame/Audio/SW_Reload_AssaultRifle.SW_Reload_AssaultRifle` — medium, duration ~2.5 s.
  - `/Game/BrGame/Audio/SW_Reload_MachineGun.SW_Reload_MachineGun` — heavy, duration ~4 s.
  - Set **ReloadSound** (Python `reload_sound`) on existing `/Game/BrGame/Weapons/DA_Weapon_Pistol.DA_Weapon_Pistol`, `DA_Weapon_AssaultRifle.DA_Weapon_AssaultRifle`, and `DA_Weapon_MachineGun.DA_Weapon_MachineGun`. Reload paths are not hardcoded: content may choose alternatives if these properties are assigned. Match duration to each actual weapon's ReloadTime and do not loop, because FinishReload deliberately does not stop playback.
- Assign these effects assets to existing `/Game/BrGame/Audio/SC_Effects.SC_Effects`; use appropriate priorities above and bypass volume-scale priority if quiet grunts/clicks should retain category priority.

Create `/Game/BrGame/Audio/SC_Voice.SC_Voice` as **USoundClass**, child of existing `SC_Master` (sibling of `SC_Effects`, not its child). Add it to existing **USoundMix** `/Game/BrGame/Audio/SM_Volume.SM_Volume` with default volume/pitch 1.

Create **UBrVoiceSet** data assets; Python class is **`unreal.BrVoiceSet`**, reflected script class `/Script/BrGame.BrVoiceSet`. Properties are **DisplayName** / **DownLines** (Python `display_name` / `down_lines`):

- `/Game/BrGame/Audio/Voice/DA_Voice_David.DA_Voice_David`, DisplayName “David”.
- `/Game/BrGame/Audio/Voice/DA_Voice_Mark.DA_Voice_Mark`, DisplayName “Mark”.
- `/Game/BrGame/Audio/Voice/DA_Voice_Zira.DA_Voice_Zira`, DisplayName “Zira”.

Fill each DownLines array with the corresponding non-looping voice SoundWave/SoundCue soft references, preferably >=2 DISTINCT paths. Individual line paths are intentionally data-driven, with no C++ naming dependency. Assign their SoundClass to SC_Voice and priority 80; runtime class override also enforces voice routing. No Blueprint/UI asset work is required.

After installing content, restart the editor (new reflected asset/properties), verify asset loading/cooking, and audibly test:

- Listen server + client reload start and cancel on weapon switch/down/destruction, including listener leaving range and rapid cancel/restart. Reload completion ends naturally.
- Empty magazine with/without reserve, automatic hold and rapid presses (fire-rate throttle).
- Full/partial armor hits, unarmored damage, 0.5 s throttle, zone silence and down scream without pain.
- Attacker voices on enemy downs only; no self/team/elimination line; no immediate repeat across respawns. Check distinct local choices on two players, preference after reconnect/seamless travel, and save migration from v1/v2.
- Voice slider affects lines only; effects affects pain/reloads/clicks; master affects both. Verify spectator hearing and fog-independent range routing.

## Commands and actual results — 2026-11-06

Checked process list before every build/test with `Get-Process UnrealEditor*,LiveCodingConsole -ErrorAction SilentlyContinue`; none were running. No process termination was needed.

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
$P = "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$P" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -ExecCmds="Automation RunTests BrGame.Audio+BrGame.Sounds+BrGame.Armor+BrGame.Session; Quit" -unattended -nullrhi -nosound -nosteam -abslog=C:\Users\oiste\Projects\Unreal\BrGame\Saved\Logs\Issue1Tests.log
git --no-pager diff --check
```

- Initial full editor build succeeded, exit 0 (21.75 s). Existing template unity compile warning C4005 `LOCTEXT_NAMESPACE` redefinition was not changed.
- First combined run: 21 tests succeeded, then NEW WireAndProfile test crashed because plain FMemoryWriter does not support FSoftObjectPath serialization. Fixed the test to use FNetBitWriter/FNetBitReader, matching existing audio tests; not a gameplay failure or the known CancelCombat issue.
- Rebuild succeeded, exit 0 (3.63 s).
- Final combined run: **22/22 Success** — Armor 5, Audio 7, Session 6, Sounds 4. `**** TEST COMPLETE. EXIT CODE: 0 ****`; process exit 0. Final log: `Saved/Logs/Issue1Tests.log` (engine-generated). Startup logged six generic `LogAutomationTest: Error: Condition failed` messages before test discovery, but no selected test failed. Known CancelCombat crash did not occur in the final run.
- `git --no-pager diff --check` passed. No editor/LiveCoding process remained. No audible gameplay/cooking/package validation was run. Aiming/Crate protocol/profile assertions were updated but those suites were not requested/run.

## Changed paths

```text
Config/DefaultGame.ini
GameDescription.md
Scripts/Issue1SoundsValidation.md
Source/BrGame/Combat/BrCombatDefinitions.h
Source/BrGame/Combat/BrInventoryComponent.cpp
Source/BrGame/Combat/BrInventoryComponent.h
Source/BrGame/Core/BrAudioRules.h
Source/BrGame/Core/BrAudioSettingsPanel.cpp
Source/BrGame/Core/BrAudioSettingsPanel.h
Source/BrGame/Core/BrAudioSubsystem.cpp
Source/BrGame/Core/BrAudioSubsystem.h
Source/BrGame/Core/BrCharacter.cpp
Source/BrGame/Core/BrCharacter.h
Source/BrGame/Core/BrDeveloperSettings.cpp
Source/BrGame/Core/BrDeveloperSettings.h
Source/BrGame/Core/BrGameInstance.cpp
Source/BrGame/Core/BrGameInstance.h
Source/BrGame/Core/BrGameMode.cpp
Source/BrGame/Core/BrGameMode.h
Source/BrGame/Core/BrPlayerController.cpp
Source/BrGame/Core/BrPlayerController.h
Source/BrGame/Core/BrPlayerState.cpp
Source/BrGame/Core/BrPlayerState.h
Source/BrGame/Core/BrSessionRules.h
Source/BrGame/Core/BrSoundEventRules.h
Source/BrGame/Core/BrVoiceSet.h
Source/BrGame/Tests/BrAimingTest.cpp
Source/BrGame/Tests/BrAudioTest.cpp
Source/BrGame/Tests/BrCrateTest.cpp
Source/BrGame/Tests/BrSoundEventsTest.cpp
```

The pre-existing untracked `Scripts/Content/Audio/Voice/` directory was neither read nor modified. Armor implementation and its status row remain intact.

## Follow-up — bounded network smoke coverage, 2026-11-06

Content was completed by the caller before this follow-up (three weapon reloads,
dry fire, pain/armor pain, SC_Voice in the master/mix, and three ten-line voice sets).
No content assets or content scripts were changed by this follow-up.

### Coverage and safety

- Added eight acknowledged sound stages to the existing `BrMatchSmoke` flow,
  after the existing armor fixture in EACH round. No previous checks were removed.
  Each stage is armed by the remote Client3 controller, acknowledged to authority,
  stimulated through production gameplay, checked after a bounded delivery
  window, and acknowledged again. C++ stage deadline is seven seconds; failures
  log `E2E SOUND PROBE FAIL` and the runner aborts on that pattern.
- Three reload stages use Pistol/AssaultRifle/MachineGun definitions and load their
  actual ReloadSound references. Receive hooks check the expected per-weapon path,
  quantized emitter position, nonzero ID, exactly one start, and exactly one stop
  with the SAME ID. Authority waits for the real received-start acknowledgement
  before cancelling through inventory RestoreState. Script assertions additionally
  require all three exact `SW_Reload_*` paths twice.
- Dry fire empties magazine AND all reserve. Two bursts of 20 authority fire
  requests separated by the weapon fire interval exercise the actual
  `ServerFire_Implementation` rate gate (not only the local prediction limiter).
  Exactly two clicks must reach Client3: one per accepted trigger interval.
  Development-only GameMode friendship provides fixture access, not a new
  client-facing combat endpoint.
- Near damage requires one normal Pain and one fully absorbed PainArmor, with
  exact developer sound references/positions and no down. Zone damage really
  reduces health but yields neither event. Repeating both combat damage cases
  with Client3 at `PainHearingRange + 500` cm (15 m at defaults) yields neither.
  Counters are reset on each acknowledged stage, including the negative cases;
  they count received new kinds even when the position is wrong, preventing an
  origin filter from hiding extra events.
- A distinct remote attacker (the existing hit-fixture victim's controller)
  sends voice index 1 using the PRODUCTION `ServerSetVoiceIndex` RPC. Authority
  requires a new successful RPC-receive counter above its baseline and stored
  index=1 before starting any sound stage. A real lethal opponent damage event
  downs the host, increments CreditDown and selects a path contained in that
  stored voice set. Client3 separately verifies exactly one VoiceLine at the
  attacker's position, membership in set 1, loaded sound, asset SC_Voice, and
  the class returned by the same production resolver used for
  UAudioComponent::SoundClassOverride.
- Extracted the existing voice class resolution into `UBrAudioSubsystem::SoundClass`;
  playback behavior/fallback is unchanged. Headless probes can exercise that
  production route without inventing a fake audio component or claiming sound
  was rendered.
- The real revive workflow restores the host before proceeding. The fixture
  restores inventory, health, teams, positions, movement modes, revive rule,
  player voice preference through the real RPC, and the host's living round
  snapshot. It neither grants persistent client choices nor writes a local save.
  All RPC implementation hooks and new non-reflected probe state are guarded
  by non-Shipping/BrMatchSmoke checks; acknowledgements cannot mutate gameplay.
  Reflected inventory snapshot storage keeps sound/gear UObject references safe.
- Runner default overall wait increased from 180 to 300 seconds because two
  rounds now contain sixteen extra acknowledged sound stages. Individual C++
  deadlines remain seven seconds. The first run genuinely timed out under the
  old total budget; no test/failure pattern was disabled to make it pass.

### Exact commands and results

```powershell
# No editor/LiveCoding processes before each build/run.
Get-Process UnrealEditor*,LiveCodingConsole -ErrorAction SilentlyContinue
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex
Copy-Item Scripts\RunMatchSmoke.ps1 Scripts\_TmpMatchSmokeTown.ps1
# In the TEMP copy only, append to Host Launch extra args:
# "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:DefaultMapId=Town"
.\Scripts\_TmpMatchSmokeTown.ps1 -RunLabel Town
# Temporary copy deleted after final run; it is not part of the change.
.\Scripts\RunAudioSmoke.ps1 -CameraPitchAngle 65 -TimeoutSeconds 90
git --no-pager diff --check
```

- First follow-up editor build failed (exit 6): UE_LOG conditional branches
  needed braces, and the fixture needed `BrZoneDamageType.h`. Fixed both;
  subsequent full builds succeeded. Final build: exit 0, 17.91 seconds.
- First Town run timed out waiting for the overall match PASS with the old
  180-second budget. Every new stage passed in round one, and reloads passed in
  round two before timeout. Runner finally block stopped only its owned PIDs.
- Final Town run: **exit 0**, 11:38:35–11:42:23 UTC, about 228 seconds.
  `MATCH SMOKE PASS (MatchTown)`; existing two-round requirements all passed.
  Final evidence is in `Saved/Logs/MP/MatchTown/{Host,Client1,Client2,Client3}.log`.
  Each of the eight client sound PASS patterns occurred twice, and the authority
  voice-set/real-RPC PASS occurred twice. No SOUND PROBE FAIL or final runner
  error-scan matches.
- Temporary `Scripts/_TmpMatchSmokeTown.ps1` was deleted after the final run.
- Audio script parameters were read first. **RunAudioSmoke passed, exit 0**:
  `AUDIO LISTENER PASS Pitch=65 ... Device=1`,
  `AUDIO WINDOW SMOKE PASS: submitted 12 category sounds with a live device`,
  `AUDIO WINDOW PASS Pitch=65`.
  Evidence: `Saved/Logs/Audio/WindowPitch65.log`. This is the existing twelve
  categories with a real device, not human listening or a new live-device voice
  cancellation test.

Observed final-run client PASS lines (each twice):

```text
E2E SOUND PROBE PASS Reload=1 Stop=1 Stage=1 Sound=/Game/BrGame/Audio/SW_Reload_Pistol.SW_Reload_Pistol Handle=1
E2E SOUND PROBE PASS Reload=1 Stop=1 Stage=2 Sound=/Game/BrGame/Audio/SW_Reload_AssaultRifle.SW_Reload_AssaultRifle Handle=2
E2E SOUND PROBE PASS Reload=1 Stop=1 Stage=3 Sound=/Game/BrGame/Audio/SW_Reload_MachineGun.SW_Reload_MachineGun Handle=3
E2E SOUND PROBE PASS DryFire=2 Attempts=2 Throttled=1
E2E SOUND PROBE PASS Pain=1 PainArmor=1 NonDowning=1
E2E SOUND PROBE PASS Zone=1 Pain=0 PainArmor=0
E2E SOUND PROBE PASS OutOfRange=1 Pain=0 PainArmor=0
E2E SOUND PROBE PASS VoiceLine=1 VoiceIndex=1 VoiceClass=1 AttackerOrigin=1
```

Final round-two authority line:

```text
E2E SOUND PROBE PASS Authority=1 VoiceIndex=1 RPC=1 OpponentDown=1 Line=/Game/BrGame/Audio/Voice/Mark/SW_Mark_Down_08.SW_Mark_Down_08
```

### Follow-up changed paths and remaining checks

```text
Source/BrGame/Combat/BrInventoryComponent.h
Source/BrGame/Core/BrAudioSubsystem.h
Source/BrGame/Core/BrAudioSubsystem.cpp
Source/BrGame/Core/BrGameMode.h
Source/BrGame/Core/BrMatchSmoke.cpp
Source/BrGame/Core/BrPlayerController.h
Source/BrGame/Core/BrPlayerController.cpp
Scripts/RunMatchSmoke.ps1
Scripts/Issue1SoundsValidation.md
GameDescription.md
```

No build/smoke blocker remains. Human listening, voice/effects slider isolation
on two machines, actual audible reload cancellation, and packaged/cooked asset
validation are still manual work. Grouped smoke and automation suites were not
rerun during this follow-up. The Town network smoke uses -nosound/-nullrhi:
receive counts and production class routing are verified, not acoustic output.

## Content evidence (editor pass)

Scripts run headlessly and serially with `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=... -unattended -nullrhi -nosound -nosteam`. Each exited 0 with no Python errors.

- `Scripts/Content/create_sound_assets.py`: `BR_SOUND_ONLY=Reload_Pistol,Reload_AssaultRifle,Reload_MachineGun,DryFire,Pain,PainArmor` generated and imported only the six new SW_ assets. Pre-existing SW_*.uasset files were not re-saved.
- `Scripts/Content/assign_weapon_reload_sounds.py`: sets `ReloadSound` on DA_Weapon_Pistol/AssaultRifle/MachineGun to SW_Reload_Pistol/AssaultRifle/MachineGun. It only saves when a value changes, and a rerun reported "already assigned" for all three.
- `Scripts/Content/create_voice_sound_class.py`: creates `/Game/BrGame/Audio/SC_Voice` (SoundClass) under SC_Master, keeping SC_Effects, and adds a neutral entry (volume 1.0, pitch 1.0) to SM_Volume. Runtime voice volume still comes from `ApplyAudioVolumes`.
- `Scripts/Content/import_voice_lines.py`: imports David, Mark and Zira (10 lines each) as `/Game/BrGame/Audio/Voice/<Voice>/SW_<Voice>_<Stem>`. It sets sound class SC_Voice and not looping, and stores the source SHA-256 as metadata so reruns skip unchanged WAVs. It creates or updates `DA_Voice_<Voice>` (BrVoiceSet). The DA_Voice paths are already in `VoiceSets`, so no config change was needed.
- Automation: `Automation RunTests BrGame.Audio+BrGame.Sounds` gave 11/11 Success (log `Saved\Logs\Issue1Content.log`). The log has no missing-reference warnings for SW_DryFire, SW_Pain, SW_PainArmor, SC_Voice or DA_Voice_*. Six "Condition failed" lines come from the engine `UE::UnifiedErrorTest` and are unrelated to these tests.

Pending manual checks: audible playback of the new placeholders in PIE or standalone, reload timing against the DA ReloadTime values (1.5 / 2.0 / 4.0 s), voice slider behavior, and cooking.
