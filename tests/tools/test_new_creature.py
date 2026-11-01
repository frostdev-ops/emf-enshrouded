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
        """The graph's additive layers (the wolf's leaning) play offsets: our clip for them turns and moves no joint,
        and its scales are 0 for every joint (an additive layer adds scales: the game's offset clips store 0; an
        offset of 1 doubled every joint's scale)."""
        built = nc.build(config(clips={"move": {"animation": "Move"}, "stand": {"additive_zero": True}}), self.glb)
        stand = built["clips"][1]["clip"]
        self.assertEqual((stand["frameCount"], stand["flags"]), (9, 0))
        rest = built["sk"]["rest"]
        for frame in (0, 4):
            for q, t, s in ja.sample(stand, frame, rest):
                self.assertTrue(rig.same_rotation(q, (0.0, 0.0, 0.0, 1.0), 1e-6), q)
                self.assertEqual(t, (0.0, 0.0, 0.0))
                self.assertEqual(s, (0.0, 0.0, 0.0))

    def test_made_and_faster_clips(self):
        """A clip may play its animation faster (speed) or be made from key turns on a pose of an animation (made:
        a death or a flinch the file does not have)."""
        cfg = config(clips={"move": {"animation": "Move", "speed": 2.0}, "stand": {"animation": "Move"},
                            "die": {"made": {"from": "Move", "at": 0.0,
                                             "keys": [{"t": 0.0}, {"t": 1.0, "turn": {"Hips": ["z", 90]}}]}}})
        clips = {c["name"]: c["clip"] for c in nc.build(cfg, self.glb)["clips"]}
        self.assertEqual(clips["move"]["frameCount"], 30)
        self.assertEqual((clips["die"]["frameCount"], clips["die"]["flags"]), (60, 0))
        bad = config(clips={"move": {"animation": "Move"}, "stand": {"animation": "Move"},
                            "die": {"made": {"keys": [{"t": 0.0}, {"t": 1.0, "turn": {"Wing": ["z", 9]}}]}}})
        with self.assertRaises(SystemExit):
            nc.build(bad, self.glb)

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
        zero = nc.clip_guid(SKELETON, nc.ZERO)
        self.assertEqual(entry["animation"], {"graph": GRAPH, "graph_guid": GRAPH_COPY,
                                              "roles": {GAME_WALK: move, GAME_BITE: move},
                                              "default": nc.clip_guid(SKELETON, "stand"),
                                              "zero": zero, "offsets": {zero: True}})

    def test_role_keys_are_game_clip_guids(self):
        """A role names a game clip by its GUID (any case; the game writes them lower case)."""
        entry = make_mod.listing_entry(dict(self.BEAST, new_skeleton=config(roles={GAME_WALK.upper(): "move"})))
        self.assertEqual(list(entry["animation"]["roles"]), [GAME_WALK])
        for roles in ({}, {"Idle": "move"}, {GAME_WALK[:-1]: "move"}):
            with self.assertRaises(SystemExit, msg=str(roles)):
                make_mod.listing_entry(dict(self.BEAST, new_skeleton=config(roles=roles)))

    def test_additive_clips_are_offsets(self):
        cfg = config(clips={"move": {"animation": "Move"}, "stand": {"animation": "Move"},
                            "lean": {"additive_zero": True}})
        offsets = make_mod.listing_entry(dict(self.BEAST, new_skeleton=cfg))["animation"]["offsets"]
        self.assertEqual(set(offsets), {nc.clip_guid(SKELETON, "lean"), nc.clip_guid(SKELETON, nc.ZERO)})

    def test_clip_names_with_an_underscore_first_are_ours(self):
        with self.assertRaises(SystemExit):
            make_mod.listing_entry(dict(self.BEAST, new_skeleton=config(
                clips={"move": {"animation": "Move"}, "stand": {"animation": "Move"}, "_mine": {"animation": "Move"}})))

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
    def run_mod(self, env=None, folder="src"):
        beast = dict(PackTests.BEAST, new_skeleton=config())
        built = nc.build(config(), self.glb)
        d = os.path.join(self.tmp.name, folder)
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
        self.assertIn("graph %s hierarchy %s ours 8 left 0" % (GRAPH_COPY, SKELETON), text)
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
        """IK chains, foot IK settings and bone masks name the game skeleton's joints: such a graph cannot play on
        ours."""
        for kind, words in (("chains", "IK"), ("feet", "IK"), ("mask", "bone mask")):
            _, text = self.run_mod({"MOCK_IK": kind}, folder=kind)
            self.assertIn("beast_wolf: not added:", text, kind)
            self.assertIn(words, text, kind)
            self.assertNotIn("template Beast_Wolf", text, kind)

    def test_nodes_that_play_offsets_get_an_offset(self):
        """A node whose clips are offsets (animationContentAdditive: the wolf's lean) plays an offset clip of ours
        even where the default role names a whole pose (a pose there is added to the pose: every joint's scale
        doubled and the wolf was drawn many times its size)."""
        built, text = self.run_mod()
        zero = nc.clip_guid(SKELETON, nc.ZERO)
        self.assertIn("graph %s item 1 %s duration 0.15" % (GRAPH_COPY, zero), text)
        self.assertIn("graph %s item 2 %s duration 0.15" % (GRAPH_COPY, zero), text)
        self.assertIn("2 in offset nodes: the zero offset", text)

    def test_game_clips_without_a_role_are_logged(self):
        _, text = self.run_mod()
        self.assertIn("4 by default (cccccccc-0000-4000-8000-000000000003 dddddddd-0000-4000-8000-000000000004 "
                      "eeeeeeee-0000-4000-8000-000000000005 ffffffff-0000-4000-8000-000000000006)", text)


if __name__ == "__main__":
    unittest.main()
