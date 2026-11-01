"""Blender (headless): a mesh made elsewhere for a rig (by Meshy, from pictures of the rig's rest pose) put on that rig:
lined up with the rig's own mesh and skinned with its weights.

  blender --background --factory-startup --python rebind_to_rig.py -- <rig.glb> <mesh.glb> <out.glb>
      [--yaw auto|0|90|180|270] [--icp 25] [--material N]

rig.glb: the rig with its own skinned mesh (new_skeleton.write_glb's copy: bones named jNNN_<name>). mesh.glb: the new
mesh, upright, at any size, place and turn about the vertical. The new mesh is turned (each quarter turn, or --yaw),
scaled to the rig mesh's height and set on it, then fitted closer (iterative closest points with one uniform scale);
the turn whose fit lies closest wins. Every vertex then takes the weights of the nearest point on the rig's mesh
(interpolated over that face), at most four joints, normalised. The rig's own mesh is dropped and the new mesh is
exported on the rig with its materials (--material N names them game_material_N: the mesh then takes slot N of
the source model, gltf_in.py). Prints REBIND {json}: yaw, scale, the mean and largest distance from the new
mesh to the rig's mesh, the mean distance back (rig mesh to new mesh: parts the new mesh lacks), vertices, and
vertices left without weights.
"""
import json
import math
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:]
rig_path, mesh_path, out_path = argv[0], argv[1], argv[2]
opts = argv[3:]


def opt(name, default=None):
    return opts[opts.index(name) + 1] if name in opts else default


yaw_opt = opt("--yaw", "auto")
rounds = int(opt("--icp", "25"))
material_slot = opt("--material")
yaws = [0, 90, 180, 270] if yaw_opt == "auto" else [int(yaw_opt)]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=rig_path)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
shapes = {pb.custom_shape for pb in arm.pose.bones if pb.custom_shape}
src = [o for o in bpy.data.objects if o.type == "MESH" and o not in shapes
       and any(m.type == "ARMATURE" for m in o.modifiers)]
if not src:
    raise SystemExit("no skinned mesh in %s" % rig_path)
arm.data.pose_position = "REST"


def join(objs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    return bpy.context.view_layer.objects.active


src = join(src)
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=mesh_path)
added = [o for o in bpy.data.objects if o not in before]
new_meshes = [o for o in added if o.type == "MESH"]
if not new_meshes:
    raise SystemExit("no mesh in %s" % mesh_path)
for o in new_meshes:
    world = o.matrix_world.copy()
    o.parent = None
    o.matrix_world = world
    for m in list(o.modifiers):
        o.modifiers.remove(m)
for o in added:
    if o not in new_meshes:
        bpy.data.objects.remove(o)
new = join(new_meshes)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

src_world = src.matrix_world
S = np.array([tuple(src_world @ v.co) for v in src.data.vertices])
src_bvh = BVHTree.FromPolygons([Vector(p) for p in S], [tuple(p.vertices) for p in src.data.polygons])
P0 = np.array([tuple(v.co) for v in new.data.vertices])
faces = [tuple(p.vertices) for p in new.data.polygons]


def nearest(points, bvh):
    out, dist = [], []
    for p in points:
        hit = bvh.find_nearest(Vector(p))
        out.append(tuple(hit[0]))
        dist.append(hit[3])
    return np.array(out), np.array(dist)


def umeyama(x, y):
    """c, R, t with y ~ c R x + t (least squares)."""
    mx, my = x.mean(0), y.mean(0)
    xc, yc = x - mx, y - my
    u, d, vt = np.linalg.svd(yc.T @ xc / len(x))
    s = np.eye(3)
    s[2, 2] = np.sign(np.linalg.det(u) * np.linalg.det(vt)) or 1.0
    r = u @ s @ vt
    c = np.trace(np.diag(d) @ s) / (xc ** 2).sum(1).mean()
    return c, r, my - c * r @ mx


s_lo, s_hi = S.min(0), S.max(0)
step = max(1, len(P0) // 3000)
best = None
for yaw in yaws:
    lo, hi = P0.min(0), P0.max(0)
    centre = (lo + hi) / 2
    a = math.radians(yaw)
    rz = np.array([[math.cos(a), -math.sin(a), 0.0], [math.sin(a), math.cos(a), 0.0], [0.0, 0.0, 1.0]])
    P = (P0 - centre) @ rz.T
    scale = (s_hi[2] - s_lo[2]) / max(hi[2] - lo[2], 1e-9)
    P = P * scale
    lo2, hi2 = P.min(0), P.max(0)
    P = P + np.array([(s_lo[0] + s_hi[0] - lo2[0] - hi2[0]) / 2, (s_lo[1] + s_hi[1] - lo2[1] - hi2[1]) / 2,
                      s_lo[2] - lo2[2]])
    for _ in range(rounds):
        q = P[::step]
        target, _ = nearest(q, src_bvh)
        c, r, t = umeyama(q, target)
        P = c * P @ r.T + t
        scale *= c
        if abs(c - 1) < 1e-6 and np.abs(r - np.eye(3)).max() < 1e-6 and np.abs(t).max() < 1e-6:
            break
    _, d_out = nearest(P[::step], src_bvh)
    new_bvh = BVHTree.FromPolygons([Vector(p) for p in P], faces)
    _, d_back = nearest(S[::max(1, len(S) // 3000)], new_bvh)
    score = d_out.mean() + d_back.mean()
    if best is None or score < best["score"]:
        best = {"score": score, "yaw": yaw, "scale": scale, "P": P}

P = best["P"]
for v, p in zip(new.data.vertices, P):
    v.co = Vector(p)
new.data.update()
_, d_out = nearest(P, src_bvh)
_, d_back = nearest(S, BVHTree.FromPolygons([Vector(p) for p in P], faces))

# weights: each vertex takes those of the nearest point on the rig's mesh, interpolated over that face
bpy.ops.object.select_all(action="DESELECT")
new.select_set(True)
bpy.context.view_layer.objects.active = new
new.vertex_groups.clear()
dt = new.modifiers.new("weights", "DATA_TRANSFER")
dt.object = src
dt.use_vert_data = True
dt.data_types_verts = {"VGROUP_WEIGHTS"}
dt.vert_mapping = "POLYINTERP_NEAREST"
dt.layers_vgroup_select_src = "ALL"
dt.layers_vgroup_select_dst = "NAME"
bpy.ops.object.datalayout_transfer(modifier=dt.name)
bpy.ops.object.modifier_apply(modifier=dt.name)
bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL", limit=4)
bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL", lock_active=False)
unweighted = sum(1 for v in new.data.vertices if not any(g.weight > 0 for g in v.groups))

new.parent = arm
new.matrix_parent_inverse = arm.matrix_world.inverted()
mod = new.modifiers.new("Armature", "ARMATURE")
mod.object = arm
bpy.data.objects.remove(src)
for o in shapes:
    bpy.data.objects.remove(o)
arm.data.pose_position = "POSE"
if material_slot is not None:
    for m in new.data.materials:
        if m is not None:
            m.name = "game_material_%d" % int(material_slot)
bpy.ops.object.select_all(action="DESELECT")
arm.select_set(True)
new.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.gltf(filepath=out_path, export_format="GLB", use_selection=True, export_skins=True,
                          export_animations=False, export_yup=True)
print("REBIND " + json.dumps({"yaw": best["yaw"], "scale": round(float(best["scale"]), 5),
                              "mean_distance": round(float(d_out.mean()), 5), "max_distance": round(float(d_out.max()), 5),
                              "mean_distance_back": round(float(d_back.mean()), 5), "vertices": len(P),
                              "unweighted": unweighted}))
if unweighted:
    sys.exit(2)
