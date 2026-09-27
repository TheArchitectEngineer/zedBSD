#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs zdesktop-browser's HTTP client against the test server (ws074-p016).

  run-http-tests.py [--program PATH] [--port N]         starts http-server.py and runs the cases on the host
  run-http-tests.py --host HOST --port N --guest        runs the cases in the guest against a server already running
                                                        on the host (the guest reaches the host as 10.0.2.2)

Each case loads a URL with `--dump=dom` and checks that the tree has a text (and, for a failure, that the program
says why).  The cases: a page with Content-Length, a chunked page, a page ended by closing the connection, six
redirects of three kinds, cookies set on a redirect and sent back (only those whose path fits), a script loaded by a
relative src over http, a 404 page, and a refused connection.
"""

import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))

CASES = [
    ("length", "/pages/first.html", "zdesktop-browser: the first page"),
    ("chunked", "/chunked", '"second part"'),
    ("close", "/close", '"ended by closing"'),
    ("redirects", "/redirect/6", "zdesktop-browser: the first page"),
    ("cookies", "/cookie/set", '"session=abc123; theme=dark"'),
    ("script", "/script", '"the script came over http"'),
    ("not-found", "/pages/none.html", '"no such page"'),
]


def run_case(program, base, path, expect, guest):
    url = base + path
    if guest:
        command = ["python3", os.path.join(ROOT, "plan/tools/guest/guest.py"), "run", "/bin/zdesktop-browser --dump=dom '%s'" % url]
    else:
        command = [program, "--dump=dom", url]
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace",
                         timeout=120, env=dict(os.environ, GUEST_RUNTIME=os.path.join(ROOT, "build/ws074-run")))
    return expect in run.stdout + run.stderr, run.stdout + run.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/zdesktop-browser"))
    parser.add_argument("--port", type=int, default=18074)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--guest", action="store_true")
    args = parser.parse_args()
    server = None
    if not args.guest:
        server = subprocess.Popen([sys.executable, os.path.join(ROOT, "plan/ws074/tests/http-server.py"),
                                   "--port", str(args.port), "--bind", "127.0.0.1"],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        server.stdout.readline()
        time.sleep(0.2)
    base = "http://%s:%d" % (args.host, args.port)
    passed = 0
    total = 0
    try:
        for name, path, expect in CASES:
            total += 1
            ok, output = run_case(args.program, base, path, expect, args.guest)
            print("%s %s" % ("pass" if ok else "FAIL", name))
            if ok:
                passed += 1
            else:
                print("  " + "\n  ".join(output.splitlines()[-8:]))
        total += 1
        ok, output = run_case(args.program, "http://%s:1" % args.host, "/", "cannot load", args.guest)
        print("%s refused" % ("pass" if ok else "FAIL"))
        if ok:
            passed += 1
        else:
            print("  " + output[-300:])
    finally:
        if server is not None:
            server.terminate()
    print("http-tests %d/%d" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
