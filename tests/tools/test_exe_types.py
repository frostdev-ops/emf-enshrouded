"""Unit tests of tools/re/exe_types.py, EMF's reader of the game's reflection data (keen type descriptors) in
enshrouded.exe, on small fake images laid out the way the exe is (reflection_fixture.py).
Run: python -I -m unittest discover -s tests/tools"""

import hashlib
import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
import struct
import subprocess
from contextlib import redirect_stderr, redirect_stdout
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "tools", "re"))
import exe_types  # noqa: E402
import reflection_fixture as fx  # noqa: E402


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def by_name(doc):
    return {t["qualified_name"]: t for t in doc["types"]}


class FindsTheTypeTable(unittest.TestCase):
    def setUp(self):
        self.b, self.t, self.order = fx.small_game()
        self.img = self.b.image()

    def test_the_longest_list_of_descriptors_is_the_type_table(self):
        table = exe_types.find_type_table(self.img)
        self.assertEqual(table.count, len(self.order))
        self.assertEqual([self.img.u64(table.list_va + 8 * i) for i in range(table.count)], self.order)

    def test_the_root_holds_the_groups(self):
        table = exe_types.find_type_table(self.img)
        self.assertIsNotNone(table.root_va)
        self.assertEqual([g[0] for g in table.groups], ["", "ecs.component"])

    def test_no_table_is_a_plain_error(self):
        empty = fx.ImageBuilder()
        empty.string("keen::nothing")
        with self.assertRaises(exe_types.ImageError) as e:
            exe_types.find_type_table(empty.image())
        self.assertIn("no type table", str(e.exception))


def three_types():
    b = fx.ImageBuilder()
    keen = b.namespace("keen")
    types = [b.type("keen::T%d" % i, "uint32", size=4, align=4, namespace=keen, hash=0x5EED0100 + i)
             for i in range(3)]
    return b, types


class DecoyLists(unittest.TestCase):
    def table(self, before_root):
        b, t, order = fx.small_game(before_root=before_root)
        img = b.image()
        return img, order, exe_types.find_type_table(img)

    def test_a_reordered_copy_of_the_same_length_found_first_does_not_win(self):
        img, order, table = self.table(lambda b, t, order: b.holder(b.pointers(order[::-1]), len(order)))
        self.assertIsNotNone(table.root_va)
        self.assertEqual([img.u64(table.list_va + 8 * i) for i in range(table.count)], order)

    def test_a_longer_list_with_repeated_entries_is_not_the_table(self):
        img, order, table = self.table(lambda b, t, order: b.holder(b.pointers(order + order[:3]), len(order) + 3))
        self.assertEqual(table.count, len(order))
        self.assertIsNotNone(table.root_va)

    def test_two_different_lists_of_the_same_length_without_a_root_are_ambiguous(self):
        b, types = three_types()
        b.holder(b.pointers(types), 3)
        b.holder(b.pointers(types[::-1]), 3)
        with self.assertRaises(exe_types.ImageError) as e:
            exe_types.find_type_table(b.image())
        self.assertIn("ambiguous", str(e.exception))

    def test_a_table_without_a_root_is_read_but_says_so(self):
        b, types = three_types()
        b.holder(b.pointers(types), 3)
        doc = exe_types.read_types(b.image())
        self.assertIsNone(doc["type_table"]["root_rva"])
        self.assertEqual(len(doc["types"]), 3)
        tmp = tempfile.mkdtemp(prefix="emf_exe_types_")
        self.addCleanup(shutil.rmtree, tmp, True)
        exe = os.path.join(tmp, "enshrouded.exe")
        with open(exe, "wb") as f:
            f.write(b.pe_bytes())
        err = io.StringIO()
        with redirect_stdout(io.StringIO()), redirect_stderr(err):
            exe_types.main(["--exe", exe, "-o", os.path.join(tmp, "t.emf.json")])
        self.assertIn("root", err.getvalue())


def fnv1a32(text):
    h = 0x811C9DC5
    for c in text.encode("utf-8"):
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


class Fixture(unittest.TestCase):
    def test_the_fixture_is_made_up(self):
        """The game's type hash is FNV-1a-32 of the qualified name; the fixture's hashes are invented, so none is."""
        b, t, order = fx.small_game()
        doc = exe_types.read_types(b.image())
        for ty in doc["types"]:
            self.assertNotEqual(ty["hash"], fnv1a32(ty["qualified_name"]), ty["qualified_name"])


class DecodesTypes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.b, cls.t, cls.order = fx.small_game()
        cls.doc = exe_types.read_types(cls.b.image())
        cls.types = by_name(cls.doc)

    def test_indices_follow_the_type_table(self):
        self.assertEqual([t["index"] for t in self.doc["types"]], list(range(len(self.doc["types"]))))
        self.assertEqual(self.types["keen::bool"]["index"], 0)
        self.assertEqual(self.doc["type_table"]["count"], len(self.order))

    def test_struct_fields_names_offsets_and_types(self):
        wp = self.types["keen::WorldPosition"]
        sint64 = self.types["keen::sint64"]["index"]
        self.assertEqual(wp["kind"], "struct")
        self.assertEqual((wp["size"], wp["alignment"], wp["element_alignment"]), (24, 8, 8))
        self.assertEqual(wp["fields"], [{"name": "x", "type": sint64, "offset": 0},
                                        {"name": "y", "type": sint64, "offset": 8},
                                        {"name": "z", "type": sint64, "offset": 16}])
        self.assertEqual((wp["hash"], wp["internal_hash"]), (0x5EED0001, 0x5EED0002))

    def test_names_and_namespace(self):
        h = self.types["keen::ecs::Health"]
        self.assertEqual(h["name"], "Health")
        self.assertEqual(h["impact_name"], "ecs.Health")
        self.assertEqual(h["namespace"], ["keen", "ecs"])
        self.assertEqual(self.types["keen::bool"]["namespace"], ["keen"])

    def test_inheritance_is_the_inner_type_of_a_struct(self):
        h = self.types["keen::ecs::Health"]
        self.assertEqual(h["inner"], self.types["keen::ecs::Component"]["index"])
        self.assertIsNone(self.types["keen::WorldPosition"]["inner"])

    def test_enum_values_and_underlying_type(self):
        f = self.types["keen::ecs::FactionType"]
        self.assertEqual(f["kind"], "enum")
        self.assertEqual(f["inner"], self.types["keen::uint8"]["index"])
        self.assertEqual(f["values"], [{"name": "Environment", "value": 0}, {"name": "HitAll", "value": 1},
                                       {"name": "Player", "value": 7}])

    def test_a_signed_enum_keeps_negative_values(self):
        self.assertEqual(self.types["keen::ecs::Signed"]["values"][0], {"name": "Minus", "value": -1})

    def test_kinds(self):
        kinds = {qn: t["kind"] for qn, t in self.types.items()}
        self.assertEqual(kinds["keen::float32"], "float32")
        self.assertEqual(kinds["keen::Guid"], "guid")
        self.assertEqual(kinds["keen::HashKey32"], "typedef")
        self.assertEqual(kinds["keen::Bitmask8<keen::ecs::FactionType>"], "bitmask8")
        self.assertEqual(kinds["keen::BlobString"], "blob_string")
        arr = self.types["keen::StaticArray<keen::uint8,3>"]
        self.assertEqual((arr["kind"], arr["count"], arr["inner"]), ("static_array", 3,
                                                                     self.types["keen::uint8"]["index"]))

    def test_an_unknown_kind_keeps_its_number(self):
        b = fx.ImageBuilder()
        keen = b.namespace("keen")
        odd = b.type("keen::Odd", 0x2A, size=4, align=4, namespace=keen)
        b.root([odd, b.type("keen::uint8", "uint8", size=1, namespace=keen)])
        self.assertEqual(by_name(exe_types.read_types(b.image()))["keen::Odd"]["kind"], "kind_42")

    def test_flags_are_the_kind_words_upper_bits(self):
        self.assertEqual(self.types["keen::BlobString"]["flags"], 0x04)
        self.assertEqual(self.types["keen::bool"]["flags"], 0)

    def test_attributes_of_types_and_fields(self):
        self.assertEqual(self.types["keen::ecs::Component"]["attributes"], [["abstract", ""]])
        self.assertEqual(self.types["keen::ecs::Health"]["attributes"],
                         [["server_only", ""], ["description", "Hit points"]])
        self.assertEqual(self.types["keen::ecs::Health"]["fields"][0]["attributes"],
                         [["description", "Current health"]])
        self.assertNotIn("attributes", self.types["keen::WorldPosition"])

    def test_default_value_bytes(self):
        self.assertEqual(self.types["keen::ecs::Health"]["default"], "0000803f" + "00" * 8)
        self.assertNotIn("default", self.types["keen::WorldPosition"])

    def test_variant_types(self):
        v = self.types["keen::BlobVariant<keen::ecs::Component>"]
        self.assertEqual(v["variants"], [self.types["keen::ecs::Health"]["index"],
                                         self.types["keen::ecs::Position"]["index"]])

    def test_a_type_outside_the_table_is_added_after_it(self):
        hidden = self.types["keen::ecs::Hidden"]
        self.assertEqual(hidden["index"], len(self.order))
        self.assertFalse(hidden["listed"])
        self.assertNotIn("listed", self.types["keen::ecs::Health"])
        self.assertEqual(self.types["keen::ecs::UsesHidden"]["fields"][0]["type"], hidden["index"])

    def test_groups_by_index(self):
        self.assertEqual(self.doc["groups"], {"ecs.component": [self.types["keen::ecs::Health"]["index"],
                                                                self.types["keen::ecs::Position"]["index"]]})


class ExeFile(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="emf_exe_types_")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.b, self.t, self.order = fx.small_game()
        self.b.build_info("0123456789abcdef0123456789abcdef01234567", "0.0.15.0", "master")
        self.exe = os.path.join(self.tmp, "enshrouded.exe")
        with open(self.exe, "wb") as f:
            f.write(self.b.pe_bytes(timestamp=0x6AD77F0C))

    def test_pe_sections_map_addresses(self):
        img = exe_types.Image.from_pe(read_bytes(self.exe))
        self.assertEqual(img.image_base, fx.IMAGE_BASE)
        self.assertEqual(img.u64(self.t["keen::bool"] + 0x20), self.b._strings["keen::bool"])

    def test_not_a_pe_file(self):
        with self.assertRaises(exe_types.ImageError):
            exe_types.Image.from_pe(b"hello")

    def test_a_cut_off_exe_is_a_plain_error(self):
        blob = read_bytes(self.exe)
        for size in (0x90, 0x100, 0x1A0, 0x300, len(blob) - 0x200):
            with self.assertRaises(exe_types.ImageError, msg=hex(size)):
                exe_types.Image.from_pe(blob[:size])

    def test_a_section_table_past_the_end_is_a_plain_error(self):
        blob = bytearray(read_bytes(self.exe))
        pe = struct.unpack_from("<I", blob, 0x3C)[0]
        struct.pack_into("<H", blob, pe + 6, 0xFFFF)  # sections
        with self.assertRaises(exe_types.ImageError):
            exe_types.Image.from_pe(bytes(blob))

    def test_a_commit_without_a_version_next_to_it_is_not_the_build_record(self):
        b, t, order = fx.small_game()
        decoy = b.string("ffffffffffffffffffffffffffffffffffffffff")
        slot = b.data.alloc(0x20)
        b.data.put(slot, "QQQQ", decoy, 0, b.string("not a version"), 0)
        b.build_info("0123456789abcdef0123456789abcdef01234567", "0.0.15.0", "master")
        img = exe_types.Image.from_pe(b.pe_bytes())
        self.assertEqual(exe_types.find_build_info(img)["git_commit"], "0123456789abcdef0123456789abcdef01234567")

    def test_build_info(self):
        img = exe_types.Image.from_pe(read_bytes(self.exe))
        self.assertEqual(exe_types.find_build_info(img), {
            "git_commit": "0123456789abcdef0123456789abcdef01234567", "version": "0.0.15.0", "branch": "master"})

    def test_read_exe_source_record(self):
        doc = exe_types.read_exe(self.exe)
        src = doc["source"]
        self.assertEqual(doc["format"], "emf-types")
        self.assertEqual(doc["format_version"], exe_types.FORMAT_VERSION)
        self.assertEqual(src["file"], "enshrouded.exe")
        self.assertEqual(src["sha256"], hashlib.sha256(read_bytes(self.exe)).hexdigest())
        self.assertEqual(src["pe_timestamp"], 0x6AD77F0C)
        self.assertEqual(src["image_base"], fx.IMAGE_BASE)
        self.assertIsNone(src["build"])  # not a known game build
        self.assertIsNone(src["server"])
        self.assertEqual(src["version"], "0.0.15.0")
        self.assertEqual(doc["type_table"]["rva"], doc["type_table"]["va"] - fx.IMAGE_BASE)

    def test_main_writes_the_types_file(self):
        out = os.path.join(self.tmp, "types.emf.json")
        with redirect_stdout(io.StringIO()) as said:
            exe_types.main(["--exe", self.exe, "-o", out])
        doc = json.loads(read_text(out))
        self.assertEqual(len(doc["types"]), len(self.order) + 1)
        self.assertIn("types", said.getvalue())

    def test_the_types_file_reads_back_as_written(self):
        doc = exe_types.read_exe(self.exe)
        for groups in (doc["groups"], {}):
            doc["groups"] = groups
            out = os.path.join(self.tmp, "back.json")
            exe_types.write_types(doc, out)
            self.assertEqual(json.loads(read_text(out)), doc)

    def test_without_an_output_it_writes_outside_the_repository(self):
        """Game data never lands in the current folder (a checkout): the default is %LOCALAPPDATA%\\EMF\\types."""
        appdata = os.path.join(self.tmp, "appdata")
        cwd = os.getcwd()
        os.chdir(self.tmp)
        self.addCleanup(os.chdir, cwd)
        sha = hashlib.sha256(read_bytes(self.exe)).hexdigest()
        with mock.patch.dict(os.environ, {"LOCALAPPDATA": appdata}), redirect_stdout(io.StringIO()) as said:
            exe_types.main(["--exe", self.exe])
        out = os.path.join(appdata, "EMF", "types", "enshrouded_%s.emf.json" % sha[:12])
        self.assertTrue(os.path.isfile(out), said.getvalue())
        self.assertIn(out, said.getvalue())
        self.assertEqual(sorted(os.listdir(self.tmp)), ["appdata", "enshrouded.exe"])

    def test_the_default_name_carries_the_build(self):
        src = {"file": "enshrouded_server.exe", "build": 1273043, "sha256": "ab" * 32}
        with mock.patch.dict(os.environ, {"LOCALAPPDATA": r"C:\x"}):
            self.assertEqual(exe_types.default_out(src),
                             os.path.join(r"C:\x", "EMF", "types", "enshrouded_server_1273043.emf.json"))

    def test_main_without_an_exe_says_what_to_pass(self):
        env = dict(os.environ)
        env.pop("EMF_GAME_DIR", None)
        with mock.patch.dict(os.environ, env, clear=True):
            with self.assertRaises(SystemExit) as e:
                exe_types.main(["-o", os.path.join(self.tmp, "x.json")])
        self.assertIn("--exe", str(e.exception.code))
        self.assertIn("EMF_GAME_DIR", str(e.exception.code))


class KnownBuilds(unittest.TestCase):
    def test_same_table_as_the_core(self):
        """exe_types names a build by the exe's sha256 with the core's table (src/common/signatures.cpp), server flag
        included."""
        src = read_text(os.path.join(REPO, "src", "common", "signatures.cpp"))
        core = {h: (int(b), bool(server)) for b, h, server in
                re.findall(r'\{(\d+)u, "([0-9a-f]{64})"(, true)?\}', src)}
        self.assertTrue(core)
        self.assertTrue(any(server for _, server in core.values()))
        self.assertEqual(exe_types.KNOWN_BUILDS, core)

    def test_describe_names_the_dedicated_server(self):
        line = exe_types.describe({"file": "enshrouded_server.exe", "build": 1273043, "server": True,
                                   "sha256": "ab" * 32})
        self.assertIn("build 1273043 (dedicated server)", line)
        self.assertNotIn("dedicated server", exe_types.describe({"file": "enshrouded.exe", "build": 1273043,
                                                                 "server": False, "sha256": "ab" * 32}))


class Repository(unittest.TestCase):
    def test_types_files_are_never_committed(self):
        """Types files are game data (8 MB with default values): .gitignore keeps them out wherever they are."""
        for path in ("types.emf.json", "tools/re/enshrouded_1273043.emf.json", "types.json", "x/types.json"):
            try:
                r = subprocess.run(["git", "-C", REPO, "check-ignore", "-q", "--no-index", path],
                                   capture_output=True)
            except OSError:
                self.skipTest("git is not installed")
            self.assertEqual(r.returncode, 0, path)

    def test_the_server_exe_is_not_claimed_as_checked(self):
        for text in (exe_types.__doc__, exe_types.build_parser().format_help()):
            self.assertIn("not checked yet", " ".join(text.split()))


if __name__ == "__main__":
    unittest.main()
