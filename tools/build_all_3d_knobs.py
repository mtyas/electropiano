import bpy
import math
import os
import sys

os.makedirs("resources/gui", exist_ok=True)
os.makedirs("resources/gui/frames", exist_ok=True)

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    
    # Select available engine
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    if 'BLENDER_EEVEE_NEXT' in engines:
        scene.render.engine = 'BLENDER_EEVEE_NEXT'
    elif 'BLENDER_EEVEE' in engines:
        scene.render.engine = 'BLENDER_EEVEE'
    elif 'CYCLES' in engines:
        scene.render.engine = 'CYCLES'
    
    scene.render.film_transparent = True
    scene.view_settings.view_transform = 'Standard'
    
    world = bpy.data.worlds.new("World")
    scene.world = world

    # Multi-point Studio Key, Fill, Rim lighting
    def add_light(name, ltype, loc, energy, color):
        ldata = bpy.data.lights.new(name, ltype)
        ldata.energy = energy
        ldata.color = color
        lobj = bpy.data.objects.new(name, ldata)
        lobj.location = loc
        scene.collection.objects.link(lobj)
        return lobj

    add_light("Key", 'AREA', (2.2, -2.5, 3.8), 350.0, (1.0, 0.98, 0.95))
    add_light("Fill", 'AREA', (-2.8, -1.8, 2.5), 160.0, (0.88, 0.94, 1.0))
    add_light("RimTop", 'POINT', (0.0, 2.8, 3.0), 220.0, (1.0, 1.0, 1.0))
    add_light("Glance", 'POINT', (1.8, 1.0, 1.5), 90.0, (0.95, 0.95, 1.0))

    # Orthographic top-down camera with slight 3.5 deg tilt for 3D depth perspective
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = 2.45
    cam_obj = bpy.data.objects.new("Cam", cam_data)
    cam_obj.location = (0, -0.15, 3.0)
    cam_obj.rotation_euler = (math.radians(3.5), 0, 0)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj
    return scene, cam_obj, cam_data

def render_filmstrip_frames(pointer_parent, prefix, num_frames=64):
    scene = bpy.context.scene
    scene.render.resolution_x = 96
    scene.render.resolution_y = 96
    start_angle = math.radians(135)
    end_angle = math.radians(-135)

    print(f"Rendering 64 frames for {prefix}...")
    for f in range(num_frames):
        norm = f / float(num_frames - 1)
        angle = start_angle + norm * (end_angle - start_angle)
        pointer_parent.rotation_euler = (0, 0, angle)
        
        path = f"resources/gui/frames/{prefix}_{f:02d}.png"
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
    print(f"Finished {prefix} frames.")

# ==============================================================================
# KNOB 1: RHODES SKIRTED BAKELITE (Piano Mechanics & Tine)
# ==============================================================================
def build_knob_mechanics():
    scene, cam_obj, cam_data = reset_scene()
    
    # 1. Fluted Bakelite Skirt
    bpy.ops.mesh.primitive_cylinder_add(radius=1.0, depth=0.38, vertices=48, location=(0, 0, 0))
    skirt = bpy.context.active_object
    mat_skirt = bpy.data.materials.new("Bakelite")
    bsdf = next(n for n in mat_skirt.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs['Base Color'].default_value = (0.05, 0.05, 0.06, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.22
    skirt.data.materials.append(mat_skirt)

    # 2. Chrome Bezel
    bpy.ops.mesh.primitive_cylinder_add(radius=0.76, depth=0.18, vertices=64, location=(0, 0, 0.16))
    bezel = bpy.context.active_object
    mat_bezel = bpy.data.materials.new("Chrome")
    bsdf_b = next(n for n in mat_bezel.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_b.inputs['Base Color'].default_value = (0.92, 0.93, 0.96, 1.0)
    bsdf_b.inputs['Metallic'].default_value = 1.0
    bsdf_b.inputs['Roughness'].default_value = 0.08
    bezel.data.materials.append(mat_bezel)

    # 3. Spun Aluminum Cap
    bpy.ops.mesh.primitive_cylinder_add(radius=0.70, depth=0.12, vertices=64, location=(0, 0, 0.22))
    cap = bpy.context.active_object
    mat_cap = bpy.data.materials.new("SpunAluminum")
    bsdf_c = next(n for n in mat_cap.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_c.inputs['Base Color'].default_value = (0.78, 0.80, 0.84, 1.0)
    bsdf_c.inputs['Metallic'].default_value = 0.95
    bsdf_c.inputs['Roughness'].default_value = 0.25
    cap.data.materials.append(mat_cap)

    # Pointer Rig
    pointer_parent = bpy.data.objects.new("PointerRig1", None)
    scene.collection.objects.link(pointer_parent)

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.42, 0.30))
    notch = bpy.context.active_object
    notch.scale = (0.065, 0.36, 0.06)
    notch.parent = pointer_parent
    mat_notch = bpy.data.materials.new("WhitePointer")
    bsdf_n = next(n for n in mat_notch.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_n.inputs['Base Color'].default_value = (0.98, 0.98, 1.0, 1.0)
    notch.data.materials.append(mat_notch)

    render_filmstrip_frames(pointer_parent, "knob_mechanics")

# ==============================================================================
# KNOB 2: PRECISION KNURLED NICKEL / CHAMPAGNE (Pickup & EQ)
# ==============================================================================
def build_knob_pickup():
    scene, cam_obj, cam_data = reset_scene()

    # 1. Knurled Cylinder Body
    bpy.ops.mesh.primitive_cylinder_add(radius=0.88, depth=0.55, vertices=64, location=(0, 0, 0))
    body = bpy.context.active_object
    mat_body = bpy.data.materials.new("ChampagneNickel")
    bsdf = next(n for n in mat_body.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs['Base Color'].default_value = (0.84, 0.80, 0.73, 1.0) # Warm champagne nickel
    bsdf.inputs['Metallic'].default_value = 0.92
    bsdf.inputs['Roughness'].default_value = 0.28
    body.data.materials.append(mat_body)

    # 2. Beveled Top Inset Disc
    bpy.ops.mesh.primitive_cylinder_add(radius=0.74, depth=0.10, vertices=64, location=(0, 0, 0.25))
    top_disc = bpy.context.active_object
    mat_disc = bpy.data.materials.new("BrushedTop")
    bsdf_d = next(n for n in mat_disc.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_d.inputs['Base Color'].default_value = (0.90, 0.88, 0.83, 1.0)
    bsdf_d.inputs['Metallic'].default_value = 0.96
    bsdf_d.inputs['Roughness'].default_value = 0.18
    top_disc.data.materials.append(mat_disc)

    # 3. Thin Chrome Perimeter Ring
    bpy.ops.mesh.primitive_torus_add(major_radius=0.80, minor_radius=0.03, major_segments=48, minor_segments=16, location=(0, 0, 0.28))
    ring = bpy.context.active_object
    mat_ring = bpy.data.materials.new("RingChrome")
    bsdf_r = next(n for n in mat_ring.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_r.inputs['Base Color'].default_value = (0.95, 0.95, 0.98, 1.0)
    bsdf_r.inputs['Metallic'].default_value = 1.0
    bsdf_r.inputs['Roughness'].default_value = 0.05
    ring.data.materials.append(mat_ring)

    # Pointer: Precision Machined Black Enamel Index Notch with Diamond Tip
    pointer_parent = bpy.data.objects.new("PointerRig2", None)
    scene.collection.objects.link(pointer_parent)

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.40, 0.32))
    notch = bpy.context.active_object
    notch.scale = (0.055, 0.32, 0.05)
    notch.parent = pointer_parent
    mat_p = bpy.data.materials.new("BlackEnamel")
    bsdf_p = next(n for n in mat_p.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_p.inputs['Base Color'].default_value = (0.03, 0.03, 0.04, 1.0)
    bsdf_p.inputs['Roughness'].default_value = 0.1
    notch.data.materials.append(mat_p)

    render_filmstrip_frames(pointer_parent, "knob_pickup")

# ==============================================================================
# KNOB 3: VINTAGE TWEED / PLEXI AMP POINTER KNOB (Preamp Drive)
# ==============================================================================
def build_knob_drive():
    scene, cam_obj, cam_data = reset_scene()

    # 1. Base Fluted Body (Vintage Black Phenolic with gold center)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.82, depth=0.45, vertices=48, location=(0, 0, 0))
    base = bpy.context.active_object
    mat_base = bpy.data.materials.new("DarkAmp")
    bsdf_b = next(n for n in mat_base.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_b.inputs['Base Color'].default_value = (0.06, 0.06, 0.07, 1.0)
    bsdf_b.inputs['Roughness'].default_value = 0.25
    base.data.materials.append(mat_base)

    # 2. Gleaming Spun Gold / Brass Cap
    bpy.ops.mesh.primitive_cylinder_add(radius=0.62, depth=0.12, vertices=64, location=(0, 0, 0.20))
    gold_cap = bpy.context.active_object
    mat_gold = bpy.data.materials.new("SpunGold")
    bsdf_g = next(n for n in mat_gold.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_g.inputs['Base Color'].default_value = (0.92, 0.74, 0.28, 1.0) # Warm vintage brass/gold
    bsdf_g.inputs['Metallic'].default_value = 0.96
    bsdf_g.inputs['Roughness'].default_value = 0.22
    gold_cap.data.materials.append(mat_gold)

    # Pointer: Heavy High-Contrast White Wedge Needle
    pointer_parent = bpy.data.objects.new("PointerRig3", None)
    scene.collection.objects.link(pointer_parent)

    bpy.ops.mesh.primitive_cone_add(radius1=0.08, radius2=0.02, depth=0.45, location=(0, 0.36, 0.28))
    needle = bpy.context.active_object
    needle.rotation_euler = (math.radians(-90), 0, 0)
    needle.scale = (0.7, 0.3, 1.0)
    needle.parent = pointer_parent
    mat_n = bpy.data.materials.new("WhiteWedge")
    bsdf_n = next(n for n in mat_n.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_n.inputs['Base Color'].default_value = (0.98, 0.98, 0.95, 1.0)
    bsdf_n.inputs['Roughness'].default_value = 0.15
    needle.data.materials.append(mat_n)

    render_filmstrip_frames(pointer_parent, "knob_drive")

# ==============================================================================
# KNOB 4: VINTAGE ANALOG STOMPBOX PEDAL KNOB (Tremolo, Chorus, Phaser)
# ==============================================================================
def build_knob_modulation():
    scene, cam_obj, cam_data = reset_scene()

    # 1. Fluted Stompbox Body (Ribbed matte rubberized texture)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.84, depth=0.48, vertices=32, location=(0, 0, 0))
    body = bpy.context.active_object
    mat_body = bpy.data.materials.new("StompRubber")
    bsdf = next(n for n in mat_body.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs['Base Color'].default_value = (0.10, 0.11, 0.13, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.38
    body.data.materials.append(mat_body)

    # 2. Electric Cyan / Teal Anodized Accent Cap
    bpy.ops.mesh.primitive_cylinder_add(radius=0.66, depth=0.10, vertices=48, location=(0, 0, 0.22))
    cap = bpy.context.active_object
    mat_cap = bpy.data.materials.new("CyanCap")
    bsdf_c = next(n for n in mat_cap.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_c.inputs['Base Color'].default_value = (0.16, 0.58, 0.72, 1.0) # Boutique electric cyan
    bsdf_c.inputs['Metallic'].default_value = 0.75
    bsdf_c.inputs['Roughness'].default_value = 0.26
    cap.data.materials.append(mat_cap)

    # Pointer: Bright White Ribbed Indicator Line
    pointer_parent = bpy.data.objects.new("PointerRig4", None)
    scene.collection.objects.link(pointer_parent)

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.40, 0.30))
    line = bpy.context.active_object
    line.scale = (0.07, 0.36, 0.08)
    line.parent = pointer_parent
    mat_l = bpy.data.materials.new("WhiteLine")
    bsdf_l = next(n for n in mat_l.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_l.inputs['Base Color'].default_value = (1.0, 1.0, 1.0, 1.0)
    bsdf_l.inputs['Roughness'].default_value = 0.1
    line.data.materials.append(mat_l)

    render_filmstrip_frames(pointer_parent, "knob_modulation")

# ==============================================================================
# KNOB 5: STUDIO CONSOLE BILLET ALUMINUM WITH JEWEL PIP (Reverb & Master)
# ==============================================================================
def build_knob_reverb():
    scene, cam_obj, cam_data = reset_scene()

    # 1. Solid Milled Aluminum Body with 45-degree chamfer
    bpy.ops.mesh.primitive_cylinder_add(radius=0.88, depth=0.50, vertices=64, location=(0, 0, 0))
    body = bpy.context.active_object
    mat_body = bpy.data.materials.new("MilledAluminum")
    bsdf = next(n for n in mat_body.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs['Base Color'].default_value = (0.82, 0.84, 0.88, 1.0)
    bsdf.inputs['Metallic'].default_value = 0.98
    bsdf.inputs['Roughness'].default_value = 0.14
    body.data.materials.append(mat_body)

    # 2. Recessed Top Center Disc
    bpy.ops.mesh.primitive_cylinder_add(radius=0.72, depth=0.08, vertices=64, location=(0, 0, 0.23))
    center_disc = bpy.context.active_object
    mat_disc = bpy.data.materials.new("InnerDisc")
    bsdf_d = next(n for n in mat_disc.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_d.inputs['Base Color'].default_value = (0.24, 0.26, 0.30, 1.0)
    bsdf_d.inputs['Roughness'].default_value = 0.22
    center_disc.data.materials.append(mat_disc)

    # Pointer: Precision Pointer with Glowing Jewel Pip
    pointer_parent = bpy.data.objects.new("PointerRig5", None)
    scene.collection.objects.link(pointer_parent)

    # Slit line
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.38, 0.28))
    slit = bpy.context.active_object
    slit.scale = (0.05, 0.28, 0.04)
    slit.parent = pointer_parent
    mat_s = bpy.data.materials.new("SilverSlit")
    bsdf_s = next(n for n in mat_s.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_s.inputs['Base Color'].default_value = (0.95, 0.95, 0.98, 1.0)
    bsdf_s.inputs['Metallic'].default_value = 1.0
    slit.data.materials.append(mat_s)

    # Illuminated Jewel Pip at perimeter tip
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.08, segments=24, ring_count=16, location=(0, 0.58, 0.28))
    pip = bpy.context.active_object
    pip.scale = (1.0, 1.0, 0.6)
    pip.parent = pointer_parent
    mat_pip = bpy.data.materials.new("JewelPip")
    bsdf_p = next(n for n in mat_pip.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf_p.inputs['Base Color'].default_value = (0.1, 0.8, 1.0, 1.0)
    if 'Emission Color' in bsdf_p.inputs:
        bsdf_p.inputs['Emission Color'].default_value = (0.1, 0.85, 1.0, 1.0)
        bsdf_p.inputs['Emission Strength'].default_value = 2.5
    pip.data.materials.append(mat_pip)

    render_filmstrip_frames(pointer_parent, "knob_reverb")

print("Rendering 5 Distinct Styles of 3D Knobs...")
build_knob_mechanics()
build_knob_pickup()
build_knob_drive()
build_knob_modulation()
build_knob_reverb()
print("ALL 5 KNOB TYPES RENDERED SUCCESSFULLY!")
