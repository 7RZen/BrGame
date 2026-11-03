"""Idempotent Canvas surface material. No new full-size masks or map-image assets."""
import unreal

lib = unreal.MaterialEditingLibrary
path = "/Game/BrGame/Minimap/M_Minimap"
mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else (
    unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_Minimap", "/Game/BrGame/Minimap", unreal.Material, unreal.MaterialFactoryNew()))
if not mat or mat.get_class() != unreal.Material.static_class():
    raise RuntimeError("Cannot create " + path)
lib.delete_all_material_expressions(mat)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("two_sided", True)


def node(cls, **props):
    obj = lib.create_material_expression(mat, cls)
    for key, value in props.items():
        obj.set_editor_property(key, value)
    return obj


def inp(name):
    obj = unreal.CustomInput()
    obj.set_editor_property("input_name", name)
    return obj


nodes = [node(unreal.MaterialExpressionTextureCoordinate)]
names = ["UV"]
for name, asset_path in (
    ("MapImage", "/Engine/EngineResources/WhiteSquareTexture"),
    ("VisibleMask", "/Game/BrGame/Fog/RT_VisibleMask"),
    ("ExploredMask", "/Game/BrGame/Fog/RT_ExploredMask"),
):
    texture = unreal.load_asset(asset_path)
    if not texture:
        raise RuntimeError("Missing texture " + asset_path)
    nodes.append(node(unreal.MaterialExpressionTextureObjectParameter,
                      parameter_name=name, texture=texture,
                      sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR))
    names.append(name)
for name, default in (
    ("View", (0, 0, 6000, 1)), ("Basis", (1, 0, 220, 1)),
    ("CaptureBounds", (0, 0, 20000, 20000)), ("FogBounds", (-10000, -10000, 20000, 20000)),
    ("Zone", (0, 0, 10000, 0)), ("TargetZone", (0, 0, 5000, 0)),
):
    nodes.append(node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                      default_value=unreal.LinearColor(*default)))
    names.append(name)
nodes.append(node(unreal.MaterialExpressionScalarParameter, parameter_name="DimOpacity", default_value=0.6))
names.append("DimOpacity")
result = node(unreal.MaterialExpressionCustom,
              output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,
              inputs=[inp(name) for name in names], code="""
float2 p = UV * 2 - 1;
float2 right = float2(-Basis.y, Basis.x);
float2 world = View.xy + (right * p.x - Basis.xy * p.y) * View.z;
float2 mapUV = float2((world.y-CaptureBounds.y)/CaptureBounds.z+0.5,
                     -(world.x-CaptureBounds.x)/CaptureBounds.z+0.5);
float2 fogUV = (world-FogBounds.xy)/FogBounds.zw;
float insideWorld = all(fogUV >= 0) && all(fogUV <= 1);
float light = 1;
if (Basis.w > 0.5) {
    float v = Texture2DSample(VisibleMask, VisibleMaskSampler, fogUV).r;
    float k = Texture2DSample(ExploredMask, ExploredMaskSampler, fogUV).g;
    light = max(v, k * (1-saturate(DimOpacity)));
}
float3 color = Texture2DSample(MapImage, MapImageSampler, mapUV).rgb * light * insideWorld;
float pixelWorld = 2 * View.z / max(Basis.z, 1);
if (Zone.w > 0.5) {
    float current = 1-smoothstep(pixelWorld, pixelWorld*2, abs(length(world-Zone.xy)-Zone.z));
    float target = 1-smoothstep(pixelWorld, pixelWorld*2, abs(length(world-TargetZone.xy)-TargetZone.z));
    color = lerp(color, float3(0.1,1,0.2), current);
    color = lerp(color, float3(1,0.8,0.1), target);
}
float edge = View.w > 0.5 ? length(p) : max(abs(p.x), abs(p.y));
float border = smoothstep(1-4/max(Basis.z,1), 1-2/max(Basis.z,1), edge);
color = lerp(color, float3(0.65,0.65,0.65), border);
return float4(color, 1-smoothstep(1-1/max(Basis.z,1),1,edge));
""")
for name, src in zip(names, nodes):
    output = "RGBA" if name in ("View", "Basis", "CaptureBounds", "FogBounds", "Zone", "TargetZone") else ""
    if not lib.connect_material_expressions(src, output, result, name):
        raise RuntimeError("Cannot wire " + name)
rgb = node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=True, a=False)
alpha = node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=False, a=True)
for mask, prop in ((rgb, unreal.MaterialProperty.MP_EMISSIVE_COLOR), (alpha, unreal.MaterialProperty.MP_OPACITY)):
    if not lib.connect_material_expressions(result, "", mask, "") or not lib.connect_material_property(mask, "", prop):
        raise RuntimeError("Cannot connect map output")
lib.layout_material_expressions(mat)
lib.recompile_material(mat)
if not unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False):
    raise RuntimeError("Cannot save " + path)
unreal.log("Minimap material saved (runtime map image and borrowed fog masks).")
