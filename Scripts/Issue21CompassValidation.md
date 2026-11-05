# GitHub issue 21 — compass and coordinates

Status: implemented and locally validated on UE 5.8.4 / Win64 Development. Quest objective bearings are intentionally deferred until issue 20 defines location-based objectives.

## Implementation

- A native Canvas HUD compass is shown at the top center during an active local match. Its tape scrolls with the viewed character's camera yaw; the centered yellow pointer indicates current heading and the cardinal ticks use the minimap convention (+X = north, positive yaw toward +Y/east).
- Signed world X/Y coordinates are displayed in meters to one decimal on the left side. During flow spectating, heading and coordinates follow the viewed character. The compass hides when the viewport is too narrow to fit safely.
- The HUD uses the existing local view and match gating; no replication, gameplay authority, quest, content asset, or input changes were made.

## Validation

UE 5.8.4 / Win64 Development:

- Full `BrGameEditor Win64 Development` build: **Succeeded**, exit 0.
- `Automation RunTests BrGame.Compass`: **2/2 passed** (`Coordinates`, `Heading`).
- `Automation RunTests BrGame.Minimap`: **3/3 passed** (`FullMapFit`, `MarkerFilter`, `Transform`).
- `Scripts/RunShotSmoke.ps1 -Minimap -RunLabel Compass`: **Passed**, captured 5 gameplay screenshots at 1280×720. The `grouped` screenshot (camera yaw 37°) and `full_map` screenshot (yaw 48.2°) were inspected; the centered heading strip and left-side X/Y meter readout render in both.
- `git diff --check`: **Passed**.

## Remaining checks

- Manually inspect narrow/ultrawide resolutions and spectator handoff; quest direction indicators remain future work once objectives have destinations.

## Changed paths

- `Source/BrGame/Core/BrHUD.h`, `BrHUD.cpp`, `BrCompassHUD.cpp`, `BrCompassRules.h`
- `Source/BrGame/Tests/BrCompassTest.cpp`
- `GameDescription.md`