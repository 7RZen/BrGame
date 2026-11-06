# GitHub issue 24 — reduce crate spread

Status: implemented and locally validated on UE 5.8 / Win64 Development.

## Decisions

- Halve both scatter radii: `CrateScatterMin` 500 → **250 cm**, `CrateScatterMax` 1000 → **500 cm**.
- Keep same-wave crate spacing at **20 m**. Spacing was previously derived as twice the scatter max, so it is now a separate developer setting, `CrateWaveSpacing` (default 2000 cm, min 170). `FBrCrateRules::WaveSpacing(MinSpacing, ScatterMin, ScatterMax)` still never goes below twice the scatter max or 170 cm, so scatter discs cannot overlap if tuning changes.
- Quest tokens use the same tighter scatter. The closer fallback band follows automatically: 150 cm to `CrateScatterMin` (now 150–250 cm).
- Side effect: the minimum supporting-floor span (`FloorHalfWidth`) drops from 21.7 m to 11.7 m at defaults, so smaller tagged floors become eligible.

## Validation

- `BrGameEditor Win64 Development` build: **Succeeded**.
- `Automation RunTests BrGame.Crate`: **8/8 passed**. `BrGame.Quest`: **4/4 passed**. New `MapCountsAndWaveSpacing` cases cover tuned spacing that ignores a smaller scatter and a non-finite spacing value.
- `Scripts/RunCratePlacementCheck.ps1` (NullRHI): **PASS** on Town (6/3), Battlefield (2/1) and TestArena (2/1). Every wave logged `Spacing=2000`. 288 crate items landed 252–499 cm from their crate, with 0 fallbacks and 0 skipped items.
- Known issue: running `BrGame.Crate+BrGame.Quest` in one session crashes at editor exit (`UBrInventoryComponent::CancelCombat`, null world during EndPlay) after all tests pass. It reproduces on the unchanged baseline, so it is unrelated to this change.

## Remaining checks

- Windowed placement check (`RunCratePlacementCheck.ps1 -Windowed`) and a manual playtest to judge the tighter loot spread visually.

## Changed paths

- `Source/BrGame/Core/BrDeveloperSettings.h`, `Config/DefaultGame.ini`
- `Source/BrGame/Pickups/BrCrateRules.h`, `Source/BrGame/Pickups/BrPickupManager.cpp`
- `Source/BrGame/Tests/BrCrateTest.cpp`
- `GameDescription.md`, `Docs/AddingMaps.md`, `.github/copilot-instructions.md`
