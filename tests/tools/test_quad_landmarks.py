"""tools/models/quad_landmarks.py: the landmarks of a four-legged body and the height map fit_to_game_skeleton.py uses
(plain Python, no Blender)."""
import collections
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "models"))
import quad_landmarks as ql  # noqa: E402

P = collections.namedtuple("P", "x y z")


def frange(a, b, step=0.05):
    n = int(round((b - a) / step))
    return [a + i * step for i in range(n + 1)]


def quadruped(front_ground=0.0, back_ground=0.0, tail_to=None):
    """A box body (y -1..1, z 0.8..1.4), a head in front (y -1.4), four legs at y -0.8 and 0.7 (x +-0.3) down to their
    ground, and optionally a long tail drooping behind from z 1.2 to `tail_to` at y 2.4 (the game sabretooth's ends at
    0.3; Meshy's panther's nearly at its feet). Forward is -y."""
    pts = []
    for y in frange(-1.0, 1.0):
        for z in frange(0.8, 1.4):
            for x in (-0.35, 0.35):
                pts.append(P(x, y, z))
    for y in frange(-1.5, -1.0):
        for z in frange(1.0, 1.6):
            pts.append(P(0.0, y, z))
    for ly, ground in ((-0.8, front_ground), (0.7, back_ground)):
        for x in (-0.3, 0.3):
            for z in frange(ground, 0.8):
                for dy in (-0.05, 0.0, 0.05):
                    pts.append(P(x, ly + dy, z))
    if tail_to is not None:
        for i, y in enumerate(frange(1.0, 2.4)):
            pts.append(P(0.0, y, 1.2 - (1.2 - tail_to) * i / 28))
    return pts


class LandmarkTests(unittest.TestCase):
    def test_a_level_body(self):
        m = ql.landmarks(quadruped())
        self.assertAlmostEqual(m["front"], -0.8, delta=0.03)
        self.assertAlmostEqual(m["back"], 0.7, delta=0.03)
        self.assertAlmostEqual(m["ground"], 0.0, delta=1e-6)
        self.assertAlmostEqual(m["ground_front"], 0.0, delta=1e-6)
        self.assertAlmostEqual(m["ground_back"], 0.0, delta=1e-6)
        self.assertAlmostEqual(m["nose"], -1.5, delta=1e-6)
        self.assertAlmostEqual(m["top"], 1.4, delta=1e-6)
        self.assertAlmostEqual(m["belly"], 0.8, delta=1e-6)
        self.assertAlmostEqual(m["width"], 0.35, delta=1e-6)

    def test_front_paws_below_the_hind_ones_and_a_long_tail(self):
        """The game sabretooth's rest pose: front paws 0.35 m below the hind ones, a tail drooping far behind."""
        m = ql.landmarks(quadruped(front_ground=-0.35, tail_to=0.3))
        self.assertAlmostEqual(m["front"], -0.8, delta=0.03)
        self.assertAlmostEqual(m["back"], 0.7, delta=0.03)  # not the front paws again, not the tail
        self.assertAlmostEqual(m["ground_front"], -0.35, delta=1e-6)
        self.assertAlmostEqual(m["ground_back"], 0.0, delta=1e-6)
        self.assertAlmostEqual(m["ground"], -0.35, delta=1e-6)

    def test_a_tail_that_droops_lower_than_the_feet_is_not_the_hind_feet(self):
        m = ql.landmarks(quadruped(back_ground=0.02, tail_to=0.0))
        self.assertAlmostEqual(m["back"], 0.7, delta=0.03)
        self.assertAlmostEqual(m["ground_back"], 0.02, delta=1e-6)


class HeightTests(unittest.TestCase):
    def setUp(self):
        self.c = ql.landmarks(quadruped())                         # the creature: level feet
        self.g = ql.landmarks(quadruped(front_ground=-0.35, tail_to=0.3))  # the game body

    def test_each_pair_of_feet_reaches_its_own_ground(self):
        self.assertAlmostEqual(ql.fit_z(0.0, -0.8, self.c, self.g), -0.35, delta=1e-6)
        self.assertAlmostEqual(ql.fit_z(0.0, 0.7, self.c, self.g), 0.0, delta=1e-6)

    def test_the_body_above_the_belly_is_not_stretched(self):
        self.assertAlmostEqual(ql.fit_z(1.4, 0.0, self.c, self.g), 1.4, delta=1e-6)
        self.assertAlmostEqual(ql.fit_z(0.8, -0.8, self.c, self.g), 0.8, delta=1e-6)
        self.assertAlmostEqual(ql.fit_z(1.1, -0.8, self.c, self.g), 1.1, delta=1e-6)

    def test_a_front_leg_stretches_evenly(self):
        # halfway down the leg (0.4 of 0.8 m) maps halfway down the game's longer leg (0.8 + 0.35 m)
        self.assertAlmostEqual(ql.fit_z(0.4, -0.8, self.c, self.g), 0.8 - 0.575, delta=1e-6)

    def test_a_level_game_body_is_a_plain_scale(self):
        g = ql.landmarks(quadruped())
        self.assertAlmostEqual(ql.fit_z(0.4, -0.8, self.c, g), 0.4, delta=1e-6)
        self.assertAlmostEqual(ql.fit_z(0.0, 0.7, self.c, g), 0.0, delta=1e-6)


class WidthTests(unittest.TestCase):
    """A creature stretched to a bigger body keeps its own proportions with "keep" (scaled 2x in length and height but
    1x in width, a creature is half as wide as designed)."""

    def setUp(self):
        self.c = ql.landmarks(quadruped())   # body 0.7 wide, feet 1.5 apart, 1.4 tall
        big = [P(p.x * 1.2, p.y * 2.0, p.z * 2.0) for p in quadruped()]  # 2x long and tall, only 1.2x wide
        self.g = ql.landmarks(big)

    def test_fit_squeezes_the_width_to_the_game_body(self):
        self.assertAlmostEqual(ql.width_scale(self.c, self.g, "fit"), 1.2, delta=1e-6)

    def test_keep_scales_the_width_with_length_and_height(self):
        self.assertAlmostEqual(ql.width_scale(self.c, self.g, "keep"), 2.0, delta=1e-6)

    def test_a_number_is_used_as_it_is(self):
        self.assertAlmostEqual(ql.width_scale(self.c, self.g, "1.5"), 1.5, delta=1e-6)


class TailTests(unittest.TestCase):
    """Behind the hind feet, the slices where the creature is thin are its tail."""

    def test_a_drooping_tail_is_tail_and_the_body_is_not(self):
        pts = quadruped(tail_to=0.3)
        g = ql.landmarks(pts)
        tail = ql.tail_slices(pts, g)
        self.assertIn(round(2.0 / 0.05), tail)        # the tail, far behind
        self.assertNotIn(round(0.0 / 0.05), tail)     # the middle of the body
        self.assertNotIn(round(0.7 / 0.05), tail)     # the hind legs

    def test_a_tail_that_curls_out_to_the_side_is_still_tail(self):
        pts = quadruped()
        for i in range(30):  # a thin tail curling out to x 0.9, behind the hind legs
            pts.append(P(0.3 * i / 29 * 3, 1.2 + 0.03 * i, 0.9))
        g = ql.landmarks(pts)
        self.assertIn(round(1.8 / 0.05), ql.tail_slices(pts, g))


class DetailWeightTests(unittest.TestCase):
    """Detail bones (a tail, a head crest, ears) keep weight only where the creature has that detail; the weight goes
    to the nearest kept bone instead."""

    PARENTS = {"pelvis": None, "spine": "pelvis", "head": "spine", "jaw": "head", "jaw_tip": "jaw", "crest": "head",
               "tail1": "pelvis", "tail2": "tail1", "thigh": "pelvis"}
    MAIN = {"pelvis", "spine", "head", "thigh"}

    def clean(self, weights, in_tail=False):
        return ql.clean_detail_weights([weights], [in_tail], self.PARENTS, self.MAIN, {"jaw"}, "head")[0]

    def test_tail_bones_leave_the_body(self):
        out = self.clean({"pelvis": 0.5, "tail1": 0.3, "tail2": 0.2})
        self.assertAlmostEqual(out["pelvis"], 1.0)
        self.assertNotIn("tail1", out)

    def test_tail_bones_stay_on_the_tail(self):
        self.assertEqual(self.clean({"tail1": 0.6, "tail2": 0.4}, in_tail=True), {"tail1": 0.6, "tail2": 0.4})

    def test_a_crest_goes_to_the_head_and_a_jaw_tip_to_the_jaw(self):
        out = self.clean({"head": 0.4, "crest": 0.3, "jaw_tip": 0.3})
        self.assertAlmostEqual(out["head"], 0.7)
        self.assertAlmostEqual(out["jaw"], 0.3)
        self.assertNotIn("crest", out)

    def test_main_and_kept_bones_are_untouched(self):
        self.assertEqual(self.clean({"thigh": 0.5, "jaw": 0.5}), {"thigh": 0.5, "jaw": 0.5})

    def test_the_head_bones_kept_are_those_the_game_animal_uses_on_its_head(self):
        # use: each bone under the head with its weight on the game animal's head; total: all weight there
        self.assertEqual(ql.kept_head_bones({"jaw": 8.0, "crest": 0.5}, 100.0), {"jaw"})


if __name__ == "__main__":
    unittest.main()
