"""Blender (headless), for tests: the mean weight on a game animal's tail bones (the subtree of one bone) of a fitted
quadruped's rump and of its own tail.

  blender --background --factory-startup --python blender_tail_weights.py -- <fitted.glb> <tail root bone>

Prints "TAILW <rump> <tail>": the rump is the back fifth of the body between the feet, above mid-height and wider than
a quarter of the body's width from the middle; the tail is everything behind the body's widest part that stays
within 0.1 m of the middle line.
"""
import sys

import bpy

path, root = sys.argv[sys.argv.index("--") + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
mesh = next(o for o in bpy.data.objects if o.type == "MESH" and o.parent is not None)


def subtree(b):
    out = {b.name}
    for c in b.children:
        out |= subtree(c)
    return out


tail_bones = subtree(arm.data.bones[root])
names = {g.index: g.name for g in mesh.vertex_groups}
pts = [(mesh.matrix_world @ v.co, sum(g.weight for g in v.groups if names[g.group] in tail_bones))
       for v in mesh.data.vertices]
xs = [abs(p.x) for p, _ in pts]
width = sorted(xs)[int(0.9 * len(xs))]
zs = [p.z for p, _ in pts]
mid = (min(zs) + max(zs)) / 2
wide = [p.y for p, _ in pts if abs(p.x) > 0.5 * width]
body_back = max(wide)
body_front = min(wide)
L = body_back - body_front
rump = [w for p, w in pts if p.y > body_back - 0.2 * L and p.y <= body_back and p.z > mid and abs(p.x) > 0.25 * width]
tail = [w for p, w in pts if p.y > body_back + 0.05 and abs(p.x) < 0.1]
print("TAILW %.3f %.3f" % (sum(rump) / max(len(rump), 1), sum(tail) / max(len(tail), 1)))
