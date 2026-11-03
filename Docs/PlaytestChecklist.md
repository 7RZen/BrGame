# BrGame playtest checklist

Record the build/commit, Windows version, player count, mode, and exact steps
when reporting an issue. Attach the relevant logs from
`Saved\Logs\MP\` (or `Saved\Logs\RunAll\<timestamp>\` for automated runs) and
screenshots/video for visual issues. Remove or redact credentials and avoid
sharing raw Steam/network logs.

## One PC: local menu and match

Start separate local windows from the project root:

```powershell
.\Scripts\RunLocalMultiplayer.ps1 -Clients 2 -Menu
```

In Player1, host; in Player2 and Player3, refresh/join. Use a short player
count and test both modes.

- [OK] **Menu/lobby:** set names, grouped mode, group count, friendly fire,
  revive rule, max players, visibility, password and pickup respawn time.
  Expected: lobby settings/list update for everyone; only host controls work.
- [OK] **Start/results/return:** start a match on L_Town, finish it, and wait
  for results. Expected: map travel, winner/table, return to lobby, and prior
  lobby settings/groups retained.
- [Issues] **Movement/map:** run/walk, crouch, prone, rotate/zoom the camera, enter
  buildings, cross low cover and approach the boundary. Expected: movement
  speeds/postures change; walls block movement and shots; low cover blocks
  shots but not fog vision; doors are passable; boundary contains players.
- [OK] **Combat:** fire/reload each weapon at standing, crouching and prone
  targets; throw grenades near and behind cover. Expected: server-authoritative
  damage follows hit chance/posture reduction, grenade radius and wall
  occlusion; self/friendly damage follows the selected rules.
- [OK] **Down/revive/respawn:** down a player, revive within range, then test
  voluntary and timed respawn and the tenth-death limit. Expected: revive
  restores configured health without a death; choice respawn penalizes max
  health by 10% per choice, timeout follows its configured rule, gear persists,
  and respawn protection ends on firing or timeout.
- [OK] **Pickups/zone:** collect health, ammo, grenade and weapon pickups; watch
  them respawn, then move outside the shrinking zone. Expected: health and
  grenade caps hold, weapon swap drops the old loaded weapon, and outside-zone
  damage/stages progress to a final result.
- [OK] **Fog/spectating/late join:** hide behind walls, leave/re-enter vision,
  eliminate one player, and join during a match. Expected: unseen enemies are
  absent on the remote client, teammates share vision, spectators see eligible
  players, and late joiners spawn away from enemies.
- [ ] **Minimap/full map:** rotate/zoom and approach the zone edge; press M,
  move/fire with the full map open, then close with M/Esc (also test rebinding).
  Expected: minimap screen-up follows camera forward; north-up full map fits
  the play area with a yellow camera-direction line. Fog aligns with terrain;
  unknown is black, explored dim, visible clear. Self arrow, group-colored
  teammates and pulsing downed crosses are readable; only group-visible enemies
  have markers in grouped mode. All-vs-all never shows enemy markers, including
  visible enemies. No pickups. Current/next zone rings and edge direction work.
  Spectators use the target team's view; menus/lobby/results have no map.
- [OK] **Clean next round:** start a second round. Expected: life/results
  bookkeeping and exploration reset, pickups repopulate, and no previous-round
  state leaks into the match.

Close instances by their window after testing. The local launcher is intended
to leave processes running for interactive use.

## ESC menu / settings / unstuck

- [ ] **Local input gate:** hold movement/fire and revive/respawn hold, then open Esc.
  Expected: movement/fire stop and no new movement, aim, posture, zoom, reload, throws or
  interaction input reaches the pawn; world/enemies/zone continue. Resume/Esc restores
  controls without permanently disabling movement. Test listen host and remote client.
- [ ] **Map/console/order:** open M then Esc, including after rebinding Menu.
  Expected: only the map closes on the first press. Next Menu opens the overlay.
  Fixed Esc closes the map regardless of Menu binding. Tilde/Period console works
  in match, lobby, results and menu, including during key capture.
- [ ] **Phases:** lobby/results/spectating show Settings/Leave/Close but no Unstuck;
  downed players cannot Unstuck. Start page reuses Settings without another Esc menu.
- [ ] **Binding capture:** on start page and Esc Settings, rebind all 16 mapping rows,
  including four screen-relative movement directions, mouse fire and wheel Zoom.
  Expected: immediate application; Esc cancels capture (does not close overlay);
  conflicts warn/reject without modifying either binding. Tilde/Period cannot be bound.
  Restart/travel/reopen to verify machine-local persistence; Reset restores defaults.
  Zoom's wheel axis is signed; a digital replacement produces the positive (zoom-in) input.
  Test a mouse-button Menu binding and selecting mouse buttons outside the settings rows.
- [ ] **Unstuck success:** stand still in blocked geometry for the configured duration.
  Expected: HUD progress, nearest sampled capsule-clear grounded spot within range,
  no player overlap, no LOS requirement, no death/health penalty or invulnerability.
  Inside-zone players remain inside; outside players cannot jump into safety.
- [ ] **Unstuck cancel/fail/cooldown:** move beyond tolerance, take damage, fire/throw,
  toggle posture, become downed, enter sudden death or change zone status during hold.
  Expected: cancellation message, no cooldown. With no open spot, explicit failure and
  no cooldown. Success charges cooldown shown in menu; repeated requests cannot restart
  hold, bypass cooldown or choose a destination. Respawn/reconnect retains cooldown;
  new round resets it. Check both nav-tile and grounded fallback maps.
- [ ] **Intentional client leave:** confirm during match, then reconnect before 60 s.
  Expected: immediate server elimination, corpse retaining gear, winner recheck, no
  restoration that round. Cancel does nothing. Lobby/results leaves impose no penalty.
  Contrast cable/packet loss: existing 60 s grace still applies if no forfeit reached server.
- [ ] **Host leave:** confirmation warns that the match ends for everyone; cancel and
  confirm in lobby/match/results. Expected: all clients return to start page with a
  useful host-left message, and no frozen settings/menu/timers after another session.

## Audio (placeholder sounds; manual listening pending)

- [ ] **Levels/persistence:** open start-page Settings, change Master/Effects,
  try 0% and 100%, restart and travel lobby → match → results → lobby.
  Expected: values persist locally, silence at zero, no multiplication/stacking
  after travel. Other players' preferences are unaffected.
- [ ] **Combat/life/pickups:** compare pistol/AR/MG, explode a grenade, down/revive,
  and collect each pickup type. Expected: distinct fire with slight variation,
  boom and original generated vocal; no scream replay on fog entry/late join;
  pickup sound only on the picker's machine. Empty/reload audio is not implemented.
- [ ] **Hearing/fog:** compare running (15 m), walking (quiet, 6 m), crouching
  (very quiet, 3 m) and prone (silent). Hide opponents behind walls and cross fog
  boundaries while moving/firing. Expected: footsteps from anyone in range,
  gunshots through fog to 50 m, booms to 80 m, screams to 40 m; no duplicate
  fire/pickup playback or exposed enemy visuals. Test listen host and remote clients.
- [ ] **Spatial view/spectating:** rotate/zoom with camera pitch 45°, 65° and 90°;
  compare left/right positions and switch spectator targets. Expected: panning
  follows screen orientation; distance follows the viewed pawn, not camera height.
  Verify listen-host spectator panning follows camera yaw rather than target facing;
  with no selected target, no gameplay sounds are received at the old position.
  Check packet-loss/fog channel handoffs for cadence gaps/doubles and many simultaneous
  players for mix balance/clipping. Sustained MG fire should not crowd out screams
  or footsteps; category caps may steal overlapping voices.
  Automated device logs cannot verify audibility.

## Two PCs: Steam

Use standalone/packaged builds on two PCs with different logged-in Steam
accounts and matching builds. Do not use PIE or two Steam users in one process.
See `Scripts\Step9Validation.md` for App ID 480/Steam launch caveats.

- [ ] **Discovery/access:** host Public, Friends only and Invite only in turn.
  Expected: browser visibility matches policy; friends/invites join only through
  the appropriate Steam route.
- [ ] **Password/capacity:** try blank, wrong and correct unique game passwords
  and a full finite-capacity lobby. Expected: protected joins require the
  password even through invites, and excess players are rejected.
- [ ] **Match/session lifecycle:** join, late join, leave/reconnect, finish a
  round and start another. Expected: status/counts update, reconnect retains
  allowed round state, and results return to the same lobby.
- [ ] **Network failure:** test host exit, unavailable host and a temporary
  disconnect. Expected: useful error/menu recovery and no indefinitely stuck
  join state.
- [ ] **Issue report:** include the exact scenario, both game logs from
  `Saved\Logs\MP\` and Steam/engine version; redact all passwords and bearer
  proofs. Do not attach raw diagnostic logs publicly.
## Issue 8 — relative mouse aiming
- In a listen-server match, move the mouse horizontally: pawn faces screen-up and camera turns instantly.
  Vertical mouse movement must do nothing. No crosshair/cursor should appear in gameplay.
- W/S forward/back and A/D strafe relative to facing, including diagonal speed and rebound keys.
  Fire/grenades must travel along facing on both host and client. Observe smooth remote facing.
- Change mouse sensitivity in Settings; restart and verify persistence without altering volume/bindings.
  There must be no mouse Look axis row in key rebinding. Reset other bindings and test them.
- Verify pawn origin stays at 75% viewport height at pitch 30/65/90, multiple aspect ratios,
  window resize, and wheel zoom limits. More world should be visible ahead, without clipping/fog leaks.
- Downed client can turn view/facing while movement/fire remain blocked; revive restores controls.
  Spectate turning host and client targets: view, minimap and positional audio orientation must agree.
- Open/close ESC, Settings, rebinding, full map, lobby/results/start page and confirmation dialogs:
  cursor usable in each UI; gameplay recaptures without a shot from the closing click.
- Alt-tab while firing/moving/turning: cursor releases, held fire/input stops. Click back:
  capture resumes without firing the capture click. Check windowed/fullscreen and console Period/Tilde.
