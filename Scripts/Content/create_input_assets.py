"""Run with UE 5.8's PythonScript commandlet. Rebuild only /Game/BrGame/Input."""
import unreal

ROOT = "/Game/BrGame/Input"
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def asset(name, folder, cls, factory):
    path = f"{folder}/{name}"
    existing = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        expected_class = cls if isinstance(cls, unreal.Class) else cls.static_class()
        if existing.get_class() != expected_class:
            raise RuntimeError(f"Unexpected asset type at {path}")
        return existing
    created = TOOLS.create_asset(name, folder, cls, factory)
    if not created:
        raise RuntimeError(f"Could not create {path}")
    return created


def save(obj):
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {obj.get_path_name()}")


def mappable(outer, name, display):
    settings = unreal.new_object(unreal.PlayerMappableKeySettings, outer=outer)
    settings.set_editor_property("name", name)
    settings.set_editor_property("display_name", display)
    settings.set_editor_property("display_category", "BrGame")
    return settings


def main():
    specs = [
        ("Move", "Move"), ("Look", "Look"), ("Walk", "Walk"), ("Crouch", "Crouch"),
        ("Prone", "Prone"), ("Fire", "Fire"), ("ThrowGrenade", "Throw Grenade"),
        ("Reload", "Reload"), ("Interact", "Interact"), ("RespawnHold", "Hold to Respawn"),
        ("Scoreboard", "Scoreboard"), ("Menu", "Menu"), ("Zoom", "Zoom"), ("Map", "Full Map"),
    ]
    actions = {}
    for name, display in specs:
        action = asset(f"IA_{name}", f"{ROOT}/Actions", unreal.InputAction, unreal.InputAction_Factory())
        action.set_editor_property("value_type", {
            "Move": unreal.InputActionValueType.AXIS2D,
            "Zoom": unreal.InputActionValueType.AXIS1D,
            "Look": unreal.InputActionValueType.AXIS1D,
        }.get(name, unreal.InputActionValueType.BOOLEAN))
        action.set_editor_property("modifiers", [])
        action.set_editor_property("triggers", [])
        action.set_editor_property("player_mappable_key_settings",
                                   None if name == "Look" else mappable(action, "Zoom" if name == "Zoom" else f"Br.{name}", display))
        save(action)
        actions[name] = action

    context = asset("IMC_BrDefault", ROOT, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    context.unmap_all()
    mappings = []

    def map_key(action_name, key, mapping_name=None, display=None, negate=False, swizzle=False):
        input_key = unreal.Key()
        input_key.set_editor_property("key_name", key)
        mapping = context.map_key(actions[action_name], input_key)
        modifiers = []
        if negate:
            modifier = unreal.new_object(unreal.InputModifierNegate, outer=context)
            modifier.set_editor_property("x", True)
            modifier.set_editor_property("y", False)
            modifier.set_editor_property("z", False)
            modifiers.append(modifier)
        if swizzle:
            modifier = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=context)
            modifier.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
            modifiers.append(modifier)
        mapping.set_editor_property("modifiers", modifiers)
        mapping.set_editor_property("setting_behavior",
                                   unreal.PlayerMappableKeySettingBehaviors.IGNORE_SETTINGS if action_name == "Look"
                                   else unreal.PlayerMappableKeySettingBehaviors.OVERRIDE_SETTINGS)
        mapping.set_editor_property("player_mappable_key_settings",
                                    None if action_name == "Look" else mappable(context, mapping_name or f"Br.{action_name}", display or action_name))
        mappings.append(mapping)

    # Keyboard digital input starts on X. Negate before swizzling negative Y.
    map_key("Move", "W", "Br.MoveForward", "Move Up (screen)", swizzle=True)
    map_key("Move", "S", "Br.MoveBackward", "Move Down (screen)", negate=True, swizzle=True)
    map_key("Move", "A", "Br.MoveLeft", "Move Left", negate=True)
    map_key("Move", "D", "Br.MoveRight", "Move Right")
    for action_name, key in [
        ("Walk", "LeftShift"), ("Crouch", "C"), ("Prone", "Z"),
        ("Fire", "LeftMouseButton"), ("ThrowGrenade", "G"), ("Reload", "R"),
        ("Interact", "E"), ("RespawnHold", "SpaceBar"), ("Scoreboard", "Tab"), ("Menu", "Escape"), ("Map", "M"),
    ]:
        map_key(action_name, key, display=dict(specs)[action_name])
    map_key("Zoom", "MouseWheelAxis", mapping_name="Zoom", display="Zoom")
    map_key("Look", "MouseX")
    # Python struct results are copies: explicitly write the modified array back.
    mapping_data = context.get_editor_property("default_key_mappings")
    mapping_data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", mapping_data)
    save(context)

    config_class = unreal.load_class(None, "/Script/BrGame.BrInputConfig")
    if not config_class:
        raise RuntimeError("BrInputConfig class missing. Build BrGameEditor first.")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", config_class)
    config = asset("DA_BrInputConfig", ROOT, config_class, factory)
    # Native property names also work for classes without an exported Python wrapper.
    config.set_editor_property("DefaultMappingContext", context)
    for name, _ in specs:
        config.set_editor_property(name + "Action", actions[name])
    save(config)

    # Validate serialized mappings rather than just trusting creation success.
    reloaded = context.get_editor_property("default_key_mappings").get_editor_property("mappings")
    expected_mappings = len(specs) + 3  # Move has four directional keys.
    if len(reloaded) != expected_mappings:
        raise RuntimeError(f"Expected {expected_mappings} mappings; got {len(reloaded)}")
    names = set()
    expected_keys = {
        "Br.MoveForward": "W", "Br.MoveBackward": "S", "Br.MoveLeft": "A", "Br.MoveRight": "D",
        "Br.Walk": "LeftShift", "Br.Crouch": "C", "Br.Prone": "Z", "Br.Fire": "LeftMouseButton",
        "Br.ThrowGrenade": "G", "Br.Reload": "R", "Br.Interact": "E", "Br.RespawnHold": "SpaceBar",
        "Br.Scoreboard": "Tab", "Br.Menu": "Escape", "Br.Map": "M", "Zoom": "MouseWheelAxis",
    }
    for mapping in reloaded:
        if mapping.get_editor_property("action") == actions["Look"]:
            if (mapping.get_editor_property("setting_behavior") != unreal.PlayerMappableKeySettingBehaviors.IGNORE_SETTINGS
                    or mapping.get_editor_property("player_mappable_key_settings")
                    or str(mapping.get_editor_property("key").get_editor_property("key_name")) != "MouseX"):
                raise RuntimeError("Look must be a non-player-mappable MouseX axis")
            continue
        settings = mapping.get_editor_property("player_mappable_key_settings")
        name = str(settings.get_editor_property("name")) if settings else ""
        if not name or name in names or not str(settings.get_editor_property("display_name")):
            raise RuntimeError(f"Invalid/duplicate player mapping: {name}")
        key = str(mapping.get_editor_property("key").get_editor_property("key_name"))
        if expected_keys.get(name) != key:
            raise RuntimeError(f"Wrong default for {name}: {key}")
        if mapping.get_editor_property("setting_behavior") != unreal.PlayerMappableKeySettingBehaviors.OVERRIDE_SETTINGS:
            raise RuntimeError(f"Mapping {name} lost player-mappable override")
        names.add(name)
    unreal.log(f"Br input assets verified: {len(specs)} actions, {len(names)} unique player-mappable keys, "
               "IMC_BrDefault, DA_BrInputConfig")


main()
