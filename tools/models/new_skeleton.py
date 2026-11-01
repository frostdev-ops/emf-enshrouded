"""A glTF skin made into a game skeleton of its own (keen::ModelHierarchyResource, as EML shows it).

The joints are the skin's, in tree order (parents before children), under one joint named "root" at the origin and
turned 180 degrees about y, as every game creature skeleton's root is. Nodes above the joints that are not joints
themselves (an armature with Blender's -90 degree turn and a scale) are folded in: every joint keeps its world rotation
and its world position (times `scale`) and gets unit scale. Game space is glTF mirrored in x (gamegltf.py).

A joint's name hash is CRC32 of its lowercase name, so the skeleton's clip hash (joint_animation.hierarchy_hash)
follows from the names. write_glb writes a copy of the model on this skeleton, every joint named jNNN_<name> by its
index, which gltf_in.load_groups reads with the hierarchy.
"""
import json
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamegltf  # noqa: E402
import gltf_in  # noqa: E402
import joint_animation as ja  # noqa: E402
import own_skeleton as osk  # noqa: E402

ROOT_REST = ((0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0))
IDENTITY_JOINT = {"orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}, "position": {"x": 0.0, "y": 0.0, "z": 0.0},
                  "scale": {"x": 1.0, "y": 1.0, "z": 1.0}}


def _trs(t, q, s):
    x, y, z, w = q
    r = ((1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
         (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
         (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)))
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0.0, 0.0, 0.0, 1.0]]


def local_matrix(node, channels=None):
    """A node's matrix (glTF, row-major); channels: {"translation"|"rotation"|"scale": value} replacing its own."""
    channels = channels or {}
    if "matrix" in node and not channels:
        return gltf_in.mat_from_colmajor(node["matrix"])
    return _trs(channels.get("translation", node.get("translation", (0.0, 0.0, 0.0))),
                channels.get("rotation", node.get("rotation", (0.0, 0.0, 0.0, 1.0))),
                channels.get("scale", node.get("scale", (1.0, 1.0, 1.0))))


def parents_of(gltf):
    return {c: i for i, nd in enumerate(gltf["nodes"]) for c in nd.get("children", [])}


def node_worlds(gltf, pose=None):
    """Per node its world matrix (glTF); pose: {node: channels} for an animation frame."""
    nodes = gltf["nodes"]
    parent = parents_of(gltf)
    pose = pose or {}
    out = [None] * len(nodes)

    def world(i):
        if out[i] is None:
            m = local_matrix(nodes[i], pose.get(i))
            out[i] = gltf_in.mat_mul(world(parent[i]), m) if i in parent else m
        return out[i]

    for i in range(len(nodes)):
        world(i)
    return out


def _quat(r):
    tr = r[0][0] + r[1][1] + r[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = ((r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25 * s)
    elif r[0][0] > r[1][1] and r[0][0] > r[2][2]:
        s = math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2
        q = (0.25 * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s)
    elif r[1][1] > r[2][2]:
        s = math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2
        q = ((r[0][1] + r[1][0]) / s, 0.25 * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s)
    else:
        s = math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2
        q = ((r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25 * s, (r[1][0] - r[0][1]) / s)
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)


def decompose(m, what="a node"):
    """(rotation, position, uniform scale) of a world matrix; a scale that differs per axis or mirrors is refused."""
    cols = [[m[r][c] for r in range(3)] for c in range(3)]
    lens = [math.sqrt(sum(v * v for v in col)) for col in cols]
    if min(lens) <= 0 or max(lens) / min(lens) - 1 > 1e-4:
        raise ValueError("%s is scaled unevenly (%s): only a uniform scale can be folded into a skeleton"
                         % (what, ", ".join("%.4g" % v for v in lens)))
    r = [[m[i][c] / lens[c] for c in range(3)] for i in range(3)]
    det = (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
           + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))
    if det < 0:
        raise ValueError("%s is mirrored: a skeleton cannot hold a mirror" % what)
    return _quat(r), (m[0][3], m[1][3], m[2][3]), sum(lens) / 3


def mesh_world(gltf, skin=0):
    """The world matrix of the first mesh node skinned to the skin: where the model stands at the bind pose."""
    for i, nd in enumerate(gltf["nodes"]):
        if "mesh" in nd and nd.get("skin") == skin:
            return node_worlds(gltf)[i]
    raise ValueError("no mesh is skinned to skin %d" % skin)


def bind_worlds(gltf, binc, skin=0):
    """{joint node: its world matrix at the bind pose}: the pose in which the skinned mesh stands as its node puts
    it (mesh world times the inverse of the joint's inverse bind). A file's nodes may hold another pose (a frame of
    an animation); without inverse binds the nodes' pose is the bind pose."""
    s = gltf["skins"][skin]
    if "inverseBindMatrices" not in s:
        worlds = node_worlds(gltf)
        return {n: worlds[n] for n in s["joints"]}
    mesh = mesh_world(gltf, skin)
    ibm = [gltf_in.mat_from_colmajor(v) for v in gltf_in.accessor(gltf, binc, s["inverseBindMatrices"])]
    return {n: gltf_in.mat_mul(mesh, gltf_in.mat_inv_affine(m)) for n, m in zip(s["joints"], ibm)}


def build(gltf, binc, skin=0, scale=1.0):
    """The skeleton of a glTF skin: names (the root's "root", then the nodes' names), nodes (the glTF node of each
    joint, None at the root), parents (None at the root), rest (per joint local rotation, position, scale in game
    space: the bind pose), scale and rest_scale (each joint's world scale in the file at the bind pose, for poses)."""
    nodes = gltf["nodes"]
    joints = set(gltf["skins"][skin]["joints"])
    order, parents = [None], [None]
    scene = gltf.get("scenes", [{}])[gltf.get("scene", 0)].get("nodes", [])
    stack = [(n, 0) for n in reversed(scene)]
    while stack:
        n, above = stack.pop()
        if n in joints:
            order.append(n)
            parents.append(above)
            above = len(order) - 1
        stack.extend((c, above) for c in reversed(nodes[n].get("children", [])))
    missing = joints - set(order[1:])
    if missing:
        raise ValueError("skin joints outside the scene: %s" % ", ".join(nodes[n].get("name", str(n)) for n in missing))
    worlds = bind_worlds(gltf, binc, skin)
    sk = {"names": ["root"] + [nodes[n].get("name") or "joint%d" % n for n in order[1:]], "nodes": order,
          "parents": parents, "scale": scale,
          "rest_scale": [1.0] + [decompose(worlds[n], nodes[n].get("name", str(n)))[2] for n in order[1:]]}
    sk["rest"] = pose(sk, worlds, gltf)
    return sk


def pose(sk, worlds, gltf=None):
    """Per joint its local (rotation, position, scale) in game space from the joint nodes' world matrices (glTF,
    indexed by node: node_worlds for a frame, bind_worlds for the rest): each joint turned and placed as in the
    file's world (positions times the skeleton's scale), its scale relative to its scale at rest. The root keeps the
    game root's rest."""
    out, placed = [ROOT_REST], [(ROOT_REST[0], (0.0, 0.0, 0.0), 1.0)]
    for j in range(1, len(sk["nodes"])):
        n = sk["nodes"][j]
        what = gltf["nodes"][n].get("name", str(n)) if gltf else str(n)
        q, p, s = decompose(worlds[n], what)
        q = gamegltf.mirror_quat(q)
        p = tuple(v * sk["scale"] for v in gamegltf.mirror_vec(p))
        s = s / sk["rest_scale"][j]
        pq, pp, ps = placed[sk["parents"][j]]
        inv = osk.qconj(pq)
        local = osk.qmul(inv, q)
        t = tuple(v / ps for v in osk.qrot(inv, tuple(a - b for a, b in zip(p, pp))))
        out.append((local, t, (s / ps,) * 3))
        placed.append((q, p, s))
    return out


def name_hash(name):
    return zlib.crc32(name.lower().encode("utf-8"))


def hierarchy(sk, guid):
    """The skeleton as EML shows a keen::ModelHierarchyResource: names hashed, parents (65535 at the root), the rest
    pose (defaultPoseJoints and the defaultPose vectors, both padded to a multiple of 4 joints as the game's are) and
    the inverse binds."""
    names = [name_hash(n) for n in sk["names"]]
    seen = {}
    for n, h in zip(sk["names"], names):
        if h in seen:
            raise ValueError("joints %r and %r have the same name hash: rename one" % (seen[h], n))
        seen[h] = n
    rest = sk["rest"]
    n = len(rest)
    aligned = (n + 3) // 4 * 4
    pad = aligned - n

    def unit(*v):
        return {"data": list(struct.pack("<4f", *v))}

    data = ([unit(*q) for q, _, _ in rest] + [unit(0.0, 0.0, 0.0, 1.0)] * pad
            + [unit(t[0], t[1], t[2], 0.0) for _, t, _ in rest] + [unit(0.0, 0.0, 0.0, 0.0)] * pad
            + [unit(s[0], s[1], s[2], 1.0) for _, _, s in rest] + [unit(1.0, 1.0, 1.0, 1.0)] * pad)
    joints = [{"orientation": dict(zip("xyzw", q)), "position": dict(zip("xyz", t)), "scale": dict(zip("xyz", s))}
              for q, t, s in rest] + [json.loads(json.dumps(IDENTITY_JOINT)) for _ in range(pad)]
    return {"hierarchyId": guid, "jointNames": names,
            "jointParentIndices": [65535 if p is None else p for p in sk["parents"]], "unbendableJoints": [],
            "skinToJointMatrices": osk.inverse_binds(rest, sk["parents"]), "jointCount": n,
            "defaultPose": {"data": data, "alignedJointCount": aligned}, "defaultPoseJoints": joints}


def clip_hash(hier):
    """The hierarchyHash a clip for this skeleton carries."""
    return ja.hierarchy_hash(hier["jointNames"])


def _pack_glb(path, gltf, binc):
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    binc = bytes(binc) + b"\0" * ((-len(binc)) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binc)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binc), 0x004E4942) + binc)


def write_glb(gltf, binc, sk, path, skin=0):
    """A copy of the model on the skeleton: the joints (named jNNN_<name>) at the rest pose, the meshes skinned to
    this skin with their vertices where the bind pose puts them (times the scale), new inverse binds; animations and
    every other node are left out."""
    if len(gltf.get("buffers", [])) != 1 or "uri" in gltf["buffers"][0]:
        raise ValueError("only a .glb with its one binary buffer is supported")
    src_nodes = gltf["nodes"]
    out = json.loads(json.dumps(gltf))
    out.pop("animations", None)
    binc = bytearray(binc)
    index = {node: j for j, node in enumerate(sk["nodes"]) if node is not None}
    nodes = []
    for j, (q, t, _) in enumerate(sk["rest"]):
        nodes.append({"name": "j%03d_%s" % (j, sk["names"][j]), "rotation": list(gamegltf.mirror_quat(q)),
                      "translation": list(gamegltf.mirror_vec(t))})
    for j, p in enumerate(sk["parents"]):
        if p is not None:
            nodes[p].setdefault("children", []).append(j)
    src_skin = gltf["skins"][skin]
    joint_order = [index[node] for node in src_skin["joints"]] + [0]

    def add(raw, count, kind, comp=5126, minmax=None):
        while len(binc) % 4:
            binc.append(0)
        out["bufferViews"].append({"buffer": 0, "byteOffset": len(binc), "byteLength": len(raw)})
        binc.extend(raw)
        acc = {"bufferView": len(out["bufferViews"]) - 1, "componentType": comp, "count": count, "type": kind}
        if minmax:
            acc["min"], acc["max"] = minmax
        out["accessors"].append(acc)
        return len(out["accessors"]) - 1

    game_ibm = osk.inverse_binds(sk["rest"], sk["parents"])
    ibm = [gamegltf.mirror_mat(gamegltf.inverse_bind_rows(game_ibm[j])) for j in joint_order]
    ibm_acc = add(b"".join(struct.pack("<16f", *[m[r][c] for c in range(4) for r in range(4)]) for m in ibm),
                  len(ibm), "MAT4")
    out["skins"] = [{"joints": joint_order, "inverseBindMatrices": ibm_acc, "skeleton": 0}]

    worlds = bind_worlds(gltf, binc, skin)
    if "inverseBindMatrices" in src_skin:
        file_ibm = [gltf_in.mat_from_colmajor(v) for v in gltf_in.accessor(gltf, binc, src_skin["inverseBindMatrices"])]
    else:
        file_ibm = [[[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)] for _ in src_skin["joints"]]
    to_world = [gltf_in.mat_mul(worlds[node], m) for node, m in zip(src_skin["joints"], file_ibm)]
    k = sk["scale"]

    scene_nodes = [0]
    done = {}
    for nd in src_nodes:
        if "mesh" not in nd or nd.get("skin") != skin:
            continue
        mesh = nd["mesh"]
        if mesh not in done:
            for prim in out["meshes"][mesh]["primitives"]:
                a = prim["attributes"]
                pos = gltf_in.accessor(gltf, binc, a["POSITION"])
                joints = gltf_in.accessor(gltf, binc, a["JOINTS_0"])
                weights = gltf_in.accessor(gltf, binc, a["WEIGHTS_0"])
                nrm = gltf_in.accessor(gltf, binc, a["NORMAL"]) if "NORMAL" in a else None
                tan = gltf_in.accessor(gltf, binc, a["TANGENT"]) if "TANGENT" in a else None
                new_pos, new_nrm, new_tan = [], [], []
                for i, p in enumerate(pos):
                    tot = sum(w for w in weights[i] if w > 0) or 1.0
                    m = [[sum(w / tot * to_world[jj][r][c] for jj, w in zip(joints[i], weights[i]) if w > 0)
                          for c in range(4)] for r in range(4)]
                    new_pos.append(tuple(v * k for v in gltf_in.xform_point(m, p)))
                    if nrm is not None:
                        new_nrm.append(gltf_in.xform_dir(m, nrm[i]))
                    if tan is not None:
                        new_tan.append(gltf_in.xform_dir(m, tan[i][:3]) + (tan[i][3],))
                lo = [min(p[c] for p in new_pos) for c in range(3)]
                hi = [max(p[c] for p in new_pos) for c in range(3)]
                a["POSITION"] = add(b"".join(struct.pack("<3f", *p) for p in new_pos), len(new_pos), "VEC3",
                                    minmax=(lo, hi))
                if nrm is not None:
                    a["NORMAL"] = add(b"".join(struct.pack("<3f", *v) for v in new_nrm), len(new_nrm), "VEC3")
                if tan is not None:
                    a["TANGENT"] = add(b"".join(struct.pack("<4f", *v) for v in new_tan), len(new_tan), "VEC4")
            done[mesh] = True
        nodes.append({"name": nd.get("name", "mesh"), "mesh": mesh, "skin": 0})
        scene_nodes.append(len(nodes) - 1)
    if len(scene_nodes) == 1:
        raise ValueError("no mesh is skinned to skin %d" % skin)
    out["nodes"] = nodes
    out["scene"] = 0
    out["scenes"] = [{"nodes": scene_nodes}]
    out["buffers"] = [{"byteLength": len(binc)}]
    _pack_glb(path, out, binc)
