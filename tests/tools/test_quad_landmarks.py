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
    0.3; some big cats' nearly at their feet). Forward is -y."""
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


class InverseWarpTests(unittest.TestCase):
    """The fit's warp (creature -> game body) undone, so the game skeleton's joints can move into the creature's own
    shape."""

    def setUp(self):
        self.c = ql.landmarks(quadruped())                                 # the creature: level feet
        self.g = ql.landmarks(quadruped(front_ground=-0.35, tail_to=0.3))  # the game body: front paws lower
        self.sx = ql.width_scale(self.c, self.g, "keep")

    def test_the_feet_and_the_nose_go_to_the_game_bodys(self):
        c, g = self.c, self.g
        for k in ("nose", "front", "back"):
            self.assertAlmostEqual(ql.warp_y(c[k], c, g), g[k], delta=1e-9)
        # keep_head: the head keeps the body's slope instead of reaching the game's nose
        slope = (g["back"] - g["front"]) / (c["back"] - c["front"])
        self.assertAlmostEqual(ql.warp_y(c["nose"], c, g, keep_head=True),
                               g["front"] + (c["nose"] - c["front"]) * slope, delta=1e-9)

    def test_unwarp_undoes_warp(self):
        for keep_head in (False, True):
            for x in (-0.3, 0.0, 0.25):
                for y in (-1.4, -0.9, -0.8, -0.2, 0.7, 1.0, 2.0):
                    for z in (0.0, 0.1, 0.4, 0.79, 0.81, 1.2, 1.4):
                        p = P(x, y, z)
                        q = ql.warp(p, self.c, self.g, self.sx, keep_head)
                        back = ql.unwarp(P(*q), self.c, self.g, self.sx, keep_head)
                        for a, b in zip(back, (x, y, z)):
                            self.assertAlmostEqual(a, b, delta=1e-9, msg=(keep_head, x, y, z))

    def test_own_space_is_a_uniform_scale_to_the_game_bodys_height(self):
        c, g = self.c, self.g
        base = max(g["ground_front"], g["ground_back"])
        k = (g["top"] - base) / (c["top"] - c["ground"])
        # the creature's front feet on the ground land where the game's front feet stand (at the higher ground)
        self.assertEqual(tuple(round(v, 9) for v in ql.own_space(P(0.0, c["front"], c["ground"]), c, g)),
                         (0.0, round(g["front"], 9), round(base, 9)))
        a, b = ql.own_space(P(0.1, 0.0, 0.5), c, g), ql.own_space(P(0.3, 0.4, 1.0), c, g)
        for d_own, d in zip((b[0] - a[0], b[1] - a[1], b[2] - a[2]), (0.2, 0.4, 0.5)):
            self.assertAlmostEqual(d_own, d * k, delta=1e-9)
        self.assertAlmostEqual(ql.own_space(P(0.3, 0.4, 1.0), c, g, size=2.0)[2] - base,
                               2 * (ql.own_space(P(0.3, 0.4, 1.0), c, g)[2] - base), delta=1e-9)


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


class RigidTests(unittest.TestCase):
    """--rigid: a part (a paw) follows one bone; the game creature's finger bones below it give it their weight."""
    PARENTS = {"leg": None, "wrist": "leg", "finger1": "wrist", "tip1": "finger1", "finger2": "wrist"}

    def test_the_fingers_weight_goes_to_the_wrist(self):
        out = ql.rigid_weights([{"leg": 0.2, "finger1": 0.3, "tip1": 0.4, "finger2": 0.1}], self.PARENTS, {"wrist"})
        self.assertEqual(set(out[0]), {"leg", "wrist"})
        self.assertAlmostEqual(out[0]["wrist"], 0.8)
        self.assertAlmostEqual(out[0]["leg"], 0.2)

    def test_bones_above_and_beside_stay(self):
        w = {"leg": 0.5, "wrist": 0.5}
        self.assertEqual(ql.rigid_weights([w], self.PARENTS, {"wrist"})[0], w)


# the game boar's bones (gamegltf.py names: only the spine's are named): parent and rest-pose height in metres
BOAR = {
    "j000_Root": ("", 0.00), "j001_Pelvis": ("j000_Root", 0.74), "j002": ("j001_Pelvis", 0.74), "j003": ("j002", 0.77),
    "j004": ("j003", 0.82), "j005_Chest": ("j004", 0.84), "j006": ("j005_Chest", 0.84), "j007": ("j006", 0.83),
    "j008": ("j007", 0.82), "j009": ("j008", 0.79), "j010_Head": ("j009", 0.79), "j011": ("j010_Head", 0.62),
    "j012": ("j011", 0.59), "j013": ("j012", 0.51), "j014": ("j013", 0.46), "j015": ("j010_Head", 0.79),
    "j016": ("j010_Head", 0.80), "j017": ("j010_Head", 0.80), "j018": ("j010_Head", 0.55), "j019": ("j010_Head", 1.02),
    "j020": ("j019", 1.12), "j021": ("j020", 1.20), "j022": ("j021", 1.25), "j023": ("j010_Head", 0.80),
    "j024": ("j010_Head", 0.80), "j025": ("j010_Head", 0.79), "j026": ("j010_Head", 1.02), "j027": ("j026", 1.12),
    "j028": ("j027", 1.20), "j029": ("j028", 1.25), "j030": ("j010_Head", 0.66), "j031": ("j010_Head", 0.66),
    "j032": ("j005_Chest", 0.84), "j033": ("j032", 0.68), "j034": ("j033", 0.57), "j035": ("j034", 0.29),
    "j036": ("j035", 0.12), "j037": ("j005_Chest", 0.84), "j038": ("j037", 0.68), "j039": ("j038", 0.57),
    "j040": ("j039", 0.29), "j041": ("j040", 0.12), "j042": ("j005_Chest", 0.57), "j043": ("j005_Chest", 0.57),
    "j044": ("j001_Pelvis", 0.79), "j045": ("j044", 0.71), "j046": ("j045", 0.63), "j047": ("j046", 0.57),
    "j048": ("j001_Pelvis", 0.76), "j049": ("j048", 0.43), "j050": ("j049", 0.29), "j051": ("j050", 0.11),
    "j052": ("j001_Pelvis", 0.76), "j053": ("j052", 0.43), "j054": ("j053", 0.29), "j055": ("j054", 0.11),
    "j056": ("j001_Pelvis", 0.43), "j057": ("j001_Pelvis", 0.43)}


class MainBoneTests(unittest.TestCase):
    """The bones that keep their copied weights: the spine (the head and the bones above it) and the four legs, found
    by where they reach in the rest pose, not by name."""

    def main(self, table, head):
        return ql.main_bones({n: p or None for n, (p, _) in table.items()}, {n: z for n, (_, z) in table.items()}, head)

    def test_the_boars_unnamed_legs_are_legs_and_its_tail_and_helpers_are_not(self):
        spine = {"j000_Root", "j001_Pelvis", "j002", "j003", "j004", "j005_Chest", "j006", "j007", "j008", "j009",
                 "j010_Head"}
        legs = {"j%03d" % i for i in list(range(32, 42)) + list(range(48, 56))}
        self.assertEqual(self.main(BOAR, "j010_Head"), spine | legs)

    def test_front_paws_below_the_hind_feet_and_a_low_tail(self):
        # like the sabretooth's rest pose: front paws 0.25 m below the hind feet, the tail reaching down to 0.41 m
        table = {"root": ("", 0.0), "pelvis": ("root", 0.9), "chest": ("pelvis", 1.0), "head": ("chest", 1.2),
                 "arm_l": ("chest", 0.9), "paw_l": ("arm_l", -0.14), "arm_r": ("chest", 0.9), "paw_r": ("arm_r", -0.14),
                 "leg_l": ("pelvis", 0.8), "foot_l": ("leg_l", 0.11), "leg_r": ("pelvis", 0.8), "foot_r": ("leg_r", 0.11),
                 "tail": ("pelvis", 0.9), "tail_tip": ("tail", 0.41), "jaw": ("head", 0.0)}
        self.assertEqual(self.main(table, "head"), {"root", "pelvis", "chest", "head", "arm_l", "paw_l", "arm_r",
                                                    "paw_r", "leg_l", "foot_l", "leg_r", "foot_r"})

    def test_fewer_than_four_branches_off_the_spine_is_an_error(self):
        table = {"root": ("", 1.0), "head": ("root", 1.0), "leg_a": ("root", 0.0), "leg_b": ("root", 0.0),
                 "leg_c": ("root", 0.0)}
        with self.assertRaises(ValueError):
            self.main(table, "head")


if __name__ == "__main__":
    unittest.main()
