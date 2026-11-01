"""tools/models/blender/palette_texture.py: a flat-coloured model gets a palette texture and UVs. Needs Blender
(environment variable BLENDER, or blender on PATH): skipped without."""
import json
import os
import shutil
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
import test_new_skeleton as rig  # noqa: E402

BLENDER = os.environ.get("BLENDER") or shutil.which("blender")
SCRIPT = os.path.join(ROOT, "tools", "models", "blender", "palette_texture.py")


def two_colour_rig(path):
    """The small rig of test_new_skeleton.py with its triangle twice, in two materials (glTF base colours, linear)."""
    gltf, binc = gltf_in.read_glb(rig.small_rig(path))
    prim = gltf["meshes"][0]["primitives"][0]
    gltf["meshes"][0]["primitives"] = [dict(prim, material=0), dict(prim, material=1)]
    gltf["materials"] = [{"name": "Fur", "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.2, 0.2, 1.0]}},
                         {"name": "Nose", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.0, 0.0, 1.0]}}]
    rig.write_glb(path, gltf, binc)
    return path


@unittest.skipUnless(BLENDER, "needs Blender")
class MaterialColourTests(unittest.TestCase):
    def test_material_colours_become_the_palette(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = two_colour_rig(os.path.join(tmp, "rig.glb"))
            gltf, binc = gltf_in.read_glb(src)
            sk = ns.build(gltf, binc)
            copy = os.path.join(tmp, "copy.glb")
            ns.write_glb(gltf, binc, sk, copy)
            out, tex = os.path.join(tmp, "palette.glb"), os.path.join(tmp, "tex")
            run = subprocess.run([BLENDER, "--background", "--factory-startup", "--python-exit-code", "1", "--python",
                                  SCRIPT, "--", copy, out, tex, "--material-colors"],
                                 capture_output=True, text=True, timeout=600)
            self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr[-2000:])
            line = next(x for x in run.stdout.splitlines() if x.startswith("PALETTE "))
            self.assertIn("PALETTE 2 colours", line)
            colours = sorted(json.loads(line.split("colours: ", 1)[1]))
            for got, want in zip(colours, ([0.4845] * 3, [1.0, 0.0, 0.0])):  # sRGB of the linear base colours
                for a, b in zip(got, want):
                    self.assertAlmostEqual(a, b, delta=0.002)
            self.assertTrue(os.path.exists(os.path.join(tex, "albedo_mip00.png")))
            groups, report = gltf_in.load_groups(out, ns.hierarchy(sk, "22222222-2222-4222-8222-222222222222"),
                                                 warn=self.fail)
            self.assertEqual(len(groups), 1)
            self.assertLess(report["max_rest_difference"], 1e-3)
            self.assertEqual(len({tuple(uv) for uv in groups[0]["uv"]}), 2)

    def test_without_colours_it_says_what_to_do(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = two_colour_rig(os.path.join(tmp, "rig.glb"))
            run = subprocess.run([BLENDER, "--background", "--factory-startup", "--python-exit-code", "1", "--python",
                                  SCRIPT, "--", src, os.path.join(tmp, "o.glb"), os.path.join(tmp, "t")],
                                 capture_output=True, text=True, timeout=600)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("--material-colors", run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
