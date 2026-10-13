#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Crash runner outcome controls."""

import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).with_name("run-interop2025-crashtests.py")
SPEC = importlib.util.spec_from_file_location("wpt_crashtests", SCRIPT)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class ClassifyTest(unittest.TestCase):
    def test_success_with_image(self):
        self.assertEqual(RUNNER.classify(0, False, True), "pass")

    def test_process_error(self):
        self.assertEqual(RUNNER.classify(2, False, False), "process_error")

    def test_signal(self):
        self.assertEqual(RUNNER.classify(-11, False, False), "crash_signal")

    def test_timeout(self):
        self.assertEqual(RUNNER.classify(None, True, False), "timeout")

    def test_missing_image(self):
        self.assertEqual(RUNNER.classify(0, False, False), "no_image")


if __name__ == "__main__":
    unittest.main()
