import unreal

mesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
if not mesh:
    raise RuntimeError("Mannequin mesh missing")
for entry in mesh.get_editor_property("materials"):
    material = entry.get_editor_property("material_interface")
    unreal.log("BR material {} vectors={}".format(
        material.get_path_name(), unreal.MaterialEditingLibrary.get_vector_parameter_names(material)))
