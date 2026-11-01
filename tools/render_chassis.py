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

    add_light("Key", 'AREA', (0.0, -3.0, 4.0), 380.0, (1.0, 0.98, 0.96))
    add_light("Fill", 'AREA', (-3.0, 0.0, 3.0), 180.0, (0.85, 0.92, 1.0))
    add_light("Rim", 'AREA', (3.0, 2.0, 3.0), 160.0, (1.0, 1.0, 1.0))

    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = 'ORTHO'
    cam_obj = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj
    return scene, cam_obj, cam_data

def render_panel_card():
    scene, cam_obj, cam_data = reset_scene()
    scene.render.resolution_x = 312
    scene.render.resolution_y = 312
    cam_data.ortho_scale = 3.2
    cam_obj.location = (0, 0, 3.0)

    # Beveled card with chamfer
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0))
    panel = bpy.context.active_object
    panel.scale = (1.50, 1.50, 0.12)

    mat = bpy.data.materials.new("PanelCard")
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs['Base Color'].default_value = (0.07, 0.08, 0.10, 1.0)
    bsdf.inputs['Metallic'].default_value = 0.45
    bsdf.inputs['Roughness'].default_value = 0.32
    panel.data.materials.append(mat)

    scene.render.filepath = "resources/gui/panel_card_3d.png"
    bpy.ops.render.render(write_still=True)
    print("Saved resources/gui/panel_card_3d.png!")

render_panel_card()
