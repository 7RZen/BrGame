"""UE 5.8 commandlet: create only the issue #23 armor plate pickup DataAsset.

Idempotent: creates /Game/BrGame/Pickups/DA_Pickup_Armor if missing, otherwise
re-applies the same properties to the existing asset. Does not touch other
pickup assets. Run after a full BrGameEditor build, serially with other asset
writers. Requires M_Pickup (create_pickup_assets.py) and the pickup sound.

Plate value/cap (ArmorPlateValue, MaxArmorPlates) and crate counts live in
UBrDeveloperSettings / DefaultGame.ini, not on this asset.
"""
import unreal


def main():
    root = "/Game/BrGame/Pickups"
    name = "DA_Pickup_Armor"
    path = root + "/" + name
    cls = unreal.load_class(None, "/Script/BrGame.BrArmorPickupDefinition")
    if not cls:
        raise RuntimeError("Build BrGameEditor with BrArmorPickupDefinition first")
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        obj = unreal.load_asset(path)
        if not obj or obj.get_class() != cls:
            raise RuntimeError("Wrong armor asset class: " + path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
        obj = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, root, cls, factory)
    if not obj:
        raise RuntimeError("Armor asset creation failed")
    refs = {
        # Flattened cube reads as a plate; same base mesh family as the ammo box.
        "Mesh": "/Engine/BasicShapes/Cube",
        "Material": root + "/M_Pickup",
        # Health chime: defensive pickup, distinct from the weapon/ammo cues.
        "PickupSound": "/Game/BrGame/Audio/SW_Pickup_Health",
    }
    for prop, ref in refs.items():
        value = unreal.load_asset(ref)
        if not value:
            raise RuntimeError("Missing required armor cosmetic: " + ref)
        obj.set_editor_property(prop, value)
    for prop, value in {
        "DisplayName": "Armor Plate",
        "Label": "Armor",
        # Pale steel: distinct from pistol blue, rifle cyan, MG orange, health green, ammo yellow, grenade dark green, quest magenta.
        "Color": unreal.LinearColor(0.62, 0.74, 0.86, 1),
        "VisualScale": unreal.Vector(0.5, 0.35, 0.08),
    }.items():
        obj.set_editor_property(prop, value)
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Armor asset save failed")
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings"))
    crates = settings.get_editor_property("CrateDefinitions")
    if len(crates) != 9 or crates[8] != obj:
        raise RuntimeError("DefaultGame.ini CrateDefinitions[8] does not resolve to " + path)
    unreal.log("Armor pickup generated: " + obj.get_path_name())


main()
