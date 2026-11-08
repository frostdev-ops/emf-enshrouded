"""gltf_in.load_static_groups: a glb without a skeleton (a Meshy prop) into encode.py vertex groups in game space,
fitted to a width, standing on the ground at the origin."""
import json
import math
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import gltf_in  # noqa: E402


def write_glb(path, gltf, binc):
    js = json.dumps(gltf).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    binc += b"\0" * ((-len(binc)) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binc)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binc), 0x004E4942) + binc)


def prop_glb(path, primitives_materials=(0,)):
    """A box-like mesh spanning x -1..1, y 0..0.5, z -0.5..0.5 under a mesh node translated (5, 3, -1) whose parent is
    scaled 2: in the file's world it spans x 8..12, y 6..7, z -3..-1 (4 wide, 1 high, 2 deep). One primitive per entry
    of `primitives_materials` (their material index)."""
    pos = [(-1, 0, -0.5), (1, 0, -0.5), (1, 0.5, 0.5), (-1, 0.5, 0.5)]
    nrm = [(0, 0, 2)] * 4  # not unit length: the reader normalises
    uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
    idx = [0, 1, 2, 0, 2, 3]
    binc = b"".join(struct.pack("<3f", *p) for p in pos) + b"".join(struct.pack("<3f", *n) for n in nrm)
    binc += b"".join(struct.pack("<2f", *t) for t in uv) + struct.pack("<6H", *idx)
    views = [{"buffer": 0, "byteOffset": 0, "byteLength": 48}, {"buffer": 0, "byteOffset": 48, "byteLength": 48},
             {"buffer": 0, "byteOffset": 96, "byteLength": 32}, {"buffer": 0, "byteOffset": 128, "byteLength": 12}]
    accessors = [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
                 {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
                 {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
                 {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}]
    prims = [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": m}
             for m in primitives_materials]
    gltf = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"name": "root", "scale": [2, 2, 2], "children": [1]},
                      {"name": "stable", "mesh": 0, "translation": [1, 0.5, -1]}],
            "meshes": [{"primitives": prims}],
            "materials": [{"name": "m%d" % i} for i in range(max(primitives_materials) + 1)],
            "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(binc)}]}
    write_glb(path, gltf, binc)


def extent(groups, k):
    vals = [p[k] for g in groups for p in g["pos"]]
    return min(vals), max(vals)


class StaticGroupsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "prop.glb")

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_still_mesh_is_fitted_to_its_width_on_the_ground_at_the_origin(self):
        prop_glb(self.path)
        groups, report = gltf_in.load_static_groups(self.path, width=3.0)
        self.assertEqual(len(groups), 1)
        g = groups[0]
        self.assertIsNone(g.get("skin"))
        self.assertEqual(g["material"], 0)
        x0, x1 = extent(groups, 0)
        y0, y1 = extent(groups, 1)
        z0, z1 = extent(groups, 2)
        self.assertAlmostEqual(x1 - x0, 3.0, places=5)  # the widest side (x, 4 in the file) is 3 m
        self.assertAlmostEqual(z1 - z0, 1.5, places=5)
        self.assertAlmostEqual(y0, 0.0, places=5)       # on the ground
        self.assertAlmostEqual(y1, 0.75, places=5)
        self.assertAlmostEqual(x0 + x1, 0.0, places=5)  # centred
        self.assertAlmostEqual(z0 + z1, 0.0, places=5)
        self.assertEqual(report["size"], (3.0, 0.75, 1.5))
        self.assertEqual(g["uv"], [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])
        for n in g["nrm"]:
            self.assertAlmostEqual(math.sqrt(sum(c * c for c in n)), 1.0, places=5)
        self.assertEqual(len(g["tan"]), 4)
        self.assertEqual(g["lods"], [[(0, 1, 2), (0, 2, 3)]])

    def test_game_space_is_the_file_mirrored_in_x(self):
        # the file's first vertex is its -x, -z corner (world (8, 6, -3)); fitted and mirrored it is at +x, -z
        prop_glb(self.path)
        groups, _ = gltf_in.load_static_groups(self.path, width=4.0)
        p = groups[0]["pos"][0]
        self.assertAlmostEqual(p[0], 2.0, places=5)
        self.assertAlmostEqual(p[1], 0.0, places=5)
        self.assertAlmostEqual(p[2], -1.0, places=5)

    def test_yaw_turns_it_about_the_vertical(self):
        prop_glb(self.path)
        groups, report = gltf_in.load_static_groups(self.path, width=4.0, yaw=90.0)
        x0, x1 = extent(groups, 0)
        z0, z1 = extent(groups, 2)
        self.assertAlmostEqual(x1 - x0, 2.0, places=5)
        self.assertAlmostEqual(z1 - z0, 4.0, places=5)
        self.assertEqual(report["size"], (2.0, 1.0, 4.0))

    def test_materials_follow_the_file_or_all_go_to_one_slot(self):
        prop_glb(self.path, primitives_materials=(0, 1))
        groups, _ = gltf_in.load_static_groups(self.path, width=4.0)
        self.assertEqual([g["material"] for g in groups], [0, 1])
        groups, _ = gltf_in.load_static_groups(self.path, width=4.0, material=2)
        self.assertEqual([g["material"] for g in groups], [2])
        self.assertEqual(len(groups[0]["pos"]), 8)
        with self.assertRaises(ValueError):
            gltf_in.load_static_groups(self.path, width=4.0, material_count=1)

    def test_a_file_without_meshes_is_refused(self):
        write_glb(self.path, {"asset": {"version": "2.0"}, "nodes": [{"name": "empty"}]}, b"")
        with self.assertRaises(ValueError):
            gltf_in.load_static_groups(self.path, width=4.0)


if __name__ == "__main__":
    unittest.main()
