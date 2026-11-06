"""UE 5.8 PythonScript commandlet (issue #1). Imports voice-line WAVs and builds BrVoiceSet assets.

HOW TO USE / REPLACE PLACEHOLDERS WITH REAL RECORDINGS
  1. Each subfolder of Scripts/Content/Audio/Voice/ is one voice (currently David, Mark, Zira).
     Each *.wav in it becomes /Game/BrGame/Audio/Voice/<Voice>/SW_<Voice>_<FileStem>
     (imported in name order). Current WAVs are Windows TTS placeholders.
  2. To replace a line, overwrite the WAV with the same file name (e.g. Down_03.wav) and rerun.
     Unchanged WAVs are skipped (source SHA-256 is stored as asset metadata).
  3. To add a line, drop a new WAV in the folder and rerun. To remove one, delete the WAV and rerun;
     its SW_ asset is deleted only when nothing references it, otherwise it is logged and kept.
  4. The DA_Voice_<Voice> asset (BrVoiceSet) gets display_name = folder name and down_lines = the
     imported sounds in name order. Rerunning keeps it in sync.
  5. A NEW folder needs a config entry. The script logs the exact `+VoiceSets=` line to add to
     Config/DefaultGame.ini (it does not edit config itself). Existing voices are already listed.
  Sound class = /Game/BrGame/Audio/SC_Voice (created by create_voice_sound_class.py). Attenuation,
  concurrency and voice priority are applied by the C++ at play time, so the assets stay simple.
  Run serially, never alongside builds/tests:
    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi -nosound -nosteam
"""
import hashlib
import os
import unreal

PROJECT_DIR = unreal.SystemLibrary.get_project_directory()
VOICE_ROOT = os.path.join(PROJECT_DIR, "Scripts", "Content", "Audio", "Voice")
DEST_ROOT = "/Game/BrGame/Audio/Voice"
SOUND_CLASS_PATH = "/Game/BrGame/Audio/SC_Voice"
CONFIG_LINE = "+VoiceSets={pkg}.{name}"
TAG_HASH = "BrSourceSHA256"
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def import_wav(wav_path, dest_dir, asset_name, sound_class):
    dest_path = dest_dir + "/" + asset_name
    digest = sha256(wav_path)
    if EAL.does_asset_exist(dest_path):
        existing = unreal.load_asset(dest_path)
        if existing and EAL.get_metadata_tag(existing, TAG_HASH) == digest:
            return existing, False
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", wav_path)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    ASSET_TOOLS.import_asset_tasks([task])
    sound = unreal.load_asset(dest_path)
    if not isinstance(sound, unreal.SoundWave):
        raise RuntimeError("Import failed for " + wav_path)
    sound.set_editor_property("sound_class_object", sound_class)
    sound.set_editor_property("looping", False)
    EAL.set_metadata_tag(sound, TAG_HASH, digest)
    if not EAL.save_loaded_asset(sound, only_if_is_dirty=False):
        raise RuntimeError("Save failed: " + dest_path)
    return sound, True


def ensure_voice_set(voice_name, dest_dir, sounds):
    asset_name = "DA_Voice_" + voice_name
    dest_path = dest_dir + "/" + asset_name
    if EAL.does_asset_exist(dest_path):
        asset = unreal.load_asset(dest_path)
    else:
        factory = unreal.DataAssetFactory()
        asset = ASSET_TOOLS.create_asset(asset_name, dest_dir, unreal.BrVoiceSet, factory)
    if not asset or asset.get_class() != unreal.BrVoiceSet.static_class():
        raise RuntimeError("DA_Voice asset missing or wrong class: " + dest_path)
    asset.set_editor_property("display_name", unreal.Text(voice_name))
    asset.set_editor_property("down_lines", sounds)
    if not EAL.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Save failed: " + dest_path)
    return asset


def remove_orphans(dest_dir, voice_name, keep_names):
    prefix = "SW_" + voice_name + "_"
    removed, kept = [], []
    for asset_path in EAL.list_assets(dest_dir, recursive=False, include_folder=False):
        name = asset_path.split(".")[0].split("/")[-1]
        if not name.startswith(prefix) or name in keep_names:
            continue
        pkg = dest_dir + "/" + name
        refs = EAL.find_package_referencers_for_asset(pkg, load_assets_to_confirm=False)
        if refs:
            kept.append(name)
            unreal.log_warning("Orphan kept (still referenced): " + pkg + " by " + ", ".join(map(str, refs)))
        else:
            EAL.delete_asset(pkg)
            removed.append(name)
    return removed, kept


def main():
    sound_class = unreal.load_asset(SOUND_CLASS_PATH)
    if not isinstance(sound_class, unreal.SoundClass):
        raise RuntimeError("Missing " + SOUND_CLASS_PATH + "; run create_voice_sound_class.py first")
    if not os.path.isdir(VOICE_ROOT):
        raise RuntimeError("Voice folder missing: " + VOICE_ROOT)
    ensure_dir(DEST_ROOT)

    voice_names = sorted(d for d in os.listdir(VOICE_ROOT) if os.path.isdir(os.path.join(VOICE_ROOT, d)))
    if not voice_names:
        raise RuntimeError("No voice subfolders in " + VOICE_ROOT)

    for voice in voice_names:
        folder = os.path.join(VOICE_ROOT, voice)
        wavs = sorted(f for f in os.listdir(folder) if f.lower().endswith(".wav"))
        dest_dir = DEST_ROOT + "/" + voice
        ensure_dir(dest_dir)
        sounds, imported, keep = [], 0, set()
        for wav in wavs:
            stem = os.path.splitext(wav)[0]
            asset_name = "SW_" + voice + "_" + stem
            sound, did_import = import_wav(os.path.join(folder, wav), dest_dir, asset_name, sound_class)
            sounds.append(sound)
            keep.add(asset_name)
            imported += 1 if did_import else 0
        removed, kept = remove_orphans(dest_dir, voice, keep)
        is_new = not EAL.does_asset_exist(DEST_ROOT + "/DA_Voice_" + voice)
        ensure_voice_set(voice, DEST_ROOT, sounds)
        unreal.log("Voice %s: %d lines (%d imported/updated), removed=%s, kept=%s"
                   % (voice, len(sounds), imported, removed or "none", kept or "none"))
        if is_new:
            unreal.log_warning("NEW voice set. Add this line to Config/DefaultGame.ini [BrDeveloperSettings]: "
                               + CONFIG_LINE.format(pkg=DEST_ROOT + "/DA_Voice_" + voice,
                                                    name="DA_Voice_" + voice))

    # Verify.
    for voice in voice_names:
        da = unreal.load_asset(DEST_ROOT + "/DA_Voice_" + voice)
        if not da or da.get_class() != unreal.BrVoiceSet.static_class():
            raise RuntimeError("Verify failed: DA_Voice_" + voice)
        lines = da.get_editor_property("down_lines")
        if len(lines) != len(os.listdir(os.path.join(VOICE_ROOT, voice))):
            raise RuntimeError("Verify failed: line count for " + voice)
        for line in lines:
            if not line or line.get_editor_property("sound_class_object") != sound_class:
                raise RuntimeError("Verify failed: bad sound class on a line of " + voice)
    unreal.log("Voice import verified for: " + ", ".join(voice_names))


main()
