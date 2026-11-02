"""tools/content/crate_licences.py: the page of Rust libraries inside emf_content.dll (licenses/rust-crates.html in
the release zip), made from `cargo metadata`: every crates.io package the DLL links, with its licence and the licence
files it ships; EML's own crates (path packages, under EML's GPL text) and test-only packages are left out.
Run: python -m unittest discover -s tests/tools -p test_crate_licences.py"""

import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "content"))

import crate_licences  # noqa: E402

REG = "registry+https://github.com/rust-lang/crates.io-index"


def package(name, version, folder, license="MIT", source=REG, repository=None, license_file=None):
    return {
        "name": name, "version": version, "id": f"{name} {version}", "source": source, "license": license,
        "license_file": license_file, "repository": repository,
        "manifest_path": os.path.join(folder, "Cargo.toml"),
    }


def dep(pkg, kinds=(None,)):
    return {"pkg": pkg["id"], "dep_kinds": [{"kind": k, "target": None} for k in kinds]}


class CrateLicences(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="crate_licences_")
        self.addCleanup(shutil.rmtree, self.tmp)

    def folder(self, name, files=None):
        d = os.path.join(self.tmp, name)
        os.makedirs(d)
        for fname, text in (files or {}).items():
            with open(os.path.join(d, fname), "w", encoding="utf-8") as f:
                f.write(text)
        return d

    def metadata(self):
        root = package("emf-content", "0.1.0", self.folder("root"), source=None)
        eml = package("mod-loader", "0.2.0", self.folder("eml"), license="GPL-3.0-or-later", source=None)
        serde = package("serde", "1.0.200", self.folder("serde", {"LICENSE-MIT": "Copyright (c) serde <b>authors</b>",
                                                                   "LICENSE-APACHE": "Apache text"}),
                        license="MIT OR Apache-2.0", repository="https://github.com/serde-rs/serde")
        zlib = package("adler2", "2.0.0", self.folder("adler2"), license="0BSD OR MIT OR Apache-2.0")
        own = package("odd", "1.0.0", self.folder("odd", {"COPYING.txt": "own terms"}), license=None,
                      license_file="COPYING.txt")
        builder = package("cc", "1.2.0", self.folder("cc", {"LICENSE": "cc"}))
        tester = package("tempfile", "3.0.0", self.folder("tempfile", {"LICENSE": "t"}))
        # a "-src" crate is a build dependency that compiles a C library into the crate that uses it (lua-src: Lua 5.4)
        lua_src = package("lua-src", "548.1.1", self.folder("lua-src", {"LICENSE": "Lua's MIT text"}))
        lua_sys = package("mlua-sys", "0.8.3", self.folder("mlua-sys"))
        nodes = [
            {"id": root["id"], "deps": [dep(eml), dep(serde), dep(tester, ("dev",))]},
            {"id": eml["id"], "deps": [dep(zlib), dep(own), dep(builder, ("build",)), dep(lua_sys)]},
            {"id": serde["id"], "deps": []}, {"id": zlib["id"], "deps": []}, {"id": own["id"], "deps": []},
            {"id": builder["id"], "deps": []}, {"id": tester["id"], "deps": []},
            {"id": lua_sys["id"], "deps": [dep(lua_src, ("build",)), dep(builder, ("build",))]}, {"id": lua_src["id"], "deps": []},
        ]
        return {"packages": [root, eml, serde, zlib, own, builder, tester, lua_src, lua_sys],
                "resolve": {"root": root["id"], "nodes": nodes}}

    def test_only_linked_crates_io_packages(self):
        names = [p["name"] for p in crate_licences.collect(self.metadata())]
        # sorted; no path crates, no dev dependencies, no build tools (cc); a -src build dependency's C code is linked
        self.assertEqual(names, ["adler2", "lua-src", "mlua-sys", "odd", "serde"])

    def test_licence_files_are_read(self):
        serde = next(p for p in crate_licences.collect(self.metadata()) if p["name"] == "serde")
        self.assertEqual([f for f, _ in serde["files"]], ["LICENSE-APACHE", "LICENSE-MIT"])
        self.assertIn("serde <b>authors</b>", dict(serde["files"])["LICENSE-MIT"])

    def test_a_licence_file_named_in_the_manifest(self):
        odd = next(p for p in crate_licences.collect(self.metadata()) if p["name"] == "odd")
        self.assertEqual(odd["license"], "see COPYING.txt")
        self.assertEqual(odd["files"], [("COPYING.txt", "own terms")])

    def test_the_page_escapes_and_lists_everything(self):
        page = crate_licences.render(crate_licences.collect(self.metadata()))
        self.assertIn("<title>", page)
        self.assertIn("serde 1.0.200", page)
        self.assertIn("MIT OR Apache-2.0", page)
        self.assertIn("https://github.com/serde-rs/serde", page)
        self.assertIn("serde &lt;b&gt;authors&lt;/b&gt;", page)
        self.assertNotIn("<b>authors</b>", page)
        # a crate without licence files says so and keeps its licence expression
        self.assertIn("adler2 2.0.0", page)
        self.assertIn("ships no licence file", page)
        self.assertNotIn("mod-loader", page)


@unittest.skipUnless(shutil.which("cargo"), "needs cargo (the emf_content build)")
class LinkedCrates(unittest.TestCase):
    """The real graph: everything linked into emf_content.dll is under a permissive licence. A copyleft library (MPL,
    LGPL, GPL) from an EML update would change what the release must carry; this stops it until someone decides."""

    def test_no_copyleft_library_is_linked(self):
        meta = crate_licences.cargo_metadata(os.path.join(REPO, "src", "content", "Cargo.toml"))
        self.assertIsNotNone(meta, "cargo metadata failed (a build fetches the crates first)")
        packages = crate_licences.collect(meta)
        self.assertGreater(len(packages), 50)
        self.assertIn("lua-src", [p["name"] for p in packages])  # EML's Lua 5.4, compiled into the DLL
        copyleft = [f"{p['name']} {p['version']}: {p['license']}" for p in packages
                    if any(w in p["license"].upper() for w in ("GPL", "MPL", "EPL", "CDDL", "EUPL"))
                    or p["license"] in ("not stated",) or p["license"].startswith("see ")]
        self.assertEqual(copyleft, [])


if __name__ == "__main__":
    unittest.main()
