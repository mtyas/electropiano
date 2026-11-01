import bpy
import math

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    scene.render.film_transparent = True
    scene.view_settings.view_transform = 'Standard'

    world = bpy.data.worlds.new("World")
    scene.world = world

    def add_light(name, ltype, loc, energy, color):
        ldata = bpy.data.lights.new(name, ltype)
        ldata.energy = energy
        ldata.color = color
        lobj = bpy.data.objects.new(name, ldata)
        lobj.location = loc
        scene.collection.objects.link(lobj)
        return lobj

    add_light("Key", 'AREA', (2.0, -2.5, 3.5), 320.0, (1.0, 0.98, 0.96))
    add_light("Fill", 'AREA', (-2.5, -1.5, 2.5), 140.0, (0.88, 0.94, 1.0))
    add_light("Rim", 'POINT', (0.0, 2.5, 2.5), 200.0, (1.0, 1.0, 1.0))

    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = 'ORTHO'
    cam_obj = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj
    return scene, cam_obj, cam_data

def render_slider_cap():
    print("Building 3D Slider Fader Cap in Blender...")
    scene, cam_obj, cam_data = reset_scene()
    scene.render.resolution_x = 64
    scene.render.resolution_y = 96
    cam_data.ortho_scale = 1.6
    cam_obj.location = (0, 0, 3.0)

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0))
    cap = bpy.context.active_object
    cap.scale = (0.55, 0.85, 0.35)

    mat_fader = bpy.data.materials.new("FaderMat")
    bsdf_fader = next(n for n in mat_fader.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_fader.inputs['Base Color'].default_value = (0.12, 0.13, 0.16, 1.0)
    bsdf_fader.inputs['Metallic'].default_value = 0.5
    bsdf_fader.inputs['Roughness'].default_value = 0.28
    cap.data.materials.append(mat_fader)

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0.36))
    line = bpy.context.active_object
    line.scale = (0.45, 0.08, 0.04)

    mat_line = bpy.data.materials.new("FaderLine")
    bsdf_line = next(n for n in mat_line.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_line.inputs['Base Color'].default_value = (0.95, 0.95, 0.98, 1.0)
    line.data.materials.append(mat_line)

    scene.render.filepath = "resources/gui/slider_cap_3d.png"
    bpy.ops.render.render(write_still=True)
    print("Saved resources/gui/slider_cap_3d.png!")

def render_rocker_switches():
    print("Building 3D Rocker Switches in Blender...")
    scene, cam_obj, cam_data = reset_scene()
    scene.render.resolution_x = 96
    scene.render.resolution_y = 48
    cam_data.ortho_scale = 2.2
    cam_obj.location = (0, 0, 3.0)

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0))
    rocker = bpy.context.active_object
    rocker.scale = (0.95, 0.42, 0.25)
    rocker.rotation_euler = (0, math.radians(6), 0)

    mat = bpy.data.materials.new("RockerDark")
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs['Base Color'].default_value = (0.10, 0.11, 0.14, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.3
    rocker.data.materials.append(mat)

    scene.render.filepath = "resources/gui/rocker_inactive_3d.png"
    bpy.ops.render.render(write_still=True)

    rocker.rotation_euler = (0, math.radians(-8), 0)
    bsdf.inputs['Base Color'].default_value = (0.16, 0.32, 0.48, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.2

    scene.render.filepath = "resources/gui/rocker_active_3d.png"
    bpy.ops.render.render(write_still=True)
    print("Saved 3D rocker buttons!")

render_slider_cap()
render_rocker_switches()
print("BLENDER CONTROLS DONE!")
