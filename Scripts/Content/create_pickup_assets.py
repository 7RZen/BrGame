"""UE 5.8 PythonScript commandlet. Idempotently rebuild only /Game/BrGame/Pickups."""
import math
import unreal

ROOT = "/Game/BrGame/Pickups"
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def asset(name, cls, factory):
    path = ROOT + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        obj = unreal.load_asset(path)
        expected = cls if isinstance(cls, unreal.Class) else cls.static_class()
        if not obj or obj.get_class() != expected:
            raise RuntimeError("Wrong asset class: " + path)
        return obj
    obj = TOOLS.create_asset(name, ROOT, cls, factory)
    if not obj:
        raise RuntimeError("Failed to create " + path)
    return obj


def data(name, native):
    cls = unreal.load_class(None, "/Script/BrGame." + native)
    if not cls:
        raise RuntimeError("Missing class " + native + "; build BrGameEditor first")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    return asset(name, cls, factory)


def save(obj):
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Could not save " + obj.get_path_name())


def material():
    obj = asset("M_Pickup", unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(obj)
    color = unreal.MaterialEditingLibrary.create_material_expression(
        obj, unreal.MaterialExpressionVectorParameter, -200, 0)
    color.set_editor_property("parameter_name", "PickupColor")
    color.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    obj.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    if not unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Pickup material connection failed")
    unreal.MaterialEditingLibrary.recompile_material(obj)
    save(obj)
    return obj


def main():
    mat = material()
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")
    cylinder = unreal.load_asset("/Engine/BasicShapes/Cylinder")
    if not cube or not sphere or not cylinder:
        raise RuntimeError("Engine placeholder meshes missing")
    definitions = {}

    def define(key, native, display, mesh, color, scale, properties=None):
        obj = data("DA_Pickup_" + key, native)
        category = "Weapon" if key.startswith("Weapon_") else "Health" if key.startswith("Health") else key
        sound = unreal.load_asset("/Game/BrGame/Audio/SW_Pickup_" + category)
        if not sound:
            raise RuntimeError("Run create_sound_assets.py first; pickup sound missing: " + category)
        props = {"DisplayName": display, "Label": display, "Mesh": mesh, "Material": mat,
                 "PickupSound": sound,
                 "Color": unreal.LinearColor(*color), "VisualScale": unreal.Vector(*scale),
                 "bRequiresInteract": key.startswith("Weapon_")}
        props.update(properties or {})
        for prop, value in props.items():
            obj.set_editor_property(prop, value)
        save(obj)
        definitions[key] = obj
        return obj

    define("Ammo", "BrAmmoPickupDefinition", "Ammo", cube, (1, 0.8, 0.02, 1), (0.4, 0.25, 0.2))
    for amount, scale in [(20, 0.3), (50, 0.45), (100, 0.65)]:
        obj = define("Health" + str(amount), "BrHealthPickupDefinition", "Health " + str(amount) + "%",
                     sphere, (0.02, 1, 0.1, 1), (scale, scale, scale), {"HealthFraction": amount / 100})
        if not math.isclose(obj.get_editor_property("HealthFraction"), amount / 100, rel_tol=1e-6):
            raise RuntimeError("Health fraction validation failed")
    define("Grenade", "BrGrenadePickupDefinition", "Hand Grenade", sphere, (0.02, 0.2, 0.04, 1), (0.25, 0.25, 0.25))
    for name, display, color in [
        ("Pistol", "Pistol", (0.15, 0.4, 1, 1)),
        ("AssaultRifle", "Assault Rifle", (0.05, 0.9, 1, 1)),
        ("MachineGun", "Machine Gun", (1, 0.35, 0.02, 1)),
    ]:
        weapon = unreal.load_asset("/Game/BrGame/Weapons/DA_Weapon_" + name)
        if not weapon:
            raise RuntimeError("Missing weapon definition: " + name)
        obj = define("Weapon_" + name, "BrWeaponPickupDefinition", display, cylinder, color,
                     (0.25, 0.25, 0.6), {"Weapon": weapon})
        if obj.get_editor_property("Weapon") != weapon:
            raise RuntimeError("Weapon reference validation failed")
    table = data("DA_BrPickupTable", "BrPickupTable")
    entries = []
    for key, weight in [("Ammo", 30), ("Health20", 18), ("Health50", 9), ("Health100", 3),
                        ("Grenade", 12), ("Weapon_AssaultRifle", 8), ("Weapon_MachineGun", 5), ("Weapon_Pistol", 0)]:
        entry = unreal.BrPickupTableEntry()
        entry.set_editor_property("Definition", definitions[key])
        entry.set_editor_property("Weight", weight)
        entries.append(entry)
    table.set_editor_property("Entries", entries)
    save(table)
    stored = table.get_editor_property("Entries")
    if len(stored) != 8 or sum(e.get_editor_property("Weight") for e in stored) != 85:
        raise RuntimeError("Pickup table validation failed")
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings"))
    if not settings.get_editor_property("PickupTable") or not settings.get_editor_property("DroppedWeaponPickup"):
        raise RuntimeError("DefaultGame.ini pickup references are missing")
    if len(settings.get_editor_property("PickupRegistry")) != 8:
        raise RuntimeError("Expected eight registered pickup cheat names")
    unreal.log("Br pickup assets verified: 8 definitions, weighted table (total=85), unlit color material; registry=8")


main()
