# GitHub #23 — Armor plates (2026-11-06)

## Implementation
- `Combat/BrArmorRules.h` is pure, header-only policy; `Combat/BrArmorComponent.{h,cpp}` owns pawn armor without Tick. Damage consumes the lowest positive plate first, then other plates, then health in the same hit.
- Posture is applied first. Bullets, explosions and other/debug damage hit armor; zone damage bypasses it. Armor-only damage participates in hit feedback and life damage notification, but cannot down a player without health depletion.
- `Plates` replicates `COND_OwnerOnly`. Spectators receive a pawn-tagged private controller mirror, also `COND_OwnerOnly`, refreshed on server target selection, armor mutation and existing spectator service. Public PlayerState never replicates armor.
- Authority PlayerState retains exact remaining values through down/revive/new-pawn possession. Round records retain reconnect snapshots. New round and final elimination clear armor (intentional assumption).
- Armor pickups are automatic, reject full/partial occupied slots, and breaking a plate queues the existing coalesced next-tick auto-pickup retry.
- Breaks route `EBrSoundKind::ArmorBreak` through `ClientSound` to the victim and current authorized spectator listeners, then `UBrAudioSubsystem::Play` / `PlaySound2D`, using existing PickupVoice concurrency. Existing hit feedback has no attacker-sound route, so none was added. Missing audio uses existing once-per-path error handling and never blocks gameplay.
- HUD draws per-plate segments/readouts alongside health for owners and selected spectators.
- Protocol is **15**, not 14: this checkout already used 14 for quests; reusing it for an incompatible layout would admit older clients.

## Changed paths
Relative to `Source/BrGame/` unless otherwise noted:
- New: `Combat/BrArmorRules.h`, `Combat/BrArmorComponent.h`, `Combat/BrArmorComponent.cpp`, `Tests/BrArmorTest.cpp`.
- `Core/BrCharacter.{h,cpp}`, `Core/BrPlayerState.{h,cpp}`, `Core/BrPlayerController.{h,cpp}`.
- `Core/BrDeveloperSettings.{h,cpp}`, `Core/BrHUD.cpp`, `Core/BrHitFeedbackComponent.{h,cpp}`.
- `Core/BrAudioRules.h`, `Core/BrAudioSubsystem.cpp`, `Core/BrSessionRules.h`.
- `Core/BrGameMode.{h,cpp}`, `Core/BrMenuGameMode.cpp`, `Life/BrLifeComponent.cpp`, `Life/BrRoundRecord.h`.
- `Pickups/BrPickupDefinition.{h,cpp}`, `Pickups/BrCrateRules.h`, `Pickups/BrPickupManager.cpp`, `Tests/BrCrateTest.cpp`.
- `Config/DefaultGame.ini`, `GameDescription.md`, this validation record.
- No binary assets, dependency/plugin changes, generated-file edits or commits.

## Tuning/config
`UBrDeveloperSettings` and `Config/DefaultGame.ini`:
- `CrateArmorMin=2`, `CrateArmorMax=8` (uniform inclusive count per crate).
- `ArmorPlateValue=20`, `MaxArmorPlates=2`.
- `ArmorBreakSound=/Game/BrGame/Audio/SW_ArmorBreak.SW_ArmorBreak`.
- Ninth `CrateDefinitions` entry: `/Game/BrGame/Pickups/DA_Pickup_Armor.DA_Pickup_Armor`; existing indices unchanged.
- `PickupRegistry` key `Armor` points to the same pickup asset.

## Actual commands/results
Working directory: `C:\Users\oiste\Projects\Unreal\BrGame`. Editor/Live Coding process checks found none before builds/tests. Full UBT editor builds, not Live Coding.

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
```

Final build succeeded (exit 0). An initial build exposed a shadowed Controller::Pawn parameter and an ambiguous test UniqueNetId conversion; both fixed. The first combined automation attempt exposed missing replication metadata initialization in the new privacy test; fixed by following FRepLayout's SetUpRuntimeReplicationData initialization.

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame.Armor+BrGame.Crate+BrGame.Pickup+BrGame.Session+BrGame.Life; Quit' -unattended -nullrhi -nosound -nosteam "-abslog=$PWD\Saved\Logs\Issue23Tests.log"
```

After the test fix, 9 tests passed before the known unrelated `UBrInventoryComponent::CancelCombat` null-world EndPlay crash (exit 3). Here it occurred during RuntimeLifecycle map loading/GC, not after all suites; later suites therefore did not execute in this combined process.

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame.Armor+BrGame.Pickup+BrGame.Session+BrGame.Life; Quit' -unattended -nullrhi -nosound -nosteam "-abslog=$PWD\Saved\Logs\Issue23NoCrateTests.log"
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame.Crate; Quit' -unattended -nullrhi -nosound -nosteam "-abslog=$PWD\Saved\Logs\Issue23CrateTests.log"
```

Non-crate suites: 24/24 passed, exit 0. Isolated crate suite: 7/8 passed, exit 255. RuntimeLifecycle logged `Crates disabled: missing loot definition /Game/BrGame/Pickups/DA_Pickup_Armor.DA_Pickup_Armor`, found zero crates, and timed out at 90 seconds. No missing-asset errors were suppressed and no fallback armor asset was created.

Final verification after final authority/feedback edits:

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame.Armor+BrGame.Pickup+BrGame.Session+BrGame.Life+BrGame.Crate.Ground+BrGame.Crate.MapCountsAndWaveSpacing+BrGame.Crate.RaisedTaggedFloorFootprint+BrGame.Crate.ScatterAndLanding+BrGame.Crate.ScheduleAndHostClamp+BrGame.Crate.SeededLoot; Quit' -unattended -nullrhi -nosound -nosteam "-abslog=$PWD\Saved\Logs\Issue23FinalTests.log"
git --no-pager diff --check
```

**31/31 passed, exit 0**, including all five BrGame.Armor tests. Diff check passed. The one excluded test is the demonstrated asset-blocked RuntimeLifecycle, not an asserted success.

## Content/editor handoff
1. Create `/Game/BrGame/Pickups/DA_Pickup_Armor` as a native `UBrArmorPickupDefinition` data asset. Set `bRequiresInteract=false`, `DisplayName`/`Label` to Armor plate, valid `Mesh` and `Material`, readable `Color`/`VisualScale`, optional `PickupSound` (otherwise existing default pickup sound). Plate value/cap come from developer settings, not this asset.
2. Create `/Game/BrGame/Audio/SW_ArmorBreak` (`USoundWave` or another `USoundBase` at the configured path), using the existing effects sound class. It plays as private 2D feedback.
3. Rerun the isolated `BrGame.Crate` suite once the pickup asset exists; crates are intentionally disabled until then.
4. Restart the editor after reflected layout changes. Manually validate network owner/opponent/spectator privacy and target switching, stationary pickup on plate break, down/revive/respawn retention, round reset/elimination clearing, segmented HUD, armor hit feedback and audible break sounds. No visual/audio/multiplayer gameplay claim is made from the headless automation run.

## Follow-up — network armor E2E evidence (2026-11-06)

The content step has now supplied DA_Pickup_Armor and SW_ArmorBreak. The caller reports crate automation 8/8 and RunCratePlacementCheck passing with Armor spawning; those commands were not rerun in this follow-up.

### Changes in this follow-up only
- `Source/BrGame/Core/BrMatchSmoke.cpp`: a per-round authority fixture after the existing hit/audio/sprint probes. Grants `{7,20}` through Clear/AddPlate/Absorb, waits for real remote observations, applies an admitted native `FBrDamageEvent` bullet for 10 post-posture damage, verifies health unchanged and remaining `{17}`, and waits for owner feedback/sound receipt plus non-owner privacy acknowledgements. Restores locations, movement modes and teams, and clears fixture armor before existing health-delta tests.
- `Source/BrGame/Core/BrGameMode.h`: development-only fixture state. Existing end-of-round elimination also grants the host `{7,20}` and checks a remote player's real flow-spectator mirror during results.
- `Source/BrGame/Core/BrPlayerController.{h,cpp}`: guarded client probe / server diagnostic acknowledgement, bounded local sampling timer, and actual received ArmorBreak sound counter. Probe RPCs carry only stage, public player ID and role, **not armor values**. Identity lookup retries locally across fog channel opening; no forced armor replication or client grant/damage/spectator selection is added. Acknowledgements are accepted only from admitted remote controllers for the expected stage/target under BrMatchSmoke; they only affect smoke diagnostics. Shipping implementations are inert and teardown clears the sampling timer.
- `Scripts/RunMatchSmoke.ps1`: requires owner and non-spectating-client initial/post-hit PASS lines in each round, both spectator mirror PASS lines, and both authority absorption/spectator PASS lines. Adds `E2E ARMOR PROBE FAIL` to immediate/final failure patterns; removes no existing checks.
- This validation record only; no other documentation/config/content edits in the follow-up.

Clients must observe the expected state continuously for a full second, within five seconds. The host waits at most seven seconds for each remote acknowledgement. The private peer must have a resolved, locally visible target actor, empty target armor, empty spectator mirror and empty public PlayerState backing storage; a missing fog channel is not accepted as a privacy PASS. The owner must observe exactly one new hit-feedback event and one actually received ArmorBreak sound RPC after the hit. The spectator must receive `{7,20}` through the HUD's mirror accessor while the viewed pawn's owner-only component remains empty.

### Commands and results

```powershell
Get-Process UnrealEditor*,LiveCodingConsole -ErrorAction SilentlyContinue
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame.Armor; Quit' -unattended -nullrhi -nosound -nosteam "-abslog=$PWD\Saved\Logs\Issue23ArmorFollowupTests.log"
Copy-Item Scripts\RunMatchSmoke.ps1 Scripts\_TmpMatchSmokeTown.ps1
# In the temporary copy only, append to Host Launch's extra args:
# "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:DefaultMapId=Town"
.\Scripts\_TmpMatchSmokeTown.ps1 -RunLabel Town
# Delete the temporary copy after validation.
git --no-pager diff --check
```

- Full editor build succeeded, exit 0. An initial build caught unbraced UE_LOG/else macro branches in the new probe; fixed before execution.
- BrGame.Armor: 5/5 passed, exit 0, including runtime lifecycle and owner-only replication metadata.
- First smoke exposed a diagnostic race: the one-shot host-pawn RPC argument was null before its spectator fog channel opened. Replaced that argument with public identity and bounded local lookup; no production replication policy changed.
- Next smoke passed both armor owner/private stages and the first-round spectator mirror, but the pre-existing second-round grenade fixture timed out. Its explosion had one candidate. No grenade/map/timeout assertion was changed or removed.
- Subsequent full smoke: **MATCH SMOKE PASS (MatchTown)**, exit 0, two rounds; every pre-existing assertion and new armor assertion passed. Final evidence is in `Saved/Logs/MP/MatchTown/{Host,Client1,Client2,Client3}.log` (the runner replaces logs on retries).
- The temporary Town runner was deleted. `DefaultMapId=Battlefield` and the real script's `L_Town` expectation were intentionally left unchanged.

Observed lines (each armor case passed in both rounds; the owner/spectator was Client2 in round 1 and Client1 in round 2):

```text
E2E ARMOR PROBE PASS Owner=1 Stage=1 Plates=7,20 Health=100.0 HitEvents=0 BreakSounds=0
E2E ARMOR PROBE PASS Owner=1 Stage=2 Plates=17 Health=100.0 HitEvents=1 BreakSounds=1
E2E ARMOR PROBE PASS Owner=0 Private=1 Stage=1 Visible=1 BreakSounds=0
E2E ARMOR PROBE PASS Owner=0 Private=1 Stage=2 Visible=1 BreakSounds=0
E2E ARMOR PROBE PASS Authority=1 Absorbed=10 HealthUnchanged=1 MostDamagedFirst=1 HitFeedback=1 BreakSoundRoute=1 Private=1
E2E ARMOR PROBE PASS Spectator=1 Mirror=1 Plates=7,20 ComponentPrivate=1
E2E ARMOR PROBE PASS Authority=1 Spectator=1 Mirror=1
E2E MATCH SMOKE PASS: two rounds, clean reset, all-vs-all
```

No outstanding armor E2E blocker in the Town run. This verifies Null/IP transport replication and sound **routing**, not audible playback (`-nosound`), rendered HUD quality (`-nullrhi`), Steam transport or grouped-mode E2E. Those manual/platform checks remain separate. The default-map/script mismatch remains caller-owned and unchanged.
