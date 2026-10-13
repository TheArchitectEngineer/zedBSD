#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Verify top-level XHTML MIME dispatch in file and asynchronous HTTP loads."""

import argparse
import http.server
from pathlib import Path
import subprocess
import tempfile
import threading


SOURCE = ('<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml">'
          '<body><div id="one"/><div id="two"/></body></html>')


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, _format, *args):
        pass

    def do_GET(self):
        mime = ("application/xhtml+xml; charset=utf-8" if self.path == "/xml"
                else "text/html")
        body = SOURCE.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def dump(program, location):
    result = subprocess.run(
        [program, "--dump=dom", "--async", location],
        capture_output=True, text=True, timeout=10,
    )
    if result.returncode:
        raise RuntimeError(result.stderr[-1000:])
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
        xml = dump(args.program, base + "/xml")
        html = dump(args.program, base + "/html")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.xhtml"
            path.write_text(SOURCE, encoding="utf-8")
            local = dump(args.program, str(path))
        checks = {
            "HTTP XHTML has sibling elements": xml.count("|     <div>\n") == 2,
            "HTTP HTML retains HTML nesting": "|       <div>\n" in html,
            "local XHTML has sibling elements": local.count("|     <div>\n") == 2,
            "XHTML does not treat declaration as comment": "<!-- ?xml" not in xml,
        }
        for name, passed in checks.items():
            if not passed:
                print("FAIL " + name)
        if not all(checks.values()):
            return 1
        print("top-level XHTML MIME: %d/%d passed" % (len(checks), len(checks)))
        return 0
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
