#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs zdesktop-browser's own JavaScript tests (plan/ws074/tests/js/*.js) and compares their output.

  run-js-tests.py [--program PATH]            run each test with --js and compare with NAME.expected
  run-js-tests.py --reference                 make NAME.expected with the host's headless Chromium
  run-js-tests.py --outputs DIR               compare outputs made elsewhere (DIR/NAME.out, e.g. the guest's)

A test prints lines with print().  The expected output is what Chromium prints for the same script (a
page defines print to collect the lines; an uncaught error is added as "Uncaught NAME"), so the reference
is another engine, not this one.  A test uses only what zdesktop-browser has so far (no built-ins before
ws074-p026) and numbers whose strings every engine writes alike.

In the guest a few lines differ for known faults outside the browser (GUEST_KNOWN: the test, the line's first
word and the bug); such a line is reported as expected until its bug is fixed, not as a failure.
"""

import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
TESTS = os.path.join(ROOT, "plan/ws074/tests/js")
GUEST_KNOWN = {
    ("operators", "compound"): "BUG-078 (libc pow is exp(y*log(x)): 14 ** 2 is 195.99999999999994)",
}
PAGE = """<!DOCTYPE html>
<meta charset="utf-8">
<pre id="out"></pre>
<script>
var __lines = [];
function print() {
  var parts = [];
  for (var i = 0; i < arguments.length; i++)
    parts.push(String(arguments[i]));
  __lines.push(parts.join(" "));
}
window.onerror = function (message, source, line, column, error) {
  __lines.push("Uncaught " + (error && error.name ? error.name : message));
};
</script>
<script src="file://%s"></script>
<script>
document.getElementById("out").textContent = __lines.join("\\n");
</script>
"""


def tests():
    return sorted(glob.glob(os.path.join(TESTS, "*.js")))


def reference():
    directory = os.path.join(ROOT, "build/ws074-chrome/js")
    profile = os.path.join(ROOT, "build/ws074-chrome/profile")
    os.makedirs(directory, exist_ok=True)
    for path in tests():
        name = os.path.splitext(os.path.basename(path))[0]
        page = os.path.join(directory, name + ".html")
        with open(page, "w") as stream:
            stream.write(PAGE % path)
        run = subprocess.run(["chromium", "--headless", "--no-sandbox", "--disable-gpu", "--allow-file-access-from-files",
                              "--user-data-dir=" + profile, "--dump-dom", "file://" + page],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=120)
        dom = run.stdout
        start = dom.find('<pre id="out">')
        end = dom.find("</pre>", start)
        if start < 0 or end < 0:
            print("%s: no output from Chromium" % name)
            return 1
        text = dom[start + len('<pre id="out">'):end]
        text = text.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"').replace("&amp;", "&")
        with open(os.path.join(TESTS, name + ".expected"), "w") as stream:
            stream.write(text + "\n")
        print("%s: %d lines" % (name, text.count("\n") + 1))
    return 0


def compare(name, output, known):
    with open(os.path.join(TESTS, name + ".expected")) as stream:
        expected = stream.read()
    if output == expected:
        print("pass %s" % name)
        return True
    expected_lines = expected.splitlines()
    output_lines = output.splitlines()
    failed = []
    excused = []
    for index in range(max(len(expected_lines), len(output_lines))):
        want = expected_lines[index] if index < len(expected_lines) else "(nothing)"
        got = output_lines[index] if index < len(output_lines) else "(nothing)"
        if want == got:
            continue
        bug = known.get((name, want.split(" ")[0]))
        if bug is not None and len(expected_lines) == len(output_lines):
            excused.append("  line %d: expected until %s: %s" % (index + 1, bug, got))
            continue
        failed.append("  line %d: expected %s\n  line %d: got      %s" % (index + 1, want, index + 1, got))
    print("%s %s" % ("FAIL" if failed else "pass", name))
    for line in excused + failed:
        print(line)
    return not failed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/zdesktop-browser"))
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--outputs")
    args = parser.parse_args()
    if args.reference:
        return reference()
    passed = 0
    for path in tests():
        name = os.path.splitext(os.path.basename(path))[0]
        if args.outputs:
            with open(os.path.join(args.outputs, name + ".out"), errors="replace") as stream:
                output = stream.read()
        else:
            run = subprocess.run([args.program, "--js", path], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, errors="replace", timeout=120)
            output = run.stdout
            for line in run.stderr.splitlines():
                if line.startswith("Uncaught "):
                    output += "Uncaught " + line[len("Uncaught "):].split(":")[0] + "\n"
                else:
                    output += line + "\n"
        if compare(name, output, GUEST_KNOWN if args.outputs else {}):
            passed += 1
    print("js-tests %d/%d" % (passed, len(tests())))
    return 0 if passed == len(tests()) else 1


if __name__ == "__main__":
    sys.exit(main())
