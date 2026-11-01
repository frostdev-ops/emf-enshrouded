"""tools/models/gltf_clips.py: a glTF animation made into a game clip for a skeleton from new_skeleton.py."""
import math
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import gltf_clips as gc  # noqa: E402
import gltf_in  # noqa: E402
import joint_animation as ja  # noqa: E402
import new_skeleton as ns  # noqa: E402
import own_skeleton as osk  # noqa: E402
import test_new_skeleton as rig  # noqa: E402

WOLF = rig.WOLF
HASH = 0x1234567890ABCDEF


def about_y(deg):
    a = math.radians(deg) / 2
    return (0.0, math.sin(a), 0.0, math.cos(a))


def add_animation(path, channels):
    """The small rig with one animation "Move": channels [(node, path, interpolation, times, values)]."""
    gltf, binc = gltf_in.read_glb(path)
    binc = bytearray(binc)
    anim = {"name": "Move", "channels": [], "samplers": []}
    for node, what, interp, times, values in channels:
        accs = []
        for data, kind, n in ((times, "SCALAR", 1), (values, "VEC4" if what == "rotation" else "VEC3", None)):
            flat = [x for v in data for x in (v if isinstance(v, (tuple, list)) else (v,))]
            while len(binc) % 4:
                binc.append(0)
            gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(binc), "byteLength": 4 * len(flat)})
            binc.extend(struct.pack("<%df" % len(flat), *flat))
            acc = {"bufferView": len(gltf["bufferViews"]) - 1, "componentType": 5126, "count": len(data), "type": kind}
            if kind == "SCALAR":
                acc["min"], acc["max"] = [min(data)], [max(data)]
            gltf["accessors"].append(acc)
            accs.append(len(gltf["accessors"]) - 1)
        anim["samplers"].append({"input": accs[0], "output": accs[1], "interpolation": interp})
        anim["channels"].append({"sampler": len(anim["samplers"]) - 1, "target": {"node": node, "path": what}})
    gltf["animations"] = [anim]
    gltf["buffers"][0]["byteLength"] = len(binc)
    rig.write_glb(path, gltf, bytes(binc))
    return gltf_in.read_glb(path)


class Fixture(unittest.TestCase):
    CHANNELS = [
        (3, "rotation", "LINEAR", [0.0, 1.0], [about_y(90), about_y(150)]),  # the head turns on: 90 to 150 degrees
        (4, "translation", "LINEAR", [0.0, 1.0], [(0.0, 0.0, -0.004), (0.0, 0.002, -0.004)]),  # the tail lifts 0.2 m
        (5, "translation", "STEP", [0.0, 0.5], [(0.002, 0.0, 0.0), (0.004, 0.0, 0.0)]),  # the helper jumps at 0.5 s
    ]

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        path = rig.small_rig(os.path.join(self.tmp.name, "rig.glb"))
        self.gltf, self.binc = add_animation(path, self.CHANNELS)
        self.sk = ns.build(self.gltf, self.binc)

    def tearDown(self):
        self.tmp.cleanup()

    def world(self, t, sk=None):
        sk = sk or self.sk
        return osk.world(gc.pose_at(self.gltf, self.binc, sk, "Move", t), sk["parents"])


class PoseTests(Fixture):
    def test_names(self):
        self.assertEqual(gc.names(self.gltf), ["Move"])

    def test_linear_rotation_at_keys_and_halfway(self):
        for t, deg in ((0.0, 90), (0.5, 120), (1.0, 150)):
            q = self.world(t)[2][0]
            self.assertTrue(rig.same_rotation(q, ja_mirror(about_y(deg)), 1e-9), "%s s: %s" % (t, q))

    def test_linear_translation_halfway(self):
        for a, b in zip(self.world(0.5)[3][1], (0.0, 1.1, -0.4)):
            self.assertAlmostEqual(a, b, places=6)

    def test_step_holds_until_the_next_key(self):
        self.assertAlmostEqual(self.world(0.49)[4][1][0], -0.2, places=6)
        self.assertAlmostEqual(self.world(0.5)[4][1][0], -0.4, places=6)

    def test_scale_resizes_the_motion(self):
        sk = ns.build(self.gltf, self.binc, scale=0.5)
        for a, b in zip(self.world(0.5, sk)[3][1], (0.0, 0.55, -0.2)):
            self.assertAlmostEqual(a, b, places=6)

    def test_before_and_after_the_keys(self):
        for a, b in zip(self.world(-1.0)[3][1], self.world(0.0)[3][1]):
            self.assertAlmostEqual(a, b, places=9)
        for a, b in zip(self.world(5.0)[3][1], self.world(1.0)[3][1]):
            self.assertAlmostEqual(a, b, places=9)

    def test_unknown_animation(self):
        with self.assertRaises(KeyError):
            gc.pose_at(self.gltf, self.binc, self.sk, "Fly", 0.0)


class CubicTests(Fixture):
    # in-tangent, value, out-tangent per key; the value comes back at its key
    CHANNELS = [(4, "translation", "CUBICSPLINE", [0.0, 1.0],
                 [(0.0, 0.0, 0.0), (0.0, 0.0, -0.004), (0.0, 0.01, 0.0),
                  (0.0, -0.01, 0.0), (0.0, 0.002, -0.004), (0.0, 0.0, 0.0)])]

    def test_values_at_keys(self):
        for t, y in ((0.0, 1.0), (1.0, 1.2)):
            self.assertAlmostEqual(self.world(t)[3][1][1], y, places=6)

    def test_tangents_bend_between_keys(self):
        self.assertGreater(self.world(0.5)[3][1][1], 1.1 + 0.05)


def ja_mirror(q):
    return (q[0], -q[1], -q[2], q[3])


class ClipTests(Fixture):
    def test_clip_header(self):
        c = gc.clip(self.gltf, self.binc, self.sk, "Move", HASH)
        self.assertEqual((c["hierarchyHash"], c["fps"], c["frameCount"], c["jointCount"], c["flags"]),
                         (HASH, 60, 60, 5, 0))
        self.assertEqual(gc.clip(self.gltf, self.binc, self.sk, "Move", HASH, loop=True)["flags"], 1)

    def test_clip_plays_the_animation(self):
        c = gc.clip(self.gltf, self.binc, self.sk, "Move", HASH)
        for frame, t in ((0, 0.0), (30, 0.5), (45, 0.75)):
            got = osk.world(ja.sample(c, frame, self.sk["rest"]), self.sk["parents"])
            want = self.world(t)
            for j in range(5):
                self.assertTrue(rig.same_rotation(got[j][0], want[j][0], 1e-6), "frame %d joint %d" % (frame, j))
                for a, b in zip(got[j][1], want[j][1]):
                    self.assertAlmostEqual(a, b, delta=1e-3)
        end = osk.world(ja.sample_end(c, self.sk["rest"]), self.sk["parents"])
        self.assertTrue(rig.same_rotation(end[2][0], ja_mirror(about_y(150)), 1e-6))

    def test_loop_without_its_closing_key_wraps_over_one_more_step(self):
        """An animation whose last key is not its first again (exported without the closing frame) loops over one
        more key step back to its first pose."""
        self.assertEqual(gc.duration(self.gltf, self.binc, "Move", loop=True), 2.0)
        c = gc.clip(self.gltf, self.binc, self.sk, "Move", HASH, loop=True)
        self.assertEqual(c["frameCount"], 120)
        for frame, deg in ((60, 150), (90, 120), (120, 90)):
            pose = ja.sample_end(c, self.sk["rest"]) if frame == 120 else ja.sample(c, frame, self.sk["rest"])
            q = osk.world(pose, self.sk["parents"])[2][0]
            self.assertTrue(rig.same_rotation(q, ja_mirror(about_y(deg)), 1e-6), "frame %d: %s" % (frame, q))

    def test_frames_cover_the_duration(self):
        self.assertEqual(gc.duration(self.gltf, self.binc, "Move"), 1.0)
        self.assertEqual(gc.frame_count(self.gltf, self.binc, "Move", 60), 60)
        self.assertEqual(gc.clip(self.gltf, self.binc, self.sk, "Move", HASH, fps=24)["frameCount"], 24)


@unittest.skipUnless(os.path.exists(WOLF), "the CC0 wolf is not in analysis/models/sources")
class WolfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gltf, cls.binc = gltf_in.read_glb(WOLF)
        cls.sk = ns.build(cls.gltf, cls.binc, scale=0.5)

    def test_loops_close(self):
        """Walk and Gallop end one key before their first pose, Idle on it: every looped clip ends where it starts."""
        for name, extra in (("Walk", 1 / 24), ("Gallop", 1 / 24), ("Idle", 0.0)):
            self.assertAlmostEqual(gc.duration(self.gltf, self.binc, name, loop=True),
                                   gc.duration(self.gltf, self.binc, name) + extra, places=4, msg=name)
            c = gc.clip(self.gltf, self.binc, self.sk, name, HASH, loop=True)
            first = osk.world(ja.sample(c, 0, self.sk["rest"]), self.sk["parents"])
            last = osk.world(ja.sample_end(c, self.sk["rest"]), self.sk["parents"])
            for j in range(len(first)):
                for a, b in zip(first[j][1], last[j][1]):
                    self.assertAlmostEqual(a, b, delta=1e-3, msg="%s %s" % (name, self.sk["names"][j]))

    def test_every_animation_has_a_name(self):
        for name in ("Idle", "Walk", "Gallop", "Attack", "Death", "Eating", "Idle_HitReact_Left", "Jump_ToIdle"):
            self.assertIn(name, gc.names(self.gltf))

    def test_walk_clip_plays_the_walk(self):
        c = gc.clip(self.gltf, self.binc, self.sk, "Walk", HASH, loop=True)
        self.assertEqual(c["frameCount"], gc.frame_count(self.gltf, self.binc, "Walk", 60, loop=True))
        self.assertEqual(c["frameCount"], 65)  # 25 key steps and the step back to the first, at 24 per second
        duration = gc.duration(self.gltf, self.binc, "Walk", loop=True)
        for frame in (0, 20, 41, 64):
            t = duration * frame / c["frameCount"]
            got = osk.world(ja.sample(c, frame, self.sk["rest"]), self.sk["parents"])
            want = osk.world(gc.pose_at(self.gltf, self.binc, self.sk, "Walk", t, loop=True), self.sk["parents"])
            for j in range(len(got)):
                self.assertTrue(rig.same_rotation(got[j][0], want[j][0], 1e-5), "frame %d %s" % (frame, self.sk["names"][j]))
                for a, b in zip(got[j][1], want[j][1]):
                    self.assertAlmostEqual(a, b, delta=2e-3)


if __name__ == "__main__":
    unittest.main()
