"""tools/models/blender/render_views.py: pictures of a model in its rest pose from several sides (Meshy's input).
Needs Blender (environment variable BLENDER, or blender on PATH): skipped without."""
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
SCRIPT = os.path.join(ROOT, "tools", "models", "blender", "render_views.py")


def render(model, out, *extra):
    run = subprocess.run([BLENDER, "--background", "--factory-startup", "--python-exit-code", "1", "--python", SCRIPT,
                          "--", model, out, *extra], capture_output=True, text=True, timeout=600)
    line = next((x for x in run.stdout.splitlines() if x.startswith("VIEWS ")), None)
    return run, json.loads(line[6:]) if line else None


@unittest.skipUnless(BLENDER and os.path.exists(rig.WOLF), "needs Blender and the CC0 wolf")
class RenderViewTests(unittest.TestCase):
    def test_three_sides_of_the_wolf(self):
        with tempfile.TemporaryDirectory() as tmp:
            gltf, binc = gltf_in.read_glb(rig.WOLF)
            copy = os.path.join(tmp, "wolf.glb")
            ns.write_glb(gltf, binc, ns.build(gltf, binc, scale=0.5), copy)
            run, views = render(copy, os.path.join(tmp, "views"), "--size", "512")
            self.assertEqual(run.returncode, 0, run.stdout[-1500:] + run.stderr[-1500:])
            self.assertEqual([v["view"] for v in views], ["side", "three_quarter", "front"])
            for v in views:
                self.assertTrue(os.path.exists(v["file"]), v)
                self.assertEqual((v["width"], v["height"]), (512, 512))
                self.assertGreater(v["fill"], 0.75, v)  # the model spans most of the picture's width or height
                self.assertLess(v["fill"], 0.95, v)  # and leaves a margin
                self.assertGreater(v["covered"], 0.05, v)
                self.assertLess(abs(v["roll"]), 3.0, v)  # upright: the camera does not roll

    def test_chosen_views(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = rig.small_rig(os.path.join(tmp, "rig.glb"))
            gltf, binc = gltf_in.read_glb(src)
            copy = os.path.join(tmp, "copy.glb")
            ns.write_glb(gltf, binc, ns.build(gltf, binc), copy)
            run, views = render(copy, os.path.join(tmp, "views"), "--views", "back,side", "--size", "256")
            self.assertEqual(run.returncode, 0, run.stdout[-1500:] + run.stderr[-1500:])
            self.assertEqual([v["view"] for v in views], ["back", "side"])


if __name__ == "__main__":
    unittest.main()
