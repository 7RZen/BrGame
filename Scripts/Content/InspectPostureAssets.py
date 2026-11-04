"""Read-only inspection; run with UE's PythonScript commandlet."""
import unreal

mesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
bs = unreal.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run")
for i in range(3):
    p = bs.get_editor_property("blend_parameters")[i]
    unreal.log("POSTURE ASSET axis {}: {} {} {}".format(i, p.get_editor_property("display_name"), p.get_editor_property("min"), p.get_editor_property("max")))
unreal.log("POSTURE ASSET mesh skeleton: {}".format(mesh.get_editor_property("skeleton")))
