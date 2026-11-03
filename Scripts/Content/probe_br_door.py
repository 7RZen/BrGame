# Read-only diagnostic: list BR_ actors whose bounds touch a doorway sweep corridor.
# Usage: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=probe_br_door.py -unattended -nullrhi
# Edit DOOR_* below to probe another doorway. Writes BRDOOR lines to the editor log.
import unreal

MAP = "/Game/BrGame/Maps/L_Town"
DOOR_X = -7700.0
DOOR_Y = 2450.0
HALF_SPAN = 150.0          # sweep runs +-150 cm along the passage axis
CLEAR = 60.0               # capsule radius (+ margin for bounds overlap)
ALONG_X = True             # passage axis


def main():
    unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    if ALONG_X:
        lo = (DOOR_X - HALF_SPAN, DOOR_Y - CLEAR)
        hi = (DOOR_X + HALF_SPAN, DOOR_Y + CLEAR)
    else:
        lo = (DOOR_X - CLEAR, DOOR_Y - HALF_SPAN)
        hi = (DOOR_X + CLEAR, DOOR_Y + HALF_SPAN)
    unreal.log("BRDOOR corridor x=[%.0f,%.0f] y=[%.0f,%.0f]" % (lo[0], hi[0], lo[1], hi[1]))
    hits = 0
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    for a in actors:
        label = a.get_actor_label()
        origin, extent = a.get_actor_bounds(False)
        if (origin.x + extent.x < lo[0] or origin.x - extent.x > hi[0] or
                origin.y + extent.y < lo[1] or origin.y - extent.y > hi[1]):
            continue
        hits += 1
        desc = []
        for c in a.get_components_by_class(unreal.PrimitiveComponent):
            try:
                desc.append("%s[profile=%s pawn=%s enabled=%s]" % (
                    c.get_name(), c.get_collision_profile_name(),
                    c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN),
                    c.get_collision_enabled()))
            except Exception as ex:
                desc.append("%s[err %s]" % (c.get_name(), ex))
        unreal.log("BRDOOR hit %s origin=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f) comps=%s" % (
            label, origin.x, origin.y, origin.z, extent.x, extent.y, extent.z, "; ".join(desc)))
    unreal.log("BRDOOR hits=%d" % hits)
def sweep_probe():
    world = unreal.EditorLevelLibrary.get_editor_world()
    for dy in (-60.0, 0.0, 60.0):
        s = unreal.Vector(DOOR_X - HALF_SPAN, DOOR_Y + dy, 100.0)
        e = unreal.Vector(DOOR_X + HALF_SPAN, DOOR_Y + dy, 100.0)
        try:
            res = unreal.SystemLibrary.capsule_trace_single_by_profile(
                world, s, e, CLEAR, 96.0, "Pawn", False, [], unreal.DrawDebugTrace.NONE, True)
            unreal.log("BRDOOR sweep dy=%.0f dict=%s" % (dy, res.to_dict()))
        except Exception as ex:
            unreal.log("BRDOOR sweep dy=%.0f error=%s" % (dy, ex))


main()
sweep_probe()
