#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Makes and checks the expected output of plan/ws076/tests/js/*.js (WS076).

  js-reference.py --reference        NAME.expected from the host's headless Chromium
  js-reference.py --outputs DIR      compare DIR/NAME.out (the guest's) with NAME.expected

The page and the comparison are those of plan/ws074/tests/run-js-tests.py, pointed at this
directory's tests, so the reference is another engine (V8), whose Math functions are its own.
"""

import argparse
import glob
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "run_js_tests", os.path.join(HERE, "../../ws074/tests/run-js-tests.py"))
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)
RUNNER.TESTS = os.path.join(HERE, "js")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--outputs")
    args = parser.parse_args()
    if args.reference:
        return RUNNER.reference()
    passed = 0
    tests = RUNNER.tests()
    for path in tests:
        name = os.path.splitext(os.path.basename(path))[0]
        with open(os.path.join(args.outputs, name + ".out"), errors="replace") as stream:
            output = stream.read()
        if RUNNER.compare(name, output, {}):
            passed += 1
    print("ws076-js %d/%d" % (passed, len(tests)))
    return 0 if passed == len(tests) else 1


if __name__ == "__main__":
    sys.exit(main())
