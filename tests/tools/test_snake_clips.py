"""tools/models/snake_clips.py: a snake's clips made by laying its spine along a body curve (a slither wave, a raised
front, an S coil, a roll), its joints keeping their lengths."""
import math
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import joint_animation as ja  # noqa: E402
import own_skeleton as osk  # noqa: E402
import snake_clips as sc  # noqa: E402

I = (0.0, 0.0, 0.0, 1.0)


def snake():
    """root, a helper at 5 cm up, a head 0.5 m forward of it, then nine joints 0.1 m apart back along -z (game space:
    z forward, y up)."""
    names = ["root", "helper", "head"] + ["s%d" % i for i in range(1, 10)]
    parents = [None, 0, 1] + list(range(2, 11))
    rest = [(I, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)), (I, (0.0, 0.05, 0.0), (1.0, 1.0, 1.0)),
            (I, (0.0, 0.0, 0.5), (1.0, 1.0, 1.0))] + [(I, (0.0, 0.0, -0.1), (1.0, 1.0, 1.0))] * 9
    return {"names": names, "parents": parents, "rest": rest}


def up_of(q):
    return osk.qrot(q, (0.0, 1.0, 0.0))


class PoseTests(unittest.TestCase):
    def setUp(self):
        self.sk = snake()
        self.body = sc.body(self.sk, "head")
        self.rest = osk.world(self.sk["rest"], self.sk["parents"])

    def world(self, params):
        return osk.world(sc.pose(self.sk, self.body, params), self.sk["parents"])

    def test_the_chain_runs_from_the_head_to_the_tail(self):
        self.assertEqual([self.sk["names"][j] for j in self.body["joints"]], ["head"] + ["s%d" % i for i in range(1, 10)])
        self.assertAlmostEqual(self.body["length"], 0.9, places=9)

    def test_no_params_keep_a_straight_body_as_it_rests(self):
        w = self.world({})
        for j in range(len(w)):
            for a, b in zip(w[j][1], self.rest[j][1]):
                self.assertAlmostEqual(a, b, places=6)
            self.assertAlmostEqual(abs(sum(x * y for x, y in zip(w[j][0], self.rest[j][0]))), 1.0, places=6)

    def test_a_raised_front_keeps_the_back_down_and_every_length(self):
        w = self.world({"rise": 60, "rise_len": 0.4})
        head, tail = self.body["joints"][0], self.body["joints"][-1]
        self.assertGreater(w[head][1][1], self.rest[head][1][1] + 0.15)
        for j in self.body["joints"][5:]:  # 0.5 m and more back from the head: on the ground
            self.assertAlmostEqual(w[j][1][1], self.rest[j][1][1], places=6)
        for a, b in zip(self.body["joints"], self.body["joints"][1:]):
            self.assertAlmostEqual(math.dist(w[a][1], w[b][1]), 0.1, places=4)

    def test_a_wave_swings_the_body_sideways(self):
        w = self.world({"wave": 25, "phase": 1.0})
        self.assertGreater(max(abs(w[j][1][0]) for j in self.body["joints"]), 0.02)
        for a, b in zip(self.body["joints"], self.body["joints"][1:]):
            self.assertAlmostEqual(math.dist(w[a][1], w[b][1]), 0.1, places=4)

    def test_a_roll_turns_the_body_over(self):
        w = self.world({"roll": 180})
        for j in self.body["joints"]:
            self.assertLess(up_of(w[j][0])[1], -0.99)


class ClipTests(unittest.TestCase):
    def setUp(self):
        self.sk = snake()

    def test_a_slither_loop_closes(self):
        c = sc.clip(self.sk, "head", [{"t": 0, "wave": 25}], length=1.0, travel=1, loop=True)
        self.assertEqual((c["frameCount"], c["flags"]), (60, 1))
        first = osk.world(ja.sample(c, 0, self.sk["rest"]), self.sk["parents"])
        end = osk.world(ja.sample_end(c, self.sk["rest"]), self.sk["parents"])
        mid = osk.world(ja.sample(c, 30, self.sk["rest"]), self.sk["parents"])
        for j in range(len(first)):
            for a, b in zip(first[j][1], end[j][1]):
                self.assertAlmostEqual(a, b, delta=2e-3)
        self.assertGreater(max(abs(mid[j][1][0] - first[j][1][0]) for j in range(3, 12)), 0.01)  # the wave moved on

    def test_keys_ease_from_one_to_the_next(self):
        c = sc.clip(self.sk, "head", [{"t": 0}, {"t": 1.0, "rise": 60, "rise_len": 0.4}], length=1.0)
        y = [osk.world(ja.sample(c, f, self.sk["rest"]), self.sk["parents"])[2][1][1] for f in (0, 30, 59)]
        self.assertLess(y[0] + 0.02, y[1])
        self.assertLess(y[1] + 0.02, y[2])

    def test_mistakes_are_refused(self):
        with self.assertRaises(ValueError):
            sc.body(self.sk, "neck")
        with self.assertRaises(ValueError):
            sc.clip(self.sk, "head", [{"t": 0.5}], length=1.0)
        with self.assertRaises(ValueError):
            sc.clip(self.sk, "head", [{"t": 0, "wobble": 3}], length=1.0)


if __name__ == "__main__":
    unittest.main()
