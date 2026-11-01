"""tools/models/new_skeleton.py: a glTF skin made into a game skeleton of its own."""
import json
import math
import os
import struct
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import gltf_in  # noqa: E402
import joint_animation as ja  # noqa: E402
import new_skeleton as ns  # noqa: E402
import own_skeleton as osk  # noqa: E402

WOLF = os.path.join(HERE, "..", "..", "analysis", "models", "sources", "quaternius_wolf", "wolf_a.glb")
H = math.sqrt(0.5)
I = (0.0, 0.0, 0.0, 1.0)
GAME_ROOT = (0.0, 1.0, 0.0, 0.0)  # every game creature skeleton's root: at the origin, turned 180 degrees about y


def trs(t=(0.0, 0.0, 0.0), q=I, s=1.0):
    x, y, z, w = q
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[i][0] * s, r[i][1] * s, r[i][2] * s, t[i]] for i in range(3)] + [[0.0, 0.0, 0.0, 1.0]]


def colmajor(m):
    return [m[r][c] for c in range(4) for r in range(4)]


def write_glb(path, gltf, binc):
    js = json.dumps(gltf).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    binc += b"\0" * ((-len(binc)) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binc)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binc), 0x004E4942) + binc)


# A small rig as Blender exports it: an armature node (not a joint) turned -90 degrees about x and scaled 100, under
# it the hips (a joint) with the head and the tail, and an IK helper joint beside the hips; the mesh node carries the
# armature's transform. World positions (glTF): hips (0, 1, 0), head (0, 1.5, 0.3), tail (0, 1, -0.4), helper
# (0.2, 0, 0); the head turned 90 degrees about y.
ARMATURE_Q = (-H, 0.0, 0.0, H)
NODES = [
    {"name": "Scene", "children": [1, 6]},
    {"name": "Armature", "rotation": list(ARMATURE_Q), "scale": [100.0] * 3, "children": [2, 5]},
    {"name": "Hips", "translation": [0.0, 0.0, 0.01], "rotation": [H, 0.0, 0.0, H], "children": [3, 4]},
    {"name": "Head", "translation": [0.0, 0.005, 0.003], "rotation": [0.0, H, 0.0, H]},
    {"name": "Tail", "translation": [0.0, 0.0, -0.004]},
    {"name": "IKFoot", "translation": [0.002, 0.0, 0.0]},
    {"name": "Body", "mesh": 0, "skin": 0, "rotation": list(ARMATURE_Q), "scale": [100.0] * 3},
]
SKIN_NODES = [5, 3, 2, 4]  # not in tree order: the helper, the head, the hips, the tail
PARENT = {1: 0, 2: 1, 3: 2, 4: 2, 5: 1, 6: 0}
WORLD_POINTS = [(0.0, 1.0, 0.0), (0.0, 1.5, 0.3), (0.2, 0.0, 0.0)]  # the three vertices: on the hips, head, helper
VERTEX_JOINTS = [2, 1, 0]  # skin indices: hips, head, helper
RAW_NORMALS = [(0.0, 0.0, 1.0), (0.0, -1.0, 0.0), (1.0, 0.0, 0.0)]
GAME_NORMALS = [(0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (-1.0, 0.0, 0.0)]


def node_world(i):
    nd = NODES[i]
    m = trs(nd.get("translation", (0.0, 0.0, 0.0)), tuple(nd.get("rotation", I)), nd.get("scale", [1.0])[0])
    return gltf_in.mat_mul(node_world(PARENT[i]), m) if i in PARENT else m


def small_rig(path, nodes=None):
    nodes = nodes or NODES
    armature = node_world(1)
    inv_arm = gltf_in.mat_inv_affine(armature)
    ibm = [gltf_in.mat_mul(gltf_in.mat_inv_affine(node_world(j)), armature) for j in SKIN_NODES]
    raw = [gltf_in.xform_point(inv_arm, p) for p in WORLD_POINTS]
    binc = b"".join(struct.pack("<16f", *colmajor(m)) for m in ibm)
    views = [(0, len(binc))]
    parts = [b"".join(struct.pack("<3f", *p) for p in raw), b"".join(struct.pack("<3f", *n) for n in RAW_NORMALS),
             b"".join(struct.pack("<4H", j, 0, 0, 0) for j in VERTEX_JOINTS),
             b"".join(struct.pack("<4f", 1.0, 0.0, 0.0, 0.0) for _ in VERTEX_JOINTS), struct.pack("<3H", 0, 1, 2)]
    for p in parts:
        binc += b"\0" * ((-len(binc)) % 4)
        views.append((len(binc), len(p)))
        binc += p
    gltf = {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": nodes,
        "skins": [{"joints": SKIN_NODES, "inverseBindMatrices": 0, "skeleton": 1}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 1, "NORMAL": 2, "JOINTS_0": 3, "WEIGHTS_0": 4},
                                    "indices": 5}]}],
        "buffers": [{"byteLength": len(binc)}],
        "bufferViews": [{"buffer": 0, "byteOffset": o, "byteLength": n} for o, n in views],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "MAT4"},
                      {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [min(p[k] for p in raw) for k in range(3)], "max": [max(p[k] for p in raw) for k in range(3)]},
                      {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3"},
                      {"bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4"},
                      {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4"},
                      {"bufferView": 5, "componentType": 5123, "count": 3, "type": "SCALAR"}],
    }
    write_glb(path, gltf, binc)
    return path


def world_space_rig(path):
    """The small rig with its inverse binds and vertices in world space, as glTF's spec has them (the skinned mesh
    node's own transform, the armature's turn and scale 100, is to be ignored)."""
    gltf, binc = gltf_in.read_glb(small_rig(path))
    binc = bytearray(binc)
    ibm = b"".join(struct.pack("<16f", *colmajor(gltf_in.mat_inv_affine(node_world(j)))) for j in SKIN_NODES)
    pos = b"".join(struct.pack("<3f", *p) for p in WORLD_POINTS)
    for view, data in ((gltf["bufferViews"][0], ibm), (gltf["bufferViews"][1], pos)):
        assert view["byteLength"] == len(data)
        binc[view.get("byteOffset", 0):view.get("byteOffset", 0) + len(data)] = data
    acc = gltf["accessors"][1]
    acc["min"] = [min(p[k] for p in WORLD_POINTS) for k in range(3)]
    acc["max"] = [max(p[k] for p in WORLD_POINTS) for k in range(3)]
    write_glb(path, gltf, bytes(binc))
    return path


def same_rotation(a, b, tol=1e-6):
    return abs(abs(sum(x * y for x, y in zip(a, b))) - 1.0) < tol


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = small_rig(os.path.join(self.tmp.name, "rig.glb"))
        self.gltf, self.binc = gltf_in.read_glb(self.path)

    def tearDown(self):
        self.tmp.cleanup()


class BuildTests(Fixture):
    def test_joints_in_tree_order_under_a_root(self):
        sk = ns.build(self.gltf, self.binc)
        self.assertEqual(sk["names"], ["root", "Hips", "Head", "Tail", "IKFoot"])
        self.assertEqual(sk["parents"], [None, 0, 1, 1, 0])
        self.assertEqual(sk["nodes"], [None, 2, 3, 4, 5])

    def test_root_is_the_game_root(self):
        sk = ns.build(self.gltf, self.binc)
        self.assertEqual(sk["rest"][0], (GAME_ROOT, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)))

    def test_rest_pose_stands_where_the_rig_stands_in_game_space(self):
        sk = ns.build(self.gltf, self.binc)
        world = osk.world(sk["rest"], sk["parents"])
        want = [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.5, 0.3), (0.0, 1.0, -0.4), (-0.2, 0.0, 0.0)]
        for j, p in enumerate(want):
            for a, b in zip(world[j][1], p):
                self.assertAlmostEqual(a, b, places=6, msg=sk["names"][j])
            for a in world[j][2]:
                self.assertAlmostEqual(a, 1.0, places=6, msg=sk["names"][j])
        rotations = {1: I, 2: (0.0, -H, 0.0, H), 3: I, 4: (-H, 0.0, 0.0, H)}  # glTF (x, y, z, w) mirrored: (x, -y, -z, w)
        for j, q in rotations.items():
            self.assertTrue(same_rotation(world[j][0], q), "%s: %s" % (sk["names"][j], world[j][0]))

    def test_scale_resizes_positions_only(self):
        sk = ns.build(self.gltf, self.binc, scale=0.5)
        world = osk.world(sk["rest"], sk["parents"])
        for a, b in zip(world[2][1], (0.0, 0.75, 0.15)):
            self.assertAlmostEqual(a, b, places=6)
        self.assertTrue(same_rotation(world[2][0], (0.0, -H, 0.0, H)))

    def test_yaw_turns_a_creature_that_faces_sideways(self):
        """yaw: degrees about the vertical (glTF's, counter-clockwise seen from above): a scan lying along +x faces
        the game's forward (+z) with yaw -90. Positions and rotations turn; the frames of clips turn alike."""
        sk = ns.build(self.gltf, self.binc, yaw=-90)
        world = osk.world(sk["rest"], sk["parents"])
        # glTF head (0, 1.5, 0.3) turned -90 about y: (-0.3, 1.5, 0); game space mirrors x: (0.3, 1.5, 0)
        for a, b in zip(world[2][1], (0.3, 1.5, 0.0)):
            self.assertAlmostEqual(a, b, places=6)
        frame = osk.world(ns.pose(sk, ns.bind_worlds(self.gltf, self.binc), self.gltf), sk["parents"])
        for j in range(len(world)):
            for a, b in zip(world[j][1], frame[j][1]):
                self.assertAlmostEqual(a, b, places=6)
        copy = os.path.join(self.tmp.name, "turned.glb")
        ns.write_glb(self.gltf, self.binc, sk, copy)
        _, report = gltf_in.load_groups(copy, ns.hierarchy(sk, "22222222-2222-4222-8222-222222222222"),
                                        warn=self.fail)
        self.assertLess(report["max_rest_difference"], 1e-3)

    def test_non_uniform_scale_is_refused(self):
        nodes = json.loads(json.dumps(NODES))
        nodes[6]["scale"] = [100.0, 50.0, 100.0]
        gltf, binc = gltf_in.read_glb(small_rig(os.path.join(self.tmp.name, "squashed.glb"), nodes))
        with self.assertRaises(ValueError):
            ns.build(gltf, binc)

    def test_uneven_scale_folds_into_the_positions_on_request(self):
        """A file scaled unevenly above its joints (an FBX export's 1.2 x 1.2 x 1.1): with uneven=True every joint
        stands exactly where the file puts it (times the scale), with the nearest rotation and unit scale."""
        nodes = json.loads(json.dumps(NODES))
        nodes[6]["scale"] = [100.0, 50.0, 100.0]
        gltf, binc = gltf_in.read_glb(small_rig(os.path.join(self.tmp.name, "squashed.glb"), nodes))
        sk = ns.build(gltf, binc, scale=0.5, uneven=True)
        world = osk.world(sk["rest"], sk["parents"])
        binds = ns.bind_worlds(gltf, binc)
        for j in range(1, len(sk["nodes"])):
            m = binds[sk["nodes"][j]]
            want = (-m[0][3] * 0.5, m[1][3] * 0.5, m[2][3] * 0.5)  # game space mirrors x
            for a, b in zip(world[j][1], want):
                self.assertAlmostEqual(a, b, places=6, msg=sk["names"][j])
            for a in world[j][2]:
                self.assertAlmostEqual(a, 1.0, places=6, msg=sk["names"][j])
            self.assertAlmostEqual(sum(c * c for c in world[j][0]), 1.0, places=6)

    def test_the_rotation_of_a_stretched_matrix_is_its_own(self):
        """A rotation stretched afterwards along the world's axes (S x R) keeps R: the polar factor."""
        r = trs(q=(0.0, math.sin(0.35), 0.0, math.cos(0.35)))  # 40 degrees about y
        stretched = gltf_in.mat_mul([[1.2, 0, 0, 0], [0, 1.2, 0, 0], [0, 0, 1.1, 0], [0, 0, 0, 1]], r)
        with self.assertRaises(ValueError):
            ns.decompose(stretched)
        q, p, s = ns.decompose(stretched, uneven=True)
        self.assertTrue(same_rotation(q, (0.0, math.sin(0.35), 0.0, math.cos(0.35))), q)
        self.assertAlmostEqual(s, (1.2 * 1.2 * 1.1) ** (1 / 3), places=6)  # the even scale of the same volume

    def test_inverse_binds_in_world_space_as_the_spec_has_them(self):
        """glTF's spec ignores a skinned mesh node's transform: inverse binds in world space and vertices in world
        space at the bind pose (Sketchfab's files, their mesh node under a scaled FBX node). The rest still stands
        where the joint nodes stand, not scaled again by the mesh node's transform."""
        gltf, binc = gltf_in.read_glb(world_space_rig(os.path.join(self.tmp.name, "spec.glb")))
        sk = ns.build(gltf, binc)
        world = osk.world(sk["rest"], sk["parents"])
        want = [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.5, 0.3), (0.0, 1.0, -0.4), (-0.2, 0.0, 0.0)]
        for j, p in enumerate(want):
            for a, b in zip(world[j][1], p):
                self.assertAlmostEqual(a, b, places=5, msg=sk["names"][j])
        self.assertTrue(same_rotation(world[2][0], (0.0, -H, 0.0, H), tol=1e-5))

    def test_a_helper_joint_with_a_meaningless_inverse_bind_stands_where_its_node_does(self):
        """Sketchfab's '_rootJoint': a joint no vertex follows, its inverse bind the identity, 100 times off its
        node's scale. It rests where its node stands, so a frame scales it by 1, not by 1/100 (which the clip's
        children could not hold)."""
        path = world_space_rig(os.path.join(self.tmp.name, "spec.glb"))
        gltf, binc = gltf_in.read_glb(path)
        binc = bytearray(binc)
        k = SKIN_NODES.index(4)  # the tail: no vertex follows it
        off = gltf["bufferViews"][0].get("byteOffset", 0) + 64 * k
        binc[off:off + 64] = struct.pack("<16f", *colmajor(trs()))
        gltf, binc = gltf, bytes(binc)
        sk = ns.build(gltf, binc)
        tail = sk["names"].index("Tail")
        world = osk.world(sk["rest"], sk["parents"])
        for a, b in zip(world[tail][1], (0.0, 1.0, -0.4)):
            self.assertAlmostEqual(a, b, places=5)
        frame = ns.pose(sk, ns.node_worlds(gltf), gltf)
        for j in range(1, len(frame)):
            for v in frame[j][2]:
                self.assertAlmostEqual(v, 1.0, places=5, msg=sk["names"][j])

    def test_rest_is_the_bind_pose_when_the_nodes_hold_another(self):
        nodes = json.loads(json.dumps(NODES))
        nodes[4]["translation"] = [0.0, 0.003, -0.002]  # the tail lifted in the file's nodes, not in its binds
        nodes[3]["rotation"] = [0.0, 0.0, 0.0, 1.0]
        gltf, binc = gltf_in.read_glb(small_rig(os.path.join(self.tmp.name, "posed.glb"), nodes))
        sk = ns.build(gltf, binc)
        world = osk.world(sk["rest"], sk["parents"])
        for a, b in zip(world[3][1], (0.0, 1.0, -0.4)):
            self.assertAlmostEqual(a, b, places=5)
        self.assertTrue(same_rotation(world[2][0], (0.0, -H, 0.0, H), tol=1e-5))


class HierarchyTests(Fixture):
    def test_names_parents_and_inverse_binds(self):
        sk = ns.build(self.gltf, self.binc)
        h = ns.hierarchy(sk, "22222222-2222-4222-8222-222222222222")
        self.assertEqual(h["hierarchyId"], "22222222-2222-4222-8222-222222222222")
        self.assertEqual(h["jointCount"], 5)
        self.assertEqual(h["jointNames"], [zlib.crc32(n.encode()) for n in ("root", "hips", "head", "tail", "ikfoot")])
        self.assertEqual(h["jointParentIndices"], [65535, 0, 1, 1, 0])
        self.assertEqual(h["unbendableJoints"], [])
        self.assertEqual(h["skinToJointMatrices"], osk.inverse_binds(sk["rest"], sk["parents"]))

    def test_default_pose_padded_to_four_joints(self):
        sk = ns.build(self.gltf, self.binc)
        h = ns.hierarchy(sk, "22222222-2222-4222-8222-222222222222")
        self.assertEqual(h["defaultPose"]["alignedJointCount"], 8)
        data = [struct.unpack("<4f", bytes(u["data"])) for u in h["defaultPose"]["data"]]
        self.assertEqual(len(data), 24)
        for j, (q, t, s) in enumerate(sk["rest"]):
            self.assertEqual(data[j], struct.unpack("<4f", struct.pack("<4f", *q)))
            self.assertEqual(data[8 + j], struct.unpack("<4f", struct.pack("<4f", t[0], t[1], t[2], 0.0)))
            self.assertEqual(data[16 + j], (1.0, 1.0, 1.0, 1.0))
        for j in range(5, 8):  # padding as the game's skeletons have it
            self.assertEqual(data[j], (0.0, 0.0, 0.0, 1.0))
            self.assertEqual(data[8 + j], (0.0, 0.0, 0.0, 0.0))
            self.assertEqual(data[16 + j], (1.0, 1.0, 1.0, 1.0))
        dpj = h["defaultPoseJoints"]
        self.assertEqual(len(dpj), 8)
        self.assertEqual(osk.rest_of(h), (sk["rest"], sk["parents"]))
        self.assertEqual(dpj[7], {"orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                                  "position": {"x": 0.0, "y": 0.0, "z": 0.0}, "scale": {"x": 1.0, "y": 1.0, "z": 1.0}})

    def test_clip_hash_of_the_skeleton(self):
        sk = ns.build(self.gltf, self.binc)
        h = ns.hierarchy(sk, "22222222-2222-4222-8222-222222222222")
        self.assertEqual(ns.clip_hash(h), ja.hierarchy_hash(h["jointNames"]))

    def test_names_that_hash_alike_are_refused(self):
        sk = ns.build(self.gltf, self.binc)
        sk["names"][3] = "HEAD"
        with self.assertRaises(ValueError):
            ns.hierarchy(sk, "22222222-2222-4222-8222-222222222222")

    def test_a_rig_with_its_own_root_joint(self):
        """Many rigs have a joint called Root: the skeleton's own root keeps the name root, the rig's is hashed as
        root.1 (its name stays, for slots and the copy's bones)."""
        nodes = json.loads(json.dumps(NODES))
        nodes[2]["name"] = "Root"
        gltf, binc = gltf_in.read_glb(small_rig(os.path.join(self.tmp.name, "root.glb"), nodes))
        sk = ns.build(gltf, binc)
        self.assertEqual(sk["names"][:2], ["root", "Root"])
        h = ns.hierarchy(sk, "22222222-2222-4222-8222-222222222222")
        self.assertEqual(h["jointNames"][:2], [zlib.crc32(b"root"), zlib.crc32(b"root.1")])


class GlbCopyTests(Fixture):
    def copy(self, scale=1.0):
        sk = ns.build(self.gltf, self.binc, scale=scale)
        h = ns.hierarchy(sk, "22222222-2222-4222-8222-222222222222")
        out = os.path.join(self.tmp.name, "copy.glb")
        ns.write_glb(self.gltf, self.binc, sk, out)
        return sk, h, out

    def test_copy_names_every_joint_by_its_index(self):
        _, _, out = self.copy()
        gltf, _ = gltf_in.read_glb(out)
        names = sorted(n["name"] for n in gltf["nodes"] if n.get("name", "").startswith("j"))
        self.assertEqual(names, ["j000_root", "j001_Hips", "j002_Head", "j003_Tail", "j004_IKFoot"])
        self.assertNotIn("animations", gltf)

    def test_copy_reads_back_on_the_skeleton_in_game_space(self):
        for scale in (1.0, 0.5):
            _, h, out = self.copy(scale)
            groups, report = gltf_in.load_groups(out, h, warn=self.fail)
            self.assertLess(report["max_rest_difference"], 1e-6)
            self.assertEqual(report["unmapped_bones"], [])
            g = groups[0]
            want = [(0.0, 1.0, 0.0), (0.0, 1.5, 0.3), (-0.2, 0.0, 0.0)]
            for p, w in zip(g["pos"], want):
                for a, b in zip(p, w):
                    self.assertAlmostEqual(a, b * scale, places=5)
            for n, w in zip(g["nrm"], GAME_NORMALS):
                for a, b in zip(n, w):
                    self.assertAlmostEqual(a, b, places=5)
            self.assertEqual([s[0] for s in g["skin"]], [[1], [2], [4]])


@unittest.skipUnless(os.path.exists(WOLF), "the CC0 wolf is not in analysis/models/sources")
class WolfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gltf, cls.binc = gltf_in.read_glb(WOLF)
        cls.sk = ns.build(cls.gltf, cls.binc, scale=0.5)

    def test_every_skin_joint_under_one_root(self):
        sk = self.sk
        skin = [self.gltf["nodes"][j]["name"] for j in self.gltf["skins"][0]["joints"]]
        self.assertEqual(sk["names"][0], "root")
        self.assertEqual(sorted(sk["names"][1:]), sorted(skin))
        self.assertTrue(all(p is None if j == 0 else p < j for j, p in enumerate(sk["parents"])))
        named = dict(zip(sk["names"], sk["parents"]))
        for name, parent in (("Body", "root"), ("IKBackLeg.L", "root"), ("FFB.L", "IKBackLeg.L"), ("Head", "Neck3")):
            self.assertEqual(sk["names"][named[name]], parent)

    def test_bind_pose_head_in_front_left_legs_at_minus_x(self):
        """The bind pose stands straight (the file's nodes hold a turned head and a curled tail); the left legs are
        at -x, where the game wolf's left feet are."""
        world = osk.world(self.sk["rest"], self.sk["parents"])
        at = {n: world[j][1] for j, n in enumerate(self.sk["names"])}
        for a, b in zip(at["Head"], (0.0, 1.0566, 0.9412)):
            self.assertAlmostEqual(a, b, delta=0.001)
        self.assertLess(at["Tail8"][2], -1.3)
        self.assertLess(at["FrontLowerLeg.L"][0], -0.15)
        self.assertLess(at["BackLowerLeg.L"][0], -0.15)
        self.assertAlmostEqual(at["FrontLowerLeg.L"][0], -at["FrontLowerLeg.R"][0], places=4)

    def test_copy_reads_back(self):
        h = ns.hierarchy(self.sk, "33333333-3333-4333-8333-333333333333")
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "wolf.glb")
            ns.write_glb(self.gltf, self.binc, self.sk, out)
            groups, report = gltf_in.load_groups(out, h, warn=self.fail)
        self.assertLess(report["max_rest_difference"], 1e-4)
        ys = [p[1] for g in groups for p in g["pos"]]
        self.assertAlmostEqual(min(ys), -0.005, delta=0.003)
        self.assertAlmostEqual(max(ys), 1.336, delta=0.003)


if __name__ == "__main__":
    unittest.main()
