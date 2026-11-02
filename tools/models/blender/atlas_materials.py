"""Blender (headless): a model whose parts wear several textures (a body, a head, the mouth and eyes) gets one texture:
an atlas of them, and UVs into it. Its skin and bones stay as they are.

  blender --background --factory-startup --python atlas_materials.py -- <in.glb> <out.glb> <atlas.png>
      [--size 2048] [--material N]

Every material in use gets a square cell of a grid (2 x 2 for two to four materials, 3 x 3 up to nine...), in the
order the meshes first use them, left to right and top to bottom. Its base colour texture is scaled into its cell (a
material without one: its base colour), and its faces' UVs are moved into that cell. UVs outside 0-1 (a texture that
repeats) cannot move into a cell: they are clamped and counted. The skinned meshes are joined into one, with that one
UV map and one material (--material N names it game_material_N: the mesh then takes slot N of the source model,
gltf_in.py), which shows the atlas. Writes <atlas.png> (size x size) and <out.glb>; prints ATLAS {json}: size, grid,
cell, materials (name, cell [column, row from the top], image), uv_outside, vertices.
"""
import json
import math
import os
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst, atlas_path = os.path.abspath(argv[0]), os.path.abspath(argv[1]), os.path.abspath(argv[2])
opts = argv[3:]


def opt(name, default=None):
    return opts[opts.index(name) + 1] if name in opts else default


size = int(opt("--size", "2048"))
material_slot = opt("--material")

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
arm = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
shapes = {pb.custom_shape for pb in arm.pose.bones if pb.custom_shape} if arm else set()
for o in shapes:
    bpy.data.objects.remove(o)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
if not meshes:
    raise SystemExit("no mesh in %s" % src)


def upstream_image(socket, seen=None):
    """The image texture node feeding `socket`, through any nodes between (the importer's mixes and conversions)."""
    seen = seen or set()
    for link in socket.links:
        node = link.from_node
        if node.name in seen:
            continue
        seen.add(node.name)
        if node.type == "TEX_IMAGE" and node.image:
            return node
        for inp in node.inputs:
            found = upstream_image(inp, seen)
            if found:
                return found
    return None


def uv_name_of(tex_node, mesh):
    """The UV map the texture node reads (a UV Map node before it), else the mesh's first."""
    vec = tex_node.inputs.get("Vector") if tex_node else None
    if vec and vec.links:
        n = vec.links[0].from_node
        if n.type == "UVMAP" and n.uv_map in mesh.uv_layers:
            return n.uv_map
    return mesh.uv_layers[0].name if len(mesh.uv_layers) else None


def linear_to_srgb(c):
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


# the materials in use, in the order the meshes first use them
order, info = [], {}
for o in meshes:
    for poly in o.data.polygons:
        m = o.data.materials[poly.material_index] if len(o.data.materials) else None
        key = m.name if m else ""
        if key not in info:
            tex, colour = None, (1.0, 1.0, 1.0, 1.0)
            if m and m.node_tree:
                bsdf = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
                if bsdf:
                    tex = upstream_image(bsdf.inputs["Base Color"])
                    colour = tuple(bsdf.inputs["Base Color"].default_value)
                else:  # an unlit material (KHR_materials_unlit: a scan's baked light) draws through an emission
                    emit = next((n for n in m.node_tree.nodes if n.type == "EMISSION"), None)
                    if emit:
                        tex = upstream_image(emit.inputs["Color"])
                        colour = tuple(emit.inputs["Color"].default_value)
            info[key] = {"material": m, "tex": tex, "colour": colour}
            order.append(key)

grid = max(1, math.ceil(math.sqrt(len(order))))
cell = size // grid
pixels = np.zeros((size, size, 4), dtype=np.float32)
pixels[:, :, 3] = 1.0
report_materials = []
for i, key in enumerate(order):
    col, row = i % grid, i // grid             # row from the top of the picture
    cy = grid - 1 - row                        # Blender's images and UVs go bottom-up
    it = info[key]
    it["cell"] = (col, cy)
    if it["tex"]:
        img = it["tex"].image.copy()
        img.scale(cell, cell)
        block = np.empty(cell * cell * 4, dtype=np.float32)
        img.pixels.foreach_get(block)
        pixels[cy * cell:(cy + 1) * cell, col * cell:(col + 1) * cell] = block.reshape(cell, cell, 4)
        bpy.data.images.remove(img)
    else:
        rgb = [linear_to_srgb(c) for c in it["colour"][:3]]
        pixels[cy * cell:(cy + 1) * cell, col * cell:(col + 1) * cell, :3] = rgb
    report_materials.append({"name": key, "cell": [col, row],
                             "image": it["tex"].image.name if it["tex"] else None})

atlas = bpy.data.images.new("atlas", size, size, alpha=False)
atlas.pixels.foreach_set(pixels.ravel())
atlas.filepath_raw = atlas_path
atlas.file_format = "PNG"
atlas.save()

# every face's UVs into its material's cell, in a new map that becomes the only one
outside = 0
for o in meshes:
    mesh = o.data
    if not len(mesh.uv_layers):
        mesh.uv_layers.new(name="UVMap")
    sources = {}
    for key in order:
        m = info[key]["material"]
        if m and m.name in [mm.name for mm in mesh.materials if mm]:
            sources[key] = uv_name_of(info[key]["tex"], mesh)
    old = {layer.name: [tuple(d.uv) for d in layer.data] for layer in mesh.uv_layers}
    new_uv = [(0.5, 0.5)] * len(mesh.loops)
    for poly in mesh.polygons:
        m = mesh.materials[poly.material_index] if len(mesh.materials) else None
        key = m.name if m else ""
        col, cy = info[key]["cell"]
        layer = old.get(sources.get(key) or mesh.uv_layers[0].name)
        for li in poly.loop_indices:
            u, v = layer[li]
            if not (0.0 <= u <= 1.0 and 0.0 <= v <= 1.0):
                outside += 1
                u, v = min(1.0, max(0.0, u)), min(1.0, max(0.0, v))
            new_uv[li] = ((col + u) / grid, (cy + v) / grid)
    while len(mesh.uv_layers):
        mesh.uv_layers.remove(mesh.uv_layers[0])
    layer = mesh.uv_layers.new(name="UVMap")
    for li, uv in enumerate(new_uv):
        layer.data[li].uv = uv

# one mesh, one material that shows the atlas
mat_name = "game_material_%d" % int(material_slot) if material_slot is not None else (order[0] or "atlas")
final = bpy.data.materials.new("atlas_tmp")
final.use_nodes = True
bsdf = next(n for n in final.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
tex = final.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = atlas
final.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
for o in meshes:
    o.data.materials.clear()
    o.data.materials.append(final)
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
body = bpy.context.view_layer.objects.active
for m in [m for m in bpy.data.materials if m is not final]:
    bpy.data.materials.remove(m)
final.name = mat_name

bpy.ops.object.select_all(action="DESELECT")
body.select_set(True)
if arm:
    arm.select_set(True)
bpy.context.view_layer.objects.active = arm or body
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", use_selection=True, export_skins=True,
                          export_tangents=True, export_animations=False, export_yup=True)
print("ATLAS " + json.dumps({"size": size, "grid": grid, "cell": cell, "materials": report_materials,
                             "uv_outside": outside, "vertices": len(body.data.vertices)}))
