# List off issues (I) and feature requests (FR) and their status

### Abbreviations
Player 1 = P1
Player 2 = P2
Player 3 = P2
Assault rifle = AR
Machine gun = MG


## 1. (I) Dying animation
When P1 has downed P2 the animation for the player P1 is wrong. It sees the P2 as standing, but for P2 he is lying down.
Status: Resolved (fix: base mesh offset cached for downed pose; verified by match smoke)

## 2. (FR) Minimap
Want to have a minimap with shared view (if grouped play)
Status: Resolved

## 3. (FR) Skeletons
Dead skeletons should lie on their back and not on the side.
Status: Resolved. Shared supine reference-pose transform for corpses and downed players: face up, head along victim facing, imported skin bounds grounded (no duplicate corpse spawn lift). CMC cached base/smoothing and separate prone animation preserved; pulsing HELP now stays upright for each viewer. Full UBT editor build zero compiler warnings; 89/89 automation and 14/14 regression steps PASS (Saved/Logs/RunAll/20261104-102131/), including both non-owner match modes. Eight owner/remote downed/corpse screenshots at default/min zoom inspected: face-up, grounded, readable HELP. Template code/assets untouched. Details: Scripts/SkeletonValidation.md.

## 4. (FR) Sound
Add sounds for:
* Firing weapon
* Handgrenade explosion
* Downed scream (The Wilhelm scream)
* Moving sounds like walking, running and crouching with falloff
* Pickup sounds
Status: Resolved (placeholder sounds; pending playtest)

## 4.1. (FR) Sound
* Reload sound
* Out of ammo sound, simple click sound
Status: Unresolved

## 5. (FR) In-game menu
Player should be allowed to leave game. Press ESC to get in-game menu with option to leave or to respawn (in case of stuck)
Status: Resolved (ESC menu, settings/rebinding, server unstuck and forfeit implemented; four review fixes validated: 62/62 automation, menu smoke, 12/12 full regression PASS. Remaining physical-input/network-stress playtests: Scripts/MenuValidation.md.)

## 6. (FR) Weapons
The model used for weapons now is the same for all weapons.
Ideally the model should be short for pistol, medium for Assault rifle (AR) and long for Machine gun (MG).
The same for the pickup, medium model for AR and long for MG.
Status: Unresolved

## 7. (FR) Flinching
When hit with a bullet the player should have a visual indication from the direction the bullet came from and a small and quick screen shak. Maybe +-20% rotation? No stun. Grenades should have a much larger flinch.
Status: Implemented (server-confirmed health loss; owner-only bounded camera twist/jolt, directional arcs/red edges, fog-gated remote mesh jerk/red overlay; downing hit excluded). Independent-review P2 fixes validated: reaction updates/restoration preserve CMC translation/rotation smoothing; single camera-local roll keeps total twist within the configured amplitude at every pitch. Full UBT build zero compiler warnings; 72/72 automation and 12/12 regression PASS, including non-owner downed/revived pose checks. Screenshot/manual validation details: Scripts/FlinchValidation.md.

## 8. (FR) Aiming
Player is locked at looking up, mouse rotates map. Keys W and S moves forward and backwards. Keys A and D is strafe left and right.
Status: Resolved (relative MouseX turns facing/camera instantly; persistent sensitivity slider, zoom/pitch-independent below-centre framing, UI cursor transitions and downed/spectator turning implemented. Independent-review P2 fixed: accepted fire/grenade aim commits server control/actor yaw; older queued moves cannot undo it, newer turns remain unrestricted. UBT build: zero compiler warnings; 68/68 automation and 12/12 regression steps PASS. Earlier pitch 30/65/90 and Settings screenshots inspected. Physical mouse/focus/network-stress playtests remain: Scripts/AimingValidation.md.)

## 9. (FR) Slide
Pressing CTRL til slide if running or sprinting.
Status: Implemented. Rebindable Left Ctrl; server-authoritative, CMC-predicted forward run/sprint slide with locked travel direction, crouch capsule/combat posture, immediate fire/throw eligibility, 0.8 s deceleration and 1 s exit-based cooldown. Native replicated procedural slide pose, fog snap, 10 m batched scrape, no footsteps and Slide HUD gait. Full UBT Editor build zero compiler warnings; 92/92 automation and 14/14 regression steps PASS (Saved/Logs/RunAll/20261104-113548/). All four remote sprint slides: 422.4 cm, zero corrections, crouched exit, cooldown rejection and accepted mid-slide shots. Four owner/remote default/close screenshots inspected. Details and remaining physical-input/audio/network-stress checks: Scripts/SlideValidation.md. Template code/assets untouched.

## 10 (FR) Maps and map selector
More maps will be added. The person hosting should be able to choose from available maps.
Status: Implemented (developer-managed map list/default, host-only replicated lobby selector with descriptions/previews/player-count hints, server-side Random re-roll each round, persistent selection across rounds, browser map advertisement and map/cook validation). Independent-review fixes: transparent match-only root with compact fading map banner (world-visible screenshot inspected); dedicated two-round multiplayer smokes verify non-default/Random carry without reapplying settings and late lobby join replication before Start. Full UBT build zero compiler warnings; 84/84 automation and 14/14 regression steps PASS (Saved/Logs/RunAll/20261104-074112/). Packaged IoStore map validation is fixed and covered by the packaged two-round selector/travel smoke; Town and Test Arena are selectable in the packaged Development build. Validation/results and remaining manual checks: Scripts/MapSelectorValidation.md; adding maps: Docs/AddingMaps.md.

## 11 (FR) All buildings have roofs
the roof should have same effect as fog of war, whn a player is inside the building the roof disapears. When a player looks inside a door from outside the roof should be invisible where the look angle for the player is.
Status: Unresolved

## 12 (FR) Tactical sprint
The player should use SHIFT as tactical sprint. Walking should be removed. When in tactical sprint the player can not shoot. Tactical sprint should be 130% running speed. This should be a developer setting.
Status: Implemented; all four P2 review defects fixed (historical replay yaw/recovery, retained authoritative fire/throw requests, shared pickup retry on stop). Full UBT build zero compiler warnings, 79/79 automation and 12/12 regression PASS; logs Saved/Logs/RunAll/20261103-222246/. Forward-only hold Sprint replaces Walk, with weapon-adjusted 130% speed, predicted server-authoritative stamina, sprint-to-fire gate, louder 20 m steps and owner stamina HUD. Four remote sprint runs reached 780 cm/s, received zero steady-sprint corrections, exhausted by the 5.50 s service poll and fired after 0.191–0.210 s recovery (configured grace). Physical-input/audio/lag and stationary-overlap pickup playtests remain: Scripts/SprintValidation.md.

## 13 (I) Wrong player name in multiplayer
When testing local the player names are correct for other players, but when joingin via steam the other players name are Player and not what they set.
Status: Unresolved

## 14 (I) Crouching or prone animation is wrong
When crouching or in prone mode the character is still standing, should be visible when crouching the character is bent. When prone the character should be lying down.
Status: Implemented; both P2 review findings fixed. Native procedural crouch-walk/crawl retains locomotion, developer blends and optional animation slots. Crouch now has developer-tunable deep pelvis drop/forward lean and exposed outward knees, with 65° projected silhouette regression checks; fog synchronously snaps/evaluates current posture BEFORE unhide, tested with production visibility/URO policies and hidden bone-finalization assertions. Template code/assets untouched. Full UBT editor build zero compiler warnings; 88/88 automation and 14/14 regression steps PASS (Saved/Logs/RunAll/20261104-091657/), including both non-owner match modes. Thirty gameplay/remote captures regenerated at default/min/max zoom; 22 frames inspected, including standing vs crouch idle/moving at all three zooms. Approach, evidence and remaining manual checks: Scripts/PostureVisualValidation.md.

## 15 (I) Moving sounds are louder than shootings sounds
My player character is making louder running and sprinting sounds than my gun does. Could this be a developer setting for all sounds?
Status: Unresolved

## 16 (FR) Dropbox
Instead of pickup spawn points for one item, there should be crates dropping from the sky (airdrop) with all items.
When the crate lands content are scattered around the create in a radius of 5 to 10 meters. 
In a crate there are random amount of items but there are always at least one Pistol and 1 or two AR's. MG should be random if it is in the crate or not. 
For other items like health and ammo there are at least three items each up to max 8 items.
Handgrenades are at least 2 and max 4.
Crates are always dropped within safe zone. A crate drops every 30 seconds (the timer should be a setting for host to set for each game).
Status: Implemented; standalone blocker and both P2 findings resolved. Server-authoritative airdrops replace spawn points/slot respawns; per-map initial/periodic counts Town 6/3, Battlefield and Test Arena 2/1, bounded safe searches and wave spacing. Navigation-independent ground/capsule checks support landing and scatter without waiting for nav; component footprint rejects small fog-tagged raised caps. TestArena now lands on its actual floor, with no asset retagging. Strict placement validation requires all requested loot, with 23 floor landings / 389 pickups verified across Town, Battlefield and TestArena checks. Empty saved navigation was repaired through editor Python with authored actors unchanged. Editor/game UBT builds zero compiler warnings, 102/102 automation and 14/14 regression steps PASS (Saved/Logs/RunAll/20261104-185631/). Developer loot/fall/scatter/lifetime tuning, host interval, protocol 13, public map markers and cleanup retained. Map migration removed 339 Town / 330 Battlefield points; Test Arena had none. Zone fallback's legacy raised-cap acceptance is reported, not changed. Recook prior map changes before shipping; evidence and manual checks: Scripts/Issue16CrateValidation.md.