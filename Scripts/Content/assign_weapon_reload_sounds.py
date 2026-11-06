"""UE 5.8 PythonScript commandlet (issue #1). Idempotently sets ONLY BrWeaponDefinition.ReloadSound.

Pistol/AssaultRifle/MachineGun -> SW_Reload_Pistol / SW_Reload_AssaultRifle / SW_Reload_MachineGun.
Every other weapon property is left untouched; an asset is saved only when its ReloadSound changes.
Run after create_sound_assets.py (BR_SOUND_ONLY filter is fine for the new reload clips).
"""
import unreal

WEAPONS = "/Game/BrGame/Weapons"
AUDIO = "/Game/BrGame/Audio"
ASSIGNMENTS = {
    "DA_Weapon_Pistol": "SW_Reload_Pistol",
    "DA_Weapon_AssaultRifle": "SW_Reload_AssaultRifle",
    "DA_Weapon_MachineGun": "SW_Reload_MachineGun",
}


def soft_path(obj):
    if obj is None:
        return ""
    return obj.get_path_name()


def main():
    weapon_class = unreal.load_class(None, "/Script/BrGame.BrWeaponDefinition")
    if not weapon_class:
        raise RuntimeError("Missing class BrWeaponDefinition; build BrGameEditor first")
    changed = []
    for weapon_name, sound_name in ASSIGNMENTS.items():
        weapon_path = WEAPONS + "/" + weapon_name
        weapon = unreal.load_asset(weapon_path)
        if not weapon or weapon.get_class() != weapon_class:
            raise RuntimeError("Weapon definition missing or wrong class: " + weapon_path)
        sound = unreal.load_asset(AUDIO + "/" + sound_name)
        if not isinstance(sound, unreal.SoundWave):
            raise RuntimeError("Reload sound missing (run create_sound_assets.py): " + sound_name)
        current = weapon.get_editor_property("reload_sound")
        if soft_path(current) == soft_path(sound):
            unreal.log("Reload sound already assigned: " + weapon_name)
            continue
        weapon.set_editor_property("reload_sound", sound)
        if not unreal.EditorAssetLibrary.save_loaded_asset(weapon, only_if_is_dirty=False):
            raise RuntimeError("Save failed: " + weapon_path)
        changed.append(weapon_name)

    # Verify every assignment after the save pass.
    for weapon_name, sound_name in ASSIGNMENTS.items():
        weapon = unreal.load_asset(WEAPONS + "/" + weapon_name)
        actual = soft_path(weapon.get_editor_property("reload_sound"))
        expected = AUDIO + "/" + sound_name + "." + sound_name
        if actual != expected:
            raise RuntimeError("ReloadSound mismatch on " + weapon_name + ": " + actual)
    unreal.log("Weapon reload sounds verified; changed=" + ",".join(changed or ["none"]))


main()
