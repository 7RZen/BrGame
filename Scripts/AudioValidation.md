# Issue 4: sound

## Implementation / replacement

- `Source/BrGame/Core/BrAudio{Rules,Subsystem,SettingsPanel,Smoke}.*`,
  `Tests/BrAudioTest.cpp`; integration in Character, PlayerController, GameInstance,
  FlowWidget, inventory firing, grenade impact, life Down and successful pickup.
  No template changes, dependencies/plugins, map edits or Escape menu.
- `Scripts/Content/create_sound_assets.py` generates 15 original seeded mono PCM WAVs
  using Python standard library only. Sources: `Scripts/Content/Audio/SW_*.wav`.
  Imported runtime assets: `/Game/BrGame/Audio/SW_*`; cooked via existing
  `/Game/BrGame` DirectoriesToAlwaysCook. No Wilhelm recording/downloads.
- Weapon `FireSound`, throwable `ExplosionSound`, polymorphic pickup `PickupSound`
  are soft asset references. `create_weapon_assets.py` and `create_pickup_assets.py`
  set them; run the sound script first. For replacements, assign your own SoundWave
  (with SC_Effects) to these fields or the Audio settings; no code changes.
  Assign the category priority below, and enable Bypass Volume Scale For Priority
  for screams/footsteps. Runtime playback supplies the category concurrency override;
  also assign SCN_Category to the replacement wave for consistent editor preview.
  The scripts intentionally restore placeholders, so change script assignments too
  if retaining custom references across regeneration.
- Project Settings → BrGame → Audio contains hearing radii (cm), inner-radius fraction
  and linear falloff, sprint/crouch volume, fire pitch/volume variation, stride (180 cm),
  minimum speed (10 cm/s), service interval (0.1 s), footsteps/scream/pickup fallback
  and class/mix soft references, batch interval and category voice policies.
  Defaults: fire 5000, explosion 8000, downed 4000,
  run/sprint/crouch 1500/2000/300; sprint/crouch volumes 1.30/.20; prone always silent.
  Issue 12 replaces walking entirely: SprintStep reuses generated RunFootsteps,
  with separate range/volume and shared footstep concurrency. No separate sound
  asset or regeneration of `create_sound_assets.py` is needed for sprint.
- `SC_Master` → `SC_Effects`, `SM_Volume`: device base mix, not a travel-accumulating
  modifier stack. Local preferences use version-1 `BrAudio.sav` under Saved/SaveGames,
  separate from the name profile. Clamp both on load/change. Startup and world/controller
  readiness reapply. Master/Effects are normalized 0..1; UI labels show percent.
  `UBrAudioSettingsPanel` is embeddable with CreateWidget and RefreshValues; it owns
  neither input mode nor viewport/Escape. Start-page Settings toggles it inline.

## Routing / cost

**Review correction: one authoritative path for every non-owner**, irrespective of
visibility, relevancy, actor-channel opening/closing or local hiding. Server queues
fire, explosions, downed screams and footsteps by eligible listener, flushing every
0.1 s in one unreliable PlayerController RPC (128-event chunks when necessary).
Each batch carries a dictionary of category/sound asset and events containing only
dictionary index and whole-centimetre position: no source identity, velocity or state.
There is no complementary audio multicast or non-owner client cadence to lose or
duplicate sounds at channel handoffs. Tracer multicast remains visual-only.
Listen-host hearing uses the same server batches, delivered locally.

All categories are batched, including fire: average 50 ms/max approximately 100 ms
additional latency, plus networking/server scheduling. This avoids 15 separate MG
RPCs/s/source/listener; owner fire remains immediate and owner footsteps predicted.
The server never sends gameplay events to their source's connection. Own scream uses
the initialized downed transition (not restored downed state); own grenade boom uses
the replicated impact notification. Other listeners never use those actor notifications
for audio. Picker-only 2D pickup RPC remains the intentional UI-sound exception.

Listeners require an admitted, world-ready controller and living/downed gameplay pawn,
or a valid admitted selected spectator target. No controller position, stale view target
or spawn-location fallback is used for hearing. Pending batches are discarded if the
listener becomes invalid or its selected pawn changes before flush.

Only authority and locally controlled owners run cadence timers. Existing character
Tick only starts/stops the timer; stationary, downed, falling and prone stop cadence.
Moving authority timers do O(listeners) cheap range tests per stride; no channel queries,
visibility cache access or new LOS traces.
Timers and transient caches are torn down. Device absence skips playback/device calls safely.
Missing assets with a live device log errors once per world instead of pretending to play.

GetAudioListenerPosition overrides the viewed-character position and horizontal camera-yaw
front/right axes. Sound Z is flattened to the listener plane for consistent world-XY hearing.
Orientation explicitly uses PlayerCameraManager's rendered-camera yaw, including the
listen-host spectator; authoritative GetPlayerViewPoint remains unchanged for relevancy.
There is no camera-component attachment that can remain stuck to a destroyed pawn.

### Concurrency / priority

The sound script idempotently generates five shared-source concurrency assets under
`/Game/BrGame/Audio/`: SCN_Fire, SCN_Explosion, SCN_Downed, SCN_Footstep, SCN_Pickup.
Developer Audio voice policies configure asset reference, cap, resolution rule and priority.
Regenerate after editing caps/rules/priorities to update assets and wave defaults.

| Category | Voices | Priority |
| --- | ---: | ---: |
| Fire | 12 | 20 |
| Explosion | 6 | 60 |
| Downed | 4 | 100 |
| Footsteps (all postures) | 16 | 90 |
| Pickup | 4 | 70 |

Default resolution: StopFarthestThenOldest, shared across sources, 20 ms steal release.
Screams and footsteps bypass volume-scaled priority so quiet/distant steps still outrank
sustained gunfire in global voice selection. Caps intentionally limit audible voices in
crowds; hearing-event delivery does not guarantee every overlapping voice survives stealing.

### Estimated 60-player bandwidth (not a packet-capture/load benchmark)

Assume all 60 players within all hearing ranges, each firing MG at 15/s and running
600 cm/s with 180 cm stride. Each listener excludes self:
`59 * (15 + 600/180) = 1081.7 events/s`.
Typically about 108 events per 100 ms batch: **10 RPCs/s/listener**, around **12/s**
with synchronized bursts; individual flushes can split into two RPCs.
For a listen server with 59 remote clients: approximately **590–710 RPCs/s egress**;
up to 1180 instantaneous-rate equivalent if every flush splits. Host delivery is local.

BatchWireFormat measured **9 bytes/event** at (-10000,10000,98) cm and **62 bytes**
for the MG category/asset dictionary entry, excluding arrays/RPC/packet headers.
Packed signed coordinates grow with world-coordinate magnitude (maximum 16 bytes/event).
For town-sized coordinates, budget 10 bytes/event, 100 bytes/type, 4–6 types per batch
(MG or mixed weapons plus three running variants), and roughly 100 bytes framing/RPC
including possible packet fragmentation:
`1081.7*10 + (10–12)*(4–6)*100 + (10–12)*100`.
Estimated **16–19 KB/s/remote listener**, approximately **0.94–1.14 MB/s host egress**.
Explosion/scream bursts, other audio types, larger coordinates and actual packet framing
change this; ordinary scattered/non-firing players cost substantially less.

## Initial implementation commands / results (superseded routing)

Engine: `C:\Program Files\Epic Games\UE_5.8`, project working directory.
Editor/UnrealEditor-Cmd/LiveCoding guard precedes every launch. Commands run serially;
the existing multiplayer runners alone launch concurrent game instances.

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development -Project="$PWD\BrGame.uproject" -WaitMutex
# Each script run twice, serially:
foreach ($script in "create_sound_assets.py","create_weapon_assets.py","create_pickup_assets.py") {
  & "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" -run=pythonscript "-script=$PWD\Scripts\Content\$script" -unattended -nullrhi -nosound -nosteam
}
.\Scripts\RunMatchSmoke.ps1 -RunLabel Audio
.\Scripts\RunAllTests.ps1 -SkipBuild
.\Scripts\RunAudioSmoke.ps1 -CameraPitchAngle 45
.\Scripts\RunAudioSmoke.ps1 -CameraPitchAngle 90
```

Asset runs: exit 0 twice for each script; only expected -nosteam warning.
MatchAudio: exit 0, both rounds pass all five remote audio assertions:
Fire=1 at 30 m, still Fire=1 after the 60 m shot; 6 running events at 10 m;
still 6 after prone; Down=1. Logs: Saved/Logs/MP/MatchAudio.
The isolated fixture temporarily sets SERVER vision radius to 500 cm and separates its
listener's group, restoring both before normal match checks. Cadence uses a frozen
authority movement component with 600 cm/s velocity; prone is entered through actual
movement posture processing before freezing again. Scream probe exercises its anonymous
dispatch directly; subsequent match down/revive/respawn checks also execute real Down hooks.

Final UE 5.8 editor compilation succeeded without source warnings (full reflected build,
not Live Coding); final guarded build also reports Succeeded/up to date.
`RunAllTests.ps1 -SkipBuild` **PASS, exit 0: all 11 steps**, 53/53 automation tests
(including BrGame.Audio.HearingRanges, HiddenTargeting, FootstepCadence and
VolumePersistence) and all ten multiplayer/session smoke cases. Final logs:
`Saved/Logs/RunAll/20261103-125233`; earlier full run also passed at
`Saved/Logs/RunAll/20261103-124147`.

Sound-enabled windowed checks at **45° and 90° both PASS, exit 0**:
`Saved/Logs/Audio/WindowPitch45.log`, `WindowPitch90.log`. Audio Device ID 1
registered with L_Town; Master/Effects overrides applied; listener position
matched the pawn and front/right matched horizontal camera yaw; 12 playback
submissions per run with Device=1 and no BrGame/audio errors.
These are device/submission/log assertions, not proof of human-perceived sound quality.
The first windowed run caught empty footstep arrays from incorrect aggregate INI syntax;
fixed to repeated `+RunFootsteps` / `+SoftFootsteps` entries and added reference validation
to the sound script, then ran it twice successfully and reran the full regression.

Early C++ signature/forward-declaration/log-branch issues were fixed before the successful
build; the initial smoke caught a fixture that froze movement before posture refresh and
was corrected. No user editor/process was killed.

## Review correction commands / results

Guarded editor build: Succeeded, exit 0, no source warnings; full compiled/link validation,
not Live Coding. A direct inline engine vector serializer initially required unavailable
link symbols; replaced by dependency-free packed signed coordinates, then rebuilt.
Sound asset/concurrency generation succeeded twice, exit 0; expected -nosteam warning only.

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame; Quit' -unattended -nullrhi -nosound -nosteam -log "-abslog=$PWD\Saved\Logs\AudioReviewAutomationFinal.log"
.\Scripts\RunAllTests.ps1 -SkipBuild
.\Scripts\RunAudioSmoke.ps1 -CameraPitchAngle 45
.\Scripts\RunAudioSmoke.ps1 -CameraPitchAngle 90
```

Direct BrGame automation: **56/56 PASS**, exit 0, including all seven BrGame.Audio tests:
ChannelIndependentRouting (withheld/opening/open/closing ownership/range routing),
ListenerEligibility (admission/readiness/no-selection), HearingRanges, FootstepCadence,
VoiceProtection, VolumePersistence and BatchWireFormat. Log:
`Saved/Logs/AudioReviewAutomationFinal.log`.
Engine startup logs contain its existing internal automation condition diagnostics;
all selected BrGame tests report Success and TEST COMPLETE exit 0.

Full regression: **11/11 steps PASS**, exit 0, 56 automation successes/zero failures
and all ten flow/fog/session/match cases. Logs:
`Saved/Logs/RunAll/20261103-131532/steps`.
Both all-vs-all and grouped match runners passed the five audio assertions in both
rounds: exactly one hidden-source shot inside range, none added beyond range,
running steps, unchanged step count while prone, and one downed event. Latest
second-round counts: all-vs-all Fire=1 Steps=8 Down=1; grouped Fire=1 Steps=6 Down=1.
Live logs: `Saved/Logs/MP/Match`, `Saved/Logs/MP/MatchGrouped`.

Sound-enabled windowed checks: **45° and 90° PASS**, exit 0. Each initialized
Audio Device ID 1, applied volumes, matched listener position/rendered-camera yaw,
and submitted all 12 category sounds without BrGame/audio errors.
Logs: `Saved/Logs/Audio/WindowPitch45.log`, `WindowPitch90.log`.
These runs exercise ordinary local-player cameras, not a dedicated listen-host
spectator scenario; the latter remains a manual orientation check.
All launched editor/game processes exited; no user editor was stopped.

Review changed paths: Core BrAudioRules/Subsystem, BrCharacter, BrPlayerController,
BrDeveloperSettings and BrAudioSmoke; Combat BrInventoryComponent/BrGrenade;
Life BrLifeComponent; Tests/BrAudioTest.cpp; Scripts/Content/create_sound_assets.py;
five new Audio/SCN_* assets and updated SoundWaves; this document and only the Audio
sections of GameDescription.md and Docs/PlaytestChecklist.md.

## Remaining

No manual Blueprint/map/editor wiring required. Replace placeholders as desired; manual
listening, slider interaction/restart, stereo perception, packet-loss handoff, many-player
load/mix balance and packaged cooking are not claimed by headless tests. Events are
deliberately unreliable; lost events are not replayed. Owner and server cadence need not
be phase-synchronized because each listener uses exactly one path for that source.
Routing-rule tests make actor channel transitions irrelevant; actual packet-loss and
60-player voice/network stress remain manual checks.
Empty-magazine/reload sounds were optional and are not implemented.
