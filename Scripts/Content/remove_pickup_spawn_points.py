"""Re-runnable Issue 16 migration. Run before removing BrPickupSpawnPoint."""
import unreal

settings = unreal.get_default_object(unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings"))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for entry in settings.get_editor_property("maps"):
    path = entry.get_editor_property("map").get_path_name().split(".")[0]
    if not levels.load_level(path):
        raise RuntimeError("Cannot load " + path)
    points = [actor for actor in actors.get_all_level_actors()
              if actor.get_class().get_name() == "BrPickupSpawnPoint"]
    for point in points:
        if not actors.destroy_actor(point):
            raise RuntimeError("Cannot destroy " + point.get_path_name())
    if points and not levels.save_current_level():
        raise RuntimeError("Cannot save " + path)
    unreal.log("BR CRATE MIGRATION PASS: {} removed={}".format(path, len(points)))
