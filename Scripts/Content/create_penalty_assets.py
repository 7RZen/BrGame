"""UE 5.8 commandlet. Original data-driven penalty placeholders; safe to rerun.
Build editor and generate SW_PenaltyFlute before running this script.
"""
import unreal

ROOT = "/Game/BrGame/Penalties"
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
cls = unreal.load_class(None, "/Script/BrGame.BrPenaltyDefinition")
if not cls:
    raise RuntimeError("Build BrGameEditor first: BrPenaltyDefinition missing")
factory = unreal.DataAssetFactory()
factory.set_editor_property("data_asset_class", cls)
sound = unreal.load_asset("/Game/BrGame/Audio/SW_PenaltyFlute")
if not isinstance(sound, unreal.SoundWave) or not sound.get_editor_property("looping"):
    raise RuntimeError("Generate looping SW_PenaltyFlute using create_sound_assets.py first")

for name, duration, speed, forward, sprint, standing, color, trail in [
    ("Rocket", 15.0, 2.0, True, True, True, unreal.LinearColor(1, 0.15, 0, 1), unreal.BrPenaltyTrail.FLAME),
    ("Smoke", 30.0, 1.0, False, False, False, unreal.LinearColor(0.4, 0.4, 0.4, 1), unreal.BrPenaltyTrail.SMOKE),
    ("Sound", 30.0, 1.0, False, False, False, unreal.LinearColor(0.2, 0.8, 1, 1), unreal.BrPenaltyTrail.NONE),
    ("Buttplug", 30.0, 0.5, False, True, False, unreal.LinearColor(0.7, 0.15, 0.8, 1), unreal.BrPenaltyTrail.NONE),
]:
    asset_name = "DA_Penalty_" + name
    path = ROOT + "/" + asset_name
    obj = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else TOOLS.create_asset(
        asset_name, ROOT, cls, factory)
    if not obj or obj.get_class() != cls:
        raise RuntimeError("Wrong/missing penalty asset: " + path)
    for prop, value in {
        "Id": name, "DisplayName": name, "Duration": duration, "SpeedMultiplier": speed,
        "bForcedForward": forward, "bDisablesSprint": sprint, "bForcesStanding": standing,
        "TubeColor": color, "Trail": trail, "LoopSound": sound if name == "Sound" else None,
    }.items():
        obj.set_editor_property(prop, value)
    # Preserve an artist-supplied VFX soft reference on subsequent runs.
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Penalty save failed: " + path)
    if str(obj.get_editor_property("Id")) != name or obj.get_editor_property("Duration") != duration:
        raise RuntimeError("Penalty verification failed: " + path)
unreal.log("Br penalties verified: Rocket, Smoke, Sound, Buttplug")
