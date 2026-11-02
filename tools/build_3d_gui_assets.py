import bpy
import math
import os

os.makedirs("resources/gui", exist_ok=True)

# Helper: Clear scene
def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    scene.render.film_transparent = True
    scene.view_settings.view_transform = 'Standard'

    world = bpy.data.worlds.new("World")
    scene.world = world

    # Lights
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

    # Camera
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = 'ORTHO'
    cam_obj = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj
    return scene, cam_obj, cam_data

# ==============================================================================
# 1. RENDER 64-FRAME 3D KNOB FILMSTRIP (Rhodes Fluted Skirt + Spun Aluminum Cap)
# ==============================================================================
def render_knob_filmstrip():
    print("Building 3D Knob Model in Blender...")
    scene, cam_obj, cam_data = reset_scene()
    scene.render.resolution_x = 96
    scene.render.resolution_y = 96
    cam_data.ortho_scale = 2.45
    cam_obj.location = (0, -0.15, 3.0)
    cam_obj.rotation_euler = (math.radians(3), 0, 0)

    # 1. Fluted Bakelite Skirt (Outer ring with fluted ridges)
    bpy.ops.mesh.primitive_cylinder_add(radius=1.0, depth=0.38, vertices=48, location=(0, 0, 0))
    skirt_obj = bpy.context.active_object
    skirt_obj.name = "Skirt"

    mat_skirt = bpy.data.materials.new("Bakelite")
    bsdf_skirt = next(n for n in mat_skirt.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_skirt.inputs['Base Color'].default_value = (0.05, 0.05, 0.06, 1.0)
    bsdf_skirt.inputs['Roughness'].default_value = 0.22
    skirt_obj.data.materials.append(mat_skirt)

    # 2. Chrome Bezel Ring
    bpy.ops.mesh.primitive_cylinder_add(radius=0.76, depth=0.18, vertices=64, location=(0, 0, 0.16))
    bezel_obj = bpy.context.active_object
    mat_bezel = bpy.data.materials.new("Chrome")
    bsdf_bezel = next(n for n in mat_bezel.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_bezel.inputs['Base Color'].default_value = (0.92, 0.93, 0.95, 1.0)
    bsdf_bezel.inputs['Metallic'].default_value = 1.0
    bsdf_bezel.inputs['Roughness'].default_value = 0.08
    bezel_obj.data.materials.append(mat_bezel)

    # 3. Spun Aluminum Center Cap
    bpy.ops.mesh.primitive_cylinder_add(radius=0.70, depth=0.12, vertices=64, location=(0, 0, 0.22))
    cap_obj = bpy.context.active_object
    mat_cap = bpy.data.materials.new("SpunAluminum")
    bsdf_cap = next(n for n in mat_cap.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_cap.inputs['Base Color'].default_value = (0.78, 0.80, 0.84, 1.0)
    bsdf_cap.inputs['Metallic'].default_value = 0.95
    bsdf_cap.inputs['Roughness'].default_value = 0.25
    if 'Anisotropic' in bsdf_cap.inputs:
        bsdf_cap.inputs['Anisotropic'].default_value = 0.9
    cap_obj.data.materials.append(mat_cap)

    # 4. Indicator Pointer Notch (Rotating group)
    pointer_parent = bpy.data.objects.new("PointerRig", None)
    scene.collection.objects.link(pointer_parent)

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.42, 0.30))
    notch = bpy.context.active_object
    notch.scale = (0.065, 0.36, 0.06)
    notch.parent = pointer_parent

    mat_notch = bpy.data.materials.new("PointerWhite")
    bsdf_notch = next(n for n in mat_notch.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_notch.inputs['Base Color'].default_value = (0.98, 0.98, 1.0, 1.0)
    bsdf_notch.inputs['Roughness'].default_value = 0.1
    notch.data.materials.append(mat_notch)

    # Render 64 rotation frames (sweep 270 deg: from -135 to +135 deg)
    num_frames = 64
    start_angle = math.radians(135)
    end_angle = math.radians(-135)

    frame_paths = []
    print(f"Rendering {num_frames} 3D knob frames...")
    for f in range(num_frames):
        norm = f / float(num_frames - 1)
        angle = start_angle + norm * (end_angle - start_angle)
        pointer_parent.rotation_euler = (0, 0, angle)
        
        path = f"resources/gui/knob_frame_{f:02d}.png"
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        frame_paths.append(path)

    print("Stitching 64 frames into vertical filmstrip knob_filmstrip.png...")
    # Stitch frames into one vertical strip using PIL / Cocoa / python
    from PIL import Image
    frame_imgs = [Image.open(p) for p in frame_paths]
    strip = Image.new("RGBA", (96, 96 * num_frames))
    for i, im in enumerate(frame_imgs):
        strip.paste(im, (0, i * 96))
        im.close()
    strip.save("resources/gui/knob_filmstrip.png")
    print("Saved resources/gui/knob_filmstrip.png!")

    # Clean up single frame files
    for p in frame_paths:
        try: os.remove(p)
        except: pass

# ==============================================================================
# 2. RENDER 3D VINTAGE CONSOLE FADER / SLIDER CAP
# ==============================================================================
def render_slider_cap():
    print("Building 3D Slider Fader Cap in Blender...")
    scene, cam_obj, cam_data = reset_scene()
    scene.render.resolution_x = 64
    scene.render.resolution_y = 96
    cam_data.ortho_scale = 1.6
    cam_obj.location = (0, 0, 3.0)
    cam_obj.rotation_euler = (0, 0, 0)

    # Sculpted Fader Cap Body
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0))
    cap = bpy.context.active_object
    cap.scale = (0.55, 0.85, 0.35)

    mat_fader = bpy.data.materials.new("FaderMat")
    bsdf_fader = next(n for n in mat_fader.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_fader.inputs['Base Color'].default_value = (0.12, 0.13, 0.16, 1.0)
    bsdf_fader.inputs['Metallic'].default_value = 0.5
    bsdf_fader.inputs['Roughness'].default_value = 0.28
    cap.data.materials.append(mat_fader)

    # Center White Indicator Line
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

# ==============================================================================
# 3. RENDER 3D VINTAGE ROCKER SWITCHES (Active vs Inactive)
# ==============================================================================
def render_rocker_switches():
    print("Building 3D Rocker Switches in Blender...")
    scene, cam_obj, cam_data = reset_scene()
    scene.render.resolution_x = 96
    scene.render.resolution_y = 48
    cam_data.ortho_scale = 2.2
    cam_obj.location = (0, 0, 3.0)

    # Inactive Rocker (flat/subtle right tilt)
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

    # Active Rocker (tilted left with illuminated blue rim)
    rocker.rotation_euler = (0, math.radians(-8), 0)
    bsdf.inputs['Base Color'].default_value = (0.15, 0.28, 0.42, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.2

    scene.render.filepath = "resources/gui/rocker_active_3d.png"
    bpy.ops.render.render(write_still=True)
    print("Saved 3D rocker buttons!")

render_knob_filmstrip()
render_slider_cap()
render_rocker_switches()
print("ALL 3D BLENDER ASSETS RENDERED SUCCESSFULLY!")
