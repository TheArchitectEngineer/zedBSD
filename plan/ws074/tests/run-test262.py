#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs test262 against zdesktop-browser's JavaScript engine.

  run-test262.py [--driver PATH] [--limit N] [--filter TEXT] [--show N] [--jobs N] [--record FILE]
                 [--manifest-only OUT] [--results RESULTS]

The tests are those of run-test262-parse.py (build/ws074-suites/test262/test/**/*.js without intl402/,
staging/, the fixtures and the proposals zdesktop-browser does not plan).  Each runs in the modes its
flags ask for (both sloppy and strict unless onlyStrict, noStrict or raw) after the harness (assert.js,
sta.js, doneprintHandle.js for async tests, then its includes; nothing for raw tests), through the batch
driver plan/ws074/tests/host-js.c (built by host-build.sh).  A test passes when every mode behaves as
expected: a parse-phase negative test fails to parse; a runtime negative test throws the named error (its
string starts with the error's name: the engine's own errors are strings until the Error objects arrive in
ws074-p026); an async test prints Test262:AsyncTestComplete; any other test runs to its end.  Module
tests fail (modules come later).

The manifest is run in chunks in parallel; a chunk that crashes or takes too long is run again one test at
a time, and a test that still takes too long counts as a timeout.  Besides the total, the count of the ES5
tests (an es5id and no features) is reported (plan/ws074/design.md §15's "ES5 range").
"""

import argparse
import concurrent.futures
import datetime
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
SUITE = os.path.join(ROOT, "build/ws074-suites/test262")
HARNESS = os.path.join(SUITE, "harness")
FRONTMATTER = re.compile(r"/\*---(.*?)---\*/", re.S)
EXCLUDED_FEATURES = {"decorators", "import-defer", "source-phase-imports", "source-phase-imports-module-source",
                     "explicit-resource-management"}
CHUNK = 400
CHUNK_TIMEOUT = 120
TEST_TIMEOUT = 10


class Test:
    def __init__(self, path, modes, harness, negative_phase, negative_type, is_async, es5):
        self.path = path
        self.modes = modes
        self.harness = harness
        self.negative_phase = negative_phase
        self.negative_type = negative_type
        self.is_async = is_async
        self.es5 = es5


def yaml_list(meta, key):
    found = re.search(r"^%s:\s*\[(.*?)\]" % key, meta, re.M | re.S)
    if found:
        return [item.strip() for item in found.group(1).split(",") if item.strip()]
    found = re.search(r"^%s:\s*\n((?:[ \t]+-.*\n?)+)" % key, meta, re.M)
    if found:
        return [line.strip()[1:].strip() for line in found.group(1).splitlines() if line.strip().startswith("-")]
    return []


def read_test(path):
    with open(path, encoding="utf-8", errors="replace") as stream:
        text = stream.read()
    match = FRONTMATTER.search(text)
    meta = match.group(1) if match else ""
    flags = yaml_list(meta, "flags")
    features = yaml_list(meta, "features")
    includes = yaml_list(meta, "includes")
    negative_phase = None
    negative_type = None
    found = re.search(r"^negative:\s*\n((?:[ \t]+.*\n?)+)", meta, re.M)
    if found:
        phase = re.search(r"phase:\s*(\w+)", found.group(1))
        kind = re.search(r"type:\s*(\w+)", found.group(1))
        negative_phase = phase.group(1) if phase else None
        negative_type = kind.group(1) if kind else None
    es5 = bool(re.search(r"^es5id:", meta, re.M)) and not features
    return flags, features, includes, negative_phase, negative_type, es5


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
            flags, features, includes, negative_phase, negative_type, es5 = read_test(path)
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
            is_async = "async" in flags
            harness = []
            if "raw" not in flags:
                harness = ["assert.js", "sta.js"]
                if is_async:
                    harness.append("doneprintHandle.js")
                harness.extend(include for include in includes if include not in harness)
            tests.append(Test(path, modes, harness, negative_phase, negative_type, is_async, es5))
            if args.limit and len(tests) >= args.limit:
                return tests
    return tests


def manifest_lines(tests):
    lines = []
    for test in tests:
        for mode in test.modes:
            if mode == "module":
                continue
            lines.append("%s\t%s\t%s\n" % (test.path, mode, ",".join(test.harness)))
    return lines


def run_driver(driver, lines, timeout):
    with tempfile.NamedTemporaryFile("w", suffix=".manifest", delete=False) as manifest:
        manifest.writelines(lines)
        name = manifest.name
    try:
        run = subprocess.run([driver, HARNESS, name], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             text=True, errors="replace", timeout=timeout)
        output = run.stdout
        complete = run.returncode == 0
    except subprocess.TimeoutExpired as expired:
        output = expired.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        complete = False
    os.remove(name)
    return output, complete


def run_chunk(driver, lines):
    output, complete = run_driver(driver, lines, CHUNK_TIMEOUT)
    results = parse_results(output)
    if complete:
        return results, 0
    for line in lines:
        path, mode, _ = line.rstrip("\n").split("\t")
        if (path, mode) in results:
            continue
        single, done = run_driver(driver, [line], TEST_TIMEOUT)
        parsed = parse_results(single)
        if (path, mode) in parsed:
            results[(path, mode)] = parsed[(path, mode)]
        elif done:
            results[(path, mode)] = "error no result"
        else:
            results[(path, mode)] = "timeout-or-crash"
    return results, 1


def parse_results(text):
    results = {}
    for line in text.splitlines():
        parts = line.split("\t", 2)
        if len(parts) == 3:
            results[(parts[0], parts[1])] = parts[2]
    return results


def verdict(test, mode, result):
    if mode == "module":
        return "modules are not supported yet"
    if result is None:
        return "missing"
    if test.negative_phase == "parse":
        if result.startswith("syntax"):
            return None
        return "a SyntaxError was expected: " + result
    if test.negative_phase in ("resolution", "early"):
        if result.startswith("syntax"):
            return None
        return "an early error was expected: " + result
    if test.negative_phase == "runtime":
        if result.startswith("throw " + (test.negative_type or "")):
            return None
        return "a %s was expected: %s" % (test.negative_type, result)
    if test.is_async:
        if result.startswith("ok") and "async-complete" in result and "async-failure" not in result:
            return None
        return result
    if result.startswith("ok") and "async-failure" not in result:
        return None
    return result


def classify(detail):
    if detail.startswith("unsupported "):
        return detail
    if detail.startswith("throw Test262Error"):
        return "throw Test262Error"
    if detail.startswith("throw "):
        return "throw " + detail[6:].split(":")[0][:40]
    if detail.startswith("syntax"):
        return "syntax error"
    if detail.startswith("a SyntaxError was expected"):
        return "SyntaxError expected"
    if detail.startswith("a ") and " was expected" in detail:
        return detail.split(" was expected")[0] + " expected"
    return detail[:40]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", default=os.path.join(ROOT, "build/ws074-host/plain/host-js"))
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--filter", default="")
    parser.add_argument("--show", type=int, default=20)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--record")
    parser.add_argument("--manifest-only")
    parser.add_argument("--results")
    parser.add_argument("--failures", default=os.path.join(ROOT, "build/ws074-test262-failures.txt"))
    args = parser.parse_args()

    tests = collect(args)
    lines = manifest_lines(tests)
    if args.manifest_only:
        with open(args.manifest_only, "w") as manifest:
            manifest.writelines(lines)
        print("manifest: %d tests, %d runs" % (len(tests), len(lines)))
        return 0
    if args.results:
        with open(args.results) as stream:
            results = parse_results(stream.read())
    else:
        results = {}
        incomplete = 0
        chunks = [lines[index:index + CHUNK] for index in range(0, len(lines), CHUNK)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for chunk_results, failed in pool.map(lambda chunk: run_chunk(args.driver, chunk), chunks):
                results.update(chunk_results)
                incomplete += failed
        if incomplete:
            print("%d chunks did not finish cleanly (a crash, a sanitizer report or a timeout); their tests ran again one by one"
                  % incomplete)

    passed = 0
    es5 = [0, 0]
    failures = []
    categories = {}
    for test in tests:
        detail = None
        for mode in test.modes:
            detail = verdict(test, mode, results.get((test.path, mode)))
            if detail is not None:
                detail = mode + ": " + detail
                break
        if test.es5:
            es5[1] += 1
        if detail is None:
            passed += 1
            if test.es5:
                es5[0] += 1
            continue
        failures.append((os.path.relpath(test.path, SUITE), detail))
        category = classify(detail.split(": ", 1)[1])
        categories[category] = categories.get(category, 0) + 1

    with open(args.failures, "w") as stream:
        for name, detail in failures:
            stream.write("%s\t%s\n" % (name, detail))
    for name, detail in failures[:args.show]:
        print("FAIL %s: %s" % (name, detail))
    for category, number in sorted(categories.items(), key=lambda item: -item[1])[:args.show]:
        print("%6d  %s" % (number, category))
    total = len(tests)
    summary = "test262 %d/%d (%.1f%%) es5 %d/%d (%.1f%%) excluded %d" % (
        passed, total, 100.0 * passed / max(total, 1), es5[0], es5[1], 100.0 * es5[0] / max(es5[1], 1),
        args.excluded)
    print(summary)
    if args.record:
        sha = subprocess.run(["git", "-C", SUITE, "rev-parse", "--short=8", "HEAD"], stdout=subprocess.PIPE,
                             text=True).stdout.strip()
        with open(args.record, "a") as stream:
            stream.write("%s %s suite=test262-%s\n" % (datetime.date.today().isoformat(), summary, sha))
    return 0


if __name__ == "__main__":
    sys.exit(main())
