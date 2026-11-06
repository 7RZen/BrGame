# GitHub #25 — Player-initiated pickups validation

## Decisions (user, 2026-11-06)
- Touch/overlap pickup removed entirely (no per-definition flag). Ammo, health, grenade, armor, weapon, quest token and future types all require Interact (E).
- Revive keeps priority over pickup when both are in range.
- Non-applicable items (full health/armor/ammo, grenade cap) stay untargetable with no prompt; E picks the nearest usable item.
- One press picks up one item; no hold-to-collect.
- `InteractRadius` stays 150 cm *(dev)*.
- HUD text prompt only (`Press E to pick up <item>`); no world highlight.
- Server-authoritative: the client sends interact intent only; the server selects and validates the pickup. Session protocol stays 16.

## Changes
- `Pickups/BrPickup.{h,cpp}`: removed overlap binding, `Overlap`, `TryAutoCollect`, activation timer/`EnableCollection`; `FindInteractPickup` accepts every collectable type. The trigger keeps query collision for the interact object query, with overlap events off.
- `Pickups/BrPickupDefinition.{h,cpp}`: removed `bRequiresInteract`.
- `Pickups/BrPickupManager.cpp`: armor/quest validation keeps type checks only.
- `Core/BrCharacter.{h,cpp}`, `Core/BrCharacterMovementComponent.cpp`, `Combat/BrArmorComponent.cpp`, `Combat/BrHealthComponent.cpp`, `Combat/BrInventoryComponent.cpp`: removed auto-pickup retry machinery and its callers.
- `Core/BrMatchSmoke.cpp`: pickup E2E collects through `StartInteract` (the real server interact path).
- Tests: `BrPickupTest.cpp` (no touch/tick collection, nearest usable selection, full-health skip, one item per press, radius, revive priority), `BrQuestTest.cpp` (interact path), `BrArmorTest.cpp`/`BrSprintTest.cpp` (retry assertions removed).
- `Scripts/Content/create_{pickup,armor_pickup,quest_pickup}_asset.py`: no longer set `bRequiresInteract`. Existing assets keep a stale serialized tag that is skipped on load (no warnings observed); re-saving is optional.

## Verification
- `Build.bat BrGameEditor Win64 Development` — succeeded.
- Automation, one suite per run: Pickups 7, Armor 5, Quest 4, Sprint 6, Crate 8, Life 7 — 37/37 passed.
- Match smoke on Town (temporary `DefaultMapId=Town` override, as in the #1/#23 runs) — PASS, both rounds, including the pickup probe through `StartInteract`.
- `RunCratePlacementCheck.ps1` — PASS (Town/Battlefield/TestArena, 12/4/4 landings, complete loot).

## Remaining manual checks
- See the "E-only pickups" item in `Docs/PlaytestChecklist.md`: HUD prompt on host and remote clients, sprinting over items, stacked crate loot, rebound Interact key.
