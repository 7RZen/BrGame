"""Rebuild/save only navigation in Town/Battlefield; preserve authored actors.

Run serially with UnrealEditor-Cmd -run=pythonscript. The editor-only bridge calls
UNavigationSystemV1::Build, which waits for completion before saving.
"""
import hashlib
import unreal


levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def fingerprint():
    rows = []
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.NavigationData):
            continue
        location, rotation, scale = (actor.get_actor_location(), actor.get_actor_rotation(),
                                     actor.get_actor_scale3d())
        rows.append((actor.get_path_name(), actor.get_class().get_path_name(),
                     actor.get_actor_label(),
                     (location.x, location.y, location.z, rotation.pitch, rotation.yaw,
                      rotation.roll, scale.x, scale.y, scale.z),
                     tuple(str(tag) for tag in actor.tags)))
    return len(rows), hashlib.sha256(repr(sorted(rows)).encode("utf-8")).hexdigest()


for path in ("/Game/BrGame/Maps/L_Town", "/Game/BrGame/Maps/L_BattleField"):
    if not levels.load_level(path):
        raise RuntimeError("Cannot load " + path)
    world = editor.get_editor_world()
    before = fingerprint()
    unreal.SystemLibrary.execute_console_command(world, "BrGame.Editor.BuildNavigation")
    if unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world):
        raise RuntimeError("Navigation still busy/locked; refusing save: " + path)
    # Project actual player-start anchors, not GetRandomPoint's misleading bool.
    starts = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
    usable = sum(unreal.NavigationSystemV1.project_point_to_navigation(
        world, a.get_actor_location(), None, None, unreal.Vector(150, 150, 500)) is not None
        for a in starts)
    if not usable:
        raise RuntimeError("No projected spawn anchors; refusing save: " + path)
    after = fingerprint()
    if before != after:
        raise RuntimeError("Authored actor fingerprint changed; refusing save: " + path)
    if not levels.save_current_level():
        raise RuntimeError("Cannot save " + path)
    unreal.log("BRNAVREPAIR Map=%s ProjectedStarts=%d/%d AuthoredActors=%d Fingerprint=%s Unchanged=1 Saved=1" %
               (path, usable, len(starts), before[0], before[1]))
