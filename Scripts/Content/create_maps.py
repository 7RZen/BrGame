"""Idempotent flow maps. Only edit the authorized GameMode property in Lvl_TopDown."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
root = "/Game/BrGame/Maps/"

for name, native in [
    ("L_MainMenu", "BrMenuGameMode"),
    ("L_Lobby", "BrLobbyGameMode"),
    ("L_Transition", "BrMenuGameMode"),
]:
    path = root + name
    cls = unreal.load_class(None, "/Script/BrGame." + native)
    if not cls:
        raise RuntimeError("Build native class first: " + native)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if not levels.load_level(path):
            raise RuntimeError("Cannot load " + path)
    elif not levels.new_level(path):
        raise RuntimeError("Cannot create " + path)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", cls)
    if not levels.save_current_level():
        raise RuntimeError("Cannot save " + path)
    unreal.log("BR map verified: " + path + " mode=" + native)

match = "/Game/TopDown/Lvl_TopDown"
if not levels.load_level(match):
    raise RuntimeError("Cannot load match map")
settings = editor.get_editor_world().get_world_settings()
if settings.get_editor_property("default_game_mode") is not None:
    settings.set_editor_property("default_game_mode", None)
    if not levels.save_current_level():
        raise RuntimeError("Cannot clear match GameMode override")
unreal.log("BR match override verified: None (global native BrGameMode)")
