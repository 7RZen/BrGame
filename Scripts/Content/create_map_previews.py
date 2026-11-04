"""Regenerate map previews (editor closed, built BrGameEditor required).

Run with system Python: python Scripts/Content/create_map_previews.py
Uses the real runtime minimap's unlit orthographic capture (512px, RHI enabled),
then imports PNGs with editor Python, serially. The commandlet-only capture
produced black images on this machine, so it is intentionally not used.
The map list is read by the import commandlet from UBrDeveloperSettings.
"""
import os
import sys

try:
    import unreal
except ImportError:
    import pathlib
    import subprocess
    root = pathlib.Path(__file__).resolve().parents[2]
    ue = pathlib.Path(r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64")
    # Never stop a user's editor; require it to be closed before starting.
    check = subprocess.run(["powershell", "-NoProfile", "-Command",
        "if (Get-Process UnrealEditor,UnrealEditor-Cmd,LiveCodingConsole -ErrorAction SilentlyContinue) { exit 1 }"])
    if check.returncode:
        raise RuntimeError("Close editor/Live Coding first; no process was stopped")
    # Phase 1 reads the developer list, tags ground and writes a capture manifest.
    command = [str(ue / "UnrealEditor-Cmd.exe"), str(root / "BrGame.uproject"),
        "-run=pythonscript", "-script=" + str(pathlib.Path(__file__).resolve()),
        "-unattended", "-nullrhi", "-nosound", "-nosteam"]
    subprocess.run(command + ["-BrPreviewPrepare"], check=True, timeout=180)
    import json
    manifest = json.loads((root / "Saved/MapPreviewManifest.json").read_text())
    for item in manifest:
        png = root / ("Saved/Screenshots/Maps/Previews/T_" + item["id"] + ".png")
        png.unlink(missing_ok=True)
        process = subprocess.Popen([str(ue / "UnrealEditor.exe"), str(root / "BrGame.uproject"),
            item["map"], "-game", "-windowed", "-ResX=640", "-ResY=640",
            "-unattended", "-nosound", "-nosteam", "-BrFlowSmoke", "-BrShotSmoke", "-BrMapPreview",
            "-ini:Game:[/Script/BrGame.BrDeveloperSettings]:MinimapTextureSize=512"])
        try:
            if process.wait(timeout=180):
                raise RuntimeError("Runtime preview failed: " + item["id"])
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
        if not png.is_file():
            raise RuntimeError("Missing runtime preview: " + str(png))
    subprocess.run(command, check=True, timeout=180)
    sys.exit(0)

import json
settings = unreal.get_default_object(unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings"))
output = os.path.abspath(os.path.join(unreal.Paths.project_saved_dir(), "Screenshots/Maps/Previews"))
os.makedirs(output, exist_ok=True)
destination = "/Game/BrGame/Maps/Previews"
prepare = "-BrPreviewPrepare" in unreal.SystemLibrary.get_command_line()
manifest = []
for entry in settings.get_editor_property("maps"):
    map_id = str(entry.get_editor_property("id"))
    path = entry.get_editor_property("map").get_path_name().split(".")[0]
    manifest.append(dict(id=map_id, map=path))
    if prepare:
        levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if not levels.load_level(path):
            raise RuntimeError("Cannot load " + path)
        changed = False
        for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
            if not isinstance(actor, unreal.StaticMeshActor):
                continue
            _, extent = actor.get_actor_bounds(False)
            if extent.x >= 250 and extent.y >= 250 and extent.z <= 100:
                tags = list(actor.get_editor_property("tags"))
                if unreal.Name("BrFogGround") not in tags:
                    actor.set_editor_property("tags", tags + [unreal.Name("BrFogGround")])
                    changed = True
        if changed and not levels.save_current_level():
            raise RuntimeError("Cannot save ground tags: " + path)
        continue
    png = os.path.join(output, "T_" + map_id + ".png")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", "T_" + map_id)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(destination + "/T_" + map_id)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError("Texture import failed for " + map_id)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    # SCS_SceneColorHDRNoAlpha exports RGB with zero alpha. Preserve the source
    # capture, but force opacity in the imported texture used by UMG.
    texture.set_editor_property("adjust_min_alpha", 1.0)
    texture.set_editor_property("adjust_max_alpha", 1.0)
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    unreal.log("BR MAP PREVIEW IMPORT PASS: " + map_id)
if prepare:
    with open(os.path.join(unreal.Paths.project_saved_dir(), "MapPreviewManifest.json"), "w") as file:
        json.dump(manifest, file)
