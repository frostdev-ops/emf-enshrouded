"""tools/models/own_skeleton.py: a copy of a game skeleton with a creature's own proportions."""
import json
import math
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import own_skeleton as osk  # noqa: E402

SABRETOOTH = os.path.join(HERE, "..", "..", "analysis", "model_survey", "hierarchies",
                          "ef2d7e9f-1a5e-4d7e-a3e5-d47448168385.json")
I = (0.0, 0.0, 0.0, 1.0)


def turn(axis, rad):
    s = math.sin(rad / 2)
    return tuple(c * s for c in axis) + (math.cos(rad / 2),)


class WorldTests(unittest.TestCase):
    # root at the origin, a parent 1 m up turned 90 degrees about y, a child 1 m along the parent's x
    PARENTS = [None, 0, 1]
    REST = [(I, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)),
            (turn((0, 1, 0), math.pi / 2), (0.0, 1.0, 0.0), (1.0, 1.0, 1.0)),
            (I, (1.0, 0.0, 0.0), (1.0, 1.0, 1.0))]

    def test_world_positions_follow_the_parents_rotation(self):
        world = osk.world(self.REST, self.PARENTS)
        # the parent's x axis points along world -z after 90 degrees about y
        for a, b in zip(world[2][1], (0.0, 1.0, -1.0)):
            self.assertAlmostEqual(a, b, places=9)

    def test_the_same_world_positions_give_the_same_rest(self):
        world = [w[1] for w in osk.world(self.REST, self.PARENTS)]
        rest = osk.rest_from_world(self.REST, self.PARENTS, world)
        for (q, t, s), (q0, t0, s0) in zip(rest, self.REST):
            self.assertEqual(q, q0)
            for a, b in zip(t, t0):
                self.assertAlmostEqual(a, b, places=9)

    def test_a_moved_joint_gets_its_offset_in_the_parents_frame(self):
        world = [w[1] for w in osk.world(self.REST, self.PARENTS)]
        world[2] = (0.0, 1.0, -2.5)  # the child 2.5 m out along the parent's x
        rest = osk.rest_from_world(self.REST, self.PARENTS, world)
        for a, b in zip(rest[2][1], (2.5, 0.0, 0.0)):
            self.assertAlmostEqual(a, b, places=9)
        self.assertEqual(rest[2][0], I)  # rotations stay: the clips' rotations keep their meaning


class InverseBindTests(unittest.TestCase):
    def test_a_rest_at_the_origin_binds_to_identity(self):
        m = osk.inverse_binds([(I, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0))], [None])[0]
        self.assertEqual(m, {"rot": {"x": {"x": 1.0, "y": 0.0, "z": 0.0}, "y": {"x": 0.0, "y": 1.0, "z": 0.0},
                                     "z": {"x": 0.0, "y": 0.0, "z": 1.0}}, "pos": {"x": 0.0, "y": 0.0, "z": 0.0}})

    @unittest.skipUnless(os.path.exists(SABRETOOTH), "needs the sabretooth skeleton dumped by mods/model_survey")
    def test_the_sabretooths_rest_gives_its_own_inverse_binds(self):
        with open(SABRETOOTH, encoding="utf-8") as f:
            hier = json.load(f)
        rest, parents = osk.rest_of(hier)
        ours = osk.inverse_binds(rest, parents)
        for j, (a, b) in enumerate(zip(ours, hier["skinToJointMatrices"])):
            for col in "xyz":
                for k in "xyz":
                    self.assertAlmostEqual(a["rot"][col][k], b["rot"][col][k], delta=2e-4, msg=(j, col, k))
            for k in "xyz":
                self.assertAlmostEqual(a["pos"][k], b["pos"][k], delta=2e-4, msg=(j, "pos", k))


class SpaceTests(unittest.TestCase):
    def test_blender_to_game(self):
        # Blender after gamegltf's glTF: forward -y, up +z; the game: up +y, and the glTF is mirrored in x
        self.assertEqual(osk.blender_to_game((1.0, 2.0, 3.0)), (-1.0, 3.0, -2.0))


def angle(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return 2 * math.acos(min(1.0, d))


class RetargetTests(unittest.TestCase):
    """A clip's pose on another rest pose of the same skeleton: each joint turns in the world as it turned away from
    the source's rest."""
    # root, a bone 1 m up (y), its child 1 m further up
    PARENTS = [None, 0, 1]
    REST = [(I, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)), (I, (0.0, 1.0, 0.0), (1.0, 1.0, 1.0)),
            (I, (0.0, 1.0, 0.0), (1.0, 1.0, 1.0))]
    # the same chain with joint 1 turned -90 degrees about z: its bone points along world +x
    TURNED = [REST[0], (turn((0, 0, 1), -math.pi / 2), (0.0, 1.0, 0.0), (1.0, 1.0, 1.0)), REST[2]]

    def test_the_same_rest_gives_the_same_pose(self):
        pose = [(I, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)), (turn((0, 0, 1), 0.4), (0.0, 1.0, 0.0), (1.0, 1.0, 1.0)),
                (turn((1, 0, 0), 0.3), (0.0, 1.0, 0.0), (1.0, 1.0, 1.0))]
        out = osk.retarget(pose, self.REST, self.REST, self.PARENTS)
        for (q, t, _), (q0, t0, _) in zip(out, pose):
            self.assertLess(angle(q, q0), 1e-7)  # acos near 1: one rounding step is 3e-8 rad

    def test_a_bend_turns_the_new_bone_the_same_way_in_the_world(self):
        moved = self.TURNED
        # the clip turns joint 1 by 90 degrees about x: on the old chain the tip goes from +y to +z
        pose = [self.REST[0], (turn((1, 0, 0), math.pi / 2), (0.0, 1.0, 0.0), (1.0, 1.0, 1.0)), self.REST[2]]
        out = osk.retarget(pose, self.REST, moved, self.PARENTS)
        w_old = osk.world(pose, self.PARENTS)
        w_new = osk.world(out, self.PARENTS)
        d_old = [b - a for a, b in zip(w_old[1][1], w_old[2][1])]
        d_new = [b - a for a, b in zip(w_new[1][1], w_new[2][1])]
        # the same turn in the world (90 degrees about x) applied to the new bone, which pointed along +x: it stays
        # along +x; the old bone, along +y, now points along +z
        for a, b in zip(d_old, (0.0, 0.0, 1.0)):
            self.assertAlmostEqual(a, b, places=9)
        for a, b in zip(d_new, (1.0, 0.0, 0.0)):
            self.assertAlmostEqual(a, b, places=9)
        for a, b in zip(osk.qrot(w_new[1][0], (0.0, 0.0, 1.0)), (0.0, -1.0, 0.0)):
            self.assertAlmostEqual(a, b, places=9)




if __name__ == "__main__":
    unittest.main()
