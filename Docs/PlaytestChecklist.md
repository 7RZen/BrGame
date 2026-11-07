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
- [ ] **Lobby map selector (host + remote):** cycle Town → Test Arena → Random;
  verify the same name, description, preview and hint on both clients. Remote
  controls must be disabled. Join the lobby late and verify the current choice.
  Start each map; verify the map announcement and destination, then results →
  lobby retains the selection. Random must remain Random and may re-roll.
- [ ] **Map browser / failure paths:** refresh the Null and two-PC Steam
  browsers after changing the map; expect its name (Random stays Random).
  Retest password/version rejection and late joins. In a developer-only test
  remove a preview: expect a text placeholder. Invalid/missing map entries
  warn and are omitted; an empty list must refuse Start with a visible error.
  Try player counts outside recommendations: hints must not enforce a cap.
- [Issues] **Movement/map:** run/sprint, crouch, prone, rotate/zoom the camera, enter
  buildings, cross low cover and approach the boundary. Expected: movement
  speeds/postures change; walls block movement and shots; low cover blocks
  shots but not fog vision; doors are passable; boundary contains players.
- [ ] **Posture visuals (Issue 14):** compare host/remote crouch idle/walk and prone
  idle/crawl at camera pitch 30/65/90 and several yaws/zoom levels. Check grounded
  crouch feet, face-down forward prone body, forward hand-held weapon and smooth
  0.25/0.4 s transitions, including rapid reversals, crouch↔prone and sprint.
  Hit each low posture; mesh jerk must recover without snapping the pose.
  Down/revive from both low postures; downed/dead must retain their back-lying pose.
  Join while another player is prone, hide/reveal through fog and spectate them:
  no stale standing pose. Check stairs/slopes and 60-character URO/visibility costs.
  Optional future animation slots need retesting with real crouch/prone assets.
- [ ] **Tactical sprint (host + remote):** hold/rebind Left Shift, test W/WA/WD versus
  A/D/S and mouse turns at the ±50° cone. Expect 1.3× weapon-adjusted run speed,
  five seconds of energy, 1 s regen wait, 5 s full regen, 0.5 s minimum restart.
  Verify partial use and HUD bar hiding at full. Crouch stands only with room;
  prone stays prone. Try blocked uncrouch beneath cover.
- [ ] **Sprint cancellation/combat:** tap/hold Fire and throw G during sprint:
  no immediate shot/throw, then action after 0.2 s; automatic fire keeps sprint
  suppressed. E stops sprint then picks up/revives. Test posture/down/menu/map/unstuck/release/non-forward/empty cancellations.
  Repeat with artificial lag/loss and watch for corrections, lost shots or extra ammo.
  Old saved binding profiles must show Sprint (not a stale Walk row/conflict).
- [ ] **Slide (host + remote):** rebind/reset Left Ctrl; standing/slow/back/strafe/
  prone/downed/air/menu presses do nothing, C still crouches. Forward run/sprint
  slides boost 1.2× and slow to crouch over 0.8 s, finishing crouched; Shift afterwards
  stands/sprints only with clearance. Confirm 1 s cooldown from exit, including wall,
  ledge, downed, Z and menu exits; no stamina charge. Turn mouse and strafe during
  slide: facing changes but travel direction stays locked. Movement cancels unstuck.
- [ ] **Slide combat/presentation:** fire/throw immediately during slide without
  sprint recovery (normal ammo/rate rules remain). Both shooter and target count as
  crouching. Host/owner/remote see low lean-back pose, legs forward and left arm down;
  fog reveal/late join snaps current gait. Hear one scrape within 10 m through fog,
  no slide footsteps or owner duplication. Test ramps/stairs, blocked ceilings,
  100–200 ms latency/jitter/loss, correction replay and teardown mid-slide.
- [OK] **Combat:** fire/reload each weapon at standing, crouching and prone
  targets; throw grenades near and behind cover. Expected: server-authoritative
  damage follows hit chance/posture reduction, grenade radius and wall
  occlusion; self/friendly damage follows the selected rules.
- [OK] **Down/revive/respawn:** down a player, revive within range, then test
  voluntary and timed respawn and the tenth-death limit. Expected: revive
  restores configured health without a death; choice respawn penalizes max
  health by 10% per choice, timeout follows its configured rule, gear persists,
  and respawn protection ends on firing or timeout.
- [ ] **#30 Plant (host + two remotes, both modes):** down someone, hold F from
  a teammate and an enemy. Verify 10 s, release/range/damage/non-alive cancellation,
  rebind/reset/conflict rejection, and actual bound key/progress in the left panel.
  Run E revive concurrently: revive-first yields no item/score; plant-first keeps
  revive running. Race two planters: earliest wins +1 exactly once, nobody can
  plant again until a later down. Repeat near timeout/voluntary respawn deadlines.
- [ ] **#30 review regressions:** delay server service across voluntary < Plant <
  revive < timeout deadlines: voluntary wins, with no later item/score/revive.
  Repeat ties (revive > timeout > voluntary; any exit beats tied Plant). Load a
  pre-feature saved profile with another action customized to F: that action
  keeps F, Plant shows "Unbound (click to bind)" in both menu and in-game settings,
  and F cannot trigger Plant. Bind Plant to a free key, restart, check persistence
  and actual HUD key, then reset defaults.
- [ ] **#30 penalty lifecycle/prediction:** test each definition via developer
  tuning on host and owner/remote. Dormant tube before revive/respawn; 15 s Rocket,
  30 s others afterwards. Rocket steers with mouse/facing and cannot stop/reverse,
  sprint/crouch/prone/slide; can shoot. Check weapon-adjusted 2× / 0.5× speeds,
  ceilings/stairs/walls, 100–200 ms lag/loss, saved-move replay and expiry corrections.
  Down again during an effect: timer continues; a new plant replaces it dormant.
  Reconnect both dormant/active, offline timeout, final elimination and next round:
  no renewed timer, duplicate point or stale item.
- [ ] **#30 cosmetics/fog/audio:** inspect backward pelvis tube on standing,
  downed supine, crouch/prone and remote smoothing. Smoke puffs stay in world
  space for a few seconds, capped at 16; flame readable at tube. Hide/reveal behind
  walls and leave vision (listen host, remote, spectator, late joiner): no tube,
  trail or flute from fog-hidden players, including retained puffs/channel delay.
  Smoke is silent; nearby flute loops and responds to master/Effects mute/volume,
  and stops on expiry, elimination, disconnect and travel. No floating text.
- [ ] **Issue 3 — face-up bodies:** down a standing/crouched/prone player at several
  facings, view as owner and remote observer at pitch 65° and min/default/max zoom.
  Expect chest/face upward, head along facing, back resting on ground, readable
  pulsing HELP. Turning while downed must keep this orientation; no downed crawl
  is currently implemented. Revive must restore normal posture/weapon/smoothing.
  Respawn/elimination/forfeit/offline timeout must leave a grounded, arms-out,
  bone-white face-up corpse at the victim's last facing; check a late joiner too.
  Living prone/crawl stays face down. Capture command: `Scripts/RunShotSmoke.ps1 -Skeletons`.
- [ ] **Side panels (16:9, ultrawide, 4:3/5:4, windowed resize):** black bars both sides, world only in the centre; all text in the panels with no overlap or clipping into the play area during dense states (downed + respawn hold, revive, outside zone, sudden death, several quests, spectating). No pickup labels or HELP text in the world. Zoom limits feel right; the full map (M) and menus use the whole screen. Check host, remote client and a spectator.
- [ ] **Armor plates:** collect up to 2 plates from a crate; a third is refused until one breaks. Hits drain the most damaged plate first, then health; zone damage skips armor. Hear the break sound; the HUD armor bar shows for owner and spectator only; armor survives revive/respawn and clears next round.
- [ ] **E-only pickups (host + remote):** walk and sprint over ammo, health, grenade, armor, weapon and quest-token pickups: nothing is collected on touch. The HUD prompts `Press E to pick up <item>` for the nearest usable item; each E press picks up exactly one. Full health/armor/ammo items get no prompt. With a downed teammate in range, E revives instead.
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
  pickup sound only on the picker's machine.
- [ ] **Volume balance (#16):** run, sprint, crouch and slide next to your own and a nearby player's pistol/AR/MG fire. Expected: gunfire is clearly louder than any movement; reloads, clicks, grunts and pickups sit below gunfire; voice lines stay clear. Tune under Project Settings → BrGame → Audio | Volume (or a weapon's FireVolume) and record final values.
- [ ] **Reload/empty/pain/voice:** reload each weapon (light/medium/heavy) and cancel mid-reload by swapping or sprinting; empty all ammo and pull the trigger; take hits with and without armor; down an opponent. Expected: a nearby player hears the reload stop on cancel, one click per pull, grunts within 10 m (muted thud when armor absorbs the hit, no grunt from zone damage), and a random line in the attacker's chosen voice within 20 m. The Voice slider changes only voice lines, and the voice choice persists after restart.
- [ ] **Hearing/fog:** compare running (15 m), sprinting (louder, 20 m), crouching
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

## Issue 7 — hit feedback (manual playtest pending)
- [ ] Compare pistol/AR/MG hits and grenade damage on listen host and remote owner:
  quick bounded twist; grenade is stronger with a positional jolt; no aim/control rotation,
  movement slowdown or stun. Sustained/multiple hits must not accumulate unbounded shake.
- [ ] Turn rapidly while arcs remain visible, at pitch 30/65/90, different zoom/aspect
  ratios and below-centre framing. Arcs should stay aligned from the pawn toward the
  original shot origin, not follow a moving shooter. Up to eight arcs, then oldest replaced.
- [ ] Hit through fog from a hidden source: victim gets direction/red edges only;
  no attacker actor, name, minimap marker or visibility change. Missed hit roll,
  spawn-protected/zero-damage/downed/downing hits give no feedback. Friendly damage counts.
- [ ] Observe another victim while standing/crouching/prone, moving/turning and with
  simulated-proxy smoothing: brief jerk away and red overlay, then exact restored pose
  and team material. Hide/reveal them during hits: no hidden reaction or replay on reveal.
- [ ] Spectate a damaged target: no camera twist/jolt or victim arcs/edge flash.
  Check rapid target switching, down/revive, respawn, reconnect, late join and round travel.
- [ ] With latency/packet loss and many simultaneous players (~60), verify comfort,
  reliable owner notification ordering and acceptable bandwidth; remote cosmetics are
  intentionally unreliable. Review flash accessibility and placeholder intensity.

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
