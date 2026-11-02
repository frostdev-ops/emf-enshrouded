"""Guards on the emf_content crate (src/content): what its build settings and its locked dependencies must keep so
emf_content.dll can be loaded, used and unloaded inside the game.
Run: python -m unittest discover -s tests/tools -p test_content_crate.py"""

import os
import tomllib
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
CONTENT = os.path.join(REPO, "src", "content")


def load(name):
    with open(os.path.join(CONTENT, name), "rb") as f:
        return tomllib.load(f)


class ContentCrate(unittest.TestCase):
    def test_no_rayon_in_the_lock_file(self):
        # rayon starts worker threads that outlive a call; FreeLibrary would unmap the code they run.
        names = {p["name"] for p in load("Cargo.lock")["package"]}
        self.assertEqual(sorted(names & {"rayon", "rayon-core"}), [])

    def test_release_profile_unwinds(self):
        # catch_unwind at the exports only works when panics unwind; cargo builds test targets with unwinding
        # whatever the profile says, so the Rust tests cannot see this setting.
        profile = load("Cargo.toml").get("profile", {}).get("release", {})
        self.assertEqual(profile.get("panic"), "unwind")


if __name__ == "__main__":
    unittest.main()
