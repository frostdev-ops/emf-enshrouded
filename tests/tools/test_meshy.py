"""tools/models/meshy.py: the requests it sends (the API is not called: no credits)."""
import base64
import contextlib
import io
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "models"))
import meshy  # noqa: E402


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.old_cwd = os.getcwd()
        os.chdir(self.tmp.name)
        self.calls = []
        self.real_call = meshy.call
        meshy.call = lambda method, path, body=None: self.calls.append((method, path, body)) or {"result": "task-1"}
        self.pngs = []
        for i in range(5):
            p = os.path.join(self.tmp.name, "view%d.png" % i)
            with open(p, "wb") as f:
                f.write(b"\x89PNG fake %d" % i)
            self.pngs.append(p)

    def tearDown(self):
        meshy.call = self.real_call
        os.chdir(self.old_cwd)
        self.tmp.cleanup()

    def run_main(self, *args):
        old = sys.argv
        sys.argv = ["meshy.py"] + list(args)
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                meshy.main()
        finally:
            sys.argv = old
        return out.getvalue()


class ImageTests(Fixture):
    def test_one_image(self):
        out = self.run_main("image", self.pngs[0], "--prompt", "black panther, short glossy fur")
        method, path, body = self.calls[0]
        self.assertEqual((method, path), ("POST", "/v1/image-to-3d"))
        self.assertTrue(body["image_url"].startswith("data:image/png;base64,"))
        self.assertEqual(base64.b64decode(body["image_url"].split(",", 1)[1]), b"\x89PNG fake 0")
        self.assertEqual(body["texture_prompt"], "black panther, short glossy fur")
        self.assertTrue(body["should_texture"] and body["enable_pbr"] and body["should_remesh"])
        self.assertEqual((body["target_polycount"], body["topology"], body["ai_model"]), (12000, "triangle", "latest"))
        self.assertIn("task-1", out)
        self.assertIn("30 credits", out)

    def test_several_images(self):
        self.run_main("image", *self.pngs[:3], "--polycount", "8000")
        method, path, body = self.calls[0]
        self.assertEqual((method, path), ("POST", "/v1/multi-image-to-3d"))
        self.assertEqual(len(body["image_urls"]), 3)
        self.assertEqual(body["target_polycount"], 8000)

    def test_smart_topology_for_one_image(self):
        out = self.run_main("image", self.pngs[0], "--smart")
        body = self.calls[0][2]
        self.assertEqual((body["model_type"], body["ai_model"]), ("smart-topology", "meshy-t2.5"))
        self.assertNotIn("should_remesh", body)
        self.assertIn("20 credits", out)

    def test_mistakes_are_refused_before_any_call(self):
        for args in ([], self.pngs, self.pngs[:2] + ["--smart"], [os.path.join(self.tmp.name, "missing.png")]):
            with self.assertRaises(SystemExit, msg=str(args)):
                self.run_main("image", *args)
        self.assertEqual(self.calls, [])

    def test_retexture(self):
        """New textures on a model Meshy made (the same geometry) from a text prompt: 10 credits."""
        out = self.run_main("retexture", "abc", "a black panther, short glossy black fur", "--keep-uv")
        method, path, body = self.calls[0]
        self.assertEqual((method, path), ("POST", "/v1/retexture"))
        self.assertEqual(body, {"input_task_id": "abc", "text_style_prompt": "a black panther, short glossy black fur",
                                "ai_model": "latest", "enable_pbr": True, "enable_original_uv": True})
        self.assertIn("10 credits", out)
        self.calls.clear()
        meshy.call = lambda method, path, body=None: self.calls.append((method, path, body)) or {"status": "SUCCEEDED"}
        self.run_main("status", "def", "--kind", "retexture")
        self.assertEqual(self.calls[0][1], "/v1/retexture/def")

    def test_status_of_image_tasks(self):
        meshy.call = lambda method, path, body=None: self.calls.append((method, path, body)) or {"id": "t", "status": "SUCCEEDED"}
        self.run_main("status", "abc", "--kind", "image")
        self.run_main("status", "abc", "--kind", "multi")
        self.run_main("status", "abc")
        self.assertEqual([c[1] for c in self.calls], ["/v1/image-to-3d/abc", "/v1/multi-image-to-3d/abc",
                                                      "/v2/text-to-3d/abc"])


if __name__ == "__main__":
    unittest.main()
