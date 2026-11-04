"""Read-only map navigation/ground audit; never rebuilds or saves assets."""
import unreal


levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
settings = unreal.get_default_object(unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings"))
for entry in settings.get_editor_property("maps"):
    path = entry.get_editor_property("map").get_path_name().split(".")[0]
    if not levels.load_level(path):
        raise RuntimeError("Cannot load " + path)
    unreal.log("BRNAV Map=" + path)
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.RecastNavMesh):
            unreal.log("BRNAV Recast=%s Generation=%s ForceRebuild=%s" % (
                actor.get_name(), actor.get_editor_property("runtime_generation"),
                actor.get_editor_property("force_rebuild_on_load")))
        if isinstance(actor, unreal.NavMeshBoundsVolume):
            unreal.log("BRNAV Bounds=%s %s" % (actor.get_name(), actor.get_actor_bounds(False)))
        for component in actor.get_components_by_class(unreal.PrimitiveComponent):
            if actor.actor_has_tag("BrFogGround") or component.component_has_tag("BrFogGround"):
                unreal.log("BRNAV Ground=%s/%s Bounds=%s Rotation=%s Scale=%s ObjectType=%s Profile=%s Mobility=%s" % (
                    actor.get_actor_label(), component.get_name(), actor.get_actor_bounds(False),
                    component.get_world_rotation(), component.get_world_scale(),
                    component.get_collision_object_type(), component.get_collision_profile_name(),
                    component.get_editor_property("mobility")))
