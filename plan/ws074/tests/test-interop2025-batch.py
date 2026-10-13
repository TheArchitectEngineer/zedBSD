#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check bounded, deterministic area selection for the WPT batch runner."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


SOURCE = Path(__file__).with_name("run-interop2025-batch.py")
SPEC = importlib.util.spec_from_file_location("interop_batch", SOURCE)
BATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BATCH)


class SelectionTests(unittest.TestCase):
    def test_one_per_area_and_overlap_executed_once(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("a.html", "b.html", "c.html"):
                (root / name).write_text("<script src='/resources/testharness.js'></script>")
            selection = {"focus_counts": {"alpha": {}, "beta": {}}, "selected": [
                {"id": "/c.html", "source": "c.html", "type": "testharness",
                 "focus": ["alpha"]},
                {"id": "/a.html", "source": "a.html", "type": "testharness",
                 "focus": ["alpha", "beta"]},
                {"id": "/b.html", "source": "b.html", "type": "testharness",
                 "focus": ["beta"]},
            ]}
            identifiers, chosen, reasons = BATCH.choose(selection, root)
            self.assertEqual(identifiers, ["/a.html"])
            self.assertEqual(chosen, {"alpha": "/a.html", "beta": "/a.html"})
            self.assertEqual(reasons["alpha"]["candidate"], 2)

    def test_generated_https_and_driver_pages_are_unrun(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "driver.html").write_text("<script src='/resources/testdriver.js'></script>")
            selection = {"focus_counts": {"alpha": {}}, "selected": [
                {"id": "/x.any.worker.html", "source": "x.any.js",
                 "type": "testharness", "focus": ["alpha"]},
                {"id": "/x.https.html", "source": "x.https.html",
                 "type": "testharness", "focus": ["alpha"]},
                {"id": "/driver.html", "source": "driver.html",
                 "type": "testharness", "focus": ["alpha"]},
            ]}
            identifiers, chosen, reasons = BATCH.choose(selection, root)
            self.assertEqual(identifiers, [])
            self.assertEqual(chosen, {})
            self.assertEqual(reasons["alpha"]["generated_context"], 1)
            self.assertEqual(reasons["alpha"]["https"], 1)
            self.assertEqual(reasons["alpha"]["requires_driver_origin_or_worker"], 1)


if __name__ == "__main__":
    unittest.main()
