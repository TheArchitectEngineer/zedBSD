#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Check loaded and failed HTML objects against real HTTP response handling."""

import argparse
import http.server
import os
import subprocess
import threading


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, _format, *args):
        pass

    def do_GET(self):
        if self.path == "/missing.html":
            self.send_error(404)
            return
        body = ("<!doctype html><html><body>"
                "<object data=/doc.html><span>LOADED-FALLBACK</span></object>"
                "<object data=/missing.html><span>FAILED-FALLBACK</span></object>"
                "</body></html>" if self.path == "/test.html" else
                "<!doctype html><html><body>CHILD-DOCUMENT</body></html>")
        data = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)


def dump(program, url, mode):
    fonts = "userland/desktop/fonts/"
    args = [program, "--dump=" + mode, "--async", "--width=800", "--height=600",
            "--settle-ms=1000", "--font=" + fonts + "Mahora-Regular.ttf",
            "--mono-font=" + fonts + "JetBrainsMono-Regular.ttf",
            "--fallback-font=" + fonts + "DroidSansFallbackFull.ttf", url]
    result = subprocess.run(args, capture_output=True, text=True, timeout=30,
                            env=dict(os.environ, XDG_DATA_HOME="build/ws074-host-data"))
    if result.returncode:
        raise RuntimeError(result.stderr[-1200:])
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    args = parser.parse_args()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = "http://127.0.0.1:%d/test.html" % server.server_port
        paint = dump(args.program, url, "paint")
        dom = dump(args.program, url, "dom")
        passed = '"LOADED-FALLBACK"' not in paint and '"FAILED-FALLBACK"' in paint
        passed = passed and "LOADED-FALLBACK" in dom and "FAILED-FALLBACK" in dom
        if not passed:
            print("FAIL object fallback: loaded=%s failed=%s" %
                  ('"LOADED-FALLBACK"' in paint, '"FAILED-FALLBACK"' in paint))
            return 1
        print("object fallback: pass")
        return 0
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
