# GitHub #16 — Movement louder than gunfire validation

## Cause
Placeholder footsteps and gunshots have similar source loudness (RMS about -19 dB each). Sprint steps were boosted ×1.3 and repeat several times per second at the listener, and every other kind played at 1.0.

## Decisions (user, 2026-11-06)
- Developer-only per-sound volumes (Project Settings → BrGame → Audio | Volume), clamped 0–2 and read on each play.
- Per-weapon `FireVolume` on `UBrWeaponDefinition` (default 1.0).
- Defaults: Fire 1.0, Explosion 1.0, Downed 0.9, VoiceLine 1.0, RunStep 0.35, SprintStep 0.5, CrouchStep 0.1, Slide 0.5, Reload 0.6, DryFire 0.5, Pain 0.7, PainArmor 0.6, Pickup 0.6, ArmorBreak 0.8.
- The old `SprintStepVolume` (×1.3, run-relative) and `CrouchStepVolume` (0.2) semantics are replaced by the absolute table.
- The player Master/Effects/Voice sliders are unchanged.

## Implementation
- `Core/BrAudioRules.h`: pure per-kind volume lookup and clamp.
- `Core/BrDeveloperSettings.h`, `Config/DefaultGame.ini`: the Audio | Volume table.
- `Core/BrAudioSubsystem.{h,cpp}`: gain applied in the 3D path and the 2D (pickup/armor break) path. The weapon gain is resolved from the fire-sound path through cached `WeaponRegistry` definitions, so local predicted and remote shots match. Unknown sounds use 1.0. Network layout and session protocol 16 are unchanged.
- `Combat/BrCombatDefinitions.h`: `UBrWeaponDefinition::FireVolume`.
- Tests: `BrAudioTest.cpp` (two new volume tests), `BrSprintTest.cpp`.

## Verification
- `Build.bat BrGameEditor Win64 Development` — succeeded.
- Automation, one suite per run: Audio 9, Sounds 4, Sprint 6, Combat 5, Pickups 7 — 31/31 passed.
- `RunAudioSmoke.ps1` on a real audio device — PASS.

## Remaining
- A human listening/multiplayer balance pass (see "Volume balance (#16)" in `Docs/PlaytestChecklist.md`). The defaults are estimates.
- Weapons sharing one fire sound must use equal `FireVolume`; on conflict a warning is logged and the first registry key (alphabetical) wins.
