#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Runs browser's fetch and XMLHttpRequest test (plan/ws074/tests/fetch/fetch.html) against a local server.

  run-fetch-tests.py [--program PATH]

A small server on 127.0.0.1 answers /echo (the request's method, path, headers and body as JSON, with the header
X-Reply: yes), /redirect (302 to /echo), /status/N and /slow (a second late).  The page prints "ok NAME" or
"FAIL NAME detail" for each check and "end" when it is done; the run passes when there is no FAIL and "end" came.
"""

import argparse
import json
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
PAGE = os.path.join(ROOT, "plan/ws074/tests/fetch/fetch.html")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def reply(self, status, body, content_type="application/json"):
        data = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Reply", "yes")
        self.end_headers()
        self.wfile.write(data)

    def handle_any(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode("utf-8") if length else ""
        if self.path.startswith("/page"):
            with open(PAGE, encoding="utf-8") as handle:
                self.reply(200, handle.read(), "text/html; charset=utf-8")
        elif self.path.startswith("/echo"):
            headers = {key.lower(): value for key, value in self.headers.items()}
            self.reply(200, json.dumps({"method": self.command, "path": self.path, "headers": headers, "body": body}))
        elif self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/echo")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path.startswith("/status/"):
            self.reply(int(self.path.split("/")[2]), "{}")
        elif self.path == "/slow":
            time.sleep(1)
            self.reply(200, "{}")
        else:
            self.reply(404, "{}")

    do_GET = do_POST = do_PUT = do_DELETE = handle_any


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/browser"))
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = "http://127.0.0.1:%d/page?verbose" % server.server_address[1]
    fonts = os.path.join(ROOT, "userland/desktop/fonts")
    status = 0
    for mode in ([], ["--async"]):
        status |= run(args.program, fonts, url, mode)
    server.shutdown()
    return status


def run(program, fonts, url, mode):
    command = [program, "--render", "--output=" + os.path.join(ROOT, "build/ws074-host-data/fetch-test.ppm"), "--settle-ms=20000",
               "--font=" + os.path.join(fonts, "Mahora-Regular.ttf")] + mode + [url]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    text = result.stdout + result.stderr
    lines = [line for line in text.replace("console: ", "").split("\n") if line.startswith(("ok ", "FAIL ", "end", "Uncaught"))]
    failed = [line for line in lines if not line.startswith(("ok ", "end"))]
    for line in failed:
        print(line)
    if os.environ.get("FETCH_TEST_VERBOSE"):
        print(text, result.returncode)
    if failed or "end" not in lines:
        print("fetch-tests FAILED " + " ".join(mode) + " (%d ok, %d failed, end %s)" % (sum(1 for l in lines if l.startswith("ok ")), len(failed), "end" in lines))
        sys.stderr.write(text[-2000:])
        return 1
    print("fetch-tests %s %d/%d" % (" ".join(mode) or "blocking", sum(1 for l in lines if l.startswith("ok ")), sum(1 for l in lines if l.startswith("ok "))))
    return 0


sys.exit(main())
