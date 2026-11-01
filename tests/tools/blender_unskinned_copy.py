"""Blender helper for test_rebind.py: a model's meshes without their skin, turned about the vertical, scaled and moved,
as a mesh made elsewhere (Meshy) arrives.
  blender --background --factory-startup --python blender_unskinned_copy.py -- <in.glb> <out.glb> <yaw deg> <scale>
      <dx> <dy> <dz>"""
import math
import sys

import bpy
from mathutils import Matrix, Vector

a = sys.argv[sys.argv.index("--") + 1:]
src, dst, yaw, scale = a[0], a[1], float(a[2]), float(a[3])
move = Vector((float(a[4]), float(a[5]), float(a[6])))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
shapes = {pb.custom_shape for o in bpy.data.objects if o.type == "ARMATURE" for pb in o.pose.bones if pb.custom_shape}
meshes = [o for o in bpy.data.objects if o.type == "MESH" and o not in shapes]
for o in meshes:
    world = o.matrix_world.copy()
    o.parent = None
    o.matrix_world = world
    for m in list(o.modifiers):
        o.modifiers.remove(m)
    o.vertex_groups.clear()
for o in [o for o in bpy.data.objects if o not in meshes]:
    bpy.data.objects.remove(o)
turn = Matrix.Translation(move) @ Matrix.Rotation(math.radians(yaw), 4, "Z") @ Matrix.Scale(scale, 4)
for o in meshes:
    o.matrix_world = turn @ o.matrix_world
bpy.ops.object.select_all(action="SELECT")
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=True, export_skins=False,
                          export_animations=False, export_yup=True)
print("COPY", len(meshes))
