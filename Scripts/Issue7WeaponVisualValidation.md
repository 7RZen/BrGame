# Issue 7 — weapon placeholder visuals

Status: implemented and validated on UE 5.8.4 / Win64 Development. Rendered in-game inspection remains a manual check.

## Implementation

- One `UBrWeaponDefinition::VisualScale` drives the held mesh and its world pickup visual. A dropped pickup resolves the mesh and scale from its weapon payload instead of the generic pickup template.
- The three weapon definitions use the Engine BasicShapes Cube, with local X as the weapon length axis: pistol 25 cm, assault rifle 50 cm, machine gun 75 cm. The 8 cm cross-section is shared; these are visible blockout placeholders, not modeled or rigged firearms.
- Pickup-specific tint/material and label remain in place. Nonweapon pickup mesh/scale behavior is unchanged.
- `create_weapon_assets.py` now writes the scales and verifies the persisted values, ordering, and cube references.

## Validation

- Full `BrGameEditor Win64 Development` build: **Succeeded**, exit 0.
- `Scripts/Content/create_weapon_assets.py`: **Succeeded**, exit 0; assets saved and checks completed.
- `Automation RunTests BrGame.Pickups`: **6/6 passed**, including `WeaponPlaceholderLengths` and `WeaponVisualResolution`.
- Win64 Development `BuildCookRun` to `Build/Package`: **Succeeded**, exit 0.
- `RunPackagedSmoke.ps1 -Packaged Build/Package/Windows/BrGame.exe -TimeoutSeconds 360`: **Passed**; packaged menu, map selector, and two-client match flow succeeded.

The packaged smoke verifies startup, map/content loading, and multiplayer flow, but does not visually assert held/pickup/dropped weapon appearance. Manually inspect the three sizes in gameplay after launch. Replacing the cube with production models later requires updating the weapon mesh references and checking their local forward axis/scale.

## Changed paths

- `Source/BrGame/Combat/BrCombatDefinitions.h`
- `Source/BrGame/Core/BrCharacter.cpp`
- `Source/BrGame/Pickups/BrPickup.cpp`
- `Source/BrGame/Tests/BrPickupTest.cpp`
- `Scripts/Content/create_weapon_assets.py`
- `Content/BrGame/Weapons/DA_Weapon_Pistol.uasset`
- `Content/BrGame/Weapons/DA_Weapon_AssaultRifle.uasset`
- `Content/BrGame/Weapons/DA_Weapon_MachineGun.uasset`
- `GameDescription.md`