#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs the html5lib tokenizer tests against zdesktop-browser's tokenizer.

  run-html5lib-tokenizer.py [--driver PATH] [--mode whole|units|both] [--show N] [--record FILE]

The tests are build/ws074-suites/html5lib/tokenizer/*.test (fetch-suites.sh html5lib).  Every
test runs once per initial state it lists; a case passes when the tokens (adjacent character
tokens merged) equal the expected ones.  Parse errors are compared separately (the codes, in
any order).  --mode units feeds the input one code unit at a time, which exercises the tokenizer's
waiting for more input; both runs each case both ways and needs both to pass.  --record appends
one result line to a file (plan/ws074/results/html5lib-tokenizer.txt).
"""

import argparse
import datetime
import glob
import json
import os
import subprocess
import sys

STATES = {
    "Data state": 0,
    "RCDATA state": 1,
    "RAWTEXT state": 2,
    "Script data state": 3,
    "PLAINTEXT state": 4,
    "CDATA section state": 5,
}


def unescape(text):
    """Decodes the \\uXXXX escapes of a doubleEscaped test into a str (lone surrogates kept)."""
    return json.loads('"' + text.replace('"', '\\"') + '"') if "\\u" in text else text


def to_hex(text):
    if text is None or text == "":
        return "-"
    data = text.encode("utf-16-le", "surrogatepass")
    units = [data[i] | (data[i + 1] << 8) for i in range(0, len(data), 2)]
    return "".join("%04x" % unit for unit in units)


def normalize(tokens):
    merged = []
    for token in tokens:
        if token[0] == "Character" and merged and merged[-1][0] == "Character":
            merged[-1] = ["Character", merged[-1][1] + token[1]]
        else:
            token = list(token)
            if token[0] == "StartTag" and len(token) == 4 and token[3] is False:
                token = token[:3]
            merged.append(token)
    return merged


def expected_tokens(test):
    tokens = test["output"]
    if test.get("doubleEscaped"):
        def fix(value):
            if isinstance(value, str):
                return unescape(value)
            if isinstance(value, dict):
                return {unescape(k): unescape(v) for k, v in value.items()}
            if isinstance(value, list):
                return [fix(v) for v in value]
            return value
        tokens = fix(tokens)
    return normalize(tokens)


def run_in_guest(root, driver, cases):
    """Copies the driver and the cases into the guest (browser-guest.sh), runs it there and reads back the results."""
    guest = os.path.join(root, "plan/ws074/tests/browser-guest.sh")
    scratch = os.path.join(root, "build/ws074-guest")
    os.makedirs(scratch, exist_ok=True)
    with open(os.path.join(scratch, "cases.txt"), "w", encoding="ascii") as stream:
        stream.write(cases)
    subprocess.run(["sh", guest, "put", driver, "/tmp/ws074-driver"], check=True, capture_output=True)
    subprocess.run(["sh", guest, "put", os.path.join(scratch, "cases.txt"), "/tmp/ws074-cases.txt"], check=True, capture_output=True)
    run = subprocess.run(["sh", guest, "run", "chmod +x /tmp/ws074-driver && /tmp/ws074-driver < /tmp/ws074-cases.txt > /tmp/ws074-results.txt"],
                         capture_output=True, check=False)
    subprocess.run(["sh", guest, "get", "/tmp/ws074-results.txt", os.path.join(scratch, "results.txt")], check=True, capture_output=True)
    with open(os.path.join(scratch, "results.txt"), encoding="ascii") as stream:
        return stream.read().splitlines(), run.returncode, run.stderr


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", default=os.path.join(root, "build/ws074-host/plain/host-tokenizer"))
    parser.add_argument("--mode", default="both", choices=["whole", "units", "both"])
    parser.add_argument("--show", type=int, default=10)
    parser.add_argument("--record")
    parser.add_argument("--guest", help="run this zedBSD build of the driver in the running browser guest")
    parser.add_argument("--suite", default=os.path.join(root, "build/ws074-suites/html5lib/tokenizer"))
    args = parser.parse_args()

    cases = []
    for path in sorted(glob.glob(os.path.join(args.suite, "*.test"))):
        with open(path, encoding="utf-8") as stream:
            data = json.load(stream)
        for index, test in enumerate(data.get("tests", [])):
            text = test["input"]
            if test.get("doubleEscaped"):
                text = unescape(text)
            last = test.get("lastStartTag")
            for state in test.get("initialStates", ["Data state"]):
                cases.append((os.path.basename(path), index, test, state, text, last))

    modes = ["whole", "units"] if args.mode == "both" else [args.mode]
    lines = []
    for case in cases:
        for mode in modes:
            lines.append("%s\t%d\t%s\t%s\n" % (mode, STATES[case[3]], to_hex(case[5]), to_hex(case[4])))
    if args.guest:
        outputs, status, stderr = run_in_guest(root, args.guest, "".join(lines))
    else:
        result = subprocess.run([args.driver], input="".join(lines).encode("ascii"), capture_output=True, check=False)
        outputs, status, stderr = result.stdout.decode("ascii").splitlines(), result.returncode, result.stderr
    if len(outputs) != len(lines):
        print("driver produced %d results for %d cases (status %d)" % (len(outputs), len(lines), status))
        print(stderr.decode(errors="replace")[-2000:])
        return 1

    passed = 0
    errors_matched = 0
    failures = []
    for number, case in enumerate(cases):
        expected = expected_tokens(case[2])
        good = True
        for offset in range(len(modes)):
            got = json.loads(outputs[number * len(modes) + offset])
            if normalize(got["tokens"]) != expected:
                good = False
                failures.append((case, modes[offset], got, expected))
                break
        if good:
            passed += 1
            got = json.loads(outputs[number * len(modes)])
            want = [error["code"] for error in case[2].get("errors", [])]
            # The order of errors raised at one position differs between readings of the standard.
            if sorted(got["codes"]) == sorted(want) or (got["errors"] > 64 and got["codes"] == want[:64]):
                errors_matched += 1

    total = len(cases)
    print("html5lib tokenizer: %d/%d cases pass (%.1f%%), parse errors match in %d; mode %s"
          % (passed, total, 100.0 * passed / total, errors_matched, args.mode))
    for case, mode, got, expected in failures[: args.show]:
        print("--- %s #%d (%s, %s): %r" % (case[0], case[1], case[3], mode, case[2]["description"]))
        print("    input:    %r" % case[4])
        print("    expected: %s" % json.dumps(expected, ensure_ascii=True))
        print("    got:      %s" % json.dumps(normalize(got["tokens"]), ensure_ascii=True))
    if args.record:
        with open(args.record, "a", encoding="utf-8") as stream:
            stream.write("%s html5lib-tokenizer %d/%d (%.1f%%) errors-match %d mode=%s suite=224991ec\n"
                         % (datetime.date.today().isoformat(), passed, total, 100.0 * passed / total,
                            errors_matched, args.mode))
    return 0


if __name__ == "__main__":
    sys.exit(main())
