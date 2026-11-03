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


def configure(obj, properties):
    for key, value in properties.items():
        obj.set_editor_property(key, value)
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Save failed: " + obj.get_path_name())
    for key, value in properties.items():
        actual = obj.get_editor_property(key)
        if isinstance(value, (int, float, bool)) and not math.isclose(actual, value, rel_tol=1e-6):
            raise RuntimeError("Value mismatch: " + key)


def main():
    weapons = "/Game/BrGame/Weapons"
    ammo = asset("DA_Ammo_Standard", weapons, "BrAmmoDefinition")
    configure(ammo, {"DisplayName": "Standard Ammo"})
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")
    if not cube or not sphere:
        raise RuntimeError("Engine placeholder meshes unavailable")
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
                        "MaxRange": max_range, "ProjectileSpeed": 15000, "AmmoType": ammo, "Mesh": cube})
        if obj.get_editor_property("AmmoType") != ammo:
            raise RuntimeError("Shared ammo reference was not assigned")
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
