#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Exercise HTTP stylesheet MIME responses against the real host browser."""

import argparse
import http.server
import os
import re
import subprocess
import threading


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, _format, *args):
        pass

    def do_GET(self):
        strict = ("<!doctype html><html><head>"
                  "<link rel=stylesheet href=/wrong.css>"
                  "<link rel=stylesheet href=/good.css>"
                  "<link rel=stylesheet href=/upper.css>"
                  "<link rel=stylesheet href=/missing.css>"
                  "</head><body><h1 id=wrong>WRONG</h1><h1 id=good>GOOD</h1>"
                  "<h1 id=upper>UPPER</h1><h1 id=missing>MISSING</h1></body></html>")
        quirks = ("<html><head><link rel=stylesheet href=/wrong.css></head>"
                  "<body><h1 id=wrong>QUIRKS</h1></body></html>")
        routes = {
            "/strict.html": ("text/html", strict),
            "/quirks.html": ("text/html", quirks),
            "/wrong.css": ("text/html", "#wrong { color: red; }"),
            "/good.css": ("text/css; charset=utf-8", "#good { color: green; }"),
            "/upper.css": ("TEXT/CSS; Charset=utf-8", "#upper { color: blue; }"),
            "/missing.css": (None, "#missing { color: purple; }"),
        }
        mime, content = routes.get(self.path, ("text/plain", "missing"))
        body = content.encode("utf-8")
        self.send_response(200)
        if mime is not None:
            self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)


def dump(program, url, kind):
    fonts = "userland/desktop/fonts/"
    command = [program, "--dump=" + kind, "--async", "--width=800", "--height=600",
               "--settle-ms=1000", "--font=" + fonts + "Mahora-Regular.ttf",
               "--mono-font=" + fonts + "JetBrainsMono-Regular.ttf",
               "--fallback-font=" + fonts + "DroidSansFallbackFull.ttf", url]
    environment = dict(os.environ, XDG_DATA_HOME="build/ws074-host-data")
    result = subprocess.run(command, env=environment, capture_output=True, text=True,
                            timeout=30)
    if result.returncode != 0:
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
        base = "http://127.0.0.1:%d" % server.server_port
        strict_style = dump(args.program, base + "/strict.html", "style")
        strict_dom = dump(args.program, base + "/strict.html", "dom")
        quirks_style = dump(args.program, base + "/quirks.html", "style")
        strict_colors = re.findall(r"^\|\s+h1 .*?color=(#[0-9a-f]+)", strict_style,
                                   re.MULTILINE)
        quirks_colors = re.findall(r"^\|\s+h1 .*?color=(#[0-9a-f]+)", quirks_style,
                                   re.MULTILINE)
        expected = ["#ff000000", "#ff008000", "#ff0000ff", "#ff800080"]
        passed = strict_colors == expected and quirks_colors == ["#ffff0000"]
        passed = passed and all(name in strict_dom for name in ("wrong.css", "good.css",
                                                              "upper.css", "missing.css"))
        if not passed:
            print("FAIL HTTP stylesheet MIME: strict=%r quirks=%r" %
                  (strict_colors, quirks_colors))
            return 1
        print("HTTP stylesheet MIME: pass")
        return 0
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
