#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Native first-ready boundary controls; retained artifacts are never deleted."""

import functools
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest

from PIL import Image

SPEC = importlib.util.spec_from_file_location(
    "scripted_reftests", Path(__file__).with_name("run-interop2025-scripted-reftests.py"))
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def html(script="", wait=False, extra=""):
    return ('<!doctype html><html class="%s"><head><meta charset="utf-8"></head>'
            '<body><div id="square" style="position:absolute;left:0;top:0;width:64px;'
            'height:64px;background:red"></div>%s<script>%s</script></body></html>' %
            ("reftest-wait" if wait else "", extra, script))


@unittest.skipUnless(os.environ.get("WPT_READY_RENDERER"), "native renderer not specified")
class NativeCaptureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = RUNNER.ROOT / "build/ws074-20261018/q18/controls"
        root.mkdir(parents=True, exist_ok=True)
        cls.directory = Path(tempfile.mkdtemp(prefix="retained-", dir=root))
        cls.program = Path(os.environ["WPT_READY_RENDERER"]).resolve()
        cls.wait, cls.wait_metadata = RUNNER.wait_script(RUNNER.ROOT / "build/ws074-suites/wpt")

    def render(self, name, source, query="", xml=False, timeout=10, child=False):
        extension = ".xht" if xml else ".html"
        test_id = "/" + name + extension + query
        server = RUNNER.Server(("127.0.0.1", 0), functools.partial(
            RUNNER.Handler, directory=str(self.directory)))
        base = "http://127.0.0.1:%d" % server.server_port
        url = base + test_id
        nonce = "native-" + name
        original = source.encode("utf-8")
        adapter = RUNNER.probe(self.wait, nonce, url, xml=xml)
        body, offset = RUNNER.inject(original, adapter)
        (self.directory / (name + extension)).write_bytes(original)
        (self.directory / (name + ".instrumented")).write_bytes(body)
        server.probes = {test_id: (body, "application/xhtml+xml" if xml else "text/html; charset=utf-8")}
        if child:
            original_child = html().encode()
            child_adapter = RUNNER.probe(self.wait, "child-" + nonce, base + "/child.html")
            child_body, _ = RUNNER.inject(original_child, child_adapter)
            server.probes["/child.html"] = (child_body, "text/html; charset=utf-8")
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            record = RUNNER.capture(self.program, url, self.directory / (name + ".ppm"), nonce,
                                    timeout=timeout, width=64, height=64)
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
        record.update(source=source, insertion_offset=offset)
        RUNNER.retain_capture(self.directory, name, record)
        return record

    def pixel(self, record):
        with Image.open(record["image"]["file"]) as image:
            return image.convert("RGB").getpixel((10, 10))

    def test_plain_original_dom(self):
        record = self.render("plain", html("console.log('script-count',document.getElementsByTagName('script').length)"))
        self.assertEqual(record["outcome"], "pass", record)
        self.assertIn("console: script-count 1", record["stderr"])
        self.assertIn("READY_CAPTURE budget=32 acknowledged=1", record["stderr"])
        self.assertEqual(self.pixel(record), (255, 0, 0))

    def test_future_timer_does_not_replace_ready_frame(self):
        record = self.render("late", html("setTimeout(function(){square.style.background='lime'},500)"))
        self.assertEqual(record["outcome"], "pass", record)
        self.assertEqual(self.pixel(record), (255, 0, 0))
        self.assertIn("budget=32 acknowledged=1", record["stderr"])

    def test_test_rendered_waits_for_changed_frame(self):
        record = self.render("rendered", html("document.documentElement.addEventListener('TestRendered',function(){square.style.background='lime';document.documentElement.classList.remove('reftest-wait')})", wait=True))
        self.assertEqual(record["outcome"], "pass", record)
        self.assertEqual(self.pixel(record), (0, 255, 0))
        self.assertIn("budget=64 acknowledged=1", record["stderr"])

    def test_delayed_wait(self):
        record = self.render("delayed", html("setTimeout(function(){square.style.background='lime';document.documentElement.classList.remove('reftest-wait')},100)", wait=True))
        self.assertEqual(record["outcome"], "pass", record)
        self.assertEqual(self.pixel(record), (0, 255, 0))

    def test_permanent_wait_is_not_blank_success(self):
        record = self.render("permanent", html(wait=True))
        self.assertEqual(record["outcome"], "readiness_missing", record)
        self.assertIn("budget=5000 acknowledged=0", record["stderr"])

    def test_exception_does_not_fake_or_replace_readiness(self):
        record = self.render("exception", html("throw new Error('controlled')"))
        self.assertEqual(record["outcome"], "pass", record)
        self.assertIn("controlled", record["stderr"])

    def test_query_identity(self):
        record = self.render("query", html("console.log('query',location.search)"), query="?value=two%20words")
        self.assertEqual(record["outcome"], "pass", record)
        self.assertIn("console: query ?value=two%20words", record["stderr"])

    def test_xhtml_reference(self):
        source = ('<!DOCTYPE html><html xmlns="http://www.w3.org/1999/xhtml"><head>'
                  '<title>XML</title></head><body><div style="position:absolute;left:0;'
                  'top:0;width:64px;height:64px;background:red"></div></body></html>')
        record = self.render("xml", source, xml=True)
        self.assertEqual(record["outcome"], "pass", record)
        self.assertEqual(self.pixel(record), (255, 0, 0))

    def test_child_context_does_not_acknowledge_parent(self):
        record = self.render("parent", html(extra='<iframe src="child.html" style="position:absolute;left:100px;top:100px"></iframe>'), child=True)
        self.assertEqual(record["outcome"], "pass", record)
        self.assertEqual(len(record["readiness"]["complete"]), 1)
        self.assertNotIn('"nonce":"child-native-parent"', record["stderr"])

    def test_zero_loop_is_bounded_non_pass(self):
        record = self.render("zero-loop", html("function repeat(){setTimeout(repeat,0)}repeat()"), timeout=0.3)
        self.assertNotEqual(record["outcome"], "pass", record)
        self.assertTrue(record["timed_out"] or record["returncode"] != 0)


if __name__ == "__main__":
    unittest.main()
