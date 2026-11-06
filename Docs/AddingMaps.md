# How to add a new map

1. Author a world with the native `BrGameMode` (no template GameMode override).
   Keep collision, orthographic visibility and the `BrVision` responses consistent
   with Town. Do not edit map binaries outside Unreal Editor/Python.
2. Provide capsule-clear PlayerStarts (radius 42 cm, half-height 96 cm).
   Navigation can also supply starts; the safe-zone spawn path has a bounded
   downward-ground-trace spiral fallback. Check actual playable collision, not
   just a visible floor.
3. Provide clear walkable ground inside the zone/playable bounds for airdrop
   crates. Crates/scatter prefer navigation but do not require it: the first
   WorldStatic-channel blocking surface must be tagged `BrFogGround` on its actor
   or component, non-simulating and at rest. Stationary geometry authored
   Movable/BlockAllDynamic (Test Arena's floor) is supported without map edits.
   Tagged small caps/cover are excluded by component footprint: both horizontal
   axes and the scaled local surface axes must span twice (scatter max + 85 cm),
   **11.7 m** at defaults. Broad floors/slopes remain eligible; no height reference,
   new per-map tags or tuning override is needed. No pickup spawn points are used.
   Allow an 85 × 96 cm
   clearance capsule 100 cm above crate floors (42 × 96 cm for scattered items),
   plus room for 2.5–5 m scatter. Bounded searches skip/log impossible drops.
4. Prefer `BrZoneBounds` for explicit playable bounds. The safe zone is spawned
   by GameMode, not placed. Its fallback bounds are navmesh volume bounds, then
   PlayerStarts/colliding static geometry; emergency 100 m bounds are not valid
   map authoring. Tag ground actors/components `BrFogGround` (including slopes).
5. In Project Settings → BrGame → Maps, add a unique stable `Id` (not None,
   Random or a path), `DisplayName`, `Map`, `Description`, `Preview`, and player
   recommendations. Recommendations are hints only. Set `DefaultMapId` if wanted.
   Choose `InitialCrates` / `CratesPerDrop` by map size: small **2 / 1**, medium
   **4 / 2** (new-entry defaults), large **6 / 3**. Initial 0 disables only the
   initial wave; periodic waves require at least 1. Both counts are capped at
   100 to bound server work. There is no global count override. Each wave keeps
   crate landing centers at least `CrateWaveSpacing` apart (20 m by default,
   never less than twice the scatter max radius), so small/final zones may skip crates after bounded searches.
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
   If recreating Town with `create_br_map.py`, ground tags and a synchronous
   navigation-only build are now included before saving; regenerate previews
   afterwards to reflect the new layout.
8. Run `BrGame.Maps` automation. `Validate.<Id>` loads every configured map in
   PIE, builds/waits for navigation, checks ground tags, usable spawns, clear
   navigable crate landing ground inside initial zone/playable bounds, and
   non-emergency zone bounds. `CookSettings` checks MapsToCook.
   Then run full regression and manually playtest the new map with multiple
   players; the validator cannot prove every reachable floor or passage safe.
   Build navigation and **save completed tiles** in the editor: UE 5.8 standalone
   can load an empty Recast actor without scheduling its first rebuild, even
   with Dynamic generation and ForceRebuildOnLoad. Do not rely on PIE's build.
   `Scripts/Content/rebuild_crate_map_navigation.py` is the nav-only repair for
   Town/Battlefield (headless editor Python, no gameplay actor changes). Adapt
   its explicit map list for a new map. `BrGame.Editor.BuildNavigation` is an
   editor-world-only bridge used by these scripts, not a gameplay rebuild.
   Close the editor and run `.\Scripts\RunCratePlacementCheck.ps1` and again
   with `-Windowed` to check Town/Battlefield/Test Arena standalone waves and
   nav readiness. This also requires complete loot per landing, fails skipped-item
   warnings, and checks Test Arena lands on its Z=0 floor rather than its raised cap.

No player-supplied path is ever a travel destination. Selection IDs replicate
inside MatchSettings and carry through GameInstance. Random draws only on the
server at Start, keeps its selection across rounds and re-rolls each round.
