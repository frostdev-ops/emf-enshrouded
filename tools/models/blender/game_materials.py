"""Put a prop on the game's own materials: every face is sorted into a material slot by the colour of the model's own
texture under it and gets texture coordinates into a region of that game material's atlas (tools/models/atlas_uv.py).
The glb written has one material per slot, named game_material_<slot> (gltf_in maps that name to the slot), no
textures of its own, and only the new texture coordinates. make_mod's prop "materials" then names the game material of
each slot: nothing of the game's art is copied.

  blender --background --factory-startup --python tools/models/blender/game_materials.py -- \\
      <in.glb> <its albedo.png> <look.json> <out.glb>

look.json: {"rules": [{"material": 0, "min_saturation": 0.55, "min_value": 0.45}], "default": 1,
            "materials": {"0": {"repeat": [2.8, 1.4], "region": [u0, v0, u1, v1]}, "1": {...}}}
Prints GAME_MATERIALS {json}: the faces per slot.
"""
import json
import os
import sys

import bmesh
import bpy
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import atlas_uv  # noqa: E402


def main():
    src, albedo, look_path, out = sys.argv[sys.argv.index("--") + 1:][:4]
    with open(look_path, encoding="utf-8") as f:
        look = json.load(f)
    slots = sorted(int(k) for k in look["materials"])
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise SystemExit("no mesh in %s" % src)
    img = bpy.data.images.load(albedo)
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)  # rows from the bottom (Blender)
    mats = [bpy.data.materials.new("game_material_%d" % s) for s in range(max(slots) + 1)]
    counts = {s: 0 for s in slots}
    for obj in meshes:
        me = obj.data
        me.materials.clear()
        for m in mats:
            me.materials.append(m)
        bm = bmesh.new()
        bm.from_mesh(me)
        old = bm.loops.layers.uv.active
        new = bm.loops.layers.uv.new("game")
        mw = obj.matrix_world
        rot = mw.to_3x3()
        for f in bm.faces:
            uvs = [l[old].uv for l in f.loops] if old else [(0.0, 0.0)] * len(f.loops)
            u = sum(x[0] for x in uvs) / len(uvs)
            v = sum(x[1] for x in uvs) / len(uvs)
            r, g, b, _ = px[min(h - 1, max(0, int(v * h))), min(w - 1, max(0, int(u * w)))]
            slot = atlas_uv.classify((float(r), float(g), float(b)), look.get("rules", []), look["default"])
            f.material_index = slot
            counts[slot] = counts.get(slot, 0) + 1
            spec = look["materials"][str(slot)]
            # Blender (x, y, z up) -> glTF (x, z, -y): the space atlas_uv works in
            pts = [mw @ l.vert.co for l in f.loops]
            gl = [(p.x, p.z, -p.y) for p in pts]
            n = rot @ f.normal
            game_uvs = atlas_uv.face_uvs(gl, (n.x, n.z, -n.y), spec["repeat"], spec["region"])
            for l, (gu, gv) in zip(f.loops, game_uvs):
                l[new].uv = (gu, 1.0 - gv)  # Blender's v grows upwards; the glTF export turns it back
        if old:
            bm.loops.layers.uv.remove(old)
        bm.to_mesh(me)
        bm.free()
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_materials="EXPORT", export_texcoords=True,
                              export_normals=True)
    print("GAME_MATERIALS " + json.dumps({"faces": counts, "out": out}))


main()
