#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Render deadline and durable-progress controls."""

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("run-wpt-reftests.py")
SPEC = importlib.util.spec_from_file_location("wpt_reftests", SCRIPT)
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class CheckpointTest(unittest.TestCase):
    def test_each_completed_result_is_independently_readable(self):
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "progress.jsonl"
            RUNNER.write_checkpoint(path, {"kind": "header", "selected": 2})
            RUNNER.write_checkpoint(path, {"kind": "result", "index": 1,
                                           "record": {"status": "pass"}})
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(rows[0]["selected"], 2)
            self.assertEqual(rows[1]["record"]["status"], "pass")

    def test_render_uses_selected_timeout(self):
        failure = subprocess.TimeoutExpired(["browser"], 5)
        with patch.object(RUNNER.subprocess, "run", side_effect=failure) as call:
            with self.assertRaises(subprocess.TimeoutExpired):
                RUNNER.render(Path("browser"), Path("fonts"), 1, 1,
                              "http://localhost/test.html", Path("out.ppm"),
                              Path("data"), 5)
            self.assertEqual(call.call_args.kwargs["timeout"], 5)


if __name__ == "__main__":
    unittest.main()
