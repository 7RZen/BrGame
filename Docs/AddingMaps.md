# How to add a new map

1. Author a world with the native `BrGameMode` (no template GameMode override).
   Keep collision, orthographic visibility and the `BrVision` responses consistent
   with Town. Do not edit map binaries outside Unreal Editor/Python.
2. Provide capsule-clear PlayerStarts (radius 42 cm, half-height 96 cm).
   Navigation can also supply starts; the safe-zone spawn path has a bounded
   downward-ground-trace spiral fallback. Check actual playable collision, not
   just a visible floor.
3. Provide walkable runtime navigation and clear ground inside the zone/playable
   bounds for airdrop crates. No pickup spawn points are used. Allow an 85 × 96 cm
   clearance capsule 100 cm above crate floors (42 × 96 cm for scattered items),
   plus room for 5–10 m scatter. Bounded searches skip/log impossible drops.
4. Prefer `BrZoneBounds` for explicit playable bounds. The safe zone is spawned
   by GameMode, not placed. Its fallback bounds are navmesh volume bounds, then
   PlayerStarts/colliding static geometry; emergency 100 m bounds are not valid
   map authoring. Tag ground actors/components `BrFogGround` (including slopes).
5. In Project Settings → BrGame → Maps, add a unique stable `Id` (not None,
   Random or a path), `DisplayName`, `Map`, `Description`, `Preview`, and player
   recommendations. Recommendations are hints only. Set `DefaultMapId` if wanted.
   Commit the resulting `Config/DefaultGame.ini`. `MatchMap` has been removed.
6. Add the world package to Packaging → **Maps to cook** (`MapsToCook` in
   DefaultGame.ini). Keep preview textures under `/Game/BrGame/Maps/Previews`;
   `/Game/BrGame` is already in `DirectoriesToAlwaysCook`. For a preview outside
   that tree explicitly cook its folder. Do not rely on the current editor map.
7. Close editor/Live Coding and full-build BrGameEditor. Regenerate previews:
   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" Scripts/Content/create_map_previews.py
   ```
   The script reads the developer list, explicitly tags broad flat floors if
   needed, serially launches a bounded real-game 512×512 runtime minimap capture
   for each map (RHI enabled), then imports the PNGs as Texture2D using editor
   Python (`-run=pythonscript -unattended -nullrhi -nosound -nosteam`).
   Set each preview reference to `T_<Id>`. Inspect
   `Saved/Screenshots/Maps/Previews/T_<Id>.png` and commit the imported textures.
   If recreating Town with `create_br_map.py`, regenerate previews afterwards
   to reapply explicit ground tags.
8. Run `BrGame.Maps` automation. `Validate.<Id>` loads every configured map in
   PIE, builds/waits for navigation, checks ground tags, usable spawns, clear
   navigable crate landing ground inside initial zone/playable bounds, and
   non-emergency zone bounds. `CookSettings` checks MapsToCook.
   Then run full regression and manually playtest the new map with multiple
   players; the validator cannot prove every reachable floor or passage safe.

No player-supplied path is ever a travel destination. Selection IDs replicate
inside MatchSettings and carry through GameInstance. Random draws only on the
server at Start, keeps its selection across rounds and re-rolls each round.
