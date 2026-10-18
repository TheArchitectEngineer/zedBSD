#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Crash outcome controls and optional real-browser official-wait controls."""

import functools
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
import unittest

SCRIPT = Path(__file__).with_name("run-interop2025-crashtests.py")
SPEC = importlib.util.spec_from_file_location("wpt_crashtests", SCRIPT)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)
URL = "http://127.0.0.1:1234/control.html"
NONCE = "control-nonce"


def ready_line(nonce=NONCE, url=URL, state="complete"):
    return RUNNER.READY_PREFIX + json.dumps({
        "nonce": nonce, "reply": [url, "complete", []], "ready_state": state,
    })


class ClassifyTest(unittest.TestCase):
    def test_image_alone_is_not_success(self):
        self.assertEqual(RUNNER.classify(0, False, True), "readiness_missing")

    def test_success_requires_one_loaded_completion(self):
        for prefix in ("", "console: "):
            ready = RUNNER.parse_readiness(prefix + ready_line(), NONCE, URL)
            self.assertEqual(RUNNER.classify(0, False, True, ready), "pass")

    def test_transport_failures_take_precedence(self):
        ready = RUNNER.parse_readiness(ready_line(), NONCE, URL)
        for code, timeout, image, expected in [
                (2, False, False, "process_error"),
                (-11, False, False, "crash_signal"),
                (None, True, False, "timeout"),
                (0, False, False, "no_image")]:
            with self.subTest(expected=expected):
                self.assertEqual(RUNNER.classify(code, timeout, image, ready), expected)

    def test_bad_identity_or_load_state_cannot_pass(self):
        for line in [ready_line(nonce="other"), ready_line(url=URL + "?child"),
                     ready_line(state="loading"), RUNNER.READY_PREFIX + "{"]:
            ready = RUNNER.parse_readiness(line, NONCE, URL)
            self.assertEqual(RUNNER.classify(0, False, True, ready), "readiness_invalid")

    def test_duplicate_completion_cannot_pass(self):
        ready = RUNNER.parse_readiness(ready_line() + "\n" + ready_line(), NONCE, URL)
        self.assertEqual(RUNNER.classify(0, False, True, ready), "readiness_multiple")

    def test_probe_error_cannot_pass(self):
        line = RUNNER.ERROR_PREFIX + json.dumps({"nonce": NONCE, "message": "probe failed"})
        ready = RUNNER.parse_readiness(line + "\n" + ready_line(), NONCE, URL)
        self.assertEqual(RUNNER.classify(0, False, True, ready), "readiness_error")

    def test_page_exception_alone_is_not_a_crash(self):
        ready = RUNNER.parse_readiness("Uncaught Error: page error\n" + ready_line(), NONCE, URL)
        self.assertEqual(RUNNER.classify(0, False, True, ready), "pass")

    def test_all_focus_keeps_only_unique_crashtests(self):
        manifest = {"selected": [
            {"id": "/a.html", "type": "crashtest", "focus": ["a"]},
            {"id": "/b.html", "type": "crashtest", "focus": ["b"]},
            {"id": "/c.html", "type": "testharness", "focus": ["a"]},
        ]}
        self.assertEqual(len(RUNNER.selected_cases(manifest, "all")), 2)
        self.assertEqual(len(RUNNER.selected_cases(manifest, "a")), 1)
        manifest["selected"].append(manifest["selected"][0])
        with self.assertRaises(ValueError):
            RUNNER.selected_cases(manifest, "all")


@unittest.skipUnless(os.environ.get("BROWSER_CRASH_PROGRAM"), "set BROWSER_CRASH_PROGRAM for native controls")
class BrowserWaitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = RUNNER.ROOT / "build/ws074-crash-controls"
        base.mkdir(exist_ok=True)
        cls.directory = Path(tempfile.mkdtemp(prefix="retained-", dir=base))
        cls.wait_script, _ = RUNNER.load_wait_script(RUNNER.ROOT / "build/ws074-suites/wpt")
        cls.args = SimpleNamespace(program=Path(os.environ["BROWSER_CRASH_PROGRAM"]).resolve(),
                                   settle_ms=500, process_timeout=10, width=64, height=64)

    def render(self, name, body, child=False):
        test_id = "/" + name + ".html"
        original = body.encode("utf-8")
        (self.directory / (name + ".html")).write_bytes(original)
        server = RUNNER.QuietServer(("127.0.0.1", 0), functools.partial(
            RUNNER.QuietHandler, directory=str(self.directory)))
        base = "http://127.0.0.1:%d" % server.server_port
        nonce = "control-" + name
        injected, _ = RUNNER.inject_probe(original, RUNNER.make_probe(self.wait_script, nonce, base + test_id))
        server.probes = {test_id: injected}
        if child:
            child_body = b"<!doctype html><html><head></head><body>child</body></html>"
            (self.directory / "child.html").write_bytes(child_body)
            server.probes["/child.html"] = RUNNER.inject_probe(child_body, RUNNER.make_probe(
                self.wait_script, "child-nonce", base + "/child.html"))[0]
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            record = RUNNER.render_case(self.args, test_id, base + test_id,
                                        self.directory / (name + ".ppm"), nonce)
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
        RUNNER.retain_case(self.directory, self.directory / "progress.jsonl", len(list(self.directory.glob("case-*.json"))), record)
        (self.directory / (name + ".result.json")).write_text(json.dumps(record, indent=2) + "\n")
        return record

    def test_plain_page(self):
        record = self.render("plain", "<!doctype html><html><head></head><body>ready</body></html>")
        self.assertEqual(record["outcome"], "pass", record["stderr"])

    def test_permanent_wait_is_not_image_success(self):
        record = self.render("permanent", '<!doctype html><html class="test-wait"><head></head><body>wait</body></html>')
        self.assertIsNotNone(record["image"])
        self.assertEqual(record["outcome"], "readiness_missing", record["stderr"])

    def test_async_class_removal(self):
        record = self.render("delayed", '''<!doctype html><html class="test-wait"><head></head><body><script>
addEventListener('load',function(){setTimeout(function(){document.documentElement.classList.remove('test-wait');},40);});
</script></body></html>''')
        self.assertEqual(record["outcome"], "pass", record["stderr"])

    def test_test_rendered_event(self):
        record = self.render("rendered-event", '''<!doctype html><html class="test-wait"><head></head><body><script>
document.documentElement.addEventListener('TestRendered',function(){document.documentElement.classList.remove('test-wait');});
</script></body></html>''')
        self.assertEqual(record["outcome"], "pass", record["stderr"])

    def test_exception_does_not_replace_readiness(self):
        record = self.render("exception", "<!doctype html><body><script>throw new Error('control expected exception');</script></body>")
        self.assertIn("control expected exception", record["stderr"])
        self.assertEqual(record["outcome"], "pass", record["stderr"])

    def test_onload_root_removal_precedes_wait(self):
        record = self.render("removed-root", '''<!doctype html><html class="test-wait"><head></head><body><script>
addEventListener('load',function(){document.documentElement.remove();});
</script></body></html>''')
        self.assertEqual(record["outcome"], "pass", record["stderr"])

    def test_child_report_is_not_mixed_with_parent(self):
        record = self.render("parent", '<!doctype html><head></head><body><iframe src="/child.html"></iframe></body>', child=True)
        self.assertEqual(record["outcome"], "pass", record["stderr"])
        self.assertEqual(len(record["readiness"]["complete"]), 1)


if __name__ == "__main__":
    unittest.main()
