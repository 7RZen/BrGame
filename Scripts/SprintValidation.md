# Issue 12 — tactical sprint

## Decisions and assumptions

All gameplay tuning lives in `UBrDeveloperSettings` (developer-only, not host/player UI).

| Setting | Default |
|---|---:|
| SprintSpeedMultiplier | 1.3 × weapon-adjusted run |
| SprintForwardHalfAngle | 50 degrees |
| SprintStaminaDuration | 5 seconds |
| SprintRegenDelay | 1 second |
| SprintRegenDuration | 5 seconds from empty to full |
| SprintMinimumStart | 0.5 seconds of energy |
| SprintToFireDelay | 0.2 seconds |
| SprintFireGrace | 0.03 seconds, runtime capped at 0.05 |
| SprintStepHearingRange | 2000 cm (run 1500, crouch 300) |
| SprintStepVolume | 1.3 × run |

- Hold rebindable Sprint (Left Shift). Only grounded forward/diagonal acceleration
  qualifies; strafe/backward/no input runs normally. Holding may resume when the
  forward cone or minimum energy becomes eligible again.
- Crouch requests native uncrouch first; blocked expansion cannot sprint. Prone
  ignores Shift. Hit chance uses standing; no damage reductions changed.
- Fire/throw stops sprint and queues the action after recovery. Quick fire taps
  still queue one shot; held automatic fire continues. Combat, E, posture, menus,
  downing and unstuck clear intent, requiring a new Shift press. Movement out of
  the cone and exhaustion stop the active gait without clearing held intent.
- E cancels sprint before pickup/revive. Automatic overlap pickup is rejected
  while sprinting and retried after sprint ends. No moving sprint revive.
- Regen uses accepted move time, not an extra character Tick or client timer.
  Energy resets for a new pawn; it is deliberately not saved in reconnect gear.

## Implementation

- Custom CMC saved moves carry sprint intent in `FLAG_Custom_0`; custom network
  moves report end stamina/rest/active state **only for comparison**. Authority
  derives eligibility from CMC acceleration and control yaw and integrates its own
  state. Linear drain/regen moves combine only without gait/eligibility transitions,
  rewinding stamina alongside native location before combined resimulation. A discrepancy forces a normal CMC
  correction with energy/rest/active snapshot at that same adjustment timestamp.
  Replay starts from that snapshot and integrates unacknowledged moves; it never
  restores stale predicted stamina in `PrepMoveFor`. Simulated proxies receive
  actual sprint gait with SkipOwner and normal CMC velocity/location replication.
- A reliable inventory combat-intent request cancels authority sprint and suppresses
  it while held/pending. Client waits recovery before requesting a shot/throw; server
  independently checks active sprint and authoritative recovery time before ammo,
  projectile, audio or damage. Arrival grace cannot bypass active sprint rejection.
  Deferred timers are cancelled on down/menu/end-play/gear changes.
- SprintStep reuses existing generated run placeholders (no new sound asset).
  Existing anonymous authoritative audio routing remains independent of fog.
- Input commandlet generates IA_Sprint/Br.Sprint and removes IA_Walk; saved
  Enhanced Input profiles may retain Br.Walk, but UI/reset/conflict scans ignore
  the stale row and current IMC never references it. Preferences are not erased.
- Gameplay wire protocol bumped 9 → 10 for custom CMC payload/enum changes.
  No template, plugin, dependency, map or Blueprint gameplay edits.

### Independent-review fixes

- Saved moves retain their original control yaw and simulation world time.
  `PrepMoveFor` supplies move-local replay context, cleared after each replay
  and after native correction processing. Forward eligibility uses historical
  yaw without modifying live controller rotation. Stamina still starts from the
  authoritative correction, not the saved move's stale predicted energy.
- Both explicit cancellation and movement-rule active→inactive transitions use
  one stop handler. Recovery uses the historical stop time during replay, never
  replay-now plus delay; an existing deadline cannot be restarted by replay.
  Deferred quick taps retain/re-arm their timer if a genuine newer recovery is
  still pending.
- Valid requests arriving during authoritative recovery occupy bounded,
  first-request-wins fire/throw slots with weak definition references and
  authority timers. Active sprint without cancellation is still rejected.
  Callbacks revalidate life, gameplay phase/input block, definition, ammo,
  reload, cooldown and aim/timestamp through the normal server action path.
  Early callbacks requeue rather than lose the action. Tap release preserves
  pending actions and suppresses sprint until serviced; full combat cancellation
  clears slots/timers, with a reliable RPC for remote menu cancellation.
- The shared stop handler requests the existing coalesced next-tick automatic
  pickup retry on authority, including listen-host Shift release over a pickup.
  Historical replay does not request gameplay side effects.

## Validation

Commands run serially, with editor/LiveCoding processes checked before every build:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -run=pythonscript -script="C:\Users\oiste\Projects\Unreal\BrGame\Scripts\Content\create_input_assets.py" -unattended -nullrhi -nosound -nosteam
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -log -nosteam
.\Scripts\RunAllTests.ps1 -SkipBuild
```

- Full UBT build succeeded without compiler warnings. Input commandlet succeeded;
  its only warning was the expected Steam-disabled-by-command-line message.
- Automation: 75/75 passed (previous 72 plus three `BrGame.Sprint.*` tests).
  Rules cover weapon speed multiplication, cone, kinds/ranges and recovery grace;
  stamina covers partial drain, exhaustion, regen wait/rate and minimum restart.
  World integration covers crouch→stand/prone, native saved flag round trip,
  end-stamina capture and correction serialization, server rejection and deferred
  callback. The synchronous callback test does not claim real timer expiry;
  the multi-process smoke exercises expiry over real frames.
- First regression run: 9/12 PASS. Found a pre-existing Session smoke assumption
  that every saved player name is "Player" (actual profile name differed), and
  sprint fixture's unresolved movement base/client mode. Fixed the assertions/
  fixture rather than weakening gameplay validation. A subsequent run passed
  10/12: the extra "recent sprint" check could miss a half-second sprint between
  half-second GameMode service polls despite valid 0.191/0.199 s recovery. Replaced
  that poll with an explicit client "Fire pressed during active sprint" assertion
  and exact authority acceptance recovery bounded to delay −40/+150 ms.
- **Initial implementation run: 12/12 PASS, exit 0**:
  `Saved/Logs/RunAll/20261103-220410/` (automation, Flow, Flow-Grouped, Flow-Zone,
  Flow-DisconnectGrace, Flow-LvlTopDown, Fog, Fog-Grouped, Session, Match-AllVsAll,
  Match-Grouped, Menu). Automation in this run again passed 75 tests, zero failures.
  Match smoke: two rounds in each mode, all four remote runs **780 cm/s (1.3×)**,
  **~5.00 s** exhaustion, **zero steady-sprint corrections received**.
  Actual accepted fire recovery: all-vs-all **0.202/0.210 s**, grouped
  **0.208/0.209 s**. Steady-sprint correction sampling covers elapsed 2.5–6.5 s;
  setup teleports and later combat/restore transitions are outside that metric.
  Standalone required automation also exited 0 with 75/75. No validation was run
  concurrently and no user's editor was terminated.
- Extended Match smoke runs on the late-joining remote client, on L_Town's reserved
  east-west road (capsule sweep checked), using real CMC movement and inventory requests.
  Requires speed 1.3× run, exhaustion near five seconds, zero corrections received
  during steady forward sprint (including native-rejected responses), and a
  deferred real shot on authority (measured at actual acceptance, not service polling).
  Runs twice in each mode and restores spawn position. Shipping RPC implementations are inert.

### Review-fix validation results

- Rechecked for `UnrealEditor`, `UnrealEditor-Cmd` and `LiveCodingConsole`
  before each full UBT invocation; none running. The first build found two test
  fixture type ambiguities (unique-ID overload and float/double comparison);
  fixed those. Final full Editor target build: **Succeeded, exit 0, zero compiler
  warnings**, not Live Coding. No editor was terminated.
- Ran the automation command above: **79/79 PASS, exit 0**, including four new
  regression tests:
  - `BrGame.Sprint.ReplayHistoricalFacing`: both directions across the forward
    cone during correction replay, identical eligibility/speed/stamina, unchanged
    live control yaw and cleared replay context.
  - `BrGame.Sprint.CoalescedIntentAndActions`: intent, fire and grenade arriving
    together; no immediate consumption; exactly one accepted shot/throw after
    recovery; bounded duplicates, early callback, tap release, remote cancel,
    downing, reload and empty-ammo revalidation.
  - `BrGame.Sprint.ReplayDoesNotExtendTapRecovery`: repeated historical stop
    replay preserves/reconstructs the original deadline and a released quick tap
    fires after that deadline.
  - `BrGame.Sprint.ReleaseRetriesAutomaticPickup`: explicit listen-host release,
    exhaustion and cancellation schedule/coalesce the real pickup-retry timer.
    This test verifies retry scheduling, not physical overlap collection.
- Ran `.\Scripts\RunAllTests.ps1 -SkipBuild` after automation, serially:
  **12/12 PASS, exit 0**, automation again **79 tests / zero failures**.
  Logs: `Saved/Logs/RunAll/20261103-222246/`.
  All four remote sprint runs reached **780 cm/s, ratio 1.3, zero steady-sprint
  corrections**. Authority's half-second service poll reported exhaustion at
  **5.50 s** in each run (coarse observation, not an exact stamina integration
  timestamp). Actual accepted fire recovery: all-vs-all **0.198/0.191 s**,
  grouped **0.195/0.210 s**, within the configured arrival grace.
- Existing engine startup diagnostics / navigation warnings remain; all tests
  completed successfully. No validation commands ran concurrently.

## Remaining manual checks / editor work

- Physical Left Shift/rebinding, persistence/travel/reset with an old Br.Walk
  profile, ±50° turns, pure strafe/back, blocked uncrouch, prone and all cancels.
  Listen host: sprint onto ammo, release Shift without leaving the overlap,
  verify the scheduled retry actually collects it.
- HUD bar size/placement and hide-at-full at varied resolutions; remote apparent
  speed; hear louder 20 m steps through fog without duplication. No separate
  sprint animation is assigned (existing locomotion); art polish is optional.
- Tap/hold/automatic fire and grenade at 100–200 ms latency, jitter/loss, including
  reliable combat intent overtaking older moves. Check missed/extra shots,
  corrections, energy drift, interactions and teardown during pending recovery.
- No required Blueprint/map/sound asset work remains after input regeneration.
  NullRHI/nosound automation does not verify visual/audio/physical-input behavior.

## Changed paths

- `Source/BrGame/Core/BrCharacter.{h,cpp}`,
  `BrCharacterMovementComponent.{h,cpp}`, `BrMovement.h`, `BrDeveloperSettings.h`,
  `BrInputConfig.h`, `BrHUD.cpp`, `BrAudioRules.h`, `BrAudioSubsystem.cpp`,
  `BrAudioSettingsPanel.cpp`, `BrMenuController.cpp`, `BrSessionRules.h`,
  `BrGameMode.h`, `BrPlayerController.h`, `BrMatchSmoke.cpp`, new `BrSprintSmoke.cpp`.
- `Source/BrGame/Combat/BrInventoryComponent.{h,cpp}`, `BrCombatMath.h`;
  `Source/BrGame/Life/BrLifeComponent.cpp`; `Source/BrGame/Pickups/BrPickup.cpp`.
- `Source/BrGame/Tests/BrSprintTest.cpp` (new), `BrAimingTest.cpp`, `BrAudioTest.cpp`,
  `BrCombatTest.cpp`, `BrMovementSpeedTest.cpp`.
- `Scripts/Content/create_input_assets.py`, `Scripts/RunMatchSmoke.ps1`,
  `Scripts/RunSessionSmoke.ps1`, `Scripts/AudioValidation.md`, this document,
  `Docs/PlaytestChecklist.md`, `GameDescription.md`, `Issues.md`.
- Editor-generated `Content/BrGame/Input/DA_BrInputConfig.uasset`,
  `IMC_BrDefault.uasset`, new `Actions/IA_Sprint.uasset`, deleted
  `Actions/IA_Walk.uasset`. Other action assets are unchanged after idempotent
  regeneration. No hand editing of binaries; no template, config or build-rule changes.
