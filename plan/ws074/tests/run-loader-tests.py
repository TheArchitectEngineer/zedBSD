#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs browser's asynchronous loader against the test server: kept connections and the memory cache (ws074-p058).

  run-loader-tests.py [--program PATH] [--port N]      makes the test CA (make-test-ca.sh), starts http-server.py
                                                        (idle connections closed after 1 second) and runs the cases
                                                        with host-loader (build/ws074-host/<variant>/host-loader)

Each case runs host-loader on some URLs, checks every URL's outcome (status and body), then reads the server's /stats
(on a connection of its own) and checks how many connections and requests the loader used:

  reuse          three pages one after another over one connection
  limit          twelve slow images at once over at most six connections to the place
  chunked        a chunked page, then another page, over one connection
  close          a page ended by closing the connection, then another page, over two connections
  redirects      four redirects (/redirect/3 to /redirect/0) and the page over one connection
  stale          two pages 1.5 seconds apart: the server closed the kept connection, the loader opens a new one
  fresh          a page with max-age=60 twice: the second comes from the cache (one request)
  revalidate     a page with an ETag twice: the second is revalidated (a 304) and keeps the body
  no-store       a page with max-age=60 and no-store twice: two requests
  expired        a page with max-age=1 and an ETag, again after 1.5 seconds: revalidated
  https-reuse    two HTTPS pages over one connection
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))

# ws074-p080: localStorage goes under XDG_DATA_HOME; the browsers this tool starts keep theirs under build/,
# away from the user's ~/.local/share.
os.environ["XDG_DATA_HOME"] = os.path.join(ROOT, "build/ws074-host-data")
TLS_DIR = os.path.join(ROOT, "build/ws074-tls")

# (name, mode, pause ms, paths, https, expected body text of every URL, check of the stats)
CASES = [
    ("reuse", "seq", 0, ["/pages/first.html"] * 3, False, "<!DOCTYPE",
     lambda s: s["connections"] == 1 and s["requests"].get("/pages/first.html") == 3),
    ("limit", "par", 0, ["/images/logo.png?delay=400"] * 12, False, "PNG",
     lambda s: 2 <= s["connections"] <= 6 and s["requests"].get("/images/logo.png") == 12),
    ("chunked", "seq", 0, ["/chunked", "/pages/first.html"], False, "",
     lambda s: s["connections"] == 1),
    ("close", "seq", 0, ["/close", "/pages/first.html"], False, "",
     lambda s: s["connections"] == 2),
    ("redirects", "seq", 0, ["/redirect/3"], False, "<!DOCTYPE",
     lambda s: s["connections"] == 1 and sum(s["requests"].values()) == 5),
    ("stale", "seq", 1500, ["/pages/first.html"] * 2, False, "<!DOCTYPE",
     lambda s: s["connections"] == 2 and s["requests"].get("/pages/first.html") == 2),
    ("fresh", "seq", 0, ["/cached/a?max-age=60"] * 2, False, "cached.a",
     lambda s: s["requests"].get("/cached/a") == 1),
    ("revalidate", "seq", 0, ["/cached/b?etag=1"] * 2, False, "cached.b",
     lambda s: s["requests"].get("/cached/b") == 2 and s["not_modified"] == 1),
    ("no-store", "seq", 0, ["/cached/c?max-age=60&no-store"] * 2, False, "cached.c",
     lambda s: s["requests"].get("/cached/c") == 2 and s["not_modified"] == 0),
    ("expired", "seq", 1500, ["/cached/d?max-age=1&etag=1"] * 2, False, "cached.d",
     lambda s: s["requests"].get("/cached/d") == 2 and s["not_modified"] == 1),
    ("https-reuse", "seq", 0, ["/pages/first.html"] * 2, True, "<!DOCTYPE",
     lambda s: s["connections"] == 1 and s["requests"].get("/pages/first.html") == 2),
]


def stats(port, reset=False):
    url = "http://127.0.0.1:%d/stats%s" % (port, "/reset" if reset else "")
    with urllib.request.urlopen(url, timeout=10) as response:
        text = response.read().decode()
    return None if reset else json.loads(text)


def run_case(program, port, tls_port, case):
    name, mode, pause, paths, https, expect, check = case
    base = "https://127.0.0.1:%d" % tls_port if https else "http://127.0.0.1:%d" % port
    stats(port, reset=True)
    command = [program, "--ca", os.path.join(TLS_DIR, "ca.pem"), "--pause", str(pause), mode]
    command += [base + path for path in paths]
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace", timeout=60)
    output = run.stdout + run.stderr
    lines = [line for line in run.stdout.splitlines() if line[:1].isdigit()]
    ok = run.returncode == 0 and len(lines) == len(paths)
    ok = ok and "Sanitizer" not in output and "runtime error" not in output
    for line in lines:
        fields = line.split(" ", 4)
        ok = ok and len(fields) == 5 and fields[1] == "0" and fields[2] == "200" and expect in fields[4]
    counts = stats(port)
    ok = ok and check(counts)
    return ok, output + json.dumps(counts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/host-loader"))
    parser.add_argument("--port", type=int, default=18174)
    parser.add_argument("--tls-port", type=int, default=18543)
    parser.add_argument("--tls-wrong-port", type=int, default=18544)
    args = parser.parse_args()
    subprocess.run([sys.executable, os.path.join(ROOT, "plan/ws074/tests/make-test-images.py")], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["sh", os.path.join(ROOT, "plan/ws074/tests/make-test-ca.sh"), TLS_DIR], check=True,
                   stdout=subprocess.DEVNULL)
    server = subprocess.Popen([sys.executable, os.path.join(ROOT, "plan/ws074/tests/http-server.py"),
                               "--port", str(args.port), "--bind", "127.0.0.1", "--tls-dir", TLS_DIR,
                               "--tls-port", str(args.tls_port), "--tls-wrong-port", str(args.tls_wrong_port),
                               "--idle", "1"],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    server.stdout.readline()
    time.sleep(0.2)
    passed = 0
    try:
        for case in CASES:
            ok, output = run_case(args.program, args.port, args.tls_port, case)
            print("%s %s" % ("pass" if ok else "FAIL", case[0]))
            if ok:
                passed += 1
            else:
                print("  " + "\n  ".join(output.splitlines()[-16:]))
    finally:
        server.terminate()
    print("loader-tests %d/%d" % (passed, len(CASES)))
    return 0 if passed == len(CASES) else 1


if __name__ == "__main__":
    sys.exit(main())
