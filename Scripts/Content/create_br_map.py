"""
Builds /Game/BrGame/Maps/L_Town: the first battle-royale match map (STEP 10).

Idempotent: every run reloads (or creates) the level, deletes only actors whose
label starts with "BR_", rebuilds them from the constants below, and saves.
Art uses template assets only (BasicShapes meshes, LevelPrototyping M_FlatCol).

Run (editor must be closed):
  UnrealEditor-Cmd.exe BrGame.uproject -run=pythonscript -script=<this file> -unattended -nullrhi -nosound -nosteam -abslog=<log>

Tuning: edit the constants and the TOWN / COMPOUND / FOREST / ROADS lists below, then rerun.
Units are centimetres. Map is 200 x 200 m, playable area +-10000 cm.
"""
import math
import random

import unreal

MAP_PATH = "/Game/BrGame/Maps/L_Town"
MAT_FOLDER = "/Game/BrGame/Town"
PARENT_MAT = "/Game/LevelPrototyping/Materials/M_FlatCol"
CUBE = "/Engine/BasicShapes/Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder"
NAV_VOLUME_CLASS = "/Script/NavigationSystem.NavMeshBoundsVolume"
ZONE_CLASS = "/Script/BrGame.BrZoneBounds"

PLAY_HALF = 10000.0      # playable half-extent (200 m map)
BOUNDARY_T = 200.0       # invisible boundary wall thickness
BOUNDARY_H = 1000.0
WALL_T = 40.0            # full wall thickness
WALL_H = 280.0           # full wall height (blocks vision, bullets, movement)
LOW_H = 110.0            # waist-high cover (blocks movement/bullets, NOT vision)
HEDGE_H = 150.0
DOOR_W = 150.0           # doorway gap width
CONTAINER_H = 250.0
TRUNK_D = 240.0
TRUNK_H = 280.0
ROAD_H = 2.0
SEED = 1010
PLAYER_START_COUNT = 48
PLAYER_START_Z = 100.0      # capsule half-height 96 + clearance above ground top (Z=0)
PLAYER_START_MARGIN = 150.0

# ---------- layout data (rects are (x0, y0, x1, y1)) ----------
ROADS = [
    (-PLAY_HALF, -400.0, PLAY_HALF, 400.0),      # main east-west road
    (-2400.0, -PLAY_HALF, -1600.0, PLAY_HALF),   # main north-south road
    (-PLAY_HALF, 3400.0, PLAY_HALF, 4000.0),     # south lane through the compound
    (5800.0, -PLAY_HALF, 6400.0, PLAY_HALF),     # east lane through the forest
]
TOWN_RECT = (-4600.0, -3000.0, 2600.0, 2600.0)
COMPOUND_RECT = (-9200.0, 1800.0, -5400.0, 8200.0)
FOREST_RECT = (3800.0, -9200.0, 9200.0, -2800.0)

# Buildings: footprint, door centres per side (N/S = x coord, W/E = y coord),
# partitions ('v', x, door_y) or ('h', y, door_x). Doorways are 150 cm, no doors.
BUILDINGS = [
    dict(name="TownHall", rect=(-4200, -2600, -2600, -1200), doors={"S": -2900, "W": -1900},
         parts=[("v", -3400, -2000)]),
    dict(name="Shop", rect=(-1400, -2600, -200, -1800), doors={"S": -800, "W": -2200}, parts=[]),
    dict(name="Inn", rect=(-1400, -1400, -200, -600), doors={"S": -500, "W": -1200},
         parts=[("h", -1000, -700)]),
    dict(name="Warehouse", rect=(300, -2800, 1800, -1400), doors={"W": -2100, "S": 1000}, parts=[]),
    dict(name="HouseA", rect=(-4200, -900, -3400, -550), doors={"E": -700}, parts=[]),
    dict(name="HouseB", rect=(-3000, -900, -2500, -550), doors={"S": -2750}, parts=[]),
    dict(name="HouseC", rect=(500, -900, 900, -550), doors={"S": 700}, parts=[]),
    # East door moved off the Y=1200 partition end (reviewer finding: 55 cm gaps only).
    dict(name="Clinic", rect=(-4200, 600, -2800, 1800), doors={"N": -3500, "E": 800},
         parts=[("h", 1200, -3300)]),
    dict(name="Barracks", rect=(-1400, 700, -600, 1500), doors={"W": 1000, "N": -1000}, parts=[]),
    dict(name="SmallHouse", rect=(-300, 600, 300, 1000), doors={"N": 0}, parts=[]),
    dict(name="MarketHall", rect=(-300, 1200, 1200, 2200), doors={"N": 400, "W": 1700}, parts=[]),
    dict(name="Garage", rect=(1400, 600, 2200, 1300), doors={"W": 900}, parts=[]),
]

# Dense close-quarters zones (Step 10 retune). Blockers are placed by seeded rejection
# sampling inside each zone, so the layout is deterministic for a given SEED.
DENSE_GAP = 130.0          # target clear distance between any two blockers in a dense zone
DENSE_MIN_GAP = 120.0      # hard floor: no passage between blockers narrower than this
ROAD_CLEAR = 120.0         # dense blockers keep this far from the lanes
DENSE_ATTEMPTS = 40000
COMPOUND_SEED = SEED + 11
FOREST_SEED = SEED + 23
COMPOUND_TARGET = 150      # containers, walls, low cover and pillars in the yard
FOREST_TARGET = 200        # trunks plus a few low-cover clumps
COMPOUND_WEIGHTS = dict(container=34, wall=18, lowcrate=18, lowwall=10, hedge=8, trunk=12)
FOREST_WEIGHTS = dict(trunk=82, hedge=8, lowcrate=6, lowwall=4)
SHOT_POINTS = [(-6200.0, 4800.0), (7000.0, -6000.0)]  # BrShotSmoke teleports: kept open
SHOT_CLEAR = 300.0
# Fixed pieces: one isolated container and one isolated trunk keep the vision probes valid
# (the probe needs 300 cm of clear line beyond each edge), and two walled sites with doorways.
ISO_CONTAINER = (-7000.0, 7300.0)
ISO_TRUNK = (4700.0, -8400.0)
SITES = [
    dict(name="CompoundPen", rect=(-8900.0, 2000.0, -7700.0, 2900.0), door=("E", 2450.0),
         crate=(-8380.0, 2570.0, -8220.0, 2730.0)),
    dict(name="ForestRuin", rect=(7900.0, -5600.0, 8700.0, -4900.0), door=("W", -5250.0),
         crate=(8220.0, -5230.0, 8380.0, -5070.0)),
]

# Walk / sight grid for the reachability and sight-line audits (25 cm cells over +-10000).
GRID_CM = 25.0
GRID_N = 800
GRID_MIN = -PLAY_HALF
CAPSULE_R = 42.0           # 84 cm capsule: blockers are inflated by this for the walk grid

# Legacy forest grid. Kept only to replay its shared-rng draws so field cover placement
# is unchanged by the forest retune (see replay_legacy_forest_draws).
TRUNK_PITCH = 1000.0
TRUNK_JITTER = 200.0
TRUNK_CLEARING_CHANCE = 0.12

FIELD_COVER_COUNT = 90

# ---------- helpers ----------
UTIL = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

solids = []            # movement/bullet blockers (full walls, low cover, containers, trunks)
vision_solids = []     # BlockAll vision blockers (walls, containers, trunks); low cover excluded
reserved = []          # rects dense pieces must not touch (shot points, doorways, probe crosses)
start_points = []      # PlayerStart xy (reachability audit)
ENTRANCES = []         # intended building doorways: x, y (door centre), axis ("X" = passage along X)
ENTRANCE_CLEAR = 120.0 # minimum clear doorway width, cm (standing capsule r=60 must pass)
footprints = []
label_counter = {"n": 0}
dense_counter = {"n": 0}
stats = {"walls": 0, "low_cover": 0, "containers": 0, "trunks": 0, "buildings": 0, "roads": 0}
rng = random.Random(SEED)
WALK = {}              # walk grid, components and sight grid, filled by build_walk_grid


def log(msg):
    unreal.log("[BR_MAP] " + msg)


def load_mesh(path):
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if mesh is None:
        raise RuntimeError("Missing template mesh: " + path)
    return mesh


def ensure_material(name, rgb):
    path = MAT_FOLDER + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.EditorAssetLibrary.load_asset(path)
    else:
        mi = TOOLS.create_asset(name, MAT_FOLDER, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        raise RuntimeError("Could not create material instance " + path)
    parent = unreal.EditorAssetLibrary.load_asset(PARENT_MAT)
    if parent is None:
        raise RuntimeError("Missing parent material " + PARENT_MAT)
    mi.set_editor_property("parent", parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        mi, "Base Color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    unreal.EditorAssetLibrary.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


MATS = {
    "ground": ensure_material("MI_Town_Ground", (0.22, 0.42, 0.20)),
    "dirt": ensure_material("MI_Town_Dirt", (0.42, 0.34, 0.24)),
    "forest": ensure_material("MI_Town_Forest", (0.07, 0.20, 0.09)),
    "road": ensure_material("MI_Town_Road", (0.13, 0.13, 0.14)),
    "floor": ensure_material("MI_Town_Floor", (0.45, 0.33, 0.20)),
    "wall": ensure_material("MI_Town_Wall", (0.78, 0.75, 0.68)),
    "boundary": ensure_material("MI_Town_Boundary", (0.30, 0.30, 0.32)),
    "rock": ensure_material("MI_Town_Rock", (0.46, 0.46, 0.49)),
    "crate": ensure_material("MI_Town_Crate", (0.64, 0.44, 0.22)),
    "lowwall": ensure_material("MI_Town_LowWall", (0.58, 0.56, 0.50)),
    "hedge": ensure_material("MI_Town_Hedge", (0.12, 0.34, 0.12)),
    "container": ensure_material("MI_Town_Container", (0.20, 0.32, 0.58)),
    "trunk": ensure_material("MI_Town_Trunk", (0.32, 0.21, 0.11)),
}

MESHES = {"cube": load_mesh(CUBE), "cyl": load_mesh(CYLINDER)}


def next_label(tag):
    # Tags starting with "D_" use the dense counter, so the global numbering of entrances,
    # starts and field cover does not shift when the dense zones change.
    if tag.startswith("D_"):
        dense_counter["n"] += 1
        return "BR_%s_D%03d" % (tag[2:], dense_counter["n"])
    label_counter["n"] += 1
    return "BR_%s_%03d" % (tag, label_counter["n"])


def spawn_static(tag, mesh_key, loc, scale, mat, profile):
    actor = UTIL.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(), False)
    if actor is None:
        raise RuntimeError("Spawn failed: " + tag)
    actor.set_actor_label(next_label(tag))
    actor.set_actor_scale3d(scale)
    smc = actor.get_editor_property("static_mesh_component")
    smc.set_editor_property("static_mesh", MESHES[mesh_key])
    smc.set_material(0, mat)
    smc.set_collision_profile_name(profile)
    return actor


def spawn_box(tag, rect, z0, z1, mat, profile="BlockAll"):
    x0, y0, x1, y1 = rect
    w, d, h = x1 - x0, y1 - y0, z1 - z0
    return spawn_static(tag, "cube",
                        unreal.Vector((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0),
                        unreal.Vector(w / 100.0, d / 100.0, h / 100.0), mat, profile)


def rect_gap(a, b):
    dx = max(a[0] - b[2], b[0] - a[2], 0.0)
    dy = max(a[1] - b[3], b[1] - a[3], 0.0)
    return math.hypot(dx, dy)


def rect_overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def point_rect(x, y, r=0.0):
    return (x - r, y - r, x + r, y + r)


def in_zone(rect, zone):
    return rect_overlap(rect, zone)


def on_road(rect):
    return any(rect_overlap(rect, r) for r in ROADS)


def wall_pieces(lo, hi, gaps):
    pieces, cur = [], lo
    for g0, g1 in sorted(gaps):
        if g0 > cur:
            pieces.append((cur, g0))
        cur = max(cur, g1)
    if cur < hi:
        pieces.append((cur, hi))
    return pieces


def wall_h(tag, y, x0, x1, gaps=()):
    for a, b in wall_pieces(x0, x1, gaps):
        rect = (a, y - WALL_T / 2, b, y + WALL_T / 2)
        spawn_box("Wall_" + tag, rect, 0.0, WALL_H, MATS["wall"], "BlockAll")
        solids.append(rect)
        vision_solids.append(rect)
        stats["walls"] += 1


def wall_v(tag, x, y0, y1, gaps=()):
    for a, b in wall_pieces(y0, y1, gaps):
        rect = (x - WALL_T / 2, a, x + WALL_T / 2, b)
        spawn_box("Wall_" + tag, rect, 0.0, WALL_H, MATS["wall"], "BlockAll")
        solids.append(rect)
        vision_solids.append(rect)
        stats["walls"] += 1


def door_gap(c):
    return (c - DOOR_W / 2, c + DOOR_W / 2)


def building(b):
    x0, y0, x1, y1 = b["rect"]
    tag = b["name"]
    for other in footprints:
        if rect_overlap(b["rect"], other):
            raise RuntimeError("Building %s overlaps another footprint" % tag)
    if on_road(b["rect"]):
        raise RuntimeError("Building %s overlaps a road" % tag)
    d = b["doors"]
    # Register each intended entrance (centre on the wall line; passage axis).
    if "N" in d:
        ENTRANCES.append(dict(name=tag + "_N", x=d["N"], y=y0, axis="Y"))
    if "S" in d:
        ENTRANCES.append(dict(name=tag + "_S", x=d["S"], y=y1, axis="Y"))
    if "W" in d:
        ENTRANCES.append(dict(name=tag + "_W", x=x0, y=d["W"], axis="X"))
    if "E" in d:
        ENTRANCES.append(dict(name=tag + "_E", x=x1, y=d["E"], axis="X"))
    wall_h(tag + "_N", y0, x0, x1, [door_gap(d["N"])] if "N" in d else [])
    wall_h(tag + "_S", y1, x0, x1, [door_gap(d["S"])] if "S" in d else [])
    wall_v(tag + "_W", x0, y0, y1, [door_gap(d["W"])] if "W" in d else [])
    wall_v(tag + "_E", x1, y0, y1, [door_gap(d["E"])] if "E" in d else [])
    for p in b["parts"]:
        if p[0] == "v":
            wall_v(tag + "_P", p[1], y0, y1, [door_gap(p[2])])
        else:
            wall_h(tag + "_P", p[1], x0, x1, [door_gap(p[2])])
    spawn_box(tag + "_Floor", b["rect"], 0.0, ROAD_H, MATS["floor"], "NoCollision")
    footprints.append(b["rect"])
    stats["buildings"] += 1


def free_for_cover(rect, margin):
    if abs(rect[0]) > PLAY_HALF - 500 or abs(rect[2]) > PLAY_HALF - 500:
        return False
    if abs(rect[1]) > PLAY_HALF - 500 or abs(rect[3]) > PLAY_HALF - 500:
        return False
    for s in solids:
        if rect_gap(rect, s) < margin:
            return False
    for r in ROADS:
        if rect_gap(rect, r) < margin:
            return False
    for z in (TOWN_RECT, COMPOUND_RECT, FOREST_RECT):
        if rect_gap(rect, z) < 300.0 or rect_overlap(rect, z):
            return False
    return True


def build_boundary():
    edge = PLAY_HALF + BOUNDARY_T / 2
    span = PLAY_HALF * 2 + BOUNDARY_T * 2
    for tag, rect in (
        ("Boundary_N", (-span / 2, -edge - BOUNDARY_T / 2, span / 2, -edge + BOUNDARY_T / 2)),
        ("Boundary_S", (-span / 2, edge - BOUNDARY_T / 2, span / 2, edge + BOUNDARY_T / 2)),
        ("Boundary_W", (-edge - BOUNDARY_T / 2, -span / 2, -edge + BOUNDARY_T / 2, span / 2)),
        ("Boundary_E", (edge - BOUNDARY_T / 2, -span / 2, edge + BOUNDARY_T / 2, span / 2)),
    ):
        actor = spawn_box(tag, rect, 0.0, BOUNDARY_H, MATS["boundary"], "BlockAll")
        actor.set_actor_hidden_in_game(True)


def build_ground_and_roads():
    spawn_box("Ground", (-PLAY_HALF, -PLAY_HALF, PLAY_HALF, PLAY_HALF), -20.0, 0.0,
              MATS["ground"], "BlockAll")
    spawn_box("GroundCompound", COMPOUND_RECT, 0.0, ROAD_H, MATS["dirt"], "NoCollision")
    spawn_box("GroundForest", FOREST_RECT, 0.0, ROAD_H, MATS["forest"], "NoCollision")
    for i, r in enumerate(ROADS):
        spawn_box("Road%d" % i, r, 0.0, ROAD_H, MATS["road"], "NoCollision")
        stats["roads"] += 1


def oriented(cx, cy, length, thick, r):
    if r.random() < 0.5:
        return (cx - length / 2.0, cy - thick / 2.0, cx + length / 2.0, cy + thick / 2.0)
    return (cx - thick / 2.0, cy - length / 2.0, cx + thick / 2.0, cy + length / 2.0)


def make_piece(kind, zone, r):
    # Candidate footprint centred uniformly in the zone; fit is checked by dense_fits.
    cx = r.uniform(zone[0], zone[2])
    cy = r.uniform(zone[1], zone[3])
    if kind == "container":
        return oriented(cx, cy, 600.0, 240.0, r)
    if kind == "wall":
        return oriented(cx, cy, r.choice([400.0, 600.0, 800.0]), WALL_T, r)
    if kind == "lowcrate":
        return point_rect(cx, cy, r.choice([120.0, 160.0]) / 2.0)
    if kind == "lowwall":
        return oriented(cx, cy, r.choice([300.0, 500.0]), 40.0, r)
    if kind == "hedge":
        return oriented(cx, cy, r.choice([400.0, 700.0]), 150.0, r)
    if kind == "trunk":
        return point_rect(cx, cy, TRUNK_D / 2.0)
    raise RuntimeError("Unknown dense kind " + kind)


def spawn_piece(kind, rect):
    cx, cy = (rect[0] + rect[2]) / 2.0, (rect[1] + rect[3]) / 2.0
    if kind == "container":
        spawn_box("D_Container", rect, 0.0, CONTAINER_H, MATS["container"], "BlockAll")
        solids.append(rect)
        vision_solids.append(rect)
        stats["containers"] += 1
    elif kind == "wall":
        spawn_box("D_Wall", rect, 0.0, WALL_H, MATS["wall"], "BlockAll")
        solids.append(rect)
        vision_solids.append(rect)
        stats["walls"] += 1
    elif kind == "trunk":
        spawn_static("D_Trunk", "cyl", unreal.Vector(cx, cy, TRUNK_H / 2),
                     unreal.Vector(TRUNK_D / 100.0, TRUNK_D / 100.0, TRUNK_H / 100.0),
                     MATS["trunk"], "BlockAll")
        solids.append(rect)
        vision_solids.append(rect)
        stats["trunks"] += 1
    elif kind == "lowcrate":
        spawn_box("D_Crate", rect, 0.0, LOW_H, MATS["crate"], "BrLowCover")
        solids.append(rect)  # blocks movement and bullets, not vision
        stats["low_cover"] += 1
    elif kind == "lowwall":
        spawn_box("D_LowWall", rect, 0.0, LOW_H, MATS["lowwall"], "BrLowCover")
        solids.append(rect)
        stats["low_cover"] += 1
    elif kind == "hedge":
        spawn_box("D_Hedge", rect, 0.0, HEDGE_H, MATS["hedge"], "BrLowCover")
        solids.append(rect)
        stats["low_cover"] += 1
    else:
        raise RuntimeError("Unknown dense kind " + kind)


def dense_fits(rect, zone):
    if not (rect[0] >= zone[0] and rect[1] >= zone[1] and rect[2] <= zone[2] and rect[3] <= zone[3]):
        return False
    for r in reserved:
        if rect_overlap(rect, r):
            return False
    for r in ROADS:
        if rect_gap(rect, r) < ROAD_CLEAR:
            return False
    for s in solids:
        if rect_gap(rect, s) < DENSE_GAP:
            return False
    return True


def place_dense(zone, weights, target, seed, name):
    # Seeded rejection sampling: a separate Random per zone keeps placement deterministic
    # and independent of field cover draws.
    r = random.Random(seed)
    kinds = list(weights.keys())
    wts = [weights[k] for k in kinds]
    placed, attempts = 0, 0
    by_kind = dict((k, 0) for k in kinds)
    while placed < target and attempts < DENSE_ATTEMPTS:
        attempts += 1
        kind = r.choices(kinds, weights=wts)[0]
        rect = make_piece(kind, zone, r)
        if not dense_fits(rect, zone):
            continue
        spawn_piece(kind, rect)
        by_kind[kind] += 1
        placed += 1
    if placed < target:
        log("WARNING %s: placed %d of %d target after %d attempts" % (name, placed, target, attempts))
    log("dense %s: placed=%d attempts=%d %s" % (name, placed, attempts,
                                               " ".join("%s=%d" % kv for kv in sorted(by_kind.items()))))
    return placed


def reserve_plus(cx, cy, reach=550.0, half=60.0):
    # Keeps the isolated vision probe lines (BrMapTest FindIsolatedProbe) clear of other blockers.
    reserved.append((cx - half, cy - reach, cx + half, cy + reach))
    reserved.append((cx - reach, cy - half, cx + reach, cy + half))


def place_isolated():
    cx, cy = ISO_CONTAINER
    reserve_plus(cx, cy)
    spawn_piece("container", (cx - 300.0, cy - 120.0, cx + 300.0, cy + 120.0))
    tx, ty = ISO_TRUNK
    reserve_plus(tx, ty)
    spawn_piece("trunk", point_rect(tx, ty, TRUNK_D / 2.0))


def build_site(site):
    # Walled pen with one doorway (150 cm, registered as a BR_Entrance) and a low crate inside.
    x0, y0, x1, y1 = site["rect"]
    side, pos = site["door"]
    tag = "Site_" + site["name"]
    wall_h(tag + "_N", y0, x0, x1)
    wall_h(tag + "_S", y1, x0, x1)
    wall_v(tag + "_W", x0, y0, y1, [door_gap(pos)] if side == "W" else [])
    wall_v(tag + "_E", x1, y0, y1, [door_gap(pos)] if side == "E" else [])
    if side == "E":
        e = dict(name=site["name"] + "_E", x=x1, y=pos, axis="X")
    else:
        e = dict(name=site["name"] + "_W", x=x0, y=pos, axis="X")
    ENTRANCES.append(e)
    reserved.append(site["rect"])
    reserved.append(entrance_probe(e))
    spawn_piece("lowcrate", site["crate"])


def build_compound():
    for p in SHOT_POINTS:
        reserved.append(point_rect(p[0], p[1], SHOT_CLEAR))
    place_isolated()
    build_site(SITES[0])
    place_dense(COMPOUND_RECT, COMPOUND_WEIGHTS, COMPOUND_TARGET, COMPOUND_SEED, "compound")


def build_forest():
    build_site(SITES[1])
    place_dense(FOREST_RECT, FOREST_WEIGHTS, FOREST_TARGET, FOREST_SEED, "forest")


def replay_legacy_forest_draws():
    # Field cover draws from the shared rng, and the old forest grid consumed these draws first.
    # Replay the same draws so field cover placement is unchanged by the forest retune.
    rng.seed(SEED)
    x0, y0, x1, y1 = FOREST_RECT
    gx = x0 + TRUNK_PITCH / 2
    while gx < x1:
        gy = y0 + TRUNK_PITCH / 2
        while gy < y1:
            if rng.random() >= TRUNK_CLEARING_CHANCE:
                rng.uniform(-TRUNK_JITTER, TRUNK_JITTER)
                rng.uniform(-TRUNK_JITTER, TRUNK_JITTER)
            gy += TRUNK_PITCH
        gx += TRUNK_PITCH


# ---------- walk grid, sight grid and audits ----------
def rasterize(rects, inflate, grid):
    N = GRID_N
    for (x0, y0, x1, y1) in rects:
        ilo = max(0, int(math.ceil((x0 - inflate - GRID_MIN) / GRID_CM - 0.5)))
        ihi = min(N - 1, int(math.floor((x1 + inflate - GRID_MIN) / GRID_CM - 0.5)))
        jlo = max(0, int(math.ceil((y0 - inflate - GRID_MIN) / GRID_CM - 0.5)))
        jhi = min(N - 1, int(math.floor((y1 + inflate - GRID_MIN) / GRID_CM - 0.5)))
        if ilo > ihi or jlo > jhi:
            continue
        seg = b"\x01" * (ihi - ilo + 1)
        for j in range(jlo, jhi + 1):
            base = j * N
            grid[base + ilo: base + ihi + 1] = seg


def cell_index(x, y):
    i = int(math.floor((x - GRID_MIN) / GRID_CM))
    j = int(math.floor((y - GRID_MIN) / GRID_CM))
    if i < 0 or j < 0 or i >= GRID_N or j >= GRID_N:
        return None
    return j * GRID_N + i


def reachable(x, y):
    c = cell_index(x, y)
    if c is None:
        return False
    return WALK["grid"][c] == 0 and WALK["seed"][c] == 1


def flood(start, grid, seen):
    N = GRID_N
    stack = [start]
    seen[start] = 1
    n = 0
    while stack:
        c = stack.pop()
        n += 1
        i = c % N
        if i > 0 and not grid[c - 1] and not seen[c - 1]:
            seen[c - 1] = 1
            stack.append(c - 1)
        if i < N - 1 and not grid[c + 1] and not seen[c + 1]:
            seen[c + 1] = 1
            stack.append(c + 1)
        if c >= N and not grid[c - N] and not seen[c - N]:
            seen[c - N] = 1
            stack.append(c - N)
        if c < N * (N - 1) and not grid[c + N] and not seen[c + N]:
            seen[c + N] = 1
            stack.append(c + N)
    return n


def build_walk_grid():
    # Walk grid: solids inflated by the capsule radius (conservative square inflation).
    # A 120 cm passage leaves a 36 cm corridor, which always holds a 25 cm cell centre.
    N = GRID_N
    grid = bytearray(N * N)
    rasterize(solids, CAPSULE_R, grid)
    vgrid = bytearray(N * N)
    rasterize(vision_solids, 0.0, vgrid)
    seen = bytearray(N * N)
    seed_cell = 400 * N + 400  # (12.5, 12.5): on the main east-west road
    if grid[seed_cell]:
        raise RuntimeError("Walk grid seed cell is blocked")
    seed_size = flood(seed_cell, grid, seen)
    WALK["grid"] = grid
    WALK["vgrid"] = vgrid
    WALK["seed"] = bytes(seen)  # copy: only the seed component (pocket floods below do not leak in)
    pockets = []
    for c in range(N * N):
        if not grid[c] and not seen[c]:
            n = flood(c, grid, seen)
            if n >= 8:  # >= 5000 cm2 of walkable floor that the seed cannot reach
                pockets.append((c, n))
    if pockets:
        desc = ["cell(%.0f,%.0f) %d cells" % (GRID_MIN + (c % N + 0.5) * GRID_CM,
                                              GRID_MIN + (c // N + 0.5) * GRID_CM, n) for c, n in pockets[:10]]
        raise RuntimeError("Sealed walkable pockets: " + "; ".join(desc))
    log("walk grid: seed component=%d cells (%.0f m2)" % (seed_size, seed_size * GRID_CM * GRID_CM / 1e4))


def audit_passages():
    # Every gap between a dense/site blocker and any other blocker must be 0 (touching) or >= 120.
    zones = (COMPOUND_RECT, FOREST_RECT)
    zs = [s for s in solids if any(rect_overlap(s, z) for z in zones)]
    bad = []
    for a in zs:
        for b in solids:
            if b is a:
                continue
            g = rect_gap(a, b)
            if 0.0 < g < DENSE_MIN_GAP:
                bad.append("gap %.0f cm at (%.0f,%.0f)" % (g, a[0], a[1]))
                break
    if bad:
        raise RuntimeError("Passage audit failed (<%.0f cm): %s" % (DENSE_MIN_GAP, "; ".join(bad[:10])))
    log("passage audit passed: %d zone blockers checked, all gaps >=%.0f cm or touching"
        % (len(zs), DENSE_MIN_GAP))


def audit_reachability():
    bad = []
    for (x, y) in start_points:
        if not reachable(x, y):
            bad.append("start (%.0f,%.0f)" % (x, y))
    for e in ENTRANCES:
        if e["axis"] == "Y":
            sides = [(e["x"], e["y"] - 100.0), (e["x"], e["y"] + 100.0)]
        else:
            sides = [(e["x"] - 100.0, e["y"]), (e["x"] + 100.0, e["y"])]
        for sx, sy in sides:
            if not reachable(sx, sy):
                bad.append("%s side (%.0f,%.0f)" % (e["name"], sx, sy))
    if bad:
        raise RuntimeError("Reachability audit failed: " + "; ".join(bad[:20]))
    log("reachability audit passed: starts=%d entrances=%d" % (len(start_points), len(ENTRANCES)))


def report_sightlines():
    # Report only: mean/median/p90 of 8-ray sight distance from reachable floor points (cap 3000 cm),
    # and blocker footprint density per zone.
    vg = WALK["vgrid"]
    for name, zone in (("compound", COMPOUND_RECT), ("forest", FOREST_RECT)):
        hits = []
        x = zone[0] + 300.0
        while x <= zone[2] - 300.0:
            y = zone[1] + 300.0
            while y <= zone[3] - 300.0:
                if reachable(x, y):
                    for k in range(8):
                        ang = k * math.pi / 4.0
                        ca, sa = math.cos(ang), math.sin(ang)
                        d = 3000.0
                        s = 25.0
                        while s <= 3000.0:
                            c = cell_index(x + ca * s, y + sa * s)
                            if c is None or vg[c]:
                                d = s
                                break
                            s += 25.0
                        hits.append(d)
                y += 600.0
            x += 600.0
        hits.sort()
        area = (zone[2] - zone[0]) * (zone[3] - zone[1])
        blk = sum((s[2] - s[0]) * (s[3] - s[1]) for s in solids
                  if rect_overlap(s, zone) and s[0] >= zone[0] and s[1] >= zone[1]
                  and s[2] <= zone[2] and s[3] <= zone[3])
        if hits:
            mean = sum(hits) / len(hits)
            med = hits[len(hits) // 2]
            p90 = hits[int(len(hits) * 0.9)]
        else:
            mean = med = p90 = 0.0
        log("sightlines %s: samples=%d mean=%.0f median=%.0f p90=%.0f cm (cap 3000); blocker footprint=%.0f%%"
            % (name, len(hits) // 8, mean, med, p90, 100.0 * blk / area))


def build_field_cover():
    placed, attempts = 0, 0
    while placed < FIELD_COVER_COUNT and attempts < 6000:
        attempts += 1
        kind = rng.choices(["rock", "crate", "lowwall", "hedge"], weights=[3, 3, 2, 2])[0]
        x = rng.uniform(-PLAY_HALF + 600, PLAY_HALF - 600)
        y = rng.uniform(-PLAY_HALF + 600, PLAY_HALF - 600)
        if kind == "rock":
            d = rng.choice([140.0, 180.0, 220.0])
            rect = point_rect(x, y, d / 2)
            if not free_for_cover(rect, 150.0):
                continue
            spawn_static("Rock", "cyl", unreal.Vector(x, y, LOW_H / 2),
                         unreal.Vector(d / 100.0, d / 100.0, LOW_H / 100.0), MATS["rock"], "BrLowCover")
        elif kind == "crate":
            s = rng.choice([120.0, 160.0])
            rect = point_rect(x, y, s / 2)
            if not free_for_cover(rect, 150.0):
                continue
            spawn_box("Crate", rect, 0.0, LOW_H, MATS["crate"], "BrLowCover")
        elif kind == "lowwall":
            long_, thin = rng.choice([300.0, 500.0]), 40.0
            w, d = (long_, thin) if rng.random() < 0.5 else (thin, long_)
            rect = (x - w / 2, y - d / 2, x + w / 2, y + d / 2)
            if not free_for_cover(rect, 150.0):
                continue
            spawn_box("LowWall", rect, 0.0, LOW_H, MATS["lowwall"], "BrLowCover")
        else:  # hedge: long, thick, waist-to-chest high; blocks movement, not vision
            long_ = rng.choice([400.0, 700.0])
            w, d = (long_, 150.0) if rng.random() < 0.5 else (150.0, long_)
            rect = (x - w / 2, y - d / 2, x + w / 2, y + d / 2)
            if not free_for_cover(rect, 150.0):
                continue
            spawn_box("Hedge", rect, 0.0, HEDGE_H, MATS["hedge"], "BrLowCover")
        solids.append(rect)
        stats["low_cover"] += 1
        placed += 1
    log("field cover placed=%d attempts=%d" % (placed, attempts))


def entrance_probe(e):
    # Corridor a standing capsule (r=60 for the 120 cm clear width) sweeps through, matching
    # BrMapTest.cpp: centre travels +-150 cm either side of the marker, and the capsule's own
    # radius (60 cm) extends that to +-210 cm along the passage axis; +-60 across the doorway.
    half_across = ENTRANCE_CLEAR / 2.0
    reach = 150.0 + half_across
    if e["axis"] == "Y":
        return (e["x"] - half_across, e["y"] - reach, e["x"] + half_across, e["y"] + reach)
    return (e["x"] - reach, e["y"] - half_across, e["x"] + reach, e["y"] + half_across)


def audit_entrances():
    expected = sum(len(b["doors"]) for b in BUILDINGS) + len(SITES)
    if len(ENTRANCES) != expected:
        raise RuntimeError("Expected %d entrances from BUILDINGS, registered %d" % (expected, len(ENTRANCES)))
    bad = []
    for e in ENTRANCES:
        p = entrance_probe(e)
        hits = [s for s in solids if rect_overlap(p, s)]
        if hits:
            bad.append("%s blocked by %d solid(s)" % (e["name"], len(hits)))
    if bad:
        raise RuntimeError("Entrance audit failed: " + "; ".join(bad))
    log("entrance audit passed: %d entrances, >=%.0f cm clear" % (len(ENTRANCES), ENTRANCE_CLEAR))


def build_entrance_markers():
    # TargetPoint markers at each intended doorway; read by BrGame.Map.TownLayout (C++ test).
    for e in ENTRANCES:
        tp = UTIL.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(e["x"], e["y"], PLAYER_START_Z),
                                         unreal.Rotator(), False)
        tp.set_actor_label(next_label("Entrance"))
        tp.set_editor_property("tags", [unreal.Name("BrEntrance"),
                                        unreal.Name("BrEntranceAxis:" + e["axis"]),
                                        unreal.Name("BrEntranceName:" + e["name"])])
    log("entrance markers=%d" % len(ENTRANCES))


def clear_of_solids(x, y, margin):
    p = point_rect(x, y, 0.0)
    return all(rect_gap(p, s) >= margin for s in solids)


def build_player_starts():
    cands = []
    v = -9000.0
    while v <= 9000.0:
        u = -9000.0
        while u <= 9000.0:
            if (not in_zone(point_rect(u, v, 300.0), COMPOUND_RECT)
                    and not in_zone(point_rect(u, v, 300.0), FOREST_RECT)
                    and clear_of_solids(u, v, PLAYER_START_MARGIN)
                    and not on_road(point_rect(u, v, 0.0))):
                cands.append((u, v))
            u += 1000.0
        v += 1000.0
    if len(cands) < PLAYER_START_COUNT:
        raise RuntimeError("Not enough player start candidates: %d" % len(cands))
    # farthest-point sampling for an even spread (deterministic)
    chosen = [max(cands, key=lambda c: math.hypot(c[0], c[1]))]
    while len(chosen) < PLAYER_START_COUNT:
        best = max(cands, key=lambda c: min(math.hypot(c[0] - k[0], c[1] - k[1]) for k in chosen))
        chosen.append(best)
    start_points.extend(chosen)
    for i, (x, y) in enumerate(chosen):
        # Respawn candidates use Start + 4 cm and a 96 cm half-height capsule (BrCharacter).
        # Z must clear the ground top (Z=0) or every start fails the capsule-clear test.
        ps = UTIL.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x, y, PLAYER_START_Z),
                                         unreal.Rotator(), False)
        ps.set_actor_label(next_label("PlayerStart"))
    log("player starts=%d (candidates=%d)" % (len(chosen), len(cands)))


def build_zone_bounds():
    cls = unreal.load_class(None, ZONE_CLASS)
    if cls is None:
        raise RuntimeError("Missing class " + ZONE_CLASS)
    zone = UTIL.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(), False)
    zone.set_actor_label("BR_ZoneBounds")
    box = zone.get_editor_property("bounds")
    box.set_box_extent(unreal.Vector(PLAY_HALF, PLAY_HALF, 500.0))
    log("zone bounds extent=%.0f" % PLAY_HALF)


def build_nav_volume():
    cls = unreal.load_class(None, NAV_VOLUME_CLASS)
    if cls is None:
        raise RuntimeError("Missing class " + NAV_VOLUME_CLASS)
    nav = UTIL.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(), False)
    nav.set_actor_label("BR_NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
    origin, extent = nav.get_actor_bounds(False)
    target = PLAY_HALF + 200.0
    nav.set_actor_scale3d(unreal.Vector(target / extent.x, target / extent.y, 1000.0 / extent.z))
    nav.set_actor_location(unreal.Vector(0.0, 0.0, 0.0), False, False)
    log("nav bounds volume scaled to +-%.0f" % target)


def build_lights():
    sun = UTIL.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 3000.0),
                                      unreal.Rotator(pitch=-55.0, yaw=35.0, roll=0.0), False)
    sun.set_actor_label("BR_Sun")
    comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    if comp is not None:
        comp.set_editor_property("intensity", 8.0)
    sky = UTIL.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0),
                                      unreal.Rotator(), False)
    sky.set_actor_label("BR_SkyLight")


def purge_br_actors():
    doomed = [a for a in UTIL.get_all_level_actors()
              if a.get_actor_label().startswith("BR_")]
    for a in doomed:
        UTIL.destroy_actor(a)
    log("purged %d previous BR_ actors" % len(doomed))


def verify_profiles():
    probe = None
    for a in UTIL.get_all_level_actors():
        if a.get_actor_label().startswith("BR_Rock"):
            probe = a
            break
    if probe is None:
        raise RuntimeError("No BR_Rock actor to verify BrLowCover profile")
    name = str(probe.get_editor_property("static_mesh_component").get_collision_profile_name())
    if name != "BrLowCover":
        raise RuntimeError("Low cover profile is %s, expected BrLowCover (check DefaultEngine.ini)" % name)


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        if not LEVELS.load_level(MAP_PATH):
            raise RuntimeError("Could not load existing level " + MAP_PATH)
        log("loaded " + MAP_PATH)
    else:
        if not LEVELS.new_level(MAP_PATH):
            raise RuntimeError("Could not create level " + MAP_PATH)
        log("created " + MAP_PATH)

    world = EDITOR.get_editor_world()
    settings = world.get_world_settings()
    if settings.get_editor_property("default_game_mode") is not None:
        settings.set_editor_property("default_game_mode", None)

    purge_br_actors()
    label_counter["n"] = 0
    dense_counter["n"] = 0
    for lst in (solids, vision_solids, reserved, start_points, ENTRANCES, footprints):
        lst.clear()
    WALK.clear()
    stats.update({"walls": 0, "low_cover": 0, "containers": 0, "trunks": 0, "buildings": 0, "roads": 0})

    build_ground_and_roads()
    build_boundary()
    for b in BUILDINGS:
        building(b)
    build_compound()
    build_forest()
    replay_legacy_forest_draws()
    build_field_cover()
    audit_entrances()
    audit_passages()
    build_walk_grid()
    build_player_starts()
    audit_reachability()
    report_sightlines()
    build_entrance_markers()
    build_zone_bounds()
    build_nav_volume()
    build_lights()
    verify_profiles()

    if not LEVELS.save_current_level():
        # A level created with new_level has no file yet; save it to its package path.
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
            raise RuntimeError("Save failed for " + MAP_PATH)
    log("stats " + ", ".join("%s=%d" % kv for kv in sorted(stats.items())))
    log("done: " + MAP_PATH)


main()
