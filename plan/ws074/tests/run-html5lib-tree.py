#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs the html5lib tree-construction tests (now kept in WPT) against browser's parser.

  run-html5lib-tree.py [--driver PATH] [--guest PATH] [--show N] [--record FILE] [--file NAME]

The tests are build/ws074-suites/wpt/html/syntax/parsing/resources/*.dat (fetch-suites.sh wpt).
A case passes when the document tree equals the expected one.  Fragment cases (#document-fragment)
are counted apart until the parser parses fragments; cases marked #script-on run with scripting
enabled (the tree is compared; scripts do not run).
"""

import argparse
import datetime
import glob
import os
import subprocess
import sys


def read_cases(path):
    cases = []
    with open(path, encoding="utf-8", newline="\n") as stream:
        text = stream.read()
    blocks = ("\n" + text).split("\n#data\n")[1:]
    for index, block in enumerate(blocks):
        sections = {}
        current = "data"
        lines = []
        for line in block.split("\n"):
            if line.startswith("#") and line[1:] in ("errors", "new-errors", "document", "document-fragment",
                                                       "script-off", "script-on"):
                sections[current] = lines
                current = line[1:]
                lines = []
                continue
            lines.append(line)
        sections[current] = lines
        data = "\n".join(sections.get("data", []))
        document = sections.get("document", [])
        while document and document[-1] == "":
            document.pop()
        cases.append({
            "file": os.path.basename(path),
            "index": index,
            "data": data,
            "fragment": "\n".join(sections["document-fragment"]).strip() if "document-fragment" in sections else None,
            "scripting": 1 if "script-on" in sections else 0,
            "expected": "\n".join(document),
        })
    return cases


def to_hex(text):
    if text == "":
        return "-"
    data = text.encode("utf-16-le", "surrogatepass")
    return "".join("%04x" % (data[i] | (data[i + 1] << 8)) for i in range(0, len(data), 2))


def case_line(case):
    if case["fragment"] is None:
        return "%d\t%s\n" % (case["scripting"], to_hex(case["data"]))
    words = case["fragment"].split()
    context = "html:" + words[0] if len(words) == 1 else words[0] + ":" + words[1]
    return "%d\t%s\t%s\n" % (case["scripting"], context, to_hex(case["data"]))


def run_in_guest(root, driver, cases):
    guest = os.path.join(root, "plan/ws074/tests/browser-guest.sh")
    scratch = os.path.join(root, "build/ws074-guest")
    os.makedirs(scratch, exist_ok=True)
    with open(os.path.join(scratch, "tree-cases.txt"), "w", encoding="ascii") as stream:
        stream.write(cases)
    subprocess.run(["sh", guest, "put", driver, "/tmp/ws074-tree"], check=True, capture_output=True)
    subprocess.run(["sh", guest, "put", os.path.join(scratch, "tree-cases.txt"), "/tmp/ws074-tree-cases.txt"],
                   check=True, capture_output=True)
    subprocess.run(["sh", guest, "run", "chmod +x /tmp/ws074-tree && /tmp/ws074-tree < /tmp/ws074-tree-cases.txt > /tmp/ws074-tree-results.txt"],
                   capture_output=True, check=False)
    subprocess.run(["sh", guest, "get", "/tmp/ws074-tree-results.txt", os.path.join(scratch, "tree-results.txt")],
                   check=True, capture_output=True)
    with open(os.path.join(scratch, "tree-results.txt"), "rb") as stream:
        return stream.read().decode("utf-8", errors="replace")


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", default=os.path.join(root, "build/ws074-host/plain/host-tree"))
    parser.add_argument("--guest")
    parser.add_argument("--show", type=int, default=5)
    parser.add_argument("--record")
    parser.add_argument("--file")
    parser.add_argument("--suite", default=os.path.join(root, "build/ws074-suites/wpt/html/syntax/parsing/resources"))
    args = parser.parse_args()

    cases = []
    for path in sorted(glob.glob(os.path.join(args.suite, "*.dat"))):
        if args.file and os.path.basename(path) != args.file:
            continue
        cases.extend(read_cases(path))
    # ws074-p081: the fragment cases run too, with their context (NS:NAME) as a field before the input.
    documents = cases
    fragments = sum(1 for case in cases if case["fragment"] is not None)
    batch = "".join(case_line(case) for case in documents)
    if args.guest:
        output = run_in_guest(root, args.guest, batch)
    else:
        output = subprocess.run([args.driver], input=batch.encode("ascii"), capture_output=True,
                                check=False).stdout.decode("utf-8", errors="replace")
    results = output.split("#end\n")
    passed = 0
    failures = []
    for number, case in enumerate(documents):
        got = results[number].rstrip("\n") if number < len(results) else "<no result>"
        if got == case["expected"]:
            passed += 1
        else:
            failures.append((case, got))
    total = len(documents)
    fragment_passed = sum(1 for number, case in enumerate(documents)
                          if case["fragment"] is not None and number < len(results)
                          and results[number].rstrip("\n") == case["expected"])
    print("tree construction: %d/%d cases pass (%.1f%%); fragments %d/%d"
          % (passed, total, 100.0 * passed / max(total, 1), fragment_passed, fragments))
    for case, got in failures[: args.show]:
        print("--- %s #%d (scripting %d): %r" % (case["file"], case["index"], case["scripting"], case["data"][:200]))
        print("expected:\n" + case["expected"])
        print("got:\n" + got)
    if args.record:
        with open(args.record, "a", encoding="utf-8") as stream:
            stream.write("%s tree-construction %d/%d (%.1f%%) fragments %d/%d suite=wpt-2d66b9b7\n"
                         % (datetime.date.today().isoformat(), passed, total, 100.0 * passed / max(total, 1),
                            fragment_passed, fragments))
    return 0


if __name__ == "__main__":
    sys.exit(main())
