"""tools/models/blender/fit_to_game_skeleton.py on a test animal and the game boar: detail bones (the boar's tail) keep
weight only on the animal's own tail. Needs Blender (environment variable BLENDER, or blender on PATH) and the boar glb
made by gamegltf.py from a model_survey dump (analysis/models/game_enemy_wildboar_01.glb, not in the repository):
skipped without."""
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
BLENDER_DIR = os.path.join(ROOT, "tools", "models", "blender")
BOAR = os.path.join(ROOT, "analysis", "models", "game_enemy_wildboar_01.glb")
BLENDER = os.environ.get("BLENDER") or shutil.which("blender")
TAIL_ROOT = "j044"  # the boar's tail: j044 < j045 < j046 < j047 under the pelvis


def blender(script, *args):
    path = script if os.path.isabs(script) else os.path.join(BLENDER_DIR, script)
    out = subprocess.run([BLENDER, "--background", "--factory-startup", "--python-exit-code", "1",
                          "--python", path, "--", *args], capture_output=True, text=True, timeout=600)
    return out.returncode, out.stdout + out.stderr


@unittest.skipUnless(BLENDER and os.path.exists(BOAR), "needs Blender and analysis/models/game_enemy_wildboar_01.glb")
class FitQuadrupedTests(unittest.TestCase):
    def test_the_rump_leaves_the_tail_bones_and_the_tail_keeps_them(self):
        """Weights copied from the nearest point of the game animal's skin put a big rump on the boar's small tail
        (it starts at the boar's rump), so the rump swings with the tail as the boar walks."""
        with tempfile.TemporaryDirectory() as d:
            fig, fitted = os.path.join(d, "animal.glb"), os.path.join(d, "fitted.glb")
            code, log = blender("make_test_quadruped.py", fig)
            self.assertEqual(code, 0, log[-2000:])
            code, log = blender("fit_to_game_skeleton.py", BOAR, fig, fitted)
            self.assertEqual(code, 0, log[-2000:])
            code, log = blender(os.path.join(HERE, "blender_tail_weights.py"), fitted, TAIL_ROOT)
            self.assertEqual(code, 0, log[-2000:])
            line = next(l for l in log.splitlines() if l.startswith("TAILW "))
            _, rump, tail = line.split()
            self.assertLess(float(rump), 0.02, line)  # the rump: none of the tail's weight
            self.assertGreater(float(tail), 0.3, line)  # the animal's own tail: on the tail bones

    def test_with_tail_hips_the_tail_follows_the_hips(self):
        """A long tail driven by a short tail's animation moves strangely (the aurochs on the boar): --tail hips gives
        the creature's whole tail to the hips."""
        with tempfile.TemporaryDirectory() as d:
            fig, fitted = os.path.join(d, "animal.glb"), os.path.join(d, "fitted.glb")
            self.assertEqual(blender("make_test_quadruped.py", fig)[0], 0)
            code, log = blender("fit_to_game_skeleton.py", BOAR, fig, fitted, "--tail", "hips")
            self.assertEqual(code, 0, log[-2000:])
            code, log = blender(os.path.join(HERE, "blender_tail_weights.py"), fitted, TAIL_ROOT)
            self.assertEqual(code, 0, log[-2000:])
            line = next(l for l in log.splitlines() if l.startswith("TAILW "))
            _, rump, tail = line.split()
            self.assertLess(float(rump), 0.02, line)
            self.assertLess(float(tail), 0.02, line)


if __name__ == "__main__":
    unittest.main()
