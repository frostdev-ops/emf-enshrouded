"""Unit tests for tools/models (encoder against the decoder; byte-exact against a game model when one is dumped)."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import encode  # noqa: E402
import make_mod  # noqa: E402
import rendermodel  # noqa: E402

DUMP = os.path.join(HERE, "..", "..", "analysis", "model_survey", "dump", "pet_dog_01")


def tiny_group():
    pos = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.5), (1.0, 1.0, 1.0)]
    nrm = [(0.0, 0.0, 1.0)] * 4
    tan = [(1.0, 0.0, 0.0, 1.0), (1.0, 0.0, 0.0, -1.0), (0.0, 1.0, 0.0, 1.0), (0.0, 1.0, 0.0, -1.0)]
    uv = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0)]
    skin = [([3], [1.0]), ([3, 7], [0.5, 0.5]), ([7, 3, 9], [0.6, 0.3, 0.1]), ([9], [1.0])]
    return {"material": 0, "pos": pos, "nrm": nrm, "tan": tan, "col": None, "uv": uv, "skin": skin,
            "lods": [[(0, 1, 2), (2, 1, 3)]]}


class EncodeTests(unittest.TestCase):
    def test_weights_sum_to_255(self):
        for ws in ([1.0], [0.5, 0.5], [0.6, 0.3, 0.1], [0.34, 0.33, 0.33], [0.25] * 4):
            self.assertEqual(sum(encode.pack_weights(ws)), 255)

    def test_tiny_model_decodes_back(self):
        blob, f = encode.build([tiny_group()])
        model = dict(f, debugName="tiny", jointCount=f["jointCount"])
        d = rendermodel.decode_mesh(model, blob, 0)
        g = tiny_group()
        for a, b in zip(d["pos"], g["pos"]):
            for k in range(3):
                self.assertAlmostEqual(a[k], b[k], places=5)
        self.assertEqual([t[3] for t in d["tan"]], [1.0, -1.0, 1.0, -1.0])
        self.assertEqual(d["tris"], [(0, 1, 2), (2, 1, 3)])
        self.assertEqual(f["skinningMap"], [3, 7, 9])
        self.assertEqual(d["skin"][2][0][:3], [7, 3, 9])
        self.assertEqual(len(blob) % 4, 0)

    @unittest.skipUnless(os.path.exists(DUMP + ".render.bin"), "needs a dumped game model (mods/model_survey)")
    def test_game_dog_round_trip_is_byte_exact(self):
        model = rendermodel.load_record(DUMP + ".json")
        with open(DUMP + ".render.bin", "rb") as f:
            orig = f.read()
        groups = encode.groups_from_model(model, orig)
        box = ([model["positionOffset"][k] for k in "xyz"], [model["positionScale"][k] for k in "xyz"])
        blob, f = encode.build(groups, palette=model["skinningMap"], box=box)
        self.assertEqual(len(blob), len(orig))
        self.assertEqual(blob, orig)
        for key in ("vertexData", "skinningData", "indexData", "meshletData"):
            self.assertEqual(f[key], model[key], key)
        for mine, theirs in zip(f["meshes"], model["meshes"]):
            for key in ("materialIndex", "vertexCount", "indexOffset", "indexCount", "vertexDataByteOffset",
                        "skinVertexOffset"):
                self.assertEqual(mine[key], theirs[key], key)
        self.assertEqual([(l["meshOffset"], l["meshCount"]) for l in f["lods"]],
                         [(l["meshOffset"], l["meshCount"]) for l in model["lods"]])


BEAR = {
    "name": "beast_bear", "model_guid": "m-1", "template_guid": "t-1", "source_model_guid": "src-model",
    "source_template": "Enemy_Wildbeast_wolf_earlygame", "template_name": "Beast_Bear",
    "template_set": {"DamageMod": {"dataStorage[2]": 1.5}, "keen::ecs::Movement": {"dataStorage[4]": 5.1, "dataStorage[5]": {"mul": 0.85}}},
    "template_add": [{"type": "ScaleComponent", "value": {"uniformScale": 1.4}}],
}

STUB_DATA = """return {debugName = "beast_bear", boundingBox = {}, boundingSphere = {}, positionScale = {},
  positionOffset = {}, wholeModelRadius = 0.5, meshes = {{}}, lods = {{}}, skinningMap = {1}, jointCount = 1,
  indexData = {}, meshletData = {}, vertexData = {}, skinningData = {}, blendShapeData = {}}
"""


class MakeModTests(unittest.TestCase):
    def test_template_edits_become_lua_entries(self):
        entry = make_mod.listing_entry(dict(BEAR))
        self.assertEqual(entry["template_set"], [
            {"component": "keen::ecs::DamageMod", "path": ["dataStorage", 3], "value": 1.5},
            {"component": "keen::ecs::Movement", "path": ["dataStorage", 5], "value": 5.1},
            {"component": "keen::ecs::Movement", "path": ["dataStorage", 6], "mul": 0.85}])
        self.assertEqual(entry["template_add"], [{"type": "keen::ecs::ScaleComponent", "value": {"uniformScale": 1.4}}])
        text = make_mod.lua_value(entry)
        self.assertIn('{component = "keen::ecs::DamageMod", path = {"dataStorage", 3}, value = 1.5}', text)
        self.assertIn('{type = "keen::ecs::ScaleComponent", value = {uniformScale = 1.4}}', text)

    def test_a_rebuild_removes_the_files_of_creatures_the_pack_no_longer_has(self):
        with tempfile.TemporaryDirectory() as d:
            for n in ("old_data.lua", "old_blob.lua", "old_albedo.lua", "kept_data.lua", "kept_blob.lua", "notes.txt"):
                open(os.path.join(d, n), "w").close()
            make_mod.remove_stale(d, {"kept"})
            self.assertEqual(sorted(os.listdir(d)), ["kept_blob.lua", "kept_data.lua", "notes.txt"])

    def test_skeleton_and_clip_files_go_when_a_creature_has_no_skeleton_of_its_own(self):
        with tempfile.TemporaryDirectory() as d:
            for n in ("own_skeleton.lua", "own_clips.lua", "plain_skeleton.lua", "plain_clips.lua", "plain_data.lua"):
                open(os.path.join(d, n), "w").close()
            make_mod.remove_stale(d, {"own", "plain"}, own={"own"})
            self.assertEqual(sorted(os.listdir(d)), ["own_clips.lua", "own_skeleton.lua", "plain_data.lua"])

    def test_a_texture_for_a_slot_the_model_does_not_draw_is_refused(self):
        """The panther's mesh drew with the sabretooth's slot 1 while its texture replaced slot 0: in game it wore
        the sabretooth's own fur through the panther's UVs."""
        groups = [{"material": 1, "pos": []}]
        with self.assertRaises(SystemExit) as e:
            make_mod.check_texture_slot("panther2", 0, groups)
        self.assertIn("slot 0", str(e.exception))
        self.assertIn("1", str(e.exception))
        make_mod.check_texture_slot("panther2", 1, groups)
        make_mod.check_texture_slot("bear", 0, [{"material": 0}, {"material": 1}])

    def test_own_proportions_needs_the_game_skeleton_dump(self):
        cfg = {k: "x" for k in make_mod.OWN_KEYS}
        with self.assertRaises(SystemExit):
            make_mod.listing_entry(dict(BEAR, own_proportions=cfg))  # no "hierarchy"
        make_mod.listing_entry(dict(BEAR, own_proportions=cfg, hierarchy="wolf.hierarchy.json"))

    def test_a_bad_field_path_is_refused(self):
        for bad in ("data storage", "dataStorage[-1]", "dataStorage[x]", ""):
            with self.assertRaises(SystemExit, msg=bad):
                make_mod.listing_entry(dict(BEAR, template_set={"DamageMod": {bad: 1}}))
        for bad in ({"mul": "2"}, {"times": 2}, [1, 2]):  # a value is a number, text, true/false or {"mul": n}
            with self.assertRaises(SystemExit, msg=str(bad)):
                make_mod.listing_entry(dict(BEAR, template_set={"DamageMod": {"dataStorage[2]": bad}}))

    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_the_generated_mod_applies_the_edits(self):
        with tempfile.TemporaryDirectory() as d:
            make_mod.write_scripts(d, {"id": "test_pack", "title": "Test"}, [make_mod.listing_entry(dict(BEAR))])
            with open(os.path.join(d, "beast_bear_data.lua"), "w", encoding="utf-8") as f:
                f.write(STUB_DATA)
            with open(os.path.join(d, "beast_bear_blob.lua"), "w", encoding="utf-8") as f:
                f.write('return "blob"\n')
            mock = os.path.join(HERE, "..", "..", "tools", "models", "mock_eml.lua")
            out = subprocess.run(["lua", mock, d], capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn("template Beast_Bear keen::ecs::DamageMod dataStorage 0 0.5 1.5 0", out.stdout)
            self.assertIn("template Beast_Bear keen::ecs::Movement dataStorage 0 1 0.5 2.3 5.1 8.5 1.8", out.stdout)
            self.assertIn("template Beast_Bear keen::ecs::ScaleComponent uniformScale 1.4", out.stdout)
            # the source template is left as it was
            self.assertIn("template Enemy_Wildbeast_wolf_earlygame keen::ecs::DamageMod dataStorage 0 0.5 1 0",
                          out.stdout)
            # the source's cloth simulation runs over the source's own vertices: never on our geometry (seen in game:
            # the lich and the bone golem, on the mage's and the knight's models, glitched as they walked)
            self.assertIn("model %s cloth none" % BEAR["model_guid"], out.stdout)
            self.assertIn("model %s cloth kept" % BEAR["source_model_guid"], out.stdout)

    def plain_mod(self, d, env):
        """The bear pack's creature (a model on the source's skeleton, with a texture) run through the mock."""
        bear = dict(BEAR, texture={"albedo_dir": "x", "slot": 0, "material_guid": "mat-ours", "flat_size": 4})
        make_mod.write_scripts(d, {"id": "test_pack", "title": "Test"}, [make_mod.listing_entry(bear)])
        with open(os.path.join(d, "beast_bear_data.lua"), "w", encoding="utf-8") as f:
            f.write(STUB_DATA)
        with open(os.path.join(d, "beast_bear_blob.lua"), "w", encoding="utf-8") as f:
            f.write('return "blob"\n')
        with open(os.path.join(d, "beast_bear_albedo.lua"), "w", encoding="utf-8") as f:
            f.write("return {}\n")
        mock = os.path.join(HERE, "..", "..", "tools", "models", "mock_eml.lua")
        return subprocess.run(["lua", mock, d], capture_output=True, text=True, timeout=60, env=dict(os.environ, **env))

    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_on_a_dedicated_server_the_template_is_made_without_the_model(self):
        """A dedicated server's data has no render data: the template copy and its edits are made, the model and the
        material are left out (the template still names the model, as the game's own templates do there)."""
        with tempfile.TemporaryDirectory() as d:
            out = self.plain_mod(d, {"MOCK_SERVER": "1"})
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn("beast_bear: no render data here (a dedicated server): the model and its material are left "
                          "out", out.stdout)
            self.assertNotIn("not added", out.stdout)
            self.assertIn("template Beast_Bear keen::ecs::ModelResource model m-1", out.stdout)
            self.assertIn("template Beast_Bear keen::ecs::DamageMod dataStorage 0 0.5 1.5 0", out.stdout)
            self.assertNotIn("keen::RenderModel", out.stdout)
            self.assertNotIn("keen::RenderMaterialResource", out.stdout)

    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_a_client_without_the_source_model_still_stops_the_creature(self):
        """Where the game has render data, a source model it no longer has is a game change: the creature is not
        added."""
        with tempfile.TemporaryDirectory() as d:
            out = self.plain_mod(d, {"MOCK_MISSING": "src-model"})
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn("beast_bear: not added: ", out.stdout)
            self.assertIn("the source model was not found", out.stdout)
            self.assertNotIn("template Beast_Bear", out.stdout)

    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_own_proportions_on_a_dedicated_server(self):
        """own_proportions on a server: the skeleton copy, the clips and the graph copy are made, the model is not."""
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as src:
            self.own_mod(d, src)
            mock = os.path.join(HERE, "..", "..", "tools", "models", "mock_eml.lua")
            out = subprocess.run(["lua", mock, d], capture_output=True, text=True, timeout=60,
                                 env=dict(os.environ, MOCK_SERVER="1"))
            self.assertEqual(out.returncode, 0, out.stderr)
            text = out.stdout
            self.assertNotIn("not added", text)
            self.assertIn("skeleton 22222222-2222-4222-8222-222222222222 joints 12 id 22222222-2222-4222-8222-222222222222",
                          text)
            self.assertEqual(text.count("clip copy "), 2)
            self.assertIn("graph 44444444-4444-4444-8444-444444444444 hierarchy 22222222-2222-4222-8222-222222222222 "
                          "ours 4 left 1", text)
            self.assertNotIn("keen::RenderModel", text)


    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_a_creature_on_a_skeleton_of_its_own(self):
        """own_proportions: a skeleton copy, the model on it, the retargeted clips, a graph copy whose clip
        references (clip nodes and blend-space items) are ours, and the template on that graph."""
        import own_creature
        if HERE not in sys.path:
            sys.path.insert(0, HERE)  # the fixtures of test_own_creature.py
        import test_own_creature as toc
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as src:
            with open(os.path.join(src, "own.json"), "w", encoding="utf-8") as f:
                json.dump(toc.sidecar(), f)
            stand = [toc.REST] * 11
            with open(os.path.join(src, "beast_aaaaaaaa-0000-4000-8000-000000000001.json"), "w", encoding="utf-8") as f:
                json.dump(toc.clip(stand), f)
            with open(os.path.join(src, "beast_bbbbbbbb-0000-4000-8000-000000000002.json"), "w", encoding="utf-8") as f:
                json.dump(toc.clip(stand, loop=False), f)
            cfg = {"sidecar": os.path.join(src, "own.json"), "skeleton_guid": "22222222-2222-4222-8222-222222222222",
                   "reference_clip": os.path.join(src, "beast_aaaaaaaa-0000-4000-8000-000000000001.json"),
                   "clips": os.path.join(src, "beast_*.json"), "graph": "33333333-3333-4333-8333-333333333333",
                   "graph_guid": "44444444-4444-4444-8444-444444444444"}
            built = own_creature.build(cfg, toc.hierarchy())
            bear = dict(BEAR, own_proportions=cfg, hierarchy="wolf.hierarchy.json")
            make_mod.write_scripts(d, {"id": "test_pack", "title": "Test"}, [make_mod.listing_entry(bear)])
            make_mod.write_own(d, bear["name"], built)
            with open(os.path.join(d, "beast_bear_data.lua"), "w", encoding="utf-8") as f:
                f.write(STUB_DATA)
            with open(os.path.join(d, "beast_bear_blob.lua"), "w", encoding="utf-8") as f:
                f.write('return "blob"\n')
            mock = os.path.join(HERE, "..", "..", "tools", "models", "mock_eml.lua")
            out = subprocess.run(["lua", mock, d], capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stderr)
            text = out.stdout
            self.assertIn("skeleton 22222222-2222-4222-8222-222222222222 joints 12 id 22222222-2222-4222-8222-222222222222",
                          text)
            self.assertIn("model %s hierarchy 22222222-2222-4222-8222-222222222222" % BEAR["model_guid"], text)
            self.assertEqual(text.count("clip copy "), 2)
            # the mock graph: a clip node per source clip and a blend space with both clips and one other
            self.assertIn("graph 44444444-4444-4444-8444-444444444444 hierarchy 22222222-2222-4222-8222-222222222222 "
                          "ours 4 left 1", text)
            self.assertIn("template Beast_Bear keen::ecs::AnimationGraphResourceComponent2 animationGraph "
                          "44444444-4444-4444-8444-444444444444", text)
            # the joints' rest pose and inverse binds as built
            q = built["bind"][3][0]
            self.assertIn("pose 4 %.4f %.4f %.4f %.4f" % q, text)
            self.assertIn("position 12 %.4f %.4f %.4f" % built["bind"][11][1], text)

    def own_mod(self, d, src):
        """A pack with one creature on a skeleton of its own (two game clips) written into d; returns the mock's run."""
        import own_creature
        if HERE not in sys.path:
            sys.path.insert(0, HERE)
        import test_own_creature as toc
        with open(os.path.join(src, "own.json"), "w", encoding="utf-8") as f:
            json.dump(toc.sidecar(), f)
        for g, loop in (("aaaaaaaa-0000-4000-8000-000000000001", True), ("bbbbbbbb-0000-4000-8000-000000000002", False)):
            with open(os.path.join(src, "beast_%s.json" % g), "w", encoding="utf-8") as f:
                json.dump(toc.clip([toc.REST] * 11, loop=loop), f)
        cfg = {"sidecar": os.path.join(src, "own.json"), "skeleton_guid": "22222222-2222-4222-8222-222222222222",
               "reference_clip": os.path.join(src, "beast_aaaaaaaa-0000-4000-8000-000000000001.json"),
               "clips": os.path.join(src, "beast_*.json"), "graph": "33333333-3333-4333-8333-333333333333",
               "graph_guid": "44444444-4444-4444-8444-444444444444"}
        built = own_creature.build(cfg, toc.hierarchy())
        bear = dict(BEAR, own_proportions=cfg, hierarchy="wolf.hierarchy.json")
        make_mod.write_scripts(d, {"id": "test_pack", "title": "Test"}, [make_mod.listing_entry(bear)])
        make_mod.write_own(d, bear["name"], built)
        with open(os.path.join(d, "beast_bear_data.lua"), "w", encoding="utf-8") as f:
            f.write(STUB_DATA)
        with open(os.path.join(d, "beast_bear_blob.lua"), "w", encoding="utf-8") as f:
            f.write('return "blob"' + chr(10))

    def run_mock(self, d, missing=""):
        mock = os.path.join(HERE, "..", "..", "tools", "models", "mock_eml.lua")
        return subprocess.run(["lua", mock, d], capture_output=True, text=True, timeout=60,
                              env=dict(os.environ, MOCK_MISSING=missing))

    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_a_game_clip_that_is_gone_is_skipped(self):
        """After a game update removed a clip: that clip stays the game's in the graph copy (it plays on the copy:
        the same skeleton hash); the creature and the other clips are still added."""
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as src:
            self.own_mod(d, src)
            out = self.run_mock(d, "bbbbbbbb-0000-4000-8000-000000000002")
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn("the game clip bbbbbbbb-0000-4000-8000-000000000002 is gone: it stays the game's", out.stdout)
            self.assertEqual(out.stdout.count("clip copy "), 1)
            self.assertIn("template Beast_Bear keen::ecs::AnimationGraphResourceComponent2 animationGraph "
                          "44444444-4444-4444-8444-444444444444", out.stdout)

    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_a_game_graph_that_is_gone_skips_only_that_creature(self):
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as src:
            self.own_mod(d, src)
            out = self.run_mock(d, "33333333-3333-4333-8333-333333333333")
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn("beast_bear: not added:", out.stdout)
            self.assertNotIn("template Beast_Bear", out.stdout)

    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_a_skeleton_whose_joint_count_is_not_a_multiple_of_four(self):
        """The game pads the default pose's rotation vectors to a multiple of 4 joints (the boar: 58 joints, 60
        slots): positions start after the padding."""
        import own_creature
        if HERE not in sys.path:
            sys.path.insert(0, HERE)
        import joint_animation as ja
        import test_own_creature as toc
        n = 10
        hier = toc.hierarchy()
        hier["jointCount"] = n
        side = toc.sidecar()
        side["joints"] = side["joints"][:n]
        rest = toc.REST[:n]
        clip = ja.encode([rest] * 11, rest, fps=60, loop=True, hierarchy_hash=ja.hierarchy_hash(list(range(100, 110))))
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as src:
            with open(os.path.join(src, "own.json"), "w", encoding="utf-8") as f:
                json.dump(side, f)
            with open(os.path.join(src, "beast_aaaaaaaa-0000-4000-8000-000000000001.json"), "w", encoding="utf-8") as f:
                json.dump(clip, f)
            cfg = {"sidecar": os.path.join(src, "own.json"), "skeleton_guid": "22222222-2222-4222-8222-222222222222",
                   "reference_clip": os.path.join(src, "beast_aaaaaaaa-0000-4000-8000-000000000001.json"),
                   "clips": os.path.join(src, "beast_*.json"), "graph": "33333333-3333-4333-8333-333333333333",
                   "graph_guid": "44444444-4444-4444-8444-444444444444"}
            built = own_creature.build(cfg, hier)
            bear = dict(BEAR, own_proportions=cfg, hierarchy="wolf.hierarchy.json")
            make_mod.write_scripts(d, {"id": "test_pack", "title": "Test"}, [make_mod.listing_entry(bear)])
            make_mod.write_own(d, bear["name"], built)
            with open(os.path.join(d, "beast_bear_data.lua"), "w", encoding="utf-8") as f:
                f.write(STUB_DATA)
            with open(os.path.join(d, "beast_bear_blob.lua"), "w", encoding="utf-8") as f:
                f.write('return "blob"\n')
            mock = os.path.join(HERE, "..", "..", "tools", "models", "mock_eml.lua")
            out = subprocess.run(["lua", mock, d], capture_output=True, text=True, timeout=60)
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn("position 10 %.4f %.4f %.4f" % built["bind"][9][1], out.stdout)


if __name__ == "__main__":
    unittest.main()
