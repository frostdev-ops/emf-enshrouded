"""tools/models/packs/beastmaster_stable.lua (the beasts pack's craftable Stable) against beast_stable_spec.lua."""
import pathlib
import shutil
import subprocess
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]


class BeastStableTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("lua"), "needs lua 5.4 on PATH")
    def test_the_stable_script(self):
        spec = REPO / "tests" / "tools" / "beast_stable_spec.lua"
        script = REPO / "tools" / "models" / "packs" / "beastmaster_stable.lua"
        out = subprocess.run(["lua", str(spec), str(script)], capture_output=True, text=True, timeout=60)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_the_beasts_pack_runs_it(self):
        import json
        pack = json.loads((REPO / "tools" / "models" / "packs" / "beastmaster_beasts.json").read_text(encoding="utf-8"))
        self.assertIn("beastmaster_stable.lua", pack.get("content_scripts", []))

    def test_the_script_never_touches_package(self):
        # EML's Lua has require but no global package table (it stopped every creature with clips once)
        text = (REPO / "tools" / "models" / "packs" / "beastmaster_stable.lua").read_text(encoding="utf-8")
        self.assertNotIn("package", text)


if __name__ == "__main__":
    unittest.main()
