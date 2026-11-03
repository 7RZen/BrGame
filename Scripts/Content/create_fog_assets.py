"""Idempotent fog material and preview RT assets; runtime uses private per-view RTs."""
import unreal

ROOT = "/Game/BrGame/Fog"
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def asset(name, cls, factory):
    path = ROOT + "/" + name
    result = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(
        name, ROOT, cls, factory)
    if not result or result.get_class() != cls.static_class():
        raise RuntimeError("Missing/wrong asset class: " + path)
    return result


mat = asset("M_Fog", unreal.Material, unreal.MaterialFactoryNew())
lib.delete_all_material_expressions(mat)
mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)


def node(cls, **props):
    result = lib.create_material_expression(mat, cls)
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result


def wire(src, dst, pin, output=""):
    if not lib.connect_material_expressions(src, output, dst, pin):
        raise RuntimeError("Cannot connect fog pin " + pin)

def custom_input(name):
    value = unreal.CustomInput()
    value.set_editor_property("input_name", name)
    return value


world = node(unreal.MaterialExpressionWorldPosition)
bounds = node(unreal.MaterialExpressionVectorParameter, parameter_name="FogBounds",
              default_value=unreal.LinearColor(-5000, -5000, 10000, 10000))
uv = node(unreal.MaterialExpressionCustom, code="return (World.xy - Bounds.xy) / Bounds.zw;",
          output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT2,
          inputs=[custom_input("World"), custom_input("Bounds")])
wire(world, uv, "World")
wire(bounds, uv, "Bounds", "RGBA")
preview = []
for name in ("VisibleMask", "ExploredMask"):
    rt = asset("RT_" + name, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew())
    rt.set_editor_property("render_target_format", unreal.TextureRenderTargetFormat.RTF_R8
                           if name == "VisibleMask" else unreal.TextureRenderTargetFormat.RTF_RG32F)
    # Material defaults only: do not allocate another full-size GPU mask when
    # loading the base material. Runtime supplies its private configured targets.
    rt.set_editor_property("size_x", 1)
    rt.set_editor_property("size_y", 1)
    rt.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    rt.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    rt.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    rt.set_editor_property("clear_color", unreal.LinearColor(0, 0, 0, 0))
    unreal.EditorAssetLibrary.save_loaded_asset(rt, only_if_is_dirty=False)
    sample = node(unreal.MaterialExpressionTextureObjectParameter, parameter_name=name, texture=rt,
                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    preview.append(sample)
scene = node(unreal.MaterialExpressionSceneTexture,
             scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
stencil = node(unreal.MaterialExpressionSceneTexture,
               scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
custom_depth = node(unreal.MaterialExpressionSceneTexture,
                    scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_DEPTH)
scene_depth = node(unreal.MaterialExpressionSceneTexture,
                   scene_texture_id=unreal.SceneTextureId.PPI_SCENE_DEPTH)
params = []
for name, default in (("FogTime", 1.0), ("DimOpacity", 0.6), ("FogDimDelay", 2.0),
                      ("EdgeFeather", 30.0)):
    params.append(node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default))
result = node(unreal.MaterialExpressionCustom, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
              inputs=[custom_input(x) for x in
                      ("Scene", "Visible", "Explored", "Time", "Opacity", "Delay", "UV",
                       "Stencil", "CustomDepth", "SceneDepth", "Bounds", "Feather")],
              code="""
float inside = all(UV >= 0) && all(UV <= 1);
float visible = Texture2DSample(Visible, VisibleSampler, UV).r * inside;
uint width, height;
Explored.GetDimensions(width, height);
float2 texSize = float2(width, height);
float2 stampUV = (floor(UV * texSize) + 0.5) / texSize;
float stamp = Texture2DSample(Explored, ExploredSampler, stampUV).r;
float known = Texture2DSample(Explored, ExploredSampler, UV).g * inside;
float vSum = visible, kSum = known;
// Inward-only 3x3 world-space feather. Center gating never dilates LOS or
// exploration through a thin wall. Point-sampled timestamps avoid bogus ages.
[unroll] for (int y = -1; y <= 1; ++y)
{
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        if (x == 0 && y == 0) continue;
        float2 tap = UV + float2(x, y) * Feather / Bounds.zw;
        float inTap = all(tap >= 0) && all(tap <= 1);
        vSum += Texture2DSample(Visible, VisibleSampler, tap).r * inTap;
        kSum += Texture2DSample(Explored, ExploredSampler, tap).g * inTap;
    }
}
visible *= smoothstep(0.5, 1.0, vSum / 9);
known *= smoothstep(0.5, 1.0, kSum / 9);
float fade = saturate((Time - stamp) / max(Delay, 0.01));
float light = max(visible, known * (1 - Opacity * fade));
// Only exempt actually visible friends, not an occluded stencil behind a wall.
float depthMatch = abs(CustomDepth.r - SceneDepth.r) <= 1;
float friendPixel = abs(Stencil.r - 1) < 0.1 && depthMatch;
light = max(light, friendPixel);
// Only C++-validated, ray-hit STATIC map obstacles use 2..17. This lights the
// object's own depth-tested pixels, never the floor or actors behind it.
if (Stencil.r >= 2 && Stencil.r <= 17 && depthMatch)
    light = 1 - Opacity * saturate((Stencil.r - 2) / 15);
return Scene.rgb * light;
""")
wire(scene, result, "Scene", "Color")
wire(preview[0], result, "Visible")
wire(preview[1], result, "Explored")
for param, pin in zip(params, ("Time", "Opacity", "Delay", "Feather")):
    wire(param, result, pin)
wire(uv, result, "UV")
wire(stencil, result, "Stencil")
wire(custom_depth, result, "CustomDepth")
wire(scene_depth, result, "SceneDepth")
wire(bounds, result, "Bounds", "RGBA")
if not lib.connect_material_property(result, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("Cannot connect fog output")
lib.layout_material_expressions(mat)
lib.recompile_material(mat)
if not unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False):
    raise RuntimeError("Cannot save fog material")
# Rings are intentionally composed after the fog. Other world translucency stays
# in the default BeforeDOF pass and is covered by the black/dim layers.
ring = unreal.load_asset("/Game/BrGame/Zone/M_ZoneRing")
if not ring:
    raise RuntimeError("Generate zone assets first")
ring.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_DOF)
lib.recompile_material(ring)
unreal.EditorAssetLibrary.save_loaded_asset(ring, only_if_is_dirty=False)
unreal.log("Br fog assets verified: M_Fog, RT_VisibleMask, RT_ExploredMask; zone rings AfterDOF")
