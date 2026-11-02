"""The pinned copy of EML (the Enshrouded Mod Loader) in third_party/eml: only its library crates, its own licence,
and a patches file that names the upstream commit, so the copy can be checked and updated.
Run: python -m unittest discover -s tests/tools -p test_eml_vendor.py"""

import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
EML = os.path.join(REPO, "third_party", "eml")

CRATES = [
    "kfc",
    "kfc-base",
    "kfc-content",
    "kfc-resource",
    "mod-loader",
    "mod-loader-base",
    "mod-loader-lua",
    "mod-loader-runtime",
]


def read(*parts):
    with open(os.path.join(EML, *parts), encoding="utf-8") as f:
        return f.read()


def workspace_members(cargo_toml):
    """The strings in the [workspace] members = [...] list (comments and blank lines ignored)."""
    m = re.search(r"^\[workspace\][^\[]*?^members\s*=\s*\[(.*?)\]", cargo_toml, re.S | re.M)
    if not m:
        return None
    body = "\n".join(line.split("#", 1)[0] for line in m.group(1).splitlines())
    return re.findall(r'"([^"]+)"', body)


class EmlVendor(unittest.TestCase):
    def test_patches_file_names_the_pinned_commit(self):
        text = read("EMF_PATCHES.md")
        self.assertIn("https://github.com/Brabb3l/kfc-parser", text)
        self.assertIn("1670859", text)
        self.assertRegex(text, r"(?m)^#+ .*Patches")
        self.assertRegex(text, r"(?m)^\s*(\d+\.|-) +\**P1\b")

    def test_licence_is_eml_gpl3(self):
        lines = [line.strip() for line in read("LICENSE").splitlines() if line.strip()]
        self.assertEqual(lines[0], "GNU GENERAL PUBLIC LICENSE")
        self.assertEqual(lines[1], "Version 3, 29 June 2007")

    def test_only_library_crates(self):
        found = sorted(d for d in os.listdir(os.path.join(EML, "crates"))
                       if os.path.isdir(os.path.join(EML, "crates", d)))
        self.assertEqual(found, CRATES)

    def test_workspace_members_match_crates(self):
        members = workspace_members(read("Cargo.toml"))
        self.assertIsNotNone(members, "no [workspace] members list in third_party/eml/Cargo.toml")
        self.assertEqual(sorted(members), ["crates/" + c for c in CRATES])


if __name__ == "__main__":
    unittest.main()
