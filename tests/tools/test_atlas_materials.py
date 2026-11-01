"""tools/models/blender/atlas_materials.py: a model whose parts wear several textures gets one texture (an atlas of
them) and UVs into it, keeping its skin. Needs Blender (environment variable BLENDER, or blender on PATH): skipped
without."""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "models"))
import gltf_in  # noqa: E402
import new_skeleton as ns  # noqa: E402
import png_crop  # noqa: E402
import test_new_skeleton as rig  # noqa: E402

BLENDER = os.environ.get("BLENDER") or shutil.which("blender")
SCRIPT = os.path.join(ROOT, "tools", "models", "blender", "atlas_materials.py")
RED, BLUE = (220, 30, 30), (30, 40, 210)


def solid_png(path, rgb, size=8):
    png_crop.write_png(path, size, size, [bytearray(rgb * size) for _ in range(size)])
    with open(path, "rb") as f:
        return f.read()


def two_texture_rig(path, tmp, unlit=False):
    """The small rig of test_new_skeleton.py with its triangle twice, each in a material with its own image texture
    (solid red, solid blue), UVs inside the image; unlit: the second material unlit (KHR_materials_unlit, a scan's
    baked light: Blender draws it with an emission shader)."""
    gltf, binc = gltf_in.read_glb(rig.small_rig(path))
    binc = bytearray(binc)

    def view(data):
        binc.extend(b"\0" * ((-len(binc)) % 4))
        gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(binc), "byteLength": len(data)})
        binc.extend(data)
        return len(gltf["bufferViews"]) - 1

    uv = view(struct.pack("<6f", 0.1, 0.1, 0.9, 0.1, 0.5, 0.9))
    gltf["accessors"].append({"bufferView": uv, "componentType": 5126, "count": 3, "type": "VEC2"})
    uv_acc = len(gltf["accessors"]) - 1
    gltf["images"] = [{"bufferView": view(solid_png(os.path.join(tmp, n + ".png"), c)), "mimeType": "image/png"}
                      for n, c in (("red", RED), ("blue", BLUE))]
    gltf["textures"] = [{"source": 0}, {"source": 1}]
    gltf["materials"] = [{"name": "Body", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}},
                         {"name": "Mouth", "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}}}]
    if unlit:
        gltf["materials"][1]["extensions"] = {"KHR_materials_unlit": {}}
        gltf["extensionsUsed"] = ["KHR_materials_unlit"]
    prim = gltf["meshes"][0]["primitives"][0]
    prim["attributes"]["TEXCOORD_0"] = uv_acc
    gltf["meshes"][0]["primitives"] = [dict(prim, material=0), dict(prim, material=1)]
    gltf["buffers"][0]["byteLength"] = len(binc)
    rig.write_glb(path, gltf, bytes(binc))
    return path


def sample(png, u, v):
    w, h, bpp, rows = png
    x, y = min(w - 1, int(u * w)), min(h - 1, int(v * h))  # glTF UVs: v goes down the image
    return tuple(rows[y][x * bpp:x * bpp + 3])


@unittest.skipUnless(BLENDER, "needs Blender")
class AtlasTests(unittest.TestCase):
    UNLIT = False

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        src = two_texture_rig(os.path.join(cls.tmp.name, "rig.glb"), cls.tmp.name, unlit=cls.UNLIT)
        gltf, binc = gltf_in.read_glb(src)
        cls.sk = ns.build(gltf, binc)
        copy = os.path.join(cls.tmp.name, "copy.glb")
        ns.write_glb(gltf, binc, cls.sk, copy)
        cls.out = os.path.join(cls.tmp.name, "atlas.glb")
        cls.png = os.path.join(cls.tmp.name, "atlas.png")
        cls.result = subprocess.run([BLENDER, "--background", "--factory-startup", "--python-exit-code", "1", "--python",
                                  SCRIPT, "--", copy, cls.out, cls.png, "--size", "64", "--material", "1"],
                                 capture_output=True, text=True, timeout=600)
        line = next((x for x in cls.result.stdout.splitlines() if x.startswith("ATLAS ")), None)
        cls.report = json.loads(line[6:]) if line else None

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_one_texture_for_every_part(self):
        self.assertEqual(self.result.returncode, 0, self.result.stdout[-2000:] + self.result.stderr[-2000:])
        self.assertEqual(self.report["size"], 64)
        self.assertEqual(self.report["grid"], 2)  # two textures: a 2 x 2 grid of cells
        self.assertEqual([m["name"] for m in self.report["materials"]], ["Body", "Mouth"])
        self.assertEqual(self.report["uv_outside"], 0)
        gltf, _ = gltf_in.read_glb(self.out)
        self.assertEqual([m["name"] for m in gltf["materials"]], ["game_material_1"])
        w, h, _, _ = png_crop.read_png(self.png)
        self.assertEqual((w, h), (64, 64))

    def test_each_part_still_wears_its_own_colours(self):
        png = png_crop.read_png(self.png)
        groups, report = gltf_in.load_groups(self.out, ns.hierarchy(self.sk, "22222222-2222-4222-8222-222222222222"),
                                             warn=self.fail)
        self.assertEqual(len(groups), 1)
        self.assertEqual(groups[0]["material"], 1)
        colours = sorted(sample(png, u, v) for u, v in groups[0]["uv"])
        self.assertEqual(colours, sorted([RED] * 3 + [BLUE] * 3))

    def test_the_skin_is_kept(self):
        _, report = gltf_in.load_groups(self.out, ns.hierarchy(self.sk, "22222222-2222-4222-8222-222222222222"),
                                        warn=self.fail)
        self.assertLess(report["max_rest_difference"], 1e-3)


class UnlitAtlasTests(AtlasTests):
    """The same with the second material unlit: its texture still goes into the atlas."""
    UNLIT = True


if __name__ == "__main__":
    unittest.main()
