#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs WPT's URL parsing tests and data: URL tests against zdesktop-browser's URL parser.

  run-url-tests.py [--driver PATH] [--show N]
  run-url-tests.py --commands FILE           write the driver's input (for the guest)
  run-url-tests.py --outputs FILE            compare the driver's output made elsewhere (the guest's)

The tests are build/ws074-suites/wpt/url/resources/urltestdata.json (each input against its base: the
URL's parts, or failure) and wpt/fetch/data-urls/resources/data-urls.json (each data: URL: its MIME
type and body, or failure), fetched by fetch-suites.sh.  The driver is plan/ws074/tests/host-url.c
(host-build.sh builds it).  Prints the counts and the first failures.
"""

import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
WPT = os.path.join(ROOT, "build/ws074-suites/wpt")
PARTS = ["href", "origin", "protocol", "username", "password", "host", "hostname", "port", "pathname", "search", "hash"]


def hexed(text):
    data = text.encode("utf-8", "surrogatepass") if isinstance(text, str) else bytes(text)
    return data.hex() if data else "-"


def unhexed(field):
    return b"" if field == "-" else bytes.fromhex(field)


def cases():
    url_tests = [t for t in json.load(open(os.path.join(WPT, "url/resources/urltestdata.json"), encoding="utf-8"))
                 if isinstance(t, dict)]
    data_tests = json.load(open(os.path.join(WPT, "fetch/data-urls/resources/data-urls.json"), encoding="utf-8"))
    commands = []
    for test in url_tests:
        base = test.get("base")
        commands.append(("url", test, "url\t%s\t%s" % (hexed(test["input"]), hexed(base) if base is not None else "-")))
    for test in data_tests:
        if not isinstance(test, list):
            continue
        commands.append(("data", test, "data\t%s" % hexed(test[0])))
    return commands


def check(kind, test, line):
    fields = line.rstrip("\n").split("\t")
    if kind == "url":
        if test.get("failure"):
            return fields[0] == "failure", "expected failure"
        if fields[0] != "ok":
            return False, "failed to parse"
        for name, field in zip(PARTS, fields[1:]):
            if name == "origin" and "origin" not in test:
                continue
            got = unhexed(field).decode("utf-8", "replace")
            if got != test[name]:
                return False, "%s: expected %r, got %r" % (name, test[name], got)
        return True, ""
    if test[1] is None:
        return fields[0] == "failure", "expected failure"
    if fields[0] != "ok":
        return False, "failed"
    mime = unhexed(fields[1]).decode("latin-1")
    body = list(unhexed(fields[2]))
    if mime != test[1]:
        return False, "mime: expected %r, got %r" % (test[1], mime)
    if body != test[2]:
        return False, "body: expected %r, got %r" % (test[2], body)
    return True, ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", default=os.path.join(ROOT, "build/ws074-host/plain/host-url"))
    parser.add_argument("--show", type=int, default=20)
    parser.add_argument("--commands")
    parser.add_argument("--outputs")
    args = parser.parse_args()
    commands = cases()
    text = "".join(command + "\n" for _, _, command in commands)
    if args.commands:
        with open(args.commands, "w") as stream:
            stream.write(text)
        return 0
    if args.outputs:
        with open(args.outputs) as stream:
            output = stream.read()
    else:
        output = subprocess.run([args.driver], input=text, stdout=subprocess.PIPE, text=True, timeout=300).stdout
    lines = output.splitlines()
    passed = {"url": 0, "data": 0}
    total = {"url": 0, "data": 0}
    shown = 0
    for index, (kind, test, _) in enumerate(commands):
        total[kind] += 1
        line = lines[index] if index < len(lines) else "(none)"
        ok, why = check(kind, test, line)
        if ok:
            passed[kind] += 1
        elif shown < args.show:
            shown += 1
            label = test["input"] if kind == "url" else test[0]
            print("FAIL %s %r (base %r): %s" % (kind, label, test.get("base") if kind == "url" else None, why))
    for kind in ("url", "data"):
        print("%s %d/%d (%.1f%%)" % (kind, passed[kind], total[kind], 100.0 * passed[kind] / max(total[kind], 1)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
