#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs test262 (parse only) against zdesktop-browser's JavaScript parser.

  run-test262-parse.py [--driver PATH] [--limit N] [--filter TEXT] [--show N] [--record FILE] [--manifest-only OUT]
  run-test262-parse.py --results RESULTS    (count the output of a driver run elsewhere, e.g. the guest's)

The tests are build/ws074-suites/test262/test/**/*.js (fetch-suites.sh test262) without intl402/,
staging/, the harness's fixtures (*_FIXTURE.js) and the tests of the proposals zdesktop-browser does
not plan yet (EXCLUDED_FEATURES: decorators, import defer, source phase imports, explicit resource
management; design.md §15 leaves unplanned proposals out).  A test's frontmatter decides how it is parsed:
a module (flags: [module]), strict only (onlyStrict), sloppy only (noStrict or raw), or both.  A test
passes when every mode it runs in behaves as expected: a parse-phase negative test (negative: phase:
parse) must fail to parse, any other test must parse.  Early errors that need scope analysis are
counted like the others.  The driver is plan/ws074/tests/host-parse.c (built by host-build.sh).
"""

import argparse
import datetime
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
SUITE = os.path.join(ROOT, "build/ws074-suites/test262")
FRONTMATTER = re.compile(r"/\*---(.*?)---\*/", re.S)
EXCLUDED_FEATURES = {"decorators", "import-defer", "source-phase-imports", "source-phase-imports-module-source",
                     "explicit-resource-management"}


def read_meta(path):
    with open(path, encoding="utf-8", errors="replace") as stream:
        text = stream.read()
    match = FRONTMATTER.search(text)
    meta = match.group(1) if match else ""
    flags = []
    found = re.search(r"^flags:\s*\[(.*?)\]", meta, re.M)
    if found:
        flags = [flag.strip() for flag in found.group(1).split(",") if flag.strip()]
    negative = None
    found = re.search(r"^negative:\s*\n((?:\s+.*\n?)+)", meta, re.M)
    if found:
        phase = re.search(r"phase:\s*(\w+)", found.group(1))
        negative = phase.group(1) if phase else None
    features = []
    found = re.search(r"^features:\s*\[(.*?)\]", meta, re.M)
    if found:
        features = [feature.strip() for feature in found.group(1).split(",") if feature.strip()]
    return flags, negative, features


def collect(args):
    tests = []
    args.excluded = 0
    base = os.path.join(SUITE, "test")
    for directory, subdirectories, files in os.walk(base):
        subdirectories.sort()
        relative = os.path.relpath(directory, base)
        top = relative.split(os.sep)[0]
        if top in ("intl402", "staging"):
            continue
        for name in sorted(files):
            if not name.endswith(".js") or name.endswith("_FIXTURE.js"):
                continue
            path = os.path.join(directory, name)
            if args.filter and args.filter not in path:
                continue
            flags, negative, features = read_meta(path)
            if EXCLUDED_FEATURES.intersection(features):
                args.excluded += 1
                continue
            if "module" in flags:
                modes = ["module"]
            elif "onlyStrict" in flags:
                modes = ["strict"]
            elif "noStrict" in flags or "raw" in flags:
                modes = ["sloppy"]
            else:
                modes = ["sloppy", "strict"]
            tests.append((path, modes, negative == "parse"))
            if args.limit and len(tests) >= args.limit:
                return tests
    return tests


def count(tests, results, args):
    outcome = {}
    for line in results.splitlines():
        parts = line.split("\t")
        if len(parts) >= 3:
            outcome[(parts[0], parts[1])] = parts[2]
    passed = 0
    positive = [0, 0]
    negative = [0, 0]
    failures = []
    for path, modes, expect_error in tests:
        ok = True
        detail = ""
        for mode in modes:
            result = outcome.get((path, mode), "missing")
            if expect_error and result == "ok":
                ok = False
                detail = mode + ": parsed, but a SyntaxError was expected"
            elif not expect_error and result != "ok":
                ok = False
                detail = mode + ": " + result
        bucket = negative if expect_error else positive
        bucket[1] += 1
        if ok:
            passed += 1
            bucket[0] += 1
        else:
            failures.append((os.path.relpath(path, SUITE), detail))
    return passed, positive, negative, failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", default=os.path.join(ROOT, "build/ws074-host/plain/host-parse"))
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--filter", default="")
    parser.add_argument("--show", type=int, default=20)
    parser.add_argument("--record")
    parser.add_argument("--manifest-only")
    parser.add_argument("--results")
    parser.add_argument("--failures", default=os.path.join(ROOT, "build/ws074-test262-parse-failures.txt"))
    args = parser.parse_args()

    tests = collect(args)
    if args.manifest_only:
        with open(args.manifest_only, "w") as manifest:
            for path, modes, _ in tests:
                for mode in modes:
                    manifest.write("%s\t%s\n" % (path, mode))
        print("manifest: %d tests" % len(tests))
        return 0
    if args.results:
        with open(args.results) as stream:
            results = stream.read()
    else:
        with tempfile.NamedTemporaryFile("w", suffix=".manifest", delete=False) as manifest:
            for path, modes, _ in tests:
                for mode in modes:
                    manifest.write("%s\t%s\n" % (path, mode))
            name = manifest.name
        run = subprocess.run([args.driver, name], stdout=subprocess.PIPE, text=True, errors="replace")
        os.remove(name)
        results = run.stdout
        if run.returncode != 0:
            print("driver exited with %d" % run.returncode)

    passed, positive, negative, failures = count(tests, results, args)
    total = len(tests)
    with open(args.failures, "w") as stream:
        for name, detail in failures:
            stream.write("%s\t%s\n" % (name, detail))
    for name, detail in failures[:args.show]:
        print("FAIL %s: %s" % (name, detail))
    summary = "test262-parse %d/%d (%.1f%%) positive %d/%d negative %d/%d excluded %d" % (
        passed, total, 100.0 * passed / max(total, 1), positive[0], positive[1], negative[0], negative[1], args.excluded)
    print(summary)
    if args.record:
        sha = subprocess.run(["git", "-C", SUITE, "rev-parse", "--short=8", "HEAD"], stdout=subprocess.PIPE,
                             text=True).stdout.strip()
        with open(args.record, "a") as stream:
            stream.write("%s %s suite=test262-%s\n" % (datetime.date.today().isoformat(), summary, sha))
    return 0


if __name__ == "__main__":
    sys.exit(main())
