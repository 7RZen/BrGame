"""Idempotently generate the cheap translucent, unlit zone ground-ring material."""
import unreal

ROOT = "/Game/BrGame/Zone"
PATH = ROOT + "/M_ZoneRing"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = unreal.load_asset(PATH) if unreal.EditorAssetLibrary.does_asset_exist(PATH) else tools.create_asset(
    "M_ZoneRing", ROOT, unreal.Material, unreal.MaterialFactoryNew())
if not mat or mat.get_class() != unreal.Material.static_class():
    raise RuntimeError("Missing/wrong zone material class")
lib = unreal.MaterialEditingLibrary
lib.delete_all_material_expressions(mat)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
mat.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_DOF)


def node(cls, **props):
    obj = lib.create_material_expression(mat, cls)
    for key, value in props.items():
        obj.set_editor_property(key, value)
    return obj


def wire(src, dst, pin):
    if not lib.connect_material_expressions(src, "", dst, pin):
        raise RuntimeError("Failed material expression connection: " + pin)


uv = node(unreal.MaterialExpressionTextureCoordinate)
center = node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5)
distance = node(unreal.MaterialExpressionDistance)
wire(uv, distance, "A")
wire(center, distance, "B")
radius = node(unreal.MaterialExpressionSubtract, const_b=0.48)
wire(distance, radius, "A")
absolute = node(unreal.MaterialExpressionAbs)
wire(radius, absolute, "")
width = node(unreal.MaterialExpressionScalarParameter, parameter_name="Width", default_value=0.012)
divide = node(unreal.MaterialExpressionDivide)
wire(absolute, divide, "A")
wire(width, divide, "B")
invert = node(unreal.MaterialExpressionOneMinus)
wire(divide, invert, "")
clamp = node(unreal.MaterialExpressionSaturate)
wire(invert, clamp, "")
opacity = node(unreal.MaterialExpressionMultiply, const_b=0.7)
wire(clamp, opacity, "A")
color = node(unreal.MaterialExpressionVectorParameter, parameter_name="Color",
             default_value=unreal.LinearColor(0.05, 0.4, 1, 1))
if not lib.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("Emissive connection failed")
if not lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
    raise RuntimeError("Opacity connection failed")
lib.layout_material_expressions(mat)
lib.recompile_material(mat)
if not unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False):
    raise RuntimeError("Zone material save failed")
unreal.log("Br zone assets verified: M_ZoneRing (translucent unlit, Color/Width parameters)")
