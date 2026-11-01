"""tools/models/new_creature.py and make_mod's "new_skeleton": a creature on a skeleton of its own, with its own clips,
played by a copy of a game creature's animation graph."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import gamegltf  # noqa: E402
import gltf_in  # noqa: E402
import joint_animation as ja  # noqa: E402
import make_mod  # noqa: E402
import new_creature as nc  # noqa: E402
import new_skeleton as ns  # noqa: E402
import test_gltf_clips as tclips  # noqa: E402
import test_new_skeleton as rig  # noqa: E402

SKELETON = "22222222-2222-4222-8222-222222222222"
GRAPH = "33333333-3333-4333-8333-333333333333"
GRAPH_COPY = "44444444-4444-4444-8444-444444444444"
GAME_WALK = "aaaaaaaa-0000-4000-8000-000000000001"
GAME_BITE = "bbbbbbbb-0000-4000-8000-000000000002"
MOCK_HIERARCHY = "55555555-5555-4555-8555-555555555555"  # the source model's skeleton in mock_eml.lua


def config(**over):
    cfg = {"scale": 1.0, "skeleton_guid": SKELETON, "graph": GRAPH, "graph_guid": GRAPH_COPY,
           "clips": {"move": {"animation": "Move"}, "stand": {"animation": "Move", "loop": True}},
           "roles": {GAME_WALK: "move", GAME_BITE: "move"}, "default_role": "stand",
           "slots": {"Head": "Head", "Pelvis": "Hips", "FootL": "IKFoot", "Root": "root"}}
    cfg.update(over)
    return cfg


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.glb = rig.small_rig(os.path.join(self.tmp.name, "rig.glb"))
        tclips.add_animation(self.glb, tclips.Fixture.CHANNELS)

    def tearDown(self):
        self.tmp.cleanup()


class BuildTests(Fixture):
    def test_skeleton_and_clips(self):
        built = nc.build(config(), self.glb)
        h = built["hierarchy"]
        self.assertEqual(h["hierarchyId"], SKELETON)
        self.assertEqual(h["jointCount"], 5)
        self.assertEqual([c["name"] for c in built["clips"]], ["move", "stand"])
        for c in built["clips"]:
            self.assertEqual(c["guid"], str(uuid.uuid5(uuid.UUID(SKELETON), "clip:" + c["name"])))
            self.assertEqual(c["clip"]["hierarchyHash"], ns.clip_hash(h))
            self.assertEqual(c["clip"]["jointCount"], 5)
        move, stand = built["clips"]
        self.assertEqual((move["clip"]["frameCount"], move["clip"]["flags"]), (60, 0))
        self.assertEqual((stand["clip"]["frameCount"], stand["clip"]["flags"]), (120, 1))  # a loop closes

    def test_an_additive_clip_that_adds_nothing(self):
        """The graph's additive layers (the wolf's leaning) play offsets: our clip for them turns and moves no joint."""
        built = nc.build(config(clips={"move": {"animation": "Move"}, "stand": {"additive_zero": True}}), self.glb)
        stand = built["clips"][1]["clip"]
        self.assertEqual((stand["frameCount"], stand["flags"]), (9, 0))
        rest = built["sk"]["rest"]
        for frame in (0, 4):
            for q, t, s in ja.sample(stand, frame, rest):
                self.assertTrue(rig.same_rotation(q, (0.0, 0.0, 0.0, 1.0), 1e-6), q)
                self.assertEqual(t, (0.0, 0.0, 0.0))
                self.assertEqual(s, (1.0, 1.0, 1.0))

    def test_model_from_a_retextured_copy(self):
        """model_glb: the model from a copy on the skeleton (new_skeleton.write_glb, then retextured): bones named
        jNNN_<name>."""
        built = nc.build(config(), self.glb)
        copy = os.path.join(self.tmp.name, "copy.glb")
        gltf, binc = gltf_in.read_glb(self.glb)
        ns.write_glb(gltf, binc, built["sk"], copy)
        groups, report = nc.model_groups(self.glb, built, model_glb=copy, warn=self.fail)
        self.assertEqual(report["unmapped_bones"], [])
        for a, b in zip(groups[0]["pos"][1], (0.0, 1.5, 0.3)):
            self.assertAlmostEqual(a, b, places=5)

    def test_slots_name_our_joints(self):
        slots = nc.build(config(), self.glb)["slots"]
        self.assertEqual(len(slots), 36)
        want = {"Head": 2, "Pelvis": 1, "FootL": 4}
        for i, name in enumerate(gamegltf.SLOTS):
            self.assertEqual(slots[i], want.get(name, 0), name)

    def test_mistakes_are_refused(self):
        for bad in (config(slots={"Tail": "Hips"}), config(slots={"Head": "Nose"}),
                    config(clips={"move": {"animation": "Fly"}, "stand": {"animation": "Move"}})):
            with self.assertRaises(SystemExit):
                nc.build(bad, self.glb)

    def test_model_on_the_skeleton(self):
        built = nc.build(config(scale=0.5), self.glb)
        groups, report = nc.model_groups(self.glb, built, warn=self.fail)
        self.assertLess(report["max_rest_difference"], 1e-6)
        for a, b in zip(groups[0]["pos"][1], (0.0, 0.75, 0.15)):
            self.assertAlmostEqual(a, b, places=5)


class PackTests(unittest.TestCase):
    BEAST = dict(name="beast_wolf", model_guid="m-1", template_guid="t-1", source_model_guid="src-model",
                 source_template="Enemy_Wildbeast_wolf_earlygame", template_name="Beast_Wolf")

    def test_listing_names_our_clips_by_role(self):
        entry = make_mod.listing_entry(dict(self.BEAST, new_skeleton=config()))
        self.assertEqual(entry["skeleton"], {"guid": SKELETON, "new": True})
        move = nc.clip_guid(SKELETON, "move")
        self.assertEqual(entry["animation"], {"graph": GRAPH, "graph_guid": GRAPH_COPY,
                                              "roles": {GAME_WALK: move, GAME_BITE: move},
                                              "default": nc.clip_guid(SKELETON, "stand")})

    def test_pack_mistakes_are_refused(self):
        cfg = config()
        del cfg["graph_guid"]
        bad = [dict(self.BEAST, new_skeleton=cfg), dict(self.BEAST, new_skeleton=config(default_role="run")),
               dict(self.BEAST, new_skeleton=config(roles={GAME_WALK: "run"})),
               dict(self.BEAST, new_skeleton=config(), own_proportions={"sidecar": "x"})]
        for c in bad:
            with self.assertRaises(SystemExit):
                make_mod.listing_entry(c)


STUB_DATA = """return {debugName = "beast_wolf", boundingBox = {}, boundingSphere = {}, positionScale = {},
  positionOffset = {}, wholeModelRadius = 0.5, meshes = {{}}, lods = {{}}, skinningMap = {1}, jointCount = 1,
  indexData = {}, meshletData = {}, vertexData = {}, skinningData = {}, blendShapeData = {}}
"""


@unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
class ModTests(Fixture):
    def run_mod(self, env=None):
        beast = dict(PackTests.BEAST, new_skeleton=config())
        built = nc.build(config(), self.glb)
        d = os.path.join(self.tmp.name, "src")
        os.makedirs(d)
        make_mod.write_scripts(d, {"id": "test_pack", "title": "Test"}, [make_mod.listing_entry(beast)])
        make_mod.write_new(d, beast["name"], built)
        with open(os.path.join(d, "beast_wolf_data.lua"), "w", encoding="utf-8") as f:
            f.write(STUB_DATA)
        with open(os.path.join(d, "beast_wolf_blob.lua"), "w", encoding="utf-8") as f:
            f.write('return "blob"\n')
        mock = os.path.join(HERE, "..", "..", "tools", "models", "mock_eml.lua")
        out = subprocess.run(["lua", mock, d], capture_output=True, text=True, timeout=60,
                             env=dict(os.environ, **(env or {})))
        self.assertEqual(out.returncode, 0, out.stderr)
        return built, out.stdout

    def test_the_skeleton_is_made_whole(self):
        built, text = self.run_mod()
        self.assertIn("skeleton %s jointCount 5 names 5 parents 5 binds 5 aligned 8 poses 8 data 24 id %s"
                      % (SKELETON, SKELETON), text)
        self.assertIn("model m-1 hierarchy %s" % SKELETON, text)
        self.assertIn("model m-1 hierarchyJointCount 5", text)
        self.assertIn("model src-model hierarchy %s" % MOCK_HIERARCHY, text)

    def test_our_clips_carry_our_skeleton(self):
        built, text = self.run_mod()
        h = ns.clip_hash(built["hierarchy"])
        signed = h - (1 << 64) if h >= 1 << 63 else h
        for c in built["clips"]:
            self.assertIn("clip copy %s hash %d joints 5 frames %d" % (c["guid"], signed, c["clip"]["frameCount"]), text)

    def test_every_clip_reference_of_the_graph_is_ours(self):
        """The mock graph: a clip node for each game clip and one the pack does not name, and a blend space; every
        reference plays our clip for its role (the default for the one not named), with our clip's duration."""
        _, text = self.run_mod()
        self.assertIn("graph %s hierarchy %s ours 6 left 0" % (GRAPH_COPY, SKELETON), text)
        move, stand = nc.clip_guid(SKELETON, "move"), nc.clip_guid(SKELETON, "stand")
        self.assertIn("graph %s node 1 %s duration 1" % (GRAPH_COPY, move), text)
        self.assertIn("graph %s node 3 %s duration 2" % (GRAPH_COPY, stand), text)
        self.assertIn("graph %s item 1 %s duration 1" % (GRAPH_COPY, move), text)
        self.assertIn("template Beast_Wolf keen::ecs::AnimationGraphResourceComponent2 animationGraph %s" % GRAPH_COPY,
                      text)

    def test_slots_point_at_our_joints(self):
        _, text = self.run_mod()
        slots = ["0"] * 36
        for name, j in (("Head", 2), ("Pelvis", 1), ("FootL", 4)):
            slots[gamegltf.SLOTS.index(name)] = str(j)
        self.assertIn("graph %s slots %s" % (GRAPH_COPY, " ".join(slots)), text)

    def test_a_graph_with_ik_is_refused(self):
        """IK chains and foot IK name the game skeleton's joints: such a graph cannot play on ours."""
        _, text = self.run_mod({"MOCK_IK": "1"})
        self.assertIn("beast_wolf: not added:", text)
        self.assertIn("IK", text)
        self.assertNotIn("template Beast_Wolf", text)


if __name__ == "__main__":
    unittest.main()
