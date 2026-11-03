"""content/lib/emf_content.lua is the helper EML content mods copy into their src; the examples carry an exact copy."""
import pathlib
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]


class ContentLibTests(unittest.TestCase):
    def test_examples_carry_the_library_unchanged(self):
        lib = (REPO / "content" / "lib" / "emf_content.lua").read_bytes()
        copies = sorted(REPO.glob("content/*/src/emf_content.lua"))
        self.assertTrue(copies, "no example uses the helper")
        for copy in copies:
            with self.subTest(copy=str(copy.relative_to(REPO))):
                self.assertEqual(copy.read_bytes(), lib, "copy content/lib/emf_content.lua over it")

    def test_the_library_returns_its_table_and_names_its_rules(self):
        text = (REPO / "content" / "lib" / "emf_content.lua").read_text(encoding="utf-8")
        self.assertTrue(text.rstrip().endswith("return M"))
        for fn in ("M.add_item", "M.add_recipe", "M.add_text", "M.add_prompt_words", "M.finish", "M.menu_groups"):
            self.assertIn("function " + fn, text)
        self.assertIn("FIXED FOREVER", text)
        self.assertIn("loader.is_server", text)

    def test_add_item_sets_the_level_range(self):
        # A loot item's range is wide (the Bronze Sword: 1..55) and a craft picks a level inside it (33 for a level-3
        # character, live on build 1285601); crafted vanilla items have one level (the own crafting weapons: 3..3).
        text = (REPO / "content" / "lib" / "emf_content.lua").read_text(encoding="utf-8")
        self.assertIn("def.level", text)
        self.assertIn("itemLevelRange", text)
        example = (REPO / "content" / "emberbrand" / "src" / "mod.lua").read_text(encoding="utf-8")
        self.assertRegex(example, r"\blevel\s*=\s*\d+")


if __name__ == "__main__":
    unittest.main()
