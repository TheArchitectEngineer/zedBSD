#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check the result boundary used by the first Interop testharness runner."""

import importlib.util
import json
from pathlib import Path
import unittest


SOURCE = Path(__file__).with_name("run-interop2025-harness.py")
SPEC = importlib.util.spec_from_file_location("interop_harness", SOURCE)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def complete(test_status, harness_status=0):
    payload = {"tests": [{"name": "case", "status": test_status, "message": ""}],
               "status": harness_status, "message": ""}
    return RUNNER.COMPLETE_PREFIX + json.dumps(payload) + "\n"


class OutcomeTests(unittest.TestCase):
    def test_pass_needs_completion_and_passing_subtest(self):
        self.assertEqual(RUNNER.classify_output(complete(0), 0)["outcome"], "pass")

    def test_subtest_failure_is_not_a_pass(self):
        self.assertEqual(RUNNER.classify_output(complete(1), 0)["outcome"], "fail")

    def test_wpt_and_process_timeouts_are_distinct(self):
        self.assertEqual(RUNNER.classify_output(complete(2), 0)["outcome"], "timeout")
        self.assertEqual(RUNNER.classify_output("", None, timed_out=True)["outcome"],
                         "process_timeout")

    def test_no_output_and_no_completion_are_not_passes(self):
        self.assertEqual(RUNNER.classify_output("", 0)["outcome"], "no_output")
        self.assertEqual(RUNNER.classify_output("ordinary log\n", 0)["outcome"],
                         "no_completion")

    def test_harness_error_and_nonzero_exit_are_not_passes(self):
        self.assertEqual(RUNNER.classify_output(complete(0, 1), 0)["outcome"],
                         "harness_error")
        self.assertEqual(RUNNER.classify_output(complete(0), 1)["outcome"],
                         "process_error")


if __name__ == "__main__":
    unittest.main()
