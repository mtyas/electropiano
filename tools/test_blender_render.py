import bpy
import math

# Clear default scene
bpy.ops.wm.read_factory_settings(use_empty=True)

scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
scene.render.resolution_x = 128
scene.render.resolution_y = 128
scene.render.film_transparent = True
scene.view_settings.view_transform = 'Standard'

# World background dark
world = bpy.data.worlds.new("StudioWorld")
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes.get("Background")
if bg:
    bg.inputs[0].default_value = (0.02, 0.02, 0.02, 1.0)

# Camera (Orthographic top-down with slight angle)
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = 'ORTHO'
cam_data.ortho_scale = 2.4
cam_obj = bpy.data.objects.new("Camera", cam_data)
scene.collection.objects.link(cam_obj)
scene.camera = cam_obj
cam_obj.location = (0, -0.2, 3.0)
cam_obj.rotation_euler = (math.radians(5), 0, 0)

# 3-Point Studio Lighting
def add_light(name, ltype, loc, energy, color):
    ldata = bpy.data.lights.new(name, ltype)
    ldata.energy = energy
    ldata.color = color
    lobj = bpy.data.objects.new(name, ldata)
    lobj.location = loc
    scene.collection.objects.link(lobj)
    return lobj

add_light("KeyLight", 'AREA', (1.5, -1.8, 2.5), 180.0, (1.0, 0.98, 0.95))
add_light("FillLight", 'AREA', (-2.0, -1.0, 1.8), 80.0, (0.85, 0.92, 1.0))
add_light("RimLight", 'POINT', (0.0, 2.0, 2.0), 120.0, (1.0, 1.0, 1.0))

# Create 3D Skirted Knob Base
bpy.ops.mesh.primitive_cylinder_add(radius=1.0, depth=0.35, vertices=64, location=(0, 0, 0))
base_obj = bpy.context.active_object
base_obj.name = "KnobBase"

# Fluted Skirt
mat_skirt = bpy.data.materials.new("BakeliteSkirt")
mat_skirt.use_nodes = True
bsdf = next(n for n in mat_skirt.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
bsdf.inputs['Base Color'].default_value = (0.06, 0.06, 0.07, 1.0)
bsdf.inputs['Roughness'].default_value = 0.25
base_obj.data.materials.append(mat_skirt)

# Top Brushed Aluminum Cap
bpy.ops.mesh.primitive_cylinder_add(radius=0.72, depth=0.12, vertices=64, location=(0, 0, 0.20))
cap_obj = bpy.context.active_object
cap_obj.name = "KnobCap"

mat_cap = bpy.data.materials.new("BrushedAluminum")
mat_cap.use_nodes = True
bsdf_cap = next(n for n in mat_cap.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
bsdf_cap.inputs['Base Color'].default_value = (0.82, 0.84, 0.88, 1.0)
bsdf_cap.inputs['Metallic'].default_value = 0.95
bsdf_cap.inputs['Roughness'].default_value = 0.18
if 'Anisotropic' in bsdf_cap.inputs:
    bsdf_cap.inputs['Anisotropic'].default_value = 0.85
cap_obj.data.materials.append(mat_cap)

# Indicator Pointer Notch
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.42, 0.26))
notch_obj = bpy.context.active_object
notch_obj.scale = (0.06, 0.38, 0.04)

mat_notch = bpy.data.materials.new("NotchInlay")
mat_notch.use_nodes = True
bsdf_notch = next(n for n in mat_notch.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
bsdf_notch.inputs['Base Color'].default_value = (0.95, 0.95, 0.98, 1.0)
bsdf_notch.inputs['Roughness'].default_value = 0.1
notch_obj.data.materials.append(mat_notch)

scene.render.filepath = "renders/test_blender_knob.png"
bpy.ops.render.render(write_still=True)
print("Rendered renders/test_blender_knob.png successfully!")
