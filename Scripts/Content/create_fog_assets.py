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
                      ("EdgeFeather", 60.0), ("DimBlurRadius", 25.0)):
    params.append(node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default))
result = node(unreal.MaterialExpressionCustom, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
              inputs=[custom_input(x) for x in
                      ("Scene", "Visible", "Explored", "Time", "Opacity", "Delay", "UV",
                       "Stencil", "CustomDepth", "SceneDepth", "Bounds", "Feather", "BlurRadius")],
              code="""
float inside = all(UV >= 0) && all(UV <= 1);
float visible = Texture2DSample(Visible, VisibleSampler, UV).r * inside;
uint width, height;
Explored.GetDimensions(width, height);
float2 texSize = float2(width, height);
float2 stampUV = (floor(UV * texSize) + 0.5) / texSize;
visible *= Texture2DSample(Visible, VisibleSampler, stampUV).r >= 0.999;
float stamp = Texture2DSample(Explored, ExploredSampler, stampUV).r;
float known = Texture2DSample(Explored, ExploredSampler, UV).g * inside;
// Bilinear coverage may feather only into a known center, never out of one.
known *= Texture2DSample(Explored, ExploredSampler, stampUV).g >= 0.999;
// Exempt centers first, and never use these pixels as blur sources.
float depthMatch = abs(CustomDepth.r - SceneDepth.r) <= 1;
if (abs(Stencil.r - 1) < 0.1 && depthMatch) return Scene.rgb;
if (Stencil.r >= 2 && Stencil.r <= 17 && depthMatch)
    return Scene.rgb * (1 - Opacity * saturate((Stencil.r - 2) / 15));
if (known <= 0 && visible <= 0) return 0;
float centerVisible = visible;
float vSum = visible, kSum = known;
// Golden-angle disk instead of the square 3x3. Center gates are retained:
// neither LOS nor exploration can grow across a thin wall.
[unroll] for (int i = 0; i < 16; ++i)
{
    float angle = i * 2.39996323;
    float2 disk = float2(cos(angle), sin(angle)) * sqrt((i + 0.5) / 16.0);
    float2 tap = UV + disk * Feather / Bounds.zw;
    float inTap = all(tap >= 0) && all(tap <= 1);
    vSum += Texture2DSample(Visible, VisibleSampler, tap).r * inTap;
    kSum += Texture2DSample(Explored, ExploredSampler, tap).g * inTap;
}
visible *= smoothstep(0.0, 1.0, vSum / 17);
known *= smoothstep(0.0, 1.0, kSum / 17);
if (visible >= 0.999) return Scene.rgb;
float fade = saturate((Time - stamp) / max(Delay, 0.01));
float light = max(visible, known * (1 - Opacity * fade));
float3 color = Scene.rgb;
// Never blur even partially currently-visible centers. Dimming and content
// mixing use precisely the same point timestamp and fade (not feathered age).
[branch] if (centerVisible <= 0 && known > 0 && fade > 0 && BlurRadius > 0)
{
    // Defense in depth for manually supplied MIDs as well as the C++ config clamp.
    float radius = min(BlurRadius, 40.0);
    float3 centerWorld = GetTranslatedWorldPosition(Parameters);
    float2 centerViewport = GetViewportUV(Parameters);
    float3 sum = Scene.rgb;
    float weights = 1;
    [loop] for (int j = 0; j < 16; ++j)
    {
        float angle = j * 2.39996323;
        float2 offset = float2(cos(angle), sin(angle)) * sqrt((j + 0.5) / 16.0) * radius;
        float4 clip = mul(float4(centerWorld + float3(offset, 0), 1), ResolvedView.TranslatedWorldToClip);
        if (clip.w <= 0) continue;
        float2 viewport = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        if (any(viewport <= 0) || any(viewport >= 1)) continue;
        // Point color sampling: bilinear scene filtering could import unknown
        // neighbors before our mask check. Snap to the actual input texel.
        float2 sceneUV = ViewportUVToSceneTextureUV(viewport, PPI_PostProcessInput0);
        float4 sceneSize = GetSceneTextureBufferSize(PPI_PostProcessInput0);
        sceneUV = (floor(sceneUV * sceneSize.xy) + 0.5) * sceneSize.zw;
        viewport = (sceneUV - PostProcessInput_0_UVViewportMin) / PostProcessInput_0_UVViewportSize;
        if (any(viewport <= 0) || any(viewport >= 1)) continue;
        float2 depthUV = ViewportUVToBufferUV(viewport);
        float depth = SceneTextureLookup(depthUV, PPI_SceneDepth, false).r;
        float2 screen = ViewportUVToScreenPos(viewport);
        float3 tapWorld = mul(float4(GetScreenPositionForProjectionType(screen, depth), depth, 1),
                              PrimaryView.ScreenToTranslatedWorld).xyz;
        // Actual depth surface, not projected floor XY. Reject discontinuities
        // (walls/foreground) and samples outside the requested world disk.
        float3 delta = tapWorld - centerWorld;
        if (length(delta.xy) > radius + 1 || abs(delta.z) > 10) continue;
        float2 tapUV = UV + delta.xy / Bounds.zw;
        if (any(tapUV < 0) || any(tapUV > 1)) continue;
        float2 pointUV = (floor(tapUV * texSize) + 0.5) / texSize;
        float tapKnown = Texture2DSample(Explored, ExploredSampler, pointUV).g;
        if (tapKnown < 0.999) continue;
        float tapStencil = SceneTextureLookup(depthUV, PPI_CustomStencil, false).r;
        float tapCustomDepth = SceneTextureLookup(depthUV, PPI_CustomDepth, false).r;
        if (tapStencil >= 1 && tapStencil <= 17 && abs(tapCustomDepth - depth) <= 1) continue;
        // Equal-height explored endpoints can be separated by a thin wall.
        // Check the rasterized segment before importing any endpoint color.
        // Bounded approximation: blockers must cover a probe in scene depth;
        // the 40 cm radius cap limits nominal probe gaps to 10 cm.
        bool blocked = false;
        [unroll] for (int segment = 1; segment <= 3; ++segment)
        {
            float2 midViewport = lerp(centerViewport, viewport, segment * 0.25);
            float2 midDepthUV = ViewportUVToBufferUV(midViewport);
            // Match reconstruction to the depth texel actually sampled.
            float4 depthSize = GetSceneTextureBufferSize(PPI_SceneDepth);
            midDepthUV = (floor(midDepthUV * depthSize.xy) + 0.5) * depthSize.zw;
            midViewport = BufferUVToViewportUV(midDepthUV);
            float midDepth = SceneTextureLookup(midDepthUV, PPI_SceneDepth, false).r;
            float2 midScreen = ViewportUVToScreenPos(midViewport);
            float3 midWorld = mul(float4(GetScreenPositionForProjectionType(midScreen, midDepth), midDepth, 1),
                                  PrimaryView.ScreenToTranslatedWorld).xyz;
            if (midWorld.z - centerWorld.z > 10)
            {
                blocked = true;
                break;
            }
            float midStencil = SceneTextureLookup(midDepthUV, PPI_CustomStencil, false).r;
            float midCustomDepth = SceneTextureLookup(midDepthUV, PPI_CustomDepth, false).r;
            if (midStencil >= 1 && midStencil <= 17 && abs(midCustomDepth - midDepth) <= 1)
            {
                blocked = true;
                break;
            }
            // Also avoid jumping a narrow unknown strip between known floors.
            float2 midUV = UV + (midWorld.xy - centerWorld.xy) / Bounds.zw;
            float2 midPointUV = (floor(midUV * texSize) + 0.5) / texSize;
            if (any(midUV < 0) || any(midUV > 1) ||
                Texture2DSample(Explored, ExploredSampler, midPointUV).g < 0.999)
            {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;
        float weight = tapKnown * Texture2DSample(Explored, ExploredSampler, tapUV).g;
        sum += SceneTextureLookup(Parameters, sceneUV, PPI_PostProcessInput0, false).rgb * weight;
        weights += weight;
    }
    color = lerp(Scene.rgb, sum / weights, fade);
}
return color * light;
""")
wire(scene, result, "Scene", "Color")
wire(preview[0], result, "Visible")
wire(preview[1], result, "Explored")
for param, pin in zip(params, ("Time", "Opacity", "Delay", "Feather", "BlurRadius")):
    wire(param, result, pin)
wire(uv, result, "UV")
wire(stencil, result, "Stencil")
wire(custom_depth, result, "CustomDepth")
wire(scene_depth, result, "SceneDepth")
wire(bounds, result, "Bounds", "RGBA")
if not lib.connect_material_property(result, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("Cannot connect fog output")
lib.layout_material_expressions(mat)
errors = lib.recompile_material(mat)
if errors:
    raise RuntimeError("Fog material compile failed: " + "; ".join(str(error) for error in errors))
if not unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False):
    raise RuntimeError("Cannot save fog material")
# Rings are intentionally composed after the fog. Other world translucency stays
# in the default BeforeDOF pass and is covered by the black/dim layers.
ring = unreal.load_asset("/Game/BrGame/Zone/M_ZoneRing")
if not ring:
    raise RuntimeError("Generate zone assets first")
if ring.get_editor_property("translucency_pass") != unreal.MaterialTranslucencyPass.MTP_AFTER_DOF:
    ring.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_DOF)
    errors = lib.recompile_material(ring)
    if errors:
        raise RuntimeError("Zone ring compile failed: " + "; ".join(str(error) for error in errors))
    if not unreal.EditorAssetLibrary.save_loaded_asset(ring, only_if_is_dirty=False):
        raise RuntimeError("Cannot save zone ring translucency pass")
unreal.log("Br fog assets verified: M_Fog, RT_VisibleMask, RT_ExploredMask; zone rings AfterDOF")
