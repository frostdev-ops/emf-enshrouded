"""Blender (headless): pictures of a model in its rest pose from several sides, as input for Meshy's image-to-3D.

  blender --background --factory-startup --python render_views.py -- <model.glb> <out_dir> [--size 1024]
      [--views side,three_quarter,front]

Views: side (the model's left), three_quarter (front left, a little from above), front, back, top. After a glTF
import the model faces -Y (glTF and the game face +Z). Each view is an orthographic picture (no perspective: the
proportions as they are, which is what a model made from the pictures should match) on a plain light background, the
model centred and spanning about 82% of its width or height, written to <out_dir>/<view>.png. Prints VIEWS [json]: per
view the file, its size, the share of the picture the model covers and how much of the picture's width or height it
spans (fill).
"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, out_dir = os.path.abspath(argv[0]), os.path.abspath(argv[1])
opts = argv[2:]


def opt(name, default=None):
    return opts[opts.index(name) + 1] if name in opts else default


size = int(opt("--size", "1024"))
names = opt("--views", "side,three_quarter,front").split(",")
DIRECTIONS = {"side": (1.0, 0.0, 0.08), "three_quarter": (0.72, -0.62, 0.3), "front": (0.0, -1.0, 0.08),
              "back": (0.0, 1.0, 0.08), "top": (0.0, -0.05, 1.0)}
for n in names:
    if n not in DIRECTIONS:
        raise SystemExit("no view %r (the views: %s)" % (n, ", ".join(DIRECTIONS)))
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
scene = bpy.context.scene
# the importer draws bones with a shape mesh (an icosphere): not part of the model
shapes = {pb.custom_shape for o in scene.objects if o.type == "ARMATURE" for pb in o.pose.bones if pb.custom_shape}
for o in shapes:
    o.hide_render = True
meshes = [o for o in scene.objects if o.type == "MESH" and o not in shapes and not o.hide_render]
if not meshes:
    raise SystemExit("no mesh in %s" % src)
depsgraph = bpy.context.evaluated_depsgraph_get()
pts = []
for o in meshes:
    ev = o.evaluated_get(depsgraph)
    me = ev.to_mesh()
    pts += [ev.matrix_world @ v.co for v in me.vertices]
    ev.to_mesh_clear()
lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
centre = (lo + hi) / 2

scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "TEXTURE" if any(i.source == "FILE" for i in bpy.data.images) else "MATERIAL"
scene.view_settings.view_transform = "Standard"
world = bpy.data.worlds.new("plain")
world.color = (0.82, 0.82, 0.82)
scene.world = world
scene.render.resolution_x = scene.render.resolution_y = size
scene.render.film_transparent = False
cam_data = bpy.data.cameras.new("cam")
cam_data.type = "ORTHO"
cam = bpy.data.objects.new("cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
FILL = 0.82
reach = max((p - centre).length for p in pts)

views = []
for n in names:
    d = Vector(DIRECTIONS[n]).normalized()
    right = (-d).cross(Vector((0.0, 0.0, 1.0)))
    if right.length < 1e-6:
        right = Vector((1.0, 0.0, 0.0))
    right.normalize()
    up = right.cross(-d)
    xs = [(q - centre).dot(right) for q in pts]
    ys = [(q - centre).dot(up) for q in pts]
    aim = centre + right * ((min(xs) + max(xs)) / 2) + up * ((min(ys) + max(ys)) / 2)
    cam_data.ortho_scale = max(max(xs) - min(xs), max(ys) - min(ys)) / FILL
    cam_data.clip_end = 4 * reach + 10
    cam.location = aim + d * (2 * reach + 1)
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    path = os.path.join(out_dir, n + ".png")
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path)
    px = list(img.pixels)
    w, h = img.size
    bg = px[0:3]
    hit = [i // 4 for i in range(0, len(px), 4)
           if abs(px[i] - bg[0]) + abs(px[i + 1] - bg[1]) + abs(px[i + 2] - bg[2]) > 0.06]
    xs, ys = [k % w for k in hit], [k // w for k in hit]
    fill = max((max(xs) - min(xs) + 1) / float(w), (max(ys) - min(ys) + 1) / float(h)) if hit else 0.0
    cam_up = cam.matrix_world.to_3x3() @ Vector((0.0, 1.0, 0.0))
    roll = math.degrees(math.atan2(cam_up.dot(right), cam_up.dot(up)))
    views.append({"view": n, "file": path, "width": w, "height": h, "covered": round(len(hit) / float(w * h), 4),
                  "fill": round(fill, 3), "roll": round(roll, 2)})
    bpy.data.images.remove(img)
print("VIEWS " + json.dumps(views))
