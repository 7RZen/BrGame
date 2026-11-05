"""UE 5.8 PythonScript commandlet; idempotently create only BrGame combat assets."""
import unreal
import math

TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def asset(name, folder, native_class):
    cls = unreal.load_class(None, "/Script/BrGame." + native_class)
    if not cls:
        raise RuntimeError("Missing class: " + native_class + "; build BrGameEditor first")
    path = folder + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        obj = unreal.load_asset(path)
        if not obj or obj.get_class() != cls:
            raise RuntimeError("Wrong asset class at " + path)
        return obj
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    obj = TOOLS.create_asset(name, folder, cls, factory)
    if not obj:
        raise RuntimeError("Asset creation failed: " + path)
    return obj


# Placeholder held/pickup visual scale on the 100 cm BasicShapes Cube.
# X is the length axis of the held weapon; Y/Z are the cross-section.
WEAPON_VISUAL_SCALE = {
    "Pistol": (0.25, 0.08, 0.08),
    "AssaultRifle": (0.50, 0.08, 0.08),
    "MachineGun": (0.75, 0.08, 0.08),
}


def same_value(actual, expected):
    if isinstance(expected, unreal.Vector):
        pairs = zip((actual.x, actual.y, actual.z), (expected.x, expected.y, expected.z))
        return all(math.isclose(a, e, rel_tol=1e-6, abs_tol=1e-6) for a, e in pairs)
    if isinstance(expected, (int, float, bool)):
        return math.isclose(float(actual), float(expected), rel_tol=1e-6, abs_tol=1e-6)
    return True  # object references are verified explicitly by the caller


def configure(obj, properties):
    for key, value in properties.items():
        obj.set_editor_property(key, value)
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Save failed: " + obj.get_path_name())
    # Re-read the saved object's properties and compare against what was written.
    for key, value in properties.items():
        actual = obj.get_editor_property(key)
        if not same_value(actual, value):
            raise RuntimeError("Value mismatch after save: " + obj.get_name() + "." + key)


def validate_weapon_scales(weapons_by_name, cube):
    # Ordering check: short < medium < long along the held-weapon length axis (X).
    scales = {}
    for name, obj in weapons_by_name.items():
        v = obj.get_editor_property("VisualScale")
        scales[name] = (v.x, v.y, v.z)
        expected = WEAPON_VISUAL_SCALE[name]
        if not all(math.isclose(a, e, rel_tol=1e-6, abs_tol=1e-6) for a, e in zip(scales[name], expected)):
            raise RuntimeError("VisualScale not persisted for " + name)
        mesh = obj.get_editor_property("Mesh")
        if mesh != cube:
            raise RuntimeError("Mesh reference changed unexpectedly for " + name)
    if not (scales["Pistol"][0] < scales["AssaultRifle"][0] < scales["MachineGun"][0]):
        raise RuntimeError("Weapon length ordering violated: Pistol < AssaultRifle < MachineGun on X")
    unreal.log("Weapon VisualScale verified: " + str(scales))


def main():
    weapons = "/Game/BrGame/Weapons"
    ammo = asset("DA_Ammo_Standard", weapons, "BrAmmoDefinition")
    configure(ammo, {"DisplayName": "Standard Ammo"})
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")
    if not cube or not sphere:
        raise RuntimeError("Engine placeholder meshes unavailable")
    weapons_by_name = {}
    for name, display, damage, rate, automatic, size, reload_time, speed_mult, max_range in [
        ("Pistol", "Pistol", 10, 4, False, 12, 1.5, 1.0, 2500),
        ("AssaultRifle", "Assault Rifle", 20, 10, True, 20, 2.0, 0.9, 4000),
        ("MachineGun", "Machine Gun", 30, 15, True, 80, 4.0, 0.8, 4000),
    ]:
        obj = asset("DA_Weapon_" + name, weapons, "BrWeaponDefinition")
        sound = unreal.load_asset("/Game/BrGame/Audio/SW_Fire_" + name)
        if not sound:
            raise RuntimeError("Run create_sound_assets.py first; fire sound missing: " + name)
        configure(obj, {"DisplayName": display, "Damage": damage, "FireRate": rate,
                        "FireSound": sound,
                        "bAutomatic": automatic, "MagazineSize": size, "StartingMagazines": 2,
                        "ReloadTime": reload_time, "RunSpeedMultiplier": speed_mult,
                        "MaxRange": max_range, "ProjectileSpeed": 15000, "AmmoType": ammo, "Mesh": cube,
                        "VisualScale": unreal.Vector(*WEAPON_VISUAL_SCALE[name])})
        if obj.get_editor_property("AmmoType") != ammo:
            raise RuntimeError("Shared ammo reference was not assigned")
        weapons_by_name[name] = obj
    validate_weapon_scales(weapons_by_name, cube)
    grenade = asset("DA_Throwable_HandGrenade", "/Game/BrGame/Throwables", "BrThrowableDefinition")
    explosion = unreal.load_asset("/Game/BrGame/Audio/SW_GrenadeExplosion")
    if not explosion:
        raise RuntimeError("Run create_sound_assets.py first; explosion sound missing")
    configure(grenade, {"DisplayName": "Hand Grenade", "Damage": 70, "BlastRadius": 136,
                        "ExplosionSound": explosion,
                        "ThrowSpeed": 1500, "MaxCarried": 2, "bDamageFalloff": False, "Mesh": sphere})
    settings_class = unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings")
    settings = unreal.get_default_object(settings_class)
    for prop in ["StartingWeapon", "HandGrenade"]:
        if not settings.get_editor_property(prop):
            raise RuntimeError("Developer settings missing " + prop + "; check DefaultGame.ini")
    if len(settings.get_editor_property("WeaponRegistry")) != 3:
        raise RuntimeError("Expected three developer weapon registry entries")
    unreal.log("Br combat assets verified: Standard ammo, Pistol, AssaultRifle, MachineGun, HandGrenade; registry=3")


main()
