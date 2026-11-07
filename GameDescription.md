# BrGame
Top-down multiplayer battle royale shooter (Unreal Engine 5.8, C++).

This document is the agreed design. Values marked *(dev)* are developer settings (Project Settings → BrGame or data assets) and cannot be changed by players. Values marked *(host)* are chosen by the host in the lobby.

## Implementation status
| Step | Feature | Status |
|---|---|---|
| 1 | Network-ready core, local multiplayer test scripts | Done |
| 2 | Movement states, camera, rebindable controls, zoom | Done |
| 3 | Weapons, grenades, ammo, hit chance, damage, health | Done |
| 4 | Downed, revive, respawn, death limit, elimination, skeletons | Done |
| 5 | Pickup definitions and collection | Done; population replaced by Issue 16 crates |
| 6 | Start page, lobby, modes, teams, colors, win condition, results, spectating | Done |
| 7 | Shrinking safe zone | Done |
| 8 | Fog of war (server-side) with shared team vision | Done |
| 9 | Steam hosting (sessions, browser, invites) | Implemented; automated checks passed, two-PC Steam validation pending |
| 10 | Real map with obstructions | Done: L_Town (200 m town/field/compound/forest) is the match map; all BrGame tests (44) and flow/fog/session/local smokes pass; gameplay screenshots reviewed; open items in Scripts\Step10Validation.md |
| 11 | Further tests and playtesting | Automated suite done; manual playtest pending |
| Issue 7 | Hit flinch / directional damage feedback | Implemented; validation and manual checks: Scripts/FlinchValidation.md |
| GitHub #7 | Weapon models | Placeholder held/pickup/dropped visuals; validation: Scripts/Issue7WeaponVisualValidation.md |
| GitHub #21 | Compass and coordinates | Heading strip and live world X/Y readout (now in the right side panel, #26); validation: Scripts/Issue21CompassValidation.md |
| GitHub #20 | Quests and round score | Implemented; quest/crate automation 12/12 and packaged smoke passed; manual interaction pending; validation: Scripts/Issue20QuestValidation.md |
| GitHub #23 | Armor plates | Implemented; armor automation 5/5, crate 8/8, placement check and Town match smoke (armor privacy/absorption probe) passed; manual HUD/audio playtest pending; validation: Scripts/Issue23ArmorValidation.md |
| GitHub #1 | Missing sounds and selectable player voices | C++ and content implemented; automation 22/22, two-round Town network sound probes and live-device audio smoke passed; human listening/multiplayer voice-volume checks pending; evidence: Scripts/Issue1SoundsValidation.md |
| Issue 10 | Maps and lobby map selector | Implemented: developer allowlist, Town/Test Arena/Random, replicated host choice, previews and browser map; validation: Scripts/MapSelectorValidation.md |
| Issue 12 | Tactical sprint replaces walking | Implemented; developer tuning, predicted authoritative stamina, recovery gate and HUD; validation: Scripts/SprintValidation.md |
| Issue 14 | Crouch/prone visuals | Native procedural crouch-walk and face-down crawl over existing unarmed locomotion; validation: Scripts/PostureVisualValidation.md |
| Issue 9 | Slide | Predicted authoritative crouch-capsule slide, pose/audio/input/HUD; validation: Scripts/SlideValidation.md |
| GitHub #25 | Player-initiated pickups | All pickup types require E; touch/overlap collection removed; validation: Scripts/Issue25InteractPickupValidation.md |
| GitHub #26 | Narrower play area with side-panel HUD | 4:3 engine-constrained play area, black side panels holding all HUD text, no world text; automation 35/35, shot/flow smokes and 16:9 / 5:4 screenshots passed; validation: Scripts/Issue26SidePanelValidation.md |
| GitHub #16 | Movement louder than gunfire | Per-sound developer volumes (Audio \| Volume) plus per-weapon FireVolume; footsteps rebalanced below gunfire; automation 31/31 and live-device audio smoke passed; listening check pending; validation: Scripts/Issue16SoundVolumeValidation.md |
| GitHub #24 | Reduce crate spread | Contents scatter halved to 250–500 cm; wave spacing kept at 20 m via `CrateWaveSpacing`; validation: Scripts/Issue24CrateSpreadValidation.md |
| GitHub #30 | Downed penalty items | Implemented including deadline/profile-migration review fixes; editor/game builds and penalty 5/5, full suite 134/134 pass; multiplayer visual/audio checks pending: Scripts/Issue30DownedPenaltyValidation.md |
| Issue 16 | Airdrop crates | Implemented; navigation-independent ground checks, per-map waves and public markers; standalone nav data repaired; evidence: Scripts/Issue16CrateValidation.md |

### Posture presentation
- Crouch lowers the pelvis, bends hips/knees with grounded sampled feet and leans the torso forward. Prone lies face-down along facing, with forward arms/weapon and alternating speed-scaled crawl limbs.
- Cosmetic transitions use 0.25 s crouch / 0.4 s prone *(dev, Project Settings → BrGame → Character/Posture)*, including exits and crouch↔prone. Sprint returns to standing. Collision, damage rules and server authority are unchanged; prone wall clipping is accepted.
- `DefaultCharacterAnimClass` now selects the native `BrCharacterAnimInstance`; no template asset is edited. Optional in-place full-body `CrouchIdle/CrouchMove/ProneIdle/ProneMove` sequence/blendspace slots replace procedural targets when supplied. Empty slots remain procedural. Downed/dead/corpse presentation and additive hit feedback remain separate from this living-posture graph.

## Hosting
- Player-hosted game (the host's game is the server) over Steam. Steam provides matchmaking, the server list, relays (hides IP addresses) and invites, but not server hosting.
- Steam test App ID 480 is configured in `Config/DefaultEngine.ini` (`SteamDevAppId`), until the game has its own App ID. OSSv1 Steam lobbies and SteamSockets provide discovery, invites and relay transport.
- Public sessions appear in the start-page browser. Results are filtered by BrGame identifier, gameplay protocol and the engine's build compatibility checks; other App ID 480 games are excluded.
- The host chooses Public, Friends only or Invite only in the lobby; changes update the Steam lobby. Friends can use overlay invites or the Steam friends-list Join Game action (except invite-only games, which require an invite).
- Steam login identity must match the authenticated SteamSockets peer. Friends-only/invite-only gameplay admission also requires current membership in the host's Steam lobby (with a short bounded propagation wait), so knowing the host's address does not bypass lobby privacy.
- Optional host passwords are checked by the server, including for invited friends and late joiners. Only the password-protected flag is advertised; the password is not replicated or saved.
- Steam requires a logged-in Steam client and standalone/packaged play, not PIE. Without Steam, the start page reports LAN/IP fallback. Use `-nosteam` to force the Null LAN browser and normal IP transport for development; the local scripts do this automatically. Null has no friends/invite privacy service, so non-public sessions are not LAN-advertised.
- Null/IP development identities are unauthenticated and must not be trusted as real Steam identities. Public/private LAN visibility changes restart discovery without restarting the running game.
- A dedicated server option is kept open for later.
- No hard player limit in code. The host sets max players (0 = unlimited). In practice a player-hosted game handles roughly 30–60 players.
- Session capacity follows the host setting. Unlimited advertises a large capacity (1,000,000 on Null, Steam's backend lobby limit of 250 on Steam); server gameplay admission remains unlimited for 0. Steam's backend cannot accommodate more than 250 lobby members.
- The host decides every shot, hit and damage (server-authoritative), to prevent cheating.

## Game flow
1. **Start page:** enter a player name, then Host, refresh/join a browser entry, Join by IP (development), or Quit. The browser shows host/game name, selected map (including Random), current/max players, mode, password lock, in-progress state and available ping. Steam/LAN-IP status and connection errors are shown here.
2. **Lobby:** player list and host settings, visible live to everyone. Only the host can change settings and press Start (a single host may start alone to test).
3. **Match.**
4. **Results:** shown for about 10 s *(dev)*: the winner, and per player: group, downs inflicted, deaths, score, survived/eliminated. Score is informational and does not change the winner.
5. **Back to the lobby,** with settings and groups kept.

If the host leaves, everyone returns to the start page with a message.
Joining a different session leaves the previous session first. Browser joins and Steam invite acceptance prompt for a protected game's password; wrong passwords return to the start page with an error. Steam cold-start invitations use the engine's `+connect_lobby` launch path; Steam must launch the BrGame executable for the selected App ID (App ID 480 normally belongs to Spacewar, so this launch routing needs manual validation).

### Host settings
- Map: Town (default), Test Arena, or Random. Click the map button to cycle; all players see the name, description, preview and recommended player-count hint. Recommendations never limit admission. Only the host can change it, until Start.
- Mode: all vs all, or grouped.
- Number of groups, e.g. 12 players as 4 groups of 3 or 2 groups of 6.
- Group selection: players choose their group, or the host lets the game assign players evenly.
- Friendly fire on/off (grouped mode).
- Max players (0 = unlimited).
- Visibility: Public / Friends only / Invite only.
- Optional password (host-only field; other players see only the lock state).
- Crate drop interval (default 30 s, host clamp 1–3600 s).
- Who can revive: anyone, or teammates only.

### Groups and colors
- Grouped mode: teammates wear the same clothing color. The first player who enters an empty group picks its color; colors are unique between groups. If nobody picks, the group gets the first free color.
- All vs all: everyone wears military green, so no one can be singled out by color.

### Joining
- Players can join a match that has already started. In grouped mode they go into the smallest group and spawn near teammates; in all vs all they spawn away from enemies.
- A player who unexpectedly disconnects keeps their deaths, penalties and gear if they return within 60 s *(dev)*. Otherwise they forfeit. Confirming Leave during a match intentionally forfeits immediately; rejoining that round cannot restore them.

## Starting a round
- All vs all: players spawn randomly around the map, spread apart.
- Grouped: one random start location per group, far apart; group members start together.
- Everyone starts with a pistol (one loaded magazine plus one spare) and 100% health.
- A map-tuned initial wave of airdrop crates *(dev)* falls inside the current safe zone and playable bounds: Town 6, Battlefield/Test Arena 2.

## Winning
- The last player (all vs all) or group (grouped) still in the game wins. A downed player who can still respawn or be revived counts as still in.
- If everyone is out at the same time, the round is a draw.
- A shrinking safe zone forces players together *(step 7)*: about 5 stages, each with a wait and a shrink time, and 2–10 damage per second outside the zone *(dev)*.

## View and controls
- Orthographic top-down camera: CameraPitchAngle defaults to 65° below the horizon *(dev; range 30–90)*.
- The world renders only in a centered play area with a `PlayAreaAspectRatio` of 4:3 *(dev)*; the sides are black panels. Each panel is at least `MinSidePanelWidth` *(dev, 160 px at 720p reference, scaled)* wide, so screens near 4:3 get a narrower play area and ultrawide screens get wider panels. Settings are identical for every player. Zoom (ortho width) is the play-area width, so you see more ahead than to the sides (about 15.5 m ahead at default zoom, matching vision range).
- HUD text lives only in the side panels: left for your status (health, armor, weapon/ammo, grenades, stamina, life/respawn/revive, interact prompt, pickup and unstuck messages), right for the match (compass and coordinates, minimap, zone, mode/group, round score, quests, spectating). Only text-free hit-direction indicators draw over the play area, and there is no floating world text (no pickup labels or downed "HELP!" signs). The full map and menus use the whole screen. A movement debug line is available to developers via `bShowMovementDebugHud` *(dev, off; not in Shipping)*.
- The pawn origin is 25% of play-area height below its centre *(dev: PlayerScreenOffset)*, independent of aspect ratio, pitch and zoom.
- Implemented issue 8: captured/hidden relative mouse X turns facing and camera together instantly; the player faces screen-up. Mouse Y does nothing. No crosshair, cursor aiming, smoothing, or quick-turn key.
- Mouse sensitivity is a machine-local comfort setting in Settings, saved with volume settings; its initial default is developer-configurable. The Look axis is not rebindable; other bindings remain rebindable.
- Downed players may turn but cannot move/fire. Spectators use the watched pawn's replicated, visually smoothed facing. Minimap and listener orientation follow rendered camera yaw.
- The cursor is visible for ESC/Settings/rebinding, full map, lobby, results, start page and confirmations. Returning to gameplay captures it; viewport reactivation consumes the capture click. Physical alt-tab/click behavior still needs playtesting.
- Mouse-wheel zoom, limited so zooming out doesn't show much more than the fog of war allows *(dev limits)*.
- Keyboard and mouse only. Settings on the start page and in-game menu saves bindings locally using Enhanced Input user settings. Conflicting keys are rejected (no automatic swap); Reset restores defaults. Esc cancels key capture; keyboard/mouse buttons are supported and scrolling captures the signed wheel axis for Zoom. Console keys (Tilde/Period) are reserved. Defaults:

| Action | Key |
|---|---|
| Move (relative to screen, W = up) | W A S D |
| Tactical sprint (forward/forward-diagonal only) | Hold Left Shift |
| Slide (run/sprint speed, forward/forward-diagonal input) | Press Left Ctrl |
| Crouch (toggle) | C |
| Lie down (toggle) | Z |
| Fire (hold for automatic weapons) | Left mouse |
| Throw grenade | G |
| Reload | R |
| Pick up weapon / revive | E (hold to revive) |
| Plant penalty in downed player | Hold F (10 s; rebindable) |
| Respawn while downed | Hold Space |
| Zoom | Mouse wheel |
| Scoreboard | Tab |
| Full map (overlay, toggle) | M (Esc closes) |
| In-game menu (local input blocked; match continues) | Esc (rebindable) |

### ESC menu
- During a match: Resume, Settings, Unstuck and Leave game. The game is **not paused**:
  enemies and zone damage continue. Opening stops local movement/fire and blocks movement,
  aim changes, posture, zoom, reload, throw, interact/revive and respawn-hold input until closed.
  Esc/Resume closes; if the full map is open, the first Esc/Menu only closes the map.
  Fixed non-consuming Escape still dismisses the map after Menu is rebound.
- Lobby, results and spectators have Settings and Leave, plus Close menu; no Unstuck.
  The start page already has Settings and does not open a second menu.
- Leave requires confirmation. A client intentionally leaving an active match sends a reliable
  server forfeit request, waits up to 2 s *(dev)* for acknowledgment, then leaves regardless.
  The server immediately eliminates them, leaves a corpse with the gear snapshot and checks
  the winner. A missing request falls back to disconnect grace. Lobby/results leaves have no
  penalty. The host is warned **"The match ends for everyone"**; existing session teardown
  returns everyone to the start page with a connection/host-left message.
- Unstuck requires staying still for 5 s *(dev)*. Movement beyond 10 cm *(dev)*, damage,
  firing/throwing, posture/life/zone-status changes cancel; sudden death disallows it.
  The server searches nearby open grounded spots within 10 m *(dev)*, using nav projection
  where available, walkable ground, capsule clearance and player-overlap checks. It chooses
  the nearest sampled valid spot without requiring LOS. **Inside stays inside; outside
  stays outside** the current safe zone, so it cannot be a zone escape.
  No death penalty or protection is granted. Only success consumes a 60 s *(dev)* cooldown,
  tracked per identity in the round ledger (survives respawn/reconnect, resets per round).
  Failure displays a message and consumes no cooldown. Progress appears on the HUD and
  remaining cooldown in the menu. Selecting Unstuck closes the menu so progress is unobscured.

## Player states
Standing, Running, Tactical sprinting, Sliding (crouching for combat), Crouching, Lying down, Downed (dying), Dead (eliminated).

### Movement speed *(dev)*
Base running speed is 600.
- **Running:** base speed × weapon modifier: pistol 100%, assault rifle 90%, machine gun 80%.
- **Tactical sprinting:** 130% of running speed, including weapon modifier (780/702/624 cm/s).
- **Crouching:** 20% of base speed, no weapon penalty.
- **Lying down:** 20% of base speed, no weapon penalty.

### Tactical sprint *(all tuning dev-only, UBrDeveloperSettings)*
- Hold the rebindable Sprint action (Left Shift) while moving within ±50° of facing.
  Pure strafe/backward movement is normal run. The server validates each CMC move's
  acceleration against its control yaw; clients cannot grant sprint speed or stamina.
- Crouch + Sprint requests standing and only sprints after capsule clearance permits
  uncrouching. Prone + Sprint stays prone.
- Five seconds of sprint energy; partial use is allowed. Regen starts after 1 s of
  rest and restores an empty bar in 5 s; starting requires 0.5 s worth of energy.
  Holding Shift can resume sprint when direction/energy becomes eligible again.
  Stamina is move-time predicted, server-authoritative, and shown near player info
  only while below full. No stamina or sprint state persists across a new pawn/round.
- Fire or grenade cancels sprint, then fires/throws after 0.2 s recovery. A quick
  fire tap still schedules one shot; holding automatic fire continues after recovery
  and suppresses sprint. The server independently rejects sprint/recovery shots,
  with 30 ms arrival grace (never while actively sprinting).
- Release, non-forward input, empty stamina, posture change, downing, blocked gameplay
  input/menu and unstuck hold stop sprint. E stops sprint then performs pickup/revive.
  Starting sprint/moving cancels an unstuck hold. Fire/interact/posture cancellation requires pressing Sprint again.

### Slide *(all tuning dev-only, UBrDeveloperSettings)*
- Rebindable Slide (Left Ctrl) is a press, not a crouch toggle. Requires grounded,
  alive, unblocked gameplay, at least weapon-adjusted running speed and input within
  the sprint forward cone (±50°). Invalid presses do nothing; C remains crouch.
- Locks current horizontal travel direction, boosts current speed by 1.2×, then
  linearly decelerates to crouch speed over 0.8 s. Default sprint slide travels
  4.224 m; a run slide travels 3.36 m (weapon penalties shorten both).
  Mouse still turns facing/camera/aim; movement input does not steer.
- Requires successful crouch transition; uses crouch collision, hit chance and
  damage multipliers as both shooter and target. Ends crouched, cancels held sprint,
  costs no stamina. Press Shift again after exit to stand/sprint with normal clearance.
- Cooldown is 1 s from every exit, including early exits. Wall-induced velocity
  loss, falling, downing, prone requests and gameplay/menu input block end slide.
  Movement/posture changes cancel an unstuck hold through existing validation.
- Slide entry explicitly waives sprint-to-fire recovery. New fire/throw requests
  are immediately eligible during slide; all normal ammo/rate/life/aim checks remain.
  A request already queued before entry retains its existing short timer.
- Remote persistent slide gait drives a short-blended native lean-back/legs-forward
  pose, including fog reveal snaps. Slide scrape is batched within 10 m; slide has
  no footsteps. HUD gait reads “Slide”.

### Downed (dying)
- When health reaches 0 the player is downed for 30 s *(dev)*. They can't move, shoot or be damaged, and they call for help (the downed pose and the minimap red cross mark them; there is no floating text).
- Downed players lie on their back, face up, with their head toward their current facing. Turning rotates this grounded pose. This is separate from the living face-down prone/crawl animation; there is no floating help text.
- **Revive:** another player stands within about 150 cm and holds E for 5 s *(dev)*. It is cancelled if they let go, move away or take damage. The revived player gets 30% of their max health *(dev)*. A revive is not a death and has no max-health penalty; any planted item activates.
- **Respawn by choice:** hold Space. Each respawn by choice permanently lowers max health by 10% for the round: 1 time = 90, 2 times = 80, and so on.
- **Timer runs out:** the player respawns with no max-health penalty; any planted item activates.
- **Death limit:** every respawn counts as a death. After 10 deaths *(dev)* the player can no longer respawn, only be revived. If the timer runs out, the player is out of the game.
- **On respawn:** the player keeps the gear they had, gets full (current max) health, spawns at a random location away from enemies, and can't be hit for 3 s *(dev)*, ending early if they fire.
- **Plant penalty (#30):** any alive player, teammate or enemy, can hold F for 10 s *(dev: PlantDuration)* within the revive radius. Release, leaving range, damage, becoming non-alive or the target leaving Downed cancels the hold. E retains its existing revive/pickup priority; the left status panel shows the bound Plant key and hold progress.
- Revive and Plant can run concurrently. A revive completing first cancels Plant with no item or score; Plant completing first leaves the revive running. One successful plant per down, with the earliest-finishing valid planter winning and earning +1 round score. The server picks from the developer's extensible `PenaltyDefinitions` soft-reference list.
- Delayed service frames resolve the earliest valid Downed-exit deadline consistently for both Plant and life. Exit ties prefer revive, then timeout, then voluntary respawn; any tied exit defeats Plant. Installing new input defaults preserves saved custom bindings: if another customized action already uses F, Plant starts unbound and remains available in settings to bind.
- A tinted cylinder tube attaches to the pelvis while an item is present, including its dormant downed period. The effect starts on revive/respawn: Rocket lasts 15 s, Smoke/Sound/Buttplug 30 s *(dev, per-definition)*. Downing again does not pause a running timer. A later successful plant replaces it with a new dormant item. Elimination and round reset clear it; reconnect retains the definition, absolute deadline/dormancy and one-per-down flag.
- **Rocket:** continuous facing-forward movement, 2× weapon-adjusted normal run speed, no sprint/slide or crouch/prone requests; shooting remains allowed. CMC predicts this movement and replays saved facing/time; native collision and uncrouch clearance remain authoritative.
- **Smoke:** bounded world-space mesh puffs linger up to 2.4 s, with no sound. **Sound:** original looping toy-flute placeholder, positional within nearby hearing range *(dev)*, using the existing Effects/master volume mix. Both follow character relevancy AND local fog visibility, never revealing a hidden player. Optional per-definition Niagara/sound assets can replace the placeholders.
- **Buttplug:** 0.5× weapon-adjusted normal run speed, sprint/slide disabled. Penalty and weapon speed multipliers multiply; ordinary crouch/prone modifiers still apply unless the item forces standing.

### Dead (eliminated)
- Every death leaves a skeleton on the ground for the rest of the round.
- Skeletons keep the arms-out reference pose, lying face up with the head toward the victim's last facing. Their reference skin bounds rest on the ground.
- Eliminated players watch living players (teammates first) and switch between them with fire.

## Weapons
- A player carries one weapon at a time. Weapons are pickups; picking one up drops the current weapon, which keeps only its loaded rounds (so swapping can't farm ammo). Dropped weapons disappear after 120 s *(dev)*.
- Held, pickup and dropped weapons use a replaceable BasicShapes Cube placeholder, scaled to 25/50/75 cm along local X for the pistol/assault rifle/machine gun.
- Bullets and grenades travel (not instant hits). Weapon bullets are very fast.
- All values are *(dev)*, in data assets, and more weapon and throwable types can be added later.

| Weapon | Damage | Fire rate | Mode | Magazine | Start ammo | Reload | Run speed |
|---|---|---|---|---|---|---|---|
| Pistol | 10 | 4 shots/s | single | 12 | 2 magazines | 1.5 s | 100% |
| Assault rifle | 20 | 10 shots/s | automatic | 20 | 2 magazines | 2.0 s | 90% |
| Machine gun | 30 | 15 shots/s | automatic | 80 | 2 magazines | 4.0 s | 80% |

- "2 magazines" means one loaded plus one spare.
- Ammo is shared by all weapons for now, but structured so it can be split per weapon later.
- An empty weapon reloads automatically when you try to fire.

### Hand grenade
- Damage 70, no damage falloff (for now).
- Blast radius 136 cm, which is twice the player's diameter; walls block the blast.
- Explodes when it hits anything.
- A player carries at most 2 grenades.
- Self damage is on; friendly fire follows the host setting.

## Fighting
### Hit chance *(dev)*
Every bullet that reaches a player rolls a hit chance based on the shooter's and target's posture. Sprinting and running count as standing. A missed bullet passes through and can hit something behind.

| Shooter ↓ / Target → | Standing | Crouching | Lying down |
|---|---|---|---|
| Lying down | 100% | 80% | 60% |
| Crouching | 80% | 60% | 40% |
| Standing | 60% | 40% | 20% |

### Damage reduction *(dev)*
On top of the hit chance, crouching targets take 75% damage and lying-down targets 50%. This also applies to grenades.

### Hit feedback *(dev: HitFeedback settings)*
- After actual bullet/grenade health loss, the victim's local camera twists up to ±3° for
  0.15 s (bullet), or ±12° with an 18 cm jolt for 0.4 s (grenade). No control/facing/aim
  rotation, stun, movement penalty or near-miss effect. Repeated hits refresh the strongest
  active profile without summing amplitudes.
- Red screen edges flash (damage-scaled); up to eight red directional arcs fade over 1 s.
  Arcs point toward the captured bullet launch position or explosion centre and track
  the current rotated camera and below-centre pawn position. Hidden attackers still give
  direction, but no attacker identity is sent to the victim.
- Other players see a brief body jerk away from the origin and a translucent red mesh
  overlay, only while the victim is locally fog-visible and network-relevant. Old hits do
  not replay on fog entry or late join. Spectators receive no camera/HUD hit feedback.
- Friendly fire with health loss counts. Protection, misses, zero damage, downed players
  **and the downing hit** give no feedback. Zone damage has no combat flinch.
- All tuning/assets are developer-only. Placeholder overlay is generated through editor
  Python; character team materials are preserved.

## Health
- Players start with 100 health, capped at their max health (which can be lowered by respawn penalties).

## Pickups
- Every pickup is player-initiated: nothing is collected by touching it. Press E (Interact) to pick up the nearest usable item within `InteractRadius` *(dev, 150 cm)*; one press picks up one item, and E stops sprint first. Items you cannot use right now (for example health at full) are skipped and get no prompt. A downed teammate in range takes priority (E revives). The HUD shows `Press E to pick up <item>`; the server chooses the item and validates it.
- **Ammo:** one magazine for your current weapon.
- **Health:** restores 100%, 50% or 20% of max health (very few, a few and several, in a 1 : 3 : 6 ratio). Not usable at full health.
- **Hand grenade:** +1, up to 2.
- **Armor plate:** 20 armor per plate, up to 2 plates *(dev)*, usable only while a plate slot is empty (a partially damaged plate keeps its slot). Damage after posture scaling hits the most damaged plate first, overflows into the next plate, then health in the same hit; zone damage bypasses armor. A distinct sound plays when a plate breaks. Armor is private to its owner and their spectators, persists through down, revive and respawn, and resets at round start and final elimination.
- **Weapons:** pistol, assault rifle and machine gun.
- **Airdrops:** each map entry has `InitialCrates` / `CratesPerDrop` *(dev)*: Town **6 / 3**, Battlefield and Test Arena **2 / 1**. New entries default to **4 / 2**; there is no global initial count. The interval stays host-controlled (30 s default), including during the final zone stage while safe landing area exists. No authored pickup spawn points or individual slot respawns.
- Every crate in a wave has an independent bounded landing search. Same-wave landing centers are at least `CrateWaveSpacing` *(dev)* apart (20 m default), never less than twice the maximum scatter radius or 170 cm for crate-body clearance. Crowded or tiny zones may place fewer crates, with skipped drops and actual placed counts logged.
- Each crate contains exactly 1 pistol, 1–2 ARs, a 50% MG chance, 3–8 health pickups (20/50/100 weighted 60/30/10), 3–8 ammo pickups (not rounds), 2–4 grenades and 2–8 armor plates. Counts, weights, chance and definition references are developer settings.
- Crates fall 3000 cm over 4 s with a yellow ground marker, then provide cover for 20 s *(dev)*. Server navigation is an optional placement preference, never an initial-wave readiness gate. Shared zone-fallback ground tracing verifies a walkable tagged `BrFogGround` floor blocking WorldStatic traces, capsule clearance and 300 cm pawn clearance. Crate/scatter ground must also be non-simulating, at rest and broad enough for the scatter footprint plus 85 cm clearance (11.7 m minimum component span at defaults); fog-tagged small raised caps are not floors. Bounded failures log attempts/skip the drop. Landing rechecks the current zone/clearance and cancels safely if conditions changed. Crates never deal damage.
- Contents and quest tokens scatter 250–500 cm *(dev)* onto clear ground using the same navigation-independent floor/capsule checks. Bounded retries may use a closer clear position; impossible items are logged/skipped, never placed inside walls. Uncollected contents expire after 120 s *(dev)*.
- Falling and landed crates have public yellow markers on minimap and full map, independent of fog. Contents retain existing pickup fog presentation. Round reset clears crates, their landing timers, periodic drops and contents; dropped weapon payload/lifetime behaviour is unchanged.

### Quests and round score
- Each landed crate adds 1 or 2 separate quest tokens among its scattered loot. Quest types may duplicate; each offer is consumed by the first player to accept it with E and expires with other crate loot. Multiple active quests are allowed.
- Objectives: down an opponent, pick up a hand grenade, or pick up a specified weapon selected randomly when the offer spawns. Progress is server-authoritative; the existing nearby pickup prompt describes each offer.
- Per-round informational score: +1 for a credited final elimination, -1 each time downed, another -1 for a later death/respawn or final elimination, and +2 per completed quest. The down and death penalties stack; score does not affect the winner.
- Accepted quests and score last for the round and survive a reconnect; round reset clears them. Unaccepted offers expire with other loot.

- Planned for later (not now): radar that reveals nearby enemies, and teleport to a safe location. Pickup types are data assets, so these can be added without changing the spawn system.

## Audio
- Replaceable, procedurally generated placeholder sounds: distinct weapon fire, grenade boom,
  original scream-like downed vocal (not the Wilhelm recording), running/soft footsteps and
  distinct weapon/ammo/health/grenade pickups.
- Spatial linear falloff *(dev, Audio settings)*: gunshots 50 m, explosions 80 m,
  downed scream 40 m, running 15 m, sprinting 20 m, crouching 3 m; prone is silent. Listener distance/orientation follows the viewed pawn
  and camera yaw, not the elevated orthographic camera.
- Volume mix *(dev, Audio | Volume, 0–2 each, applied to the next sound played)*: fire 1.0, explosion 1.0, downed 0.9, voice line 1.0, run step 0.35, sprint step 0.5, crouch step 0.1, slide 0.5, reload 0.6, empty click 0.5, pain 0.7, armor thud 0.6, pickup 0.6, armor break 0.8, so movement stays below gunfire. Each weapon data asset also has a `FireVolume` (default 1.0) multiplied into its gunshots; remote listeners resolve it locally from the fire sound (weapons sharing a fire sound must share this value). Player sliders scale on top.
- Combat sounds *(dev ranges)*: per-weapon reloads (pistol light, AR medium, MG heavy; 15 m) stop for every listener when the reload is cancelled; an empty-weapon click (15 m, throttled) plays only when no reserve ammo remains, since an empty magazine with reserve auto-reloads. Non-zone hits that do not down a character play a pain grunt (10 m, at most one per 0.5 s); hits fully absorbed by armor play a muted armor thud instead. The downing hit keeps the downed scream.
- Down voice lines: when a player downs an opponent (not self, zone or teammate), the attacker says a random server-chosen line from their voice set (no immediate repeat), heard within 20 m. Voice sets (`UBrVoiceSet`, listed in `VoiceSets` *(dev)*) are built by `Scripts\Content\import_voice_lines.py` from WAV folders in `Scripts\Content\Audio\Voice\<Voice>\`; English Windows TTS placeholders (David, Mark, Zira) ship until real recordings replace them. Each player chooses their own voice in Settings; the choice is saved locally and validated by the server.
- All nearby players can hear fire, explosions, screams and footsteps through fog.
  All non-owner sources send batched anonymous sound type/position events, not enemy
  actors or velocity; routing does not depend on fog actor channels.
  Own shots/footsteps are immediate; pickups are heard only by their picker.
  Spectators hear only at a valid selected target; category voice limits prioritize
  screams and footsteps over sustained gunfire *(dev, Audio settings)*.
- Start-page Settings has Master, Effects and Voice sliders (0–100%) and a voice picker, saved locally per
  machine. These are player preferences, unlike developer hearing/combat tuning.
  The same panel, including locally persisted key bindings, is reused by the ESC menu.

## Fog of war *(step 8)*
- Unexplored map areas are black.
- Each player reveals a circle around them; everything inside it (enemies, pickups and so on) is visible. Walls block line of sight.
- Explored areas outside the circle dim after a few seconds: enemies are hidden, but stationary things like pickups and the map stay visible.
- Teammates share vision.
- Hidden enemies are not sent to a player's computer at all, so they can't be revealed by cheating.

## Map *(step 10)*
The default match map is `/Game/BrGame/Maps/L_Town` (about 200 x 200 m) with a town of roofless, enterable buildings, open fields with low cover, and dense clusters. Walls are 280 cm tall, low cover is 110 cm and does not block vision, and invisible boundary walls enclose the map. It has 48 player starts and a navmesh bounds volume; crate landings use clear navigable ground, not spawn points. Built by `Scripts\Content\create_br_map.py` (re-runnable). `/Game/TopDown/Lvl_TopDown` is selectable in the lobby as **Test Arena**; `-MatchLevel Lvl_TopDown` still works in flow/fog smokes by selecting its allowlisted ID. Battlefield is also in the developer map allowlist.

Maps are developer-managed in Project Settings → BrGame → **Maps** (`Maps`, `DefaultMapId`), replacing the old `MatchMap` path setting. Each entry has a stable ID, localized name/description, soft world/preview references, recommended min/max players and `InitialCrates` / `CratesPerDrop` (defaults 4 / 2; runtime/editor bounds 0–100 / 1–100). The server uses the resolved match map verified against its current world, not a client map choice, for crate counts. Invalid/missing/duplicate entries are skipped with a warning. A missing preview shows a text placeholder; no valid maps means Start reports an error instead of travelling to an arbitrary destination.

Only IDs are accepted from the lobby; the authoritative server resolves the destination from its own list. **Random** draws uniformly from valid entries at Start. The chosen map is announced at match entry. The selection survives results → lobby → next round; Random stays Random and **re-rolls each round**. Session keys `BRMAP`/`BRMAPNAME` advertise the selection, not the last Random outcome. Protocol 11 prevents older wire layouts joining; Steam/Null password and engine-version checks are unchanged.

See [How to add a new map](Docs/AddingMaps.md) for authoring, cooking, previews and validation.

### Minimap
- Circular minimap in the right side panel, below the compass and fitted to the panel width (220 reference pixels max, 60 m radius, developer settings), rotating with the camera: screen-up is camera forward.
- M toggles a centered north-up (+X) full map; the yellow line shows camera direction. M/Esc closes it; movement/combat continue.
- One static-world, unlit top-down capture per map load provides a lighting/exposure-independent background. Existing world-XY fog masks render unknown black, explored dim and current vision clear; no pickups.
- Self has a direction arrow. In grouped mode teammates use group colors, downed friends pulse a red cross, and only enemies currently visible to the shared vision group have markers. In all-vs-all **no enemy markers appear**, even for visible enemies.
- Current/next zone rings and an off-minimap zone direction indicator remain visible. Spectators inherit the target's vision group. Maps are hidden in menus, lobby and results.
- No enemy position replication/relevancy changes. Friends already bypass distance culling in active matches. All tuning is developer-only.

## Open questions
- Does respawning after the downed timer runs out count toward the 10-death limit? Currently it does (a developer setting can turn this off).
