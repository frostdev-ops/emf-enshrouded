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


class InPlaceTests(Fixture):
    """A walk that carries its creature forward (root motion: the hips travel 1 m and rise 0.2 m) played in place:
    the game moves creatures itself, and a loop that travels would jump back at every turn."""
    CHANNELS = [(2, "translation", "LINEAR", [0.0, 1.0], [(0.0, 0.0, 0.01), (0.0, 0.01, 0.012)])]

    def hips(self, c, frame):
        return osk.world(ja.sample(c, frame, self.sk["rest"]), self.sk["parents"])[1][1]

    def test_the_travel_is_taken_out_and_the_rest_kept(self):
        moving = gc.clip(self.gltf, self.binc, self.sk, "Move", HASH)
        self.assertAlmostEqual(self.hips(moving, 60)[2] - self.hips(moving, 0)[2], -1.0, delta=1e-3)
        c = gc.clip(self.gltf, self.binc, self.sk, "Move", HASH, in_place="Hips")
        for frame in (0, 30, 60):
            x, y, z = self.hips(c, frame)
            self.assertAlmostEqual(x, 0.0, delta=1e-3)
            self.assertAlmostEqual(z, 0.0, delta=1e-3)          # no travel along the ground
            self.assertAlmostEqual(y, 1.0 + 0.2 * frame / 60, delta=1e-3)  # the rise is kept

    def test_a_clip_that_starts_away_from_its_spot_comes_back_over_it(self):
        """The last part of a charge (the Minotaur's RamAttack) starts where the run before it ended, 3 m on, and
        stays there: in place, the joint starts over its resting spot."""
        gltf, binc = add_animation(rig.small_rig(os.path.join(self.tmp.name, "off.glb")),
                                   [(2, "translation", "LINEAR", [0.0, 1.0], [(0.0, 0.03, 0.01), (0.0, 0.03, 0.01)])])
        sk = ns.build(gltf, binc)
        moved = gc.clip(gltf, binc, sk, "Move", HASH)
        hips = lambda c, f: osk.world(ja.sample(c, f, sk["rest"]), sk["parents"])[1][1]
        self.assertAlmostEqual(hips(moved, 0)[2], -3.0, delta=1e-3)  # 3 m away (the armature turns its y into -z)
        c = gc.clip(gltf, binc, sk, "Move", HASH, in_place="Hips")
        for frame in (0, 30, 60):
            x, y, z = hips(c, frame)
            self.assertAlmostEqual(x, 0.0, delta=1e-3)
            self.assertAlmostEqual(z, 0.0, delta=1e-3)
            self.assertAlmostEqual(y, 1.0, delta=1e-3)

    def test_the_joint_must_be_ours(self):
        with self.assertRaises(KeyError):
            gc.clip(self.gltf, self.binc, self.sk, "Move", HASH, in_place="Pelvis")

    def test_the_speed_of_the_travel(self):
        self.assertAlmostEqual(gc.travel_speed(self.gltf, self.binc, self.sk, "Move", "Hips"), 1.0, places=3)


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


class TimelineTests(Fixture):
    """timeline: a clip that times an animation's moments, keys [at, animation, time]: a swing that lands when a
    game attack hits, two swings in one combo."""

    def head_degrees(self, c, frame):
        """The head's turn about y at a frame (the fixture turns it about y only)."""
        pose = ja.sample_end(c, self.sk["rest"]) if frame is None else ja.sample(c, frame, self.sk["rest"])
        q = osk.world(pose, self.sk["parents"])[2][0]
        self.assertAlmostEqual(q[0], 0.0, delta=1e-4)
        self.assertAlmostEqual(q[2], 0.0, delta=1e-4)
        return math.degrees(2 * math.atan2(-q[1], q[3]))  # ja_mirror(about_y(deg))

    def test_between_two_keys_forward_in_one_animation_it_plays_that_stretch(self):
        """Move's first quarter over the first half second, the rest over the second half."""
        c = gc.timeline(self.gltf, self.binc, self.sk, [[0.0, "Move", 0.0], [0.5, "Move", 0.25], [1.0, "Move", 1.0]],
                        HASH)
        self.assertEqual((c["frameCount"], c["flags"], c["hierarchyHash"], c["fps"]), (60, 0, HASH, 60))
        for frame, t in ((0, 0.0), (15, 0.125), (30, 0.25), (45, 0.625)):
            got = osk.world(ja.sample(c, frame, self.sk["rest"]), self.sk["parents"])
            want = self.world(t)
            for j in range(5):
                self.assertTrue(rig.same_rotation(got[j][0], want[j][0], 1e-6), "frame %d joint %d" % (frame, j))
                for a, b in zip(got[j][1], want[j][1]):
                    self.assertAlmostEqual(a, b, delta=1e-3, msg="frame %d joint %d" % (frame, j))

    def test_between_keys_that_do_not_play_forward_it_blends_eased(self):
        """Back in time (a swing again from its start) or to another animation: the one pose blends into the other,
        eased in and out (3u^2 - 2u^3), each held."""
        c = gc.timeline(self.gltf, self.binc, self.sk, [[0.0, "Move", 1.0], [0.5, "Move", 1.0], [1.0, "Move", 0.0]],
                        HASH)
        for frame, deg in ((15, 150.0), (30, 150.0), (36, 150.0 - 60.0 * 0.104), (45, 120.0), (None, 90.0)):
            self.assertAlmostEqual(self.head_degrees(c, frame), deg, delta=0.05, msg="frame %s" % frame)

    def test_two_keys_at_one_moment_cut(self):
        """Two keys at the same moment: the clip cuts from the one to the other there."""
        c = gc.timeline(self.gltf, self.binc, self.sk,
                        [[0.0, "Move", 0.0], [0.5, "Move", 0.5], [0.5, "Move", 0.0], [1.0, "Move", 0.5]], HASH)
        for frame, deg in ((29, 119.0), (30, 90.0), (45, 105.0)):
            self.assertAlmostEqual(self.head_degrees(c, frame), deg, delta=0.05, msg="frame %d" % frame)

    def test_keys_start_at_zero_and_go_forward(self):
        for keys in ([[0.0, "Move", 0.0]],
                     [[0.1, "Move", 0.0], [1.0, "Move", 1.0]],
                     [[0.0, "Move", 0.0], [0.6, "Move", 0.5], [0.4, "Move", 1.0]],
                     [[0.0, "Move", 0.0], [0.0, "Move", 1.0]],
                     [[0.0, "Move"], [1.0, "Move", 1.0]]):
            with self.assertRaises(ValueError, msg=str(keys)):
                gc.timeline(self.gltf, self.binc, self.sk, keys, HASH)
        with self.assertRaises(KeyError):
            gc.timeline(self.gltf, self.binc, self.sk, [[0.0, "Move", 0.0], [1.0, "Fly", 1.0]], HASH)


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
