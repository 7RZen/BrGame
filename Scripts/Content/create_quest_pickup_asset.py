"""UE 5.8 commandlet: create only the issue #20 quest pickup DataAsset.

Run after a full BrGameEditor build, serially with other asset writers.
Requires the existing M_Pickup and pickup sound (create_pickup_assets.py).
"""
import unreal


def main():
    root = "/Game/BrGame/Pickups"
    name = "DA_Pickup_Quest"
    path = root + "/" + name
    cls = unreal.load_class(None, "/Script/BrGame.BrQuestPickupDefinition")
    if not cls:
        raise RuntimeError("Build BrGameEditor with BrQuestPickupDefinition first")
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        obj = unreal.load_asset(path)
        if not obj or obj.get_class() != cls:
            raise RuntimeError("Wrong quest asset class: " + path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
        obj = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, root, cls, factory)
    if not obj:
        raise RuntimeError("Quest asset creation failed")
    refs = {
        "Mesh": "/Engine/BasicShapes/Cylinder",
        "Material": root + "/M_Pickup",
        "PickupSound": "/Game/BrGame/Audio/SW_Pickup_Weapon",
    }
    for prop, ref in refs.items():
        value = unreal.load_asset(ref)
        if not value:
            raise RuntimeError("Missing required quest cosmetic: " + ref)
        obj.set_editor_property(prop, value)
    for prop, value in {
        "DisplayName": "Quest",
        "Label": "Quest",
        "Color": unreal.LinearColor(0.9, 0.15, 1, 1),
        "VisualScale": unreal.Vector(0.45, 0.45, 0.12),
    }.items():
        obj.set_editor_property(prop, value)
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Quest asset save failed")
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings"))
    if settings.get_editor_property("QuestPickup") != obj:
        raise RuntimeError("DefaultGame.ini QuestPickup does not resolve to generated asset")
    unreal.log("Quest pickup generated: " + obj.get_path_name())


main()
