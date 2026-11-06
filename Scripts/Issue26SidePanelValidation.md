# GitHub #26 — Narrower play area with side-panel HUD validation

## Decisions (user, 2026-11-06)
- Centered 4:3 play area; black side panels keep a minimum width so text fits (narrower play area on near-4:3 screens, wider panels on ultrawide).
- Framing A: ortho width / zoom limits apply to the play-area width, so players see more ahead (about 15.5 m at default zoom vs the 15 m vision radius).
- Developer setting, identical for everyone.
- Floating world text removed: pickup labels and the downed "HELP!" sign.
- The movement debug line is developer-only (`bShowMovementDebugHud`, default off, compiled out of Shipping).
- Approach A: engine-constrained view (nothing rendered under the panels).

## Implementation
- `Core/BrViewLayoutRules.h` (new, pure): viewport size + aspect + min panel width + scale → play rect and left/right panel rects.
- `Core/BrCharacter.{h,cpp}`: `CalcCamera` constrains the view to the play rect; forward offset and clip planes use the play aspect; `HelpText` removed.
- `Core/BrPlayerController.{h,cpp}`: fallback (no-target spectator) camera uses the same constraint.
- `Core/BrHUD.{h,cpp}`, `BrCompassHUD.cpp`, `BrMinimapHUD.cpp`, `BrHitFeedbackComponent.cpp`: left/right panels with wrapping and height caps; compass and minimap fit to the right panel; hit indicators stay on the pawn in the play rect; dark team colours are lightened to a luminance floor. UE 5.8 initializes the HUD Canvas to the constrained view rect, so a scoped absolute Canvas transform draws the panels and the full map outside it. Panels are computed from the full viewport, not the current view target.
- `Core/BrAimingRules.h`, `BrMinimapRules.h`: small numerical-safety bound for narrow aspects; minimap markers scale with map size, with a positive inset.
- `Pickups/BrPickup.{h,cpp}`: `Label` component removed (`GetPickupLabel()` kept for the HUD prompt and logs).
- `Core/BrDeveloperSettings.h`: `PlayAreaAspectRatio=1.3333`, `MinSidePanelWidth=160` (720p reference, i.e. 240 px at 1080p), `bShowMovementDebugHud=false`.
- Tests: `BrViewLayoutTest.cpp` (new), `BrAimingTest.cpp`, `BrMinimapTest.cpp`, `BrPickupTest.cpp`, `BrHitFeedbackTest.cpp`.

## Verification
- `Build.bat BrGameEditor Win64 Development` — succeeded.
- Automation, one suite per run, initial: ViewLayout 1, Aiming 6, Pickups 7, Life 7, Compass 2, Minimap 3, Armor 5, HitFeedback 4 — 35/35. After the review fixes: ViewLayout 2, Aiming 8, Minimap 4, Compass 2, HitFeedback 4 — 20/20.
- `RunShotSmoke.ps1` (default and `-Minimap`) and `RunFlowSmoke.ps1` — PASS.
- Screenshots, visually inspected: `Saved/Screenshots/Issue26/1920x1080_review_grouped.png`, `1280x1024_grouped.png`, `1920x1080_full_map.png`.
- Independent review: three P2 issues fixed (spectator fallback panels, narrow-aspect pawn offset, tiny-minimap marker inset).

## Remaining manual checks
- See "Side panels" in `Docs/PlaytestChecklist.md`: PIE, ultrawide, live window resize, dense HUD states, remote client and spectator, packaged Shipping.
- Split-screen is not supported (one local player per viewport).
