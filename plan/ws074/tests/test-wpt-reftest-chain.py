#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Focused reference-chain controls without a browser process."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image


SCRIPT = Path(__file__).with_name("run-wpt-reftests.py")
SPEC = importlib.util.spec_from_file_location("wpt_reftests", SCRIPT)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class ReferenceChainTest(unittest.TestCase):
    def setUp(self):
        self.suite = tempfile.TemporaryDirectory()
        self.addCleanup(self.suite.cleanup)
        self.root = Path(self.suite.name)
        (self.root / "a.html").write_text(
            '<link rel="match" href="b.html">', encoding="utf-8")
        (self.root / "b.html").write_text("leaf", encoding="utf-8")
        self.root_patch = patch.object(RUNNER, "WPT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def check_chain(self, child_color):
        parent = Image.new("RGB", (1, 1), "white")

        def render(_program, _fonts, _width, _height, _url, _output,
                   _data, _timeout):
            return Image.new("RGB", (1, 1), child_color), []

        with patch.object(RUNNER, "render", side_effect=render):
            return RUNNER.match_chain(
                "a.html", parent, "http://localhost/", None, None, 1, 1,
                self.root, self.root, {"test.html", "a.html"}, 1)

    def test_equal_intermediate_with_different_leaf_fails(self):
        passed, comparisons = self.check_chain("black")
        self.assertFalse(passed)
        self.assertEqual(comparisons[0]["different_pixels"], 1)

    def test_equal_leaf_passes(self):
        passed, comparisons = self.check_chain("white")
        self.assertTrue(passed)
        self.assertTrue(comparisons[0]["equal"])

    def test_cycle_is_error(self):
        (self.root / "b.html").write_text(
            '<link rel="match" href="a.html">', encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "cycle"):
            self.check_chain("white")

    def test_mixed_relation_is_error(self):
        (self.root / "a.html").write_text(
            '<link rel="mismatch" href="b.html">', encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "mixed"):
            self.check_chain("white")


if __name__ == "__main__":
    unittest.main()
