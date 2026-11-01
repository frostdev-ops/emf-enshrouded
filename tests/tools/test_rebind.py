"""tools/models/blender/rebind_to_rig.py: a mesh made elsewhere (Meshy, from pictures of a rig's rest pose) lined up
with the rig's own mesh and skinned with its weights. Needs Blender (environment variable BLENDER, or blender on PATH)
and the CC0 wolf: skipped without."""
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
SCRIPT = os.path.join(ROOT, "tools", "models", "blender", "rebind_to_rig.py")


def blender(script, *args):
    run = subprocess.run([BLENDER, "--background", "--factory-startup", "--python-exit-code", "1", "--python", script,
                          "--", *args], capture_output=True, text=True, timeout=900)
    return run


@unittest.skipUnless(BLENDER and os.path.exists(rig.WOLF), "needs Blender and the CC0 wolf")
class RebindTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        gltf, binc = gltf_in.read_glb(rig.WOLF)
        cls.sk = ns.build(gltf, binc, scale=0.5)
        cls.hier = ns.hierarchy(cls.sk, "33333333-3333-4333-8333-333333333333")
        cls.rig_glb = os.path.join(cls.tmp.name, "wolf_rig.glb")
        ns.write_glb(gltf, binc, cls.sk, cls.rig_glb)
        cls.moved = os.path.join(cls.tmp.name, "wolf_moved.glb")
        run = blender(os.path.join(HERE, "blender_unskinned_copy.py"), cls.rig_glb, cls.moved, "90", "1.7", "3", "-2", "1")
        assert run.returncode == 0, run.stdout[-1500:] + run.stderr[-1500:]
        cls.out = os.path.join(cls.tmp.name, "rebound.glb")
        cls.result = blender(SCRIPT, cls.rig_glb, cls.moved, cls.out, "--material", "1")
        line = next((x for x in cls.result.stdout.splitlines() if x.startswith("REBIND ")), None)
        cls.report = json.loads(line[7:]) if line else None

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_mesh_is_lined_up_with_the_rig(self):
        self.assertEqual(self.result.returncode, 0, self.result.stdout[-2000:] + self.result.stderr[-2000:])
        r = self.report
        self.assertAlmostEqual(r["scale"], 1 / 1.7, delta=0.01)
        self.assertIn(r["yaw"], (-90, 270))
        self.assertLess(r["mean_distance"], 0.003)  # metres, in the rig's size
        self.assertLess(r["mean_distance_back"], 0.003)
        self.assertEqual(r["unweighted"], 0)

    def test_the_mesh_takes_the_game_material_slot(self):
        """--material N names the materials game_material_N: gltf_in puts the mesh in that slot of the source model."""
        gltf, _ = gltf_in.read_glb(self.out)
        self.assertTrue(all(gltf_in.re.match(r"^game_material_1(\.\d+)?$", m["name"]) for m in gltf["materials"]),
                        [m["name"] for m in gltf["materials"]])
        groups, _ = gltf_in.load_groups(self.out, self.hier, warn=lambda m: None)
        self.assertEqual([g["material"] for g in groups], [1])

    def test_every_vertex_keeps_its_weights(self):
        """The same mesh comes back with the artist's weights: the same joints and weights per vertex."""
        self.assertEqual(self.result.returncode, 0, self.result.stdout[-2000:] + self.result.stderr[-2000:])
        want, _ = gltf_in.load_groups(self.rig_glb, self.hier)
        got, report = gltf_in.load_groups(self.out, self.hier, warn=lambda m: None)
        self.assertLess(report["max_rest_difference"], 1e-3)
        index = {}
        for g in want:
            for p, s in zip(g["pos"], g["skin"]):
                index[tuple(round(c, 3) for c in p)] = dict(zip(*s))
        same = close = n = 0
        for g in got:
            for p, s in zip(g["pos"], g["skin"]):
                w = index.get(tuple(round(c, 3) for c in p))
                if w is None:
                    continue
                n += 1
                mine = dict(zip(*s))
                same += max(mine, key=mine.get) == max(w, key=w.get)
                close += all(abs(mine.get(j, 0) - w.get(j, 0)) < 0.05 for j in set(mine) | set(w))
        self.assertGreater(n, 3000)
        self.assertGreater(same / n, 0.97)
        self.assertGreater(close / n, 0.9)


if __name__ == "__main__":
    unittest.main()
