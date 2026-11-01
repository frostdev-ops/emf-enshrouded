"""Blender (headless), for tests: the total skin weight on every bone of a fitted .glb's mesh.

  blender --background --factory-startup --python blender_bone_weights.py -- <fitted.glb>

Prints "BW <bone> <total weight>" for each bone."""
import sys

import bpy

path = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
total = {}
for o in bpy.data.objects:
    if o.type == "MESH" and o.parent is arm:
        names = {g.index: g.name for g in o.vertex_groups}
        for v in o.data.vertices:
            for g in v.groups:
                total[names[g.group]] = total.get(names[g.group], 0.0) + g.weight
for b in arm.data.bones:
    print("BW %s %.4f" % (b.name, total.get(b.name, 0.0)))
