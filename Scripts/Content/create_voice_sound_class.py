"""UE 5.8 PythonScript commandlet (issue #1). Creates/updates the voice volume class.

- /Game/BrGame/Audio/SC_Voice (SoundClass), parented under SC_Master as a sibling of SC_Effects.
  SC_Master's existing child list is kept; SC_Voice is appended only when missing.
- /Game/BrGame/Audio/SM_Volume: a neutral entry (volume 1.0, pitch 1.0) for SC_Voice so the class is
  present in the mix asset. Runtime volume still comes from BrGameInstance::ApplyAudioVolumes
  (SetSoundMixClassOverride with the Voice slider value); SC_Effects is also only controlled at runtime.
Idempotent: assets are saved only when something changed.
"""
import unreal

AUDIO = "/Game/BrGame/Audio"
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def save(obj):
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Save failed: " + obj.get_path_name())


def load_required(name, cls):
    obj = unreal.load_asset(AUDIO + "/" + name)
    if not obj or obj.get_class() != cls.static_class():
        raise RuntimeError("Missing or wrong class: " + AUDIO + "/" + name + " (expected " + cls.__name__ + ")")
    return obj


def main():
    master = load_required("SC_Master", unreal.SoundClass)
    effects = load_required("SC_Effects", unreal.SoundClass)
    mix = load_required("SM_Volume", unreal.SoundMix)

    voice_path = AUDIO + "/SC_Voice"
    created = False
    if unreal.EditorAssetLibrary.does_asset_exist(voice_path):
        voice = load_required("SC_Voice", unreal.SoundClass)
    else:
        voice = TOOLS.create_asset("SC_Voice", AUDIO, unreal.SoundClass, unreal.SoundClassFactory())
        created = True
    if not voice or voice.get_class() != unreal.SoundClass.static_class():
        raise RuntimeError("SC_Voice creation failed")

    voice_dirty = created
    if voice.get_editor_property("parent_class") != master:
        voice.set_editor_property("parent_class", master)
        voice_dirty = True
    if voice_dirty:
        save(voice)

    children = list(master.get_editor_property("child_classes"))
    if effects not in children:
        raise RuntimeError("SC_Effects is no longer a child of SC_Master; refusing to rewrite the hierarchy")
    if voice not in children:
        children.append(voice)
        master.set_editor_property("child_classes", children)
        save(master)
        unreal.log("SC_Voice added to SC_Master children")

    effects_list = list(mix.get_editor_property("sound_class_effects"))
    if not any(entry.get_editor_property("sound_class_object") == voice for entry in effects_list):
        adjuster = unreal.SoundClassAdjuster()
        adjuster.set_editor_property("sound_class_object", voice)
        adjuster.set_editor_property("volume_adjuster", 1.0)
        adjuster.set_editor_property("pitch_adjuster", 1.0)
        effects_list.append(adjuster)
        mix.set_editor_property("sound_class_effects", effects_list)
        save(mix)
        unreal.log("SC_Voice added to SM_Volume (neutral entry)")

    # Verify the saved state.
    voice = load_required("SC_Voice", unreal.SoundClass)
    if voice.get_editor_property("parent_class") != master:
        raise RuntimeError("SC_Voice is not parented under SC_Master")
    if not any(c == voice for c in unreal.load_asset(AUDIO + "/SC_Master").get_editor_property("child_classes")):
        raise RuntimeError("SC_Voice missing from SC_Master children")
    mix = load_required("SM_Volume", unreal.SoundMix)
    if not any(e.get_editor_property("sound_class_object") == voice for e in mix.get_editor_property("sound_class_effects")):
        raise RuntimeError("SC_Voice missing from SM_Volume")
    unreal.log("SC_Voice verified: parent=SC_Master, present in SM_Volume")


main()
