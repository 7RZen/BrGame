# Issue 5 — ESC menu

## Behavior / decisions

- C++-built local overlay using the FlowWidget style. Match: Resume, Settings,
  Unstuck, Leave with confirmation. Lobby/results/spectators: Close, Settings,
  Leave; downed players cannot Unstuck. No second start-page menu.
- Never pauses multiplayer. Local pawn callbacks stop accepting gameplay input,
  automatic fire and movement input are stopped on open, revive/respawn hold is
  released, aim/camera changes are gated. Native movement remains enabled so
  Resume cannot strand the pawn in MOVE_None. GameAndUI keeps viewport console
  handling available. Fixed non-consuming Escape/map dismissal is preserved;
  same-frame suppression prevents opening the menu on that map-close press.
  Native overlay key handling also closes on Escape or the current Menu binding.
- Client confirmation sends reliable ServerForfeit before disconnecting and
  retries every developer-configured ForfeitRetryInterval (default 0.5 s).
  ForfeitAckTimeout (default 2 s) bounds waiting, **not** automatic disconnect:
  only an accepted server ack automatically leaves a live connection. Timeout
  keeps the player connected and shows "Couldn't reach the host", "Try again",
  and "Leave anyway". The latter explicitly warns that normal disconnect/rejoin
  rules apply, like closing the game. An already lost connection returns to the
  start page. Late accepted acks are safe; teardown clears retries.
  Server validates admission, owning controller and ledger identity, marks
  forfeited before life callbacks, eliminates, records a geared corpse and
  defers winner evaluation to the existing FlowService, just like ordinary
  elimination, so two same-frame forfeits both leave corpses and produce a draw.
  Subsequent destruction/rejoin snapshots cannot clear
  elimination. Results reconnect restores existing ledger bookkeeping but
  never spawns a pawn. Lobby/results requests are penalty-free.
  Host confirmation warns "The match ends for everyone" and reuses existing
  GameInstance/SessionSubsystem Leave rather than creating another session path.
  Host connection-close errors are expected and displayed on clients' start page.
- Unstuck is a server timer/round-ledger operation. Default 5 s, 10 cm movement
  tolerance, 1000 cm search radius, 100 cm sample step, 60 s success-only cooldown,
  0.05 s service interval. All are Project Settings → BrGame → Menu/Unstuck.
  Damage, fire/throw requests, actual posture changes and authority movement
  callbacks cancel immediately; life/zone eligibility also checked on timer.
  Active admitted alive players only; no sudden death or invulnerability grant.
  Duplicate requests do not restart progress; clients cannot supply a location.
- **Zone rule: preserve status. Inside → inside, outside → outside.** Moving
  closer while still outside is allowed; changing into safety is not. A shrinking
  circle changing status during the hold cancels it. Damage outside normally
  prevents completing the hold anyway.
- Nearest *sampled* open spot, not a geometric nearest-point guarantee. Bounded
  concentric search, optional nav projection plus mandatory walkable-ground trace,
  capsule clearance and explicit overlaps against living/downed player capsules.
  Full 3D displacement stays within configured radius. A Pawn-profile capsule
  sweep from origin to each otherwise-valid candidate rejects blocking walls
  and BrLowCover; only blocking components overlapping the capsule at origin
  are ignored by this path query (not by destination clearance). This permits
  escaping embedded geometry without sweeping through a separate wall.
  L_Town had
  nav data but zero usable projected tiles in validation: real ground/clearance
  checks provide the agreed grounded alternative, like the existing spawn path.
  No candidate or failed teleport leaves cooldown untouched and reports failure.
- Owner-only replicated progress/deadlines/messages drive HUD and menu cooldown.
  The round identity ledger retains cooldown across respawn/reconnect; world/round
  flow resets it. Timers and transient UI references are cleared on teardown.
- Corpse gear is a server-only inventory snapshot for **normal elimination,
  offline forfeit and intentional forfeit**. No new loot/inventory UI or corpse
  interaction system is introduced; existing skeleton presentation is unchanged.

## Settings / assets

UBrAudioSettingsPanel remains embeddable and owns no viewport/input mode.
It reuses Master/Effects persistence and adds all **16** serialized IMC_BrDefault
mapping rows (13 actions; four separate screen-relative movement directions).
Includes scoreboard as well as combat, life, map, menu and signed wheel Zoom.

**Conflicts reject, not swap.** Warning identifies the occupied mapping; neither
binding changes. Click current key, press keyboard/mouse; Escape cancels capture.
Mouse buttons can be selected outside the panel rows through parent preview input.
Wheel captures MouseWheelAxis, allowed only for Zoom. Digital Zoom replacements
produce positive/zoom-in input; wheel is the signed bidirectional default.
Tilde/Period are reserved and leave capture to open the viewport console.
Escape is reserved for the Menu default even after Menu is rebound; it cannot
be assigned to Fire or another row. Fixed Escape always dismisses an open full
map, otherwise toggles the menu. Configurable Menu remains an additional key;
frame-counter coalescing prevents both dispatchers toggling on the same press.
Reset restores the registered defaults.

Verified UE 5.8 source APIs: EnhancedInputUserSettings / FMapPlayerKeyArgs,
GetActiveKeyProfile (not deprecated GetCurrentKeyProfile), GetPlayerMappingRows,
MapPlayerKey, ResetAllPlayerKeysInRow, ApplySettings and SaveSettings. Engine-owned
Enhanced Input save/profile serialization provides per-machine persistence; no
parallel custom binding save format or player combat-tuning settings.
Mappings were already player-mappable; create_input_assets.py now validates all
names, default keys and override settings and labels movement Up/Down (screen).
Commandlet regeneration was run twice successfully, exit 0; only expected
-nosteam warning. Updated asset: Content/BrGame/Input/IMC_BrDefault.uasset.
No hand-edited binaries or Blueprint/map wiring.

SlateCore (native FReply/input events) and GameplayTags (Enhanced Input failure
tags) are explicit private module dependencies, not new plugins.

## Changed paths

- New Core: BrMenuRules.h, BrMenuWidget.{h,cpp}, BrMenuController.cpp,
  BrMenuGameMode.cpp, BrMenuSmoke.cpp.
- Core integration: BrPlayerController.{h,cpp}, BrCharacter.{h,cpp},
  BrCharacterMovementComponent.cpp, BrAudioSettingsPanel.{h,cpp},
  BrFlowWidget.{h,cpp}, BrGameInstance.h, BrGameMode.{h,cpp},
  BrFlowGameModes.cpp, BrDeveloperSettings.h, BrHUD.cpp, BrMinimapHUD.cpp,
  BrShotSmoke.cpp.
- Combat/BrInventoryComponent.cpp; Life/BrLifeComponent.cpp, BrRoundRecord.h,
  BrCorpse.h; Tests/BrMenuTest.cpp; BrGame.Build.cs.
- Scripts: RunMenuSmoke.ps1 (new), RunAllTests.ps1, RunMatchSmoke.ps1
  (clear owned stale role logs before scanning assertions), RunShotSmoke.ps1
  (-Menu), Content/create_input_assets.py; this document.
- GameDescription.md, Docs/PlaytestChecklist.md, Issues.md issue 5;
  generated IMC_BrDefault asset. Build/Saved outputs are tool-generated,
  not hand-edited source.

## Executed validation

Engine C:\Program Files\Epic Games\UE_5.8; project root working directory.
Editor/UnrealEditor-Cmd/LiveCoding process guard before every build/editor launch.
No user editor killed, no overlapping editor launches outside owned multiplayer
runners. Full reflected editor builds, not Live Coding.

```powershell
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
# Input script run twice serially:
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\create_input_assets.py" -unattended -nullrhi -nosound -nosteam
.\Scripts\RunMenuSmoke.ps1
.\Scripts\RunShotSmoke.ps1 -Menu -RunLabel Final
.\Scripts\RunMatchSmoke.ps1 -Grouped -RunLabel MenuReview
.\Scripts\RunAllTests.ps1 -SkipBuild
```

Editor build succeeded, exit 0, no source warnings. Early member-shadowing/API
and direct module-symbol build failures were corrected (no warning suppression).
Early menu smoke found zero nav projection tiles; corrected to mandatory grounded
clearance alternative. Rejoin fixture waited for existing session teardown to
settle before issuing its new Join; it never overwrites a saved player profile.

Menu smoke PASS, exit 0: actual owning-client overlay unstuck/confirmation intents,
server relocation and cooldown, actual damage cancellation without cooldown,
immediate forfeit ledger/corpse gear/winner, same-process identity reconnect remains
eliminated, host overlay Leave returns both remote clients to start page.
Logs: Saved/Logs/MP/Menu/.

First full regression: Saved/Logs/RunAll/20261103-141336, exit 1. Automation
**60/60 PASS** including BrGame.Menu.{BindingConflicts,ForfeitLedger} and
BrGame.Unstuck.{CancelConditions,CooldownAndZone}; nine existing smoke cases passed.
Grouped match had an existing audio Stage=1 Fire=0 probe failure. The next Menu
runner exposed stale prior client error logs before launching those clients;
owned role-log cleanup fixed that reproducibility bug. Grouped narrow rerun
MenuReview subsequently PASS, exit 0, both rounds/five audio probes and sustained
mesh checks; no audio gameplay assertions weakened or audio implementation changed.

RunShotSmoke PASS, exit 0: three screenshots in Saved/Screenshots/Menu/:
esc_menuFinal.png, settings_bindingsFinal.png, unstuck_progressFinal.png.
Inspected all three: actual opaque/translucent native menu, all 16 key rows with
Master/Effects and Reset visible, and readable green **UNSTUCK 40%** HUD progress.
Initial screenshot attempt excluded Slate UI; corrected Menu-only capture to
ShowUI=true and regenerated/inspected the final images. Log: MP/ShotFinal/Host.log.

### Four-finding review follow-up — 2026-11-03

All four findings are fixed:
1. Forfeit retries until accepted ack; timeout displays explicit retry/ordinary
   disconnect choices instead of silently applying disconnect grace.
2. Collision-path capsule sweep rejects destinations across blocking walls or
   movement-blocking low cover, retaining radius/distance and zone policy.
3. Immediate forfeit ledger/elimination/corpse with FlowService winner evaluation.
4. Fixed Escape/map/menu dispatch independent of rebinding, same-frame
   suppression, and Escape reservation in the shared rebinding validator.

Follow-up changed paths (relative to the reviewed uncommitted implementation):
`Source/BrGame/Core/BrMenuRules.h`, `BrMenuController.cpp`,
`BrMenuGameMode.cpp`, `BrMenuWidget.{h,cpp}`, `BrPlayerController.{h,cpp}`,
`BrDeveloperSettings.h`, `BrGameMode.h` (automation friendship only);
`Source/BrGame/Tests/BrMenuTest.cpp`, this document, `Issues.md`.
No template, plugin, dependency, audio implementation, script runner or binary
asset changes were needed for the review fixes.

Executed serially, with editor/LiveCoding guards (no user processes stopped):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" "-ExecCmds=Automation RunTests BrGame; Quit" -unattended -nullrhi -nosound -log -nosteam
.\Scripts\RunMenuSmoke.ps1
.\Scripts\RunAllTests.ps1 -SkipBuild
```

Final full UBT editor build: **Succeeded, exit 0, zero compiler warnings**.
Two initial test-fixture compile errors (incorrect project definition include,
ambiguous engine UniqueNetId constructor) were corrected before validation;
no suppression or dependency changes. Fresh command editor automation:
**62/62 PASS, zero failed tests, exit 0**, log `Saved/Logs/BrGame.log`.
Includes extended `BrGame.Menu.BindingConflicts` Escape reservation and new
`BrGame.Menu.SameFrameForfeits` (two actual server forfeit calls, both immediate
geared corpses, no first-call results, deferred Draw) and
`BrGame.Unstuck.BlockingWall` (280cm wall, 110cm BrLowCover, open direction,
embedded-origin escape without ignoring a separate wall).

Standalone menu smoke: **PASS, exit 0**, all existing assertions unchanged.
Final full regression: **12/12 steps PASS, exit 0**, automation **62/62 PASS**
and all **11 smoke cases PASS**. Logs:
`Saved/Logs/RunAll/20261103-151731` (per-step output and copied role logs).
No remaining build/test blockers.

## Remaining manual checks

No asset/editor wiring remains. Physical key/mouse capture, actual input blocking
while held, console/editable-field focus, persistent rebind/reset across process
restart, packet-loss retry/timeout/explicit Leave-anyway UI, nav-tile maps,
embedded multi-component geometry, no-spot geometry, outside-zone
holds, Steam/two-PC behavior and high-player-count search/replication cost require
the checklist playtest. Policy automation and scripted UI intents are not proof
of those physical-input/network-stress cases. No corpse loot UI added.
