"""Blender (headless), for tests: the size of the skinned mesh in a .glb (or of all meshes when none is skinned).

  blender --background --factory-startup --python blender_bbox.py -- <file.glb>

Prints "BBOX <x> <y> <z>": the mesh's extent along each axis (Blender axes after the glTF import)."""
import sys

import bpy

path = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
meshes = [o for o in bpy.data.objects if o.type == "MESH" and o.parent is not None and o.parent.type == "ARMATURE"]
meshes = meshes or [o for o in bpy.data.objects if o.type == "MESH"]  # an unskinned figure: every mesh
pts = [o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
print("BBOX %.6f %.6f %.6f" % tuple(max(getattr(p, a) for p in pts) - min(getattr(p, a) for p in pts) for a in "xyz"))
