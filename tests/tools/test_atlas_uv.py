"""atlas_uv: a prop's faces sorted by the colour under them and mapped into a region of a game texture atlas."""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import atlas_uv  # noqa: E402

RULES = [{"material": 0, "min_saturation": 0.55, "min_value": 0.45}]  # bright, saturated straw; the rest wood (1)
REGION = (0.4, 0.1, 0.9, 0.7)  # u0, v0, u1, v1


def inside(uvs, region):
    u0, v0, u1, v1 = region
    return all(u0 - 1e-9 <= u <= u1 + 1e-9 and v0 - 1e-9 <= v <= v1 + 1e-9 for u, v in uvs)


class AtlasUvTests(unittest.TestCase):
    def test_faces_are_sorted_by_the_colour_under_them(self):
        self.assertEqual(atlas_uv.classify((0.79, 0.56, 0.14), RULES, 1), 0)  # Meshy's straw
        self.assertEqual(atlas_uv.classify((0.32, 0.25, 0.19), RULES, 1), 1)  # its wood
        self.assertEqual(atlas_uv.classify((0.9, 0.9, 0.9), RULES, 1), 1)     # bright but grey
        self.assertEqual(atlas_uv.classify((0.3, 0.2, 0.02), RULES, 1), 1)    # saturated but dark

    def test_a_small_face_maps_inside_the_region(self):
        wall = [(1.0, 0.3, 2.0), (1.0, 0.3, 2.2), (1.0, 0.5, 2.2)]  # facing +x
        uvs = atlas_uv.face_uvs(wall, (1, 0, 0), 1.5, REGION)
        self.assertEqual(len(uvs), 3)
        self.assertTrue(inside(uvs, REGION), uvs)

    def test_whole_repeats_away_the_same_face_gets_the_same_coordinates(self):
        a = [(1.0, 0.3, 2.0), (1.0, 0.3, 2.2), (1.0, 0.5, 2.2)]
        b = [(x, y + 3.0, z - 4.5) for x, y, z in a]  # 2 and 3 repeats of 1.5 m away
        for p, q in zip(atlas_uv.face_uvs(a, (1, 0, 0), 1.5, REGION), atlas_uv.face_uvs(b, (1, 0, 0), 1.5, REGION)):
            self.assertAlmostEqual(p[0], q[0])
            self.assertAlmostEqual(p[1], q[1])

    def test_a_face_is_projected_along_its_main_axis(self):
        floor = [(0.1, 0.5, 0.1), (0.3, 0.5, 0.1), (0.3, 0.5, 0.3)]  # facing up: x and z matter, not y
        higher = [(x, y + 0.7, z) for x, y, z in floor]
        self.assertEqual(atlas_uv.face_uvs(floor, (0, 1, 0), 1.5, REGION),
                         atlas_uv.face_uvs(higher, (0, 1, 0), 1.5, REGION))
        tilted = (0.2, -0.9, 0.3)  # mostly facing down: still the floor's projection
        self.assertEqual(atlas_uv.face_uvs(floor, tilted, 1.5, REGION), atlas_uv.face_uvs(floor, (0, 1, 0), 1.5, REGION))

    def test_up_in_the_world_is_up_in_the_texture(self):
        # texture v grows downwards (the game's and glTF's convention): the higher corner has the smaller v
        wall = [(0.0, 0.2, 0.5), (0.1, 0.2, 0.5), (0.0, 0.4, 0.5)]  # facing +z
        uvs = atlas_uv.face_uvs(wall, (0, 0, 1), 1.5, REGION)
        self.assertLess(uvs[2][1], uvs[0][1])
        self.assertGreater(uvs[1][0], uvs[0][0])  # and +x is to the right

    def test_the_scale_follows_the_repeat_length(self):
        wall = [(0.0, 0.2, 0.5), (0.3, 0.2, 0.5), (0.0, 0.4, 0.5)]
        short = atlas_uv.face_uvs(wall, (0, 0, 1), 1.0, REGION)
        long_ = atlas_uv.face_uvs(wall, (0, 0, 1), 3.0, REGION)
        self.assertAlmostEqual((short[1][0] - short[0][0]) / (long_[1][0] - long_[0][0]), 3.0)

    def test_a_region_that_is_not_square_gets_its_own_length_down(self):
        # the game's straw patch is 2.8 m across and 1.4 m down at the game's roof density: no stretching
        wall = [(0.0, 0.0, 0.5), (0.1, 0.0, 0.5), (0.0, 0.1, 0.5)]
        uvs = atlas_uv.face_uvs(wall, (0, 0, 1), (2.8, 1.4), REGION)
        du = uvs[1][0] - uvs[0][0]
        dv = uvs[0][1] - uvs[2][1]
        self.assertAlmostEqual(du / (REGION[2] - REGION[0]) * 2.8, dv / (REGION[3] - REGION[1]) * 1.4)


if __name__ == "__main__":
    unittest.main()
