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
| 5 | Pickups and pickup respawning | Done |
| 6 | Start page, lobby, modes, teams, colors, win condition, results, spectating | Done |
| 7 | Shrinking safe zone | Done |
| 8 | Fog of war (server-side) with shared team vision | Done |
| 9 | Steam hosting (sessions, browser, invites) | Implemented; automated checks passed, two-PC Steam validation pending |
| 10 | Real map with obstructions | Done: L_Town (200 m town/field/compound/forest) is the match map; all BrGame tests (44) and flow/fog/session/local smokes pass; gameplay screenshots reviewed; open items in Scripts\Step10Validation.md |
| 11 | Further tests and playtesting | Automated suite done; manual playtest pending |
| Issue 7 | Hit flinch / directional damage feedback | Implemented; validation and manual checks: Scripts/FlinchValidation.md |

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
1. **Start page:** enter a player name, then Host, refresh/join a browser entry, Join by IP (development), or Quit. The browser shows host/game name, current/max players, mode, password lock, in-progress state and available ping. Steam/LAN-IP status and connection errors are shown here.
2. **Lobby:** player list and host settings, visible live to everyone. Only the host can change settings and press Start (a single host may start alone to test).
3. **Match.**
4. **Results:** shown for about 10 s *(dev)*: the winner, and per player: group, downs inflicted, deaths, survived/eliminated.
5. **Back to the lobby,** with settings and groups kept.

If the host leaves, everyone returns to the start page with a message.
Joining a different session leaves the previous session first. Browser joins and Steam invite acceptance prompt for a protected game's password; wrong passwords return to the start page with an error. Steam cold-start invitations use the engine's `+connect_lobby` launch path; Steam must launch the BrGame executable for the selected App ID (App ID 480 normally belongs to Spacewar, so this launch routing needs manual validation).

### Host settings
- Mode: all vs all, or grouped.
- Number of groups, e.g. 12 players as 4 groups of 3 or 2 groups of 6.
- Group selection: players choose their group, or the host lets the game assign players evenly.
- Friendly fire on/off (grouped mode).
- Max players (0 = unlimited).
- Visibility: Public / Friends only / Invite only.
- Optional password (host-only field; other players see only the lock state).
- Pickup respawn time (default 30 s).
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
- Pickups are placed randomly around the map.

## Winning
- The last player (all vs all) or group (grouped) still in the game wins. A downed player who can still respawn or be revived counts as still in.
- If everyone is out at the same time, the round is a draw.
- A shrinking safe zone forces players together *(step 7)*: about 5 stages, each with a wait and a shrink time, and 2–10 damage per second outside the zone *(dev)*.

## View and controls
- Orthographic top-down camera: CameraPitchAngle defaults to 65° below the horizon *(dev; range 30–90)*.
- The pawn origin is 25% of view height below centre *(dev: PlayerScreenOffset)*, independent of aspect ratio, pitch and zoom.
- Implemented issue 8: captured/hidden relative mouse X turns facing and camera together instantly; the player faces screen-up. Mouse Y does nothing. No crosshair, cursor aiming, smoothing, or quick-turn key.
- Mouse sensitivity is a machine-local comfort setting in Settings, saved with volume settings; its initial default is developer-configurable. The Look axis is not rebindable; other bindings remain rebindable.
- Downed players may turn but cannot move/fire. Spectators use the watched pawn's replicated, visually smoothed facing. Minimap and listener orientation follow rendered camera yaw.
- The cursor is visible for ESC/Settings/rebinding, full map, lobby, results, start page and confirmations. Returning to gameplay captures it; viewport reactivation consumes the capture click. Physical alt-tab/click behavior still needs playtesting.
- Mouse-wheel zoom, limited so zooming out doesn't show much more than the fog of war allows *(dev limits)*.
- Keyboard and mouse only. Settings on the start page and in-game menu saves bindings locally using Enhanced Input user settings. Conflicting keys are rejected (no automatic swap); Reset restores defaults. Esc cancels key capture; keyboard/mouse buttons are supported and scrolling captures the signed wheel axis for Zoom. Console keys (Tilde/Period) are reserved. Defaults:

| Action | Key |
|---|---|
| Move (relative to screen, W = up) | W A S D |
| Walk (running is the default) | Hold Shift |
| Crouch (toggle) | C |
| Lie down (toggle) | Z |
| Fire (hold for automatic weapons) | Left mouse |
| Throw grenade | G |
| Reload | R |
| Pick up weapon / revive | E (hold to revive) |
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
Standing, Walking, Running, Crouching, Lying down, Downed (dying), Dead (eliminated).

### Movement speed *(dev)*
Base running speed is 600.
- **Running:** base speed × weapon modifier: pistol 100%, assault rifle 90%, machine gun 80%.
- **Walking:** 50% of base speed, no weapon penalty.
- **Crouching:** 20% of base speed, no weapon penalty.
- **Lying down:** 20% of base speed, no weapon penalty.

### Downed (dying)
- When health reaches 0 the player is downed for 30 s *(dev)*. They can't move, shoot or be damaged, and they call for help (a pulsing "HELP!" sign above them).
- **Revive:** another player stands within about 150 cm and holds E for 5 s *(dev)*. It is cancelled if they let go, move away or take damage. The revived player gets 30% of their max health *(dev)*. A revive is not a death and has no penalty.
- **Respawn by choice:** hold Space. Each respawn by choice permanently lowers max health by 10% for the round: 1 time = 90, 2 times = 80, and so on.
- **Timer runs out:** the player respawns with no penalty.
- **Death limit:** every respawn counts as a death. After 10 deaths *(dev)* the player can no longer respawn, only be revived. If the timer runs out, the player is out of the game.
- **On respawn:** the player keeps the gear they had, gets full (current max) health, spawns at a random location away from enemies, and can't be hit for 3 s *(dev)*, ending early if they fire.

### Dead (eliminated)
- Every death leaves a skeleton on the ground for the rest of the round.
- Eliminated players watch living players (teammates first) and switch between them with fire.

## Weapons
- A player carries one weapon at a time. Weapons are pickups; picking one up drops the current weapon, which keeps only its loaded rounds (so swapping can't farm ammo). Dropped weapons disappear after 120 s *(dev)*.
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
Every bullet that reaches a player rolls a hit chance based on the shooter's and target's posture. Walking and running count as standing. A missed bullet passes through and can hit something behind.

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
- **Ammo:** one magazine for your current weapon. Collected by walking over it.
- **Health:** restores 100%, 50% or 20% of max health (very few, a few and several, in a 1 : 3 : 6 ratio). Collected by walking over it, but not at full health.
- **Hand grenade:** +1, up to 2. Collected by walking over it.
- **Weapons:** assault rifle and machine gun. Press E to pick up.
- Placed randomly; the number grows with the player count. After pickup the spot refills after the host's respawn time (default 30 s) with a randomly chosen pickup.
- Planned for later (not now): radar that reveals nearby enemies, and teleport to a safe location. Pickup types are data assets, so these can be added without changing the spawn system.

## Audio
- Replaceable, procedurally generated placeholder sounds: distinct weapon fire, grenade boom,
  original scream-like downed vocal (not the Wilhelm recording), running/soft footsteps and
  distinct weapon/ammo/health/grenade pickups.
- Spatial linear falloff *(dev, Audio settings)*: gunshots 50 m, explosions 80 m,
  downed scream 40 m, running 15 m, walking 6 m at 45% volume, crouching 3 m at
  20% volume; prone is silent. Listener distance/orientation follows the viewed pawn
  and camera yaw, not the elevated orthographic camera.
- All nearby players can hear fire, explosions, screams and footsteps through fog.
  All non-owner sources send batched anonymous sound type/position events, not enemy
  actors or velocity; routing does not depend on fog actor channels.
  Own shots/footsteps are immediate; pickups are heard only by their picker.
  Spectators hear only at a valid selected target; category voice limits prioritize
  screams and footsteps over sustained gunfire *(dev, Audio settings)*.
- Start-page Settings has Master and Effects sliders (0–100%), saved locally per
  machine. These are player preferences, unlike developer hearing/combat tuning.
  The same panel, including locally persisted key bindings, is reused by the ESC menu.

## Fog of war *(step 8)*
- Unexplored map areas are black.
- Each player reveals a circle around them; everything inside it (enemies, pickups and so on) is visible. Walls block line of sight.
- Explored areas outside the circle dim after a few seconds: enemies are hidden, but stationary things like pickups and the map stay visible.
- Teammates share vision.
- Hidden enemies are not sent to a player's computer at all, so they can't be revealed by cheating.

## Map *(step 10)*
The match map is `/Game/BrGame/Maps/L_Town` (about 200 x 200 m) with a town of roofless, enterable buildings, open fields with low cover, and dense clusters. Walls are 280 cm tall, low cover is 110 cm and does not block vision, and invisible boundary walls enclose the map. It has 48 player starts, 244 pickup spawn points and a navmesh bounds volume. Built by `Scripts\Content\create_br_map.py` (re-runnable). `Lvl_TopDown` remains available with `-MatchLevel Lvl_TopDown` in the smoke scripts.

### Minimap
- Circular top-right minimap (220 reference pixels, 60 m radius, developer settings), rotating with the camera: screen-up is camera forward.
- M toggles a centered north-up (+X) full map; the yellow line shows camera direction. M/Esc closes it; movement/combat continue.
- One static-world, unlit top-down capture per map load provides a lighting/exposure-independent background. Existing world-XY fog masks render unknown black, explored dim and current vision clear; no pickups.
- Self has a direction arrow. In grouped mode teammates use group colors, downed friends pulse a red cross, and only enemies currently visible to the shared vision group have markers. In all-vs-all **no enemy markers appear**, even for visible enemies.
- Current/next zone rings and an off-minimap zone direction indicator remain visible. Spectators inherit the target's vision group. Maps are hidden in menus, lobby and results.
- No enemy position replication/relevancy changes. Friends already bypass distance culling in active matches. All tuning is developer-only.

## Open questions
- Does respawning after the downed timer runs out count toward the 10-death limit? Currently it does (a developer setting can turn this off).
