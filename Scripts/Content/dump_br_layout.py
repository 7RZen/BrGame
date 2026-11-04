"""Read-only layout fingerprint for L_Town (no save).

Prints checksums of the parts of the layout that the Step 10 dense-zone retune
must keep stable: PlayerStarts, field cover (outside the dense zones), and the
entrance markers. Run with the same UnrealEditor-Cmd pythonscript command as the
generator; grep the log for 'BRDUMP'.
"""
import hashlib

import unreal

MAP_PATH = "/Game/BrGame/Maps/L_Town"
FIELD_PREFIXES = ("BR_Rock", "BR_LowWall", "BR_Hedge", "BR_Crate_")


def r(v):
    return round(v, 1)


def sig(items):
    h = hashlib.sha1()
    for it in sorted(items):
        h.update(repr(it).encode("utf-8"))
    return h.hexdigest()[:16]


def main():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    util = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not levels.load_level(MAP_PATH):
        raise RuntimeError("Could not load " + MAP_PATH)
    starts, field, entrances, counts = [], [], [], {}
    for a in util.get_all_level_actors():
        label = a.get_actor_label()
        if not label.startswith("BR_"):
            continue
        loc = a.get_actor_location()
        scale = a.get_actor_scale3d()
        if label.startswith("BR_PlayerStart"):
            starts.append((r(loc.x), r(loc.y), r(loc.z)))
        elif label.startswith("BR_Entrance"):
            entrances.append((r(loc.x), r(loc.y)))
        elif label.startswith(FIELD_PREFIXES) and "_D" not in label:
            field.append((label.split("_")[1], r(loc.x), r(loc.y), r(loc.z),
                          r(scale.x), r(scale.y), r(scale.z)))
        key = label.split("_")[1] if "_" in label else label
        counts[key] = counts.get(key, 0) + 1
    unreal.log("BRDUMP starts=%d sig=%s" % (len(starts), sig(starts)))
    unreal.log("BRDUMP field=%d sig=%s" % (len(field), sig(field)))
    unreal.log("BRDUMP entrances=%d sig=%s" % (len(entrances), sig(entrances)))
    unreal.log("BRDUMP counts " + ", ".join("%s=%d" % kv for kv in sorted(counts.items())))


main()
