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
Status: Unresolved

## 4. (FR) Sound
Add sounds for:
* Firing weapon
* Handgrenade explosion
* Downed scream (The Wilhelm scream)
* Moving sounds like walking, running and crouching with falloff
* Pickup sounds
Status: Resolved (placeholder sounds; pending playtest)

## 5. (FR) In-game menu
Player should be allowed to leave game. Press ESC to get in-game menu with option to leave or to respawn (in case of stuck)
Status: Resolved (ESC menu, settings/rebinding, server unstuck and forfeit implemented; four review fixes validated: 62/62 automation, menu smoke, 12/12 full regression PASS. Remaining physical-input/network-stress playtests: Scripts/MenuValidation.md.)

## 6. (FR) Weapons
The model used for weapons now is the same for all weapons.
Ideally the model should be short for pistol, medium for Assault rifle (AR) and long for Machine gun (MG).
The same for the pickup, medium model for AR and long for MG.
Status: Unresolved

## 7. (FR) Flinching
When hit with a bullet the player should have a visual indication. 30% flinch on gun aiming.
Want to discuss option, but it could be like a brief flinch, brief stun and/or read overlay on screen.
Status: Unresolved

## 8. (FR) Aiming
Player is locked at looking up, mouse rotates map. Keys W and S moves forward and backwards. Keys A and D is strafe left and right.
Status: Resolved (relative MouseX turns facing/camera instantly; persistent sensitivity slider, zoom/pitch-independent below-centre framing, UI cursor transitions and downed/spectator turning implemented. Independent-review P2 fixed: accepted fire/grenade aim commits server control/actor yaw; older queued moves cannot undo it, newer turns remain unrestricted. UBT build: zero compiler warnings; 68/68 automation and 12/12 regression steps PASS. Earlier pitch 30/65/90 and Settings screenshots inspected. Physical mouse/focus/network-stress playtests remain: Scripts/AimingValidation.md.)

## 9. (FR) Slide
Pressing CTRL til slide if running.
Status: Unresolved

## 10 (FR) Maps and map selector
The hoster should be able to choose from available maps.