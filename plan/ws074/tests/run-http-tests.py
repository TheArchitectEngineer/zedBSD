#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs zdesktop-browser's HTTP and HTTPS client against the test server (ws074-p016, p017).

  run-http-tests.py [--program PATH] [--port N]         makes the test CA (make-test-ca.sh), starts http-server.py
                                                        and runs the cases on the host
  run-http-tests.py --host HOST --port N --guest        runs the cases in the guest against a server already running
                                                        on the host (the guest reaches the host as 10.0.2.2), with
                                                        --tls-dir and the default TLS ports; the CA goes into the
                                                        guest as /tmp/ws074-ca.pem

Each case loads a URL with `--dump=dom` and checks that the tree has a text (and, for a failure, that the program
says why).  The cases: a page with Content-Length, a chunked page, a page ended by closing the connection, six
redirects of three kinds, cookies set on a redirect and sent back (only those whose path fits), a script loaded by a
relative src over http, a 404 page, and a refused connection.  Over HTTPS (with --ca-file): a page with Content-Length,
a chunked page, an http page redirected to https, a Secure cookie not sent back over http; and the failures: the
test CA not trusted (no --ca-file) and a certificate for another name.
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

# (name, scheme, port kind, path, trust the test CA, expected text)
TLS_CASES = [
    ("https-length", "https", "tls", "/pages/first.html", True, "zdesktop-browser: the first page"),
    ("https-chunked", "https", "tls", "/chunked", True, '"second part"'),
    ("to-https", "http", "http", "/to-https", True, "zdesktop-browser: the first page"),
    ("secure-cookie", "https", "tls", "/cookie/secure-set", True, '"p=2"'),
    ("untrusted", "https", "tls", "/pages/first.html", False, "certificate verify failed"),
    ("wrong-name", "https", "wrong", "/pages/first.html", True, "mismatch"),
]
TLS_DIR = os.path.join(ROOT, "build/ws074-tls")
GUEST_CA = "/tmp/ws074-ca.pem"


def run_case(program, base, path, expect, guest, ca=None):
    url = base + path
    options = []
    if ca is not None:
        options.append("--ca-file=" + ca)
    if guest:
        command = ["python3", os.path.join(ROOT, "plan/tools/guest/guest.py"), "run",
                   "/bin/zdesktop-browser --dump=dom %s '%s'" % (" ".join(options), url)]
    else:
        command = [program, "--dump=dom"] + options + [url]
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace",
                         timeout=120, env=dict(os.environ, GUEST_RUNTIME=os.path.join(ROOT, "build/ws074-run")))
    return expect in run.stdout + run.stderr, run.stdout + run.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/zdesktop-browser"))
    parser.add_argument("--port", type=int, default=18074)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--guest", action="store_true")
    parser.add_argument("--tls-port", type=int, default=18443)
    parser.add_argument("--tls-wrong-port", type=int, default=18444)
    args = parser.parse_args()
    server = None
    ca = os.path.join(TLS_DIR, "ca.pem")
    if not args.guest:
        subprocess.run(["sh", os.path.join(ROOT, "plan/ws074/tests/make-test-ca.sh"), TLS_DIR], check=True,
                       stdout=subprocess.DEVNULL)
        server = subprocess.Popen([sys.executable, os.path.join(ROOT, "plan/ws074/tests/http-server.py"),
                                   "--port", str(args.port), "--bind", "127.0.0.1", "--tls-dir", TLS_DIR,
                                   "--tls-port", str(args.tls_port), "--tls-wrong-port", str(args.tls_wrong_port)],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        server.stdout.readline()
        time.sleep(0.2)
    else:
        subprocess.run(["python3", os.path.join(ROOT, "plan/tools/guest/guest.py"), "put", ca, GUEST_CA], check=True,
                       stdout=subprocess.DEVNULL, env=dict(os.environ, GUEST_RUNTIME=os.path.join(ROOT, "build/ws074-run")))
        ca = GUEST_CA
    ports = {"http": args.port, "tls": args.tls_port, "wrong": args.tls_wrong_port}
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
        for name, scheme, kind, path, trusted, expect in TLS_CASES:
            total += 1
            tls_base = "%s://%s:%d" % (scheme, args.host, ports[kind])
            ok, output = run_case(args.program, tls_base, path, expect, args.guest, ca if trusted else None)
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
