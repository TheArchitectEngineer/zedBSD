#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Check page-observed iframe navigation, visited paint and script privacy."""

import argparse
import http.server
import os
import subprocess
import threading


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, _format, *args):
        pass

    def do_GET(self):
        if self.path == "/drop.html":
            self.close_connection = True
            return
        if self.path == "/visit.html":
            body = "<!doctype html><title>visited</title>"
        elif self.path == "/test.html":
            body = '''<!doctype html><style>
                a:link { color: red }
                a:visited { color: green }
                a:any-link { text-decoration: underline }
                </style>
                <a id=v href=/visit.html>VISITED</a>
                <a id=u href=/other.html>UNVISITED</a>
                <a id=x href=http://example.invalid/visit.html>EXTERNAL</a>
                <a id=f href=/drop.html>FAILED</a>
                <iframe src=/visit.html onload="
                  var v=document.getElementById('v');
                  var s=getComputedStyle(v).color;
                  var q=document.querySelector(':visited');
                  document.getElementById('state').textContent=
                    s+' / '+(q===null?'private':'exposed')+' / '+
                    document.querySelectorAll(':any-link').length;
                "></iframe><iframe src=/drop.html></iframe><span id=state>pending</span>'''
        else:
            self.send_error(404)
            return
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
        style = dump(args.program, url, "style")
        dom = dump(args.program, url, "dom")
        checks = [
            ('paint visited green', '#ff008000 "VISITED"' in paint),
            ('paint unvisited red', '#ffff0000 "UNVISITED"' in paint),
            ('paint external red', '#ffff0000 "EXTERNAL"' in paint),
            ('paint failed navigation red', '#ffff0000 "FAILED"' in paint),
            ('style remains unvisited', 'a display=inline' in style and
             style.count('color=#ffff0000') >= 3),
            ('script cannot read visit', 'private / 4' in dom and 'pending' not in dom),
        ]
        for name, passed in checks:
            print('%s: %s' % (name, 'pass' if passed else 'FAIL'))
        if not all(passed for _, passed in checks):
            print(paint[-1200:])
            print(dom[-1200:])
            return 1
        return 0
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == '__main__':
    raise SystemExit(main())
