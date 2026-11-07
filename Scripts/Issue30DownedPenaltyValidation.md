# GitHub #30 — Downed penalty items

Implemented 2026-11-07, based on clean `4f98924`; no commit made.
Engine: `C:\Program Files\Epic Games\UE_5.8` (reported 5.8.4).

## Behavior / decisions

- Rebindable Plant defaults to F, verified free in the existing BR defaults and
  serialized mapping. Hold duration is `PlantDuration=10 s`; range uses
  `ReviveRadius=150 cm`. Server receives held/released intent only, finds the
  target, checks admission/life/range/round phase, and owns the deadline and RNG.
  Release, damage, non-alive planter, range loss, target leaving Downed, target
  destruction, menu/map opening and round end cancel. No client-selected item,
  target, progress, score or timer is accepted.
- Plant and revive are independent. Life service selects the earliest valid
  revive/voluntary-respawn/timeout deadline once; both plant arbitration and life
  resolution use that snapshot. Exit ties prefer revive, then timeout, then
  voluntary respawn; any tied exit defeats planting. Earliest valid plant wins; all competing holds cancel and a
  persistent one-per-down flag prevents repeat awards (even after effect expiry).
  Weak target-local planter registration avoids scanning every character each
  service frame. Service uses timers, not component Tick.
- Uniform server choice from valid unique configured definitions. Open-ended
  FName IDs and data-driven flags/multipliers avoid a closed gameplay type enum.
  Invalid/missing/duplicate definitions log errors; an empty valid list disables
  planting. Four generated assets: Rocket, Smoke, Sound, Buttplug.
- Successful planting adds +1 through `FBrQuestRules::AddScore` to the authority
  journal, then publishes the journal to PlayerState in either match mode.
  Round records retain the definition, absolute times and one-per-down flag.
  Respawn transfers the snapshot. Offline downed timeout starts a dormant effect
  at its original timeout deadline, not reconnect time. Expired restores do not
  renew effects. Elimination, forfeit and round reset clear penalties.
- Effects are dormant until revive/respawn, then Rocket lasts 15 s and others
  30 s (per-definition developer tuning). Later downs do not pause active timers;
  a new plant replaces the old effect with a dormant one.
- **Rocket prediction:** replicated authority definition/start/end plus
  synchronized GameState server time. CMC applies forced horizontal facing
  velocity on owner and authority (walking/falling; gravity/collision remain
  native). Saved moves retain synchronized time and historical control yaw for
  replay; penalty moves do not combine across timer boundaries. The server never
  trusts a client effect flag/time. Sprint/slide are suppressed, standing is
  requested through native clearance checks, shooting remains available.
  Activation/expiry replication and latency can cause transient boundary
  corrections; steady-state forcing does not rely on unpredicted server input.
  Real network correction counts have NOT been measured.
- Speed multiplies weapon-adjusted normal run speed: Rocket ×2, Buttplug ×0.5.
  Both disable sprint/slide so slide cannot bypass the speed rule. Ordinary
  posture modifiers still apply unless standing is forced. Native ceiling
  clearance is not bypassed to force an unsafe capsule expansion.
- **Cosmetics:** pelvis-attached, backward-oriented tinted BasicShapes cylinder.
  Existing pickup tint material is reused. Flame/smoke use up to 16 reusable
  non-colliding mesh puffs per affected actor, world-space, smoke lifetime 2.4 s.
  Engine search found smoke tutorial textures/materials, not a usable gameplay
  Niagara fire/smoke system. Optional per-definition Niagara soft slot can
  replace puffs later; no new module dependency/plugin/subfolder was needed.
  Mesh/material CDO references keep engine placeholders cook-discoverable.
- **Fog/audio:** cosmetics remain character-owned, subject to existing actor
  relevancy and local primitive fog presentation. Local character mesh visibility
  also gates tube, existing puffs, emissions, Niagara and attached looping audio,
  covering the channel-close delay and listen-host presentation. Hidden trails
  are discarded. No AudioSubsystem `Emit` route (which can play through fog).
  Original synthesized toy-flute SoundWave loops on SC_Effects and the existing
  master/Effects volume mix with positional attenuation; Smoke has no sound.
- HUD adds bound-key Plant prompt/progress and victim name/timer/dormant status
  to the left status panel. Existing E priority stays unchanged. No world text.
  Session protocol is 17.

## Changed paths

- New `Source/BrGame/Life/BrPenaltyRules.h`, `BrPenaltyDefinition.h`,
  `BrPenaltyComponent.h/.cpp`; `Source/BrGame/Tests/BrPenaltyTest.cpp`.
- Life wiring: `Source/BrGame/Life/BrLifeComponent.cpp`, `BrRoundRecord.h`.
- Core: `Source/BrGame/Core/BrCharacter.h/.cpp`,
  `BrCharacterMovementComponent.h/.cpp`, `BrDeveloperSettings.h`,
  `BrGameMode.h/.cpp`, `BrQuestGameMode.cpp`, `BrHUD.cpp`, `BrInputConfig.h`,
  `BrMenuController.cpp`, `BrPlayerController.cpp`, `BrSessionRules.h`.
- Auxiliary teardown fix: `Source/BrGame/Combat/BrInventoryComponent.cpp`;
  protocol assertions: `Tests/BrCrateTest.cpp`, `Tests/BrSoundEventsTest.cpp`;
  valid possession fixture: `Tests/BrSlideTest.cpp`.
- `Config/DefaultGame.ini`; `Scripts/Content/create_penalty_assets.py`,
  `create_input_assets.py`, `create_sound_assets.py`,
  `Scripts/Content/Audio/SW_PenaltyFlute.wav`.
- Generated `Content/BrGame/Penalties/DA_Penalty_{Rocket,Smoke,Sound,Buttplug}.uasset`,
  `Content/BrGame/Audio/SW_PenaltyFlute.uasset`,
  `Content/BrGame/Input/Actions/IA_Plant.uasset`,
  `Content/BrGame/Input/IMC_BrDefault.uasset`, `DA_BrInputConfig.uasset`.
  Unchanged input-action re-save noise was restored; binaries were created through
  Unreal Python, not hand-edited.
- `GameDescription.md`, `Docs/PlaytestChecklist.md`, this validation file.
  `Issues.md` only directs readers to GitHub; it has no #30/FR entry to update.

## Commands / results

All builds, tests and content commandlets were serial, with no editor/Live Coding
process left running. Run from `C:\Users\oiste\Projects\Unreal\BrGame`:

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGame Win64 Development -Project="$PWD\BrGame.uproject"

$env:BR_SOUND_ONLY = "PenaltyFlute"
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript -script="$PWD\Scripts\Content\create_sound_assets.py" -unattended -nullrhi -nosound -nosteam
# In a fresh shell (no BR_SOUND_ONLY required):
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript -script="$PWD\Scripts\Content\create_penalty_assets.py" -unattended -nullrhi -nosound -nosteam
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript -script="$PWD\Scripts\Content\create_input_assets.py" -unattended -nullrhi -nosound -nosteam

# Each filter below was run in its own process, serially; finally use BrGame.
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame.Penalty; Quit" -unattended -nullrhi -nosound -nosteam "-abslog=$PWD\Saved\Logs\Issue30PenaltyFinal.log"
git --no-pager diff --check
```

Final editor/game UBT results: **Succeeded**, exit 0 (full UBT, no Live Coding).
Three content commandlets: exit 0, 0 errors, one expected `-nosteam` warning each.
Final automation counts, directly counted from completed-test log records:

| Filter | Passed | Failed | Exit | Log under Saved/Logs |
|---|---:|---:|---:|---|
| BrGame.Penalty | 5 | 0 | 0 | Issue30PenaltyFinal.log |
| BrGame.Life (includes reconnect) | 7 | 0 | 0 | Issue30LifeFinal.log |
| BrGame.Menu (rebinding) | 3 | 0 | 0 | Issue30MenuFinal.log |
| BrGame.Session | 6 | 0 | 0 | Issue30SessionFinal.log |
| BrGame.Sprint | 6 | 0 | 0 | Issue30SprintFinal.log |
| BrGame.Slide | 4 | 0 | 0 | Issue30SlideFinal.log |
| BrGame.Aiming | 8 | 0 | 0 | Issue30AimingFinal.log |
| BrGame (full suite) | 131 | 0 | 0 | Issue30AllFinal.log |

Penalty tests include pure eligibility/deadline/timer/speed policy, generated
definition/input checks and a three-player authority world for cancellation,
competition, revive ordering, score/ledger, replacement, restore, Rocket saved
facing/time, standing/sprint/slide and cosmetic pool/fog gating.

Earlier failures were not ignored:
- Initial editor compilation failed on a TObjectPtr range-loop deduction and
  ambiguous UniqueNetId test conversion; fixed and rebuilt.
- Initial Penalty run: 4 passed, 1 failed due to fixture collision-adjusted player
  positions; fixed fixture positions (no gameplay range relaxation).
- Slide regression: 1 passed, 3 failed because its pre-existing fixture lacked
  BrPlayerState for armor restore. Added the required state before possession.
- First full suite: 33 completed successes, then access-violation crash during GC
  in Inventory CancelCombat after a fixture world had been destroyed. Guarded
  world-dependent timer/RPC teardown in inventory/character/life/penalty; state
  clearing and delegate removal remain. No production error logs suppressed.
- `BrGame.Reconnect` matched zero tests and exited 255; actual reconnect tests
  live under `BrGame.Life`, which passed.

## Independent-review fixes and revalidation

Both P2 findings fixed on 2026-11-07; no additional asset changes or commit.

1. **Single Downed-exit ordering:** `FBrLifeRules::SelectDownedExit` compares valid
   absolute deadlines, with explicit ties as documented above. Life service
   validates candidates once and passes that selection into plant arbitration
   before resolving the same exit. Planter service only cancels invalid holds;
   target life service owns completion, so a late frame cannot independently
   reject an earlier plant solely because wall time has passed the timeout.
   New pure life coverage and the authority penalty fixture cross voluntary=10,
   plant=15, revive=20, timeout=25 in one service frame at 30: voluntary wins,
   later plant grants no item/score, and later revive is cancelled.
   The mapless authority fixture deliberately has no valid respawn ground:
   placement warns and retries, but never substitutes the later revive.
2. **Saved-binding migration:** shared `FBrInputProfiles::Install` is used by
   gameplay and menu/settings registration. It reconciles defaults against
   retained customized mappings across available profiles, including contexts
   already registered during deserialization. The user's custom F stays;
   conflicting default Plant becomes explicitly Invalid/unbound, with its
   settings row showing **Unbound (click to bind)**. Prompts report `[unbound]`
   rather than misleadingly showing F. Existing manual conflict checks remain.
   UE 5.8 `UnMapPlayerKey` resets to the default, so migration instead uses
   `MapPlayerKey(Invalid)`. Real Enhanced Input profile tests confirm that the
   rebuild query returns a nonempty Invalid override (no fallback to default F),
   preserve pre-feature Fire=F through memory save/load, retain the editable
   Plant row, and cover repeated/already-registered installation and subsequent
   user rebinding. Tests never save to the user's disk profile.

Additional changed paths:
- `Source/BrGame/Life/BrLifeRules.h`, `BrLifeComponent.h/.cpp`,
  `BrPenaltyRules.h`, `BrPenaltyComponent.h/.cpp`.
- `Source/BrGame/Core/BrInputProfiles.h/.cpp` (new), `BrMenuRules.h`,
  `BrPlayerController.cpp`, `BrAudioSettingsPanel.cpp`, `BrMenuController.cpp`.
- `Source/BrGame/Tests/BrLifeTest.cpp`, `BrPenaltyTest.cpp`,
  `BrInputProfilesTest.cpp` (new).
- `GameDescription.md`, `Docs/PlaytestChecklist.md`, this file.

Commands actually run, serially, after the fixes:

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGame Win64 Development -Project="$PWD\BrGame.uproject"
# Each group ran in its own process, in this order:
foreach ($Group in @("Penalty", "Life", "Input", "Menu", "Audio", "Full")) {
    $Filter = if ($Group -eq "Full") { "BrGame" } else { "BrGame.$Group" }
    & "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" "-ExecCmds=Automation RunTests $Filter; Quit" -unattended -nullrhi -nosound -nosteam "-abslog=$PWD\Saved\Logs\Issue30Review$Group.log"
}
git --no-pager diff --check
```

Editor **Succeeded**, exit 0 (34.02 s); game **Succeeded**, exit 0 (36.01 s).
No editor/Live Coding process remained. No follow-up build or test failed.
Completed-test records, not merely process exit, give:

| Filter | Passed | Failed | Exit | Log under Saved/Logs |
|---|---:|---:|---:|---|
| BrGame.Penalty | 5 | 0 | 0 | Issue30ReviewPenalty.log |
| BrGame.Life | 8 | 0 | 0 | Issue30ReviewLife.log |
| BrGame.Input | 1 | 0 | 0 | Issue30ReviewInput.log |
| BrGame.Menu | 4 | 0 | 0 | Issue30ReviewMenu.log |
| BrGame.Audio (includes PerKindVolume / VolumePersistence settings tests) | 9 | 0 | 0 | Issue30ReviewAudio.log |
| BrGame (full suite) | 134 | 0 | 0 | Issue30ReviewFull.log |

There is no separate existing Settings filter; menu/input and audio volume
coverage were run above, then the complete suite. Logs still contain six
pre-engine-initialization `LogAutomationTest: Error: Condition failed` lines,
also present in the original passing logs; these are outside the requested
BrGame test runs. The mapless penalty respawn warning is described above.
`git diff --check` passed.

## Remaining human/editor work

No required Blueprint/map/animation edits remain for these placeholders.
Optional production Niagara/flame/smoke and tube art can be supplied later.
NOT verified: multi-process gameplay/lag correction counts, rendered pelvis tube
alignment on supine/prone/remote poses, audible flute/master/Effects mix, physical
input focus/actual on-disk old-profile UI migration (memory profile migration is
now automated), packaged cooking, Steam, spectator/late-join
visuals and real wall/vision transitions. Use the #30 entries in
`Docs/PlaytestChecklist.md`; these are manual checks, not claimed passes.
