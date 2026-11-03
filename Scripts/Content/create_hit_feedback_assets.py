"""Idempotent translucent red overlay; independent of mannequin material parameters."""
import unreal

mesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
if not mesh:
    raise RuntimeError("Missing character mesh")
for entry in mesh.get_editor_property("materials"):
    material = entry.get_editor_property("material_interface")
    unreal.log("HitFeedback base material {} vectors={}".format(
        material.get_path_name(), unreal.MaterialEditingLibrary.get_vector_parameter_names(material)))
path = "/Game/BrGame/Feedback/M_HitOverlay"
mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if not mat:
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_HitOverlay", "/Game/BrGame/Feedback", unreal.Material, unreal.MaterialFactoryNew())
if not mat:
    raise RuntimeError("Cannot create hit overlay")
lib = unreal.MaterialEditingLibrary
lib.delete_all_material_expressions(mat)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("two_sided", True)
mat.set_editor_property("used_with_skeletal_mesh", True)
color = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter)
color.set_editor_property("parameter_name", "HitColor")
color.set_editor_property("default_value", unreal.LinearColor(2, 0, 0, 1))
opacity = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
opacity.set_editor_property("parameter_name", "HitOpacity")
opacity.set_editor_property("default_value", 0.65)
if not lib.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("Cannot connect overlay color")
if not lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
    raise RuntimeError("Cannot connect overlay opacity")
lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log("HIT FEEDBACK ASSET PASS")
