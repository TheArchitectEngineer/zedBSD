#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Selected reftest discovery controls."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("run-wpt-reftests.py")
SPEC = importlib.util.spec_from_file_location("wpt_reftests", SCRIPT)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class SelectionTest(unittest.TestCase):
    def setUp(self):
        self.suite = tempfile.TemporaryDirectory()
        self.addCleanup(self.suite.cleanup)
        self.root = Path(self.suite.name)
        (self.root / "ref.html").write_text("reference", encoding="utf-8")
        (self.root / "a.html").write_text(
            '<link rel="match" href="ref.html">', encoding="utf-8")
        (self.root / "script.html").write_text(
            '<link rel="match" href="ref.html"><script></script>', encoding="utf-8")
        self.root_patch = patch.object(RUNNER, "WPT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def test_selected_ids_have_specific_unrun_reasons(self):
        rows = [
            {"id": "/a.html", "source": "a.html", "type": "reftest"},
            {"id": "/a.html?variant=1", "source": "a.html", "type": "reftest"},
            {"id": "/script.html", "source": "script.html", "type": "reftest"},
            {"id": "/absent.html", "source": "absent.html", "type": "reftest"},
            {"id": "/ref.html", "source": "ref.html", "type": "testharness"},
        ]
        candidates, skipped, omitted = RUNNER.discover(".", rows)
        self.assertEqual([item["path"] for item in candidates], ["a.html"])
        self.assertEqual(skipped, {"variant_url": 1, "script": 1, "missing_test": 1})
        self.assertEqual({item["id"]: item["reason"] for item in omitted}, {
            "/a.html?variant=1": "variant_url",
            "/script.html": "script",
            "/absent.html": "missing_test",
        })

    def test_duplicate_selected_id_is_error(self):
        row = {"id": "/a.html", "source": "a.html", "type": "reftest"}
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            RUNNER.discover(".", [row, row])

    def test_sample_round_robins_focus(self):
        rows = [{"path": "a/1.html", "focus": ["a"]},
                {"path": "a/2.html", "focus": ["a"]},
                {"path": "b/1.html", "focus": ["b"]}]
        sample = RUNNER.stratified(rows, ".", 2)
        self.assertEqual([item["path"] for item in sample],
                         ["a/1.html", "b/1.html"])


if __name__ == "__main__":
    unittest.main()
