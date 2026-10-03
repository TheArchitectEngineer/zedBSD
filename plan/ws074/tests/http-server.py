#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""The HTTP test server of browser (ws074-p016, p017): serves the test pages and the cases the HTTP client meets.

  http-server.py [--port N] [--bind ADDRESS] [--tls-dir DIR --tls-port N --tls-wrong-port N]
      (default 8074 on 0.0.0.0; the guest reaches the host as 10.0.2.2)

With --tls-dir (made by make-test-ca.sh) the same paths are also served over HTTPS: on --tls-port with DIR/good.pem
(localhost, 127.0.0.1, 10.0.2.2) and on --tls-wrong-port with DIR/wrong.pem (a name that matches nothing).

  /pages/NAME            a page of plan/ws074/tests/pages, with Content-Length
  /images/NAME           a file of build/ws074-images (make-test-images.py: the image test pages and their pictures,
                         ws074-p050), with Content-Length and its type; ?delay=MS waits that long first
  /redirect/N            N redirects (302, 301, 307 in turn) and then /pages/first.html
  /chunked               an HTML page sent in chunks of a few bytes (Transfer-Encoding: chunked)
  /close                 an HTML page without a length, ended by closing the connection
  /cookie/set            sets two cookies (one for /cookie only) and redirects to /cookie/echo
  /cookie/echo           a page showing the Cookie header it was sent
  /user-agent           a page showing the User-Agent header it was sent
  /script                a page whose script (/script.js, relative) writes into it
  /status/N              a page with status N
  /to-https              a redirect to the same host's https port, /pages/first.html
  /cookie/secure-set     sets s=1 (Secure) and p=2, then redirects to the http port's /cookie/echo
  /cached/NAME           a page kept by the client's cache (ws074-p058): ?max-age=N gives Cache-Control: max-age=N,
                         &etag=1 an ETag (If-None-Match with it is answered 304), ?no-store Cache-Control: no-store
  /stats                 {"connections": N, "requests": {path: N}, "not_modified": N} since the last /stats/reset:
                         the connections (counted at their first request, not for /stats) and the requests per path
  /stats/reset           starts the counts again

A kept connection that stays idle for --idle seconds (default 1) is closed by the server.
"""

import argparse
import http.server
import json
import os
import time
import socketserver
import ssl
import sys
import threading

PAGES = os.path.join(os.path.abspath(os.path.dirname(__file__)), "pages")
IMAGES = os.path.join(os.path.abspath(os.path.dirname(__file__)), "../../../build/ws074-images")
TYPES = {".html": "text/html; charset=utf-8", ".jpg": "image/jpeg", ".png": "image/png", ".gif": "image/gif"}


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    http_port = 0
    tls_port = 0
    timeout = 1
    counts_lock = threading.Lock()
    counts = {"connections": 0, "requests": {}, "not_modified": 0}

    def setup(self):
        super().setup()
        self.first_request = True

    def count(self, path):
        with Handler.counts_lock:
            if self.first_request and not path.startswith("/stats"):
                Handler.counts["connections"] += 1
            if not path.startswith("/stats"):
                Handler.counts["requests"][path] = Handler.counts["requests"].get(path, 0) + 1
        self.first_request = False

    def host_name(self):
        host = self.headers.get("Host", "127.0.0.1")
        if host.startswith("["):
            return host[:host.index("]") + 1]
        return host.split(":")[0]

    def log_message(self, fmt, *args):
        sys.stderr.write("http-server: %s\n" % (fmt % args))

    def send_page(self, body, status=200, headers=()):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(data)

    def redirect(self, status, location, headers=()):
        self.send_response(status)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()

    def do_GET(self):
        path = self.path.split("?")[0]
        self.count(path)
        if path == "/stats":
            with Handler.counts_lock:
                body = json.dumps(Handler.counts)
            return self.send_page(body)
        if path == "/stats/reset":
            with Handler.counts_lock:
                Handler.counts = {"connections": 0, "requests": {}, "not_modified": 0}
            return self.send_page("reset")
        if path.startswith("/cached/"):
            name = os.path.basename(path)
            query = self.path.split("?")[1] if "?" in self.path else ""
            options = dict(part.split("=", 1) if "=" in part else (part, "") for part in query.split("&") if part)
            headers = []
            if "max-age" in options:
                headers.append(("Cache-Control", "max-age=%s" % options["max-age"]))
            if "no-store" in options:
                headers.append(("Cache-Control", "no-store"))
            tag = '"v-%s"' % name
            if options.get("etag") == "1":
                headers.append(("ETag", tag))
                if self.headers.get("If-None-Match") == tag:
                    with Handler.counts_lock:
                        Handler.counts["not_modified"] += 1
                    self.send_response(304)
                    for header, value in headers:
                        self.send_header(header, value)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
            return self.send_page("<!DOCTYPE html><title>cached</title><p id=c>cached %s</p>" % name, 200, headers)
        if path.startswith("/pages/"):
            name = os.path.basename(path)
            full = os.path.join(PAGES, name)
            if not os.path.isfile(full):
                return self.send_page("<!DOCTYPE html><title>not found</title><p>no such page", 404)
            with open(full, "rb") as stream:
                return self.send_page(stream.read())
        if path.startswith("/images/"):
            name = os.path.basename(path)
            full = os.path.join(IMAGES, name)
            if not os.path.isfile(full):
                return self.send_page("<!DOCTYPE html><title>not found</title><p>no such page", 404)
            query = self.path.split("?")[1] if "?" in self.path else ""
            if query.startswith("delay="):
                time.sleep(int(query[6:]) / 1000.0)
            with open(full, "rb") as stream:
                data = stream.read()
            self.send_response(200)
            self.send_header("Content-Type", TYPES.get(os.path.splitext(name)[1], "application/octet-stream"))
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if path.startswith("/redirect/"):
            count = int(path.split("/")[2])
            if count <= 0:
                return self.redirect(302, "/pages/first.html")
            status = (302, 301, 307)[count % 3]
            return self.redirect(status, "/redirect/%d" % (count - 1))
        if path == "/chunked":
            body = b"<!DOCTYPE html><title>chunked</title><p id=a>first part</p><p id=b>second part</p>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for start in range(0, len(body), 7):
                chunk = body[start:start + 7]
                self.wfile.write(b"%x;ext=1\r\n%s\r\n" % (len(chunk), chunk))
            self.wfile.write(b"0\r\nX-Trailer: yes\r\n\r\n")
            return
        if path == "/close":
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"<!DOCTYPE html><title>close</title><p>ended by closing</p>")
            self.close_connection = True
            return
        if path == "/cookie/set":
            return self.redirect(302, "/cookie/echo", (("Set-Cookie", "session=abc123; Path=/; HttpOnly"),
                                                       ("Set-Cookie", "theme=dark; Path=/cookie"),
                                                       ("Set-Cookie", "other=x; Path=/elsewhere")))
        if path == "/cookie/echo":
            cookies = self.headers.get("Cookie", "")
            return self.send_page("<!DOCTYPE html><title>cookies</title><p id=cookies>%s</p>" % cookies)
        if path == "/user-agent":
            agent = self.headers.get("User-Agent", "")
            return self.send_page("<!DOCTYPE html><title>agent</title><p id=agent>%s</p>" % agent)
        if path == "/script":
            return self.send_page("<!DOCTYPE html><title>script</title><p id=out>not run</p>"
                                  "<script src=\"script.js?v=1\"></script>")
        if path == "/script.js":
            data = b"document.getElementById('out').textContent = 'the script came over http';"
            self.send_response(200)
            self.send_header("Content-Type", "text/javascript")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if path == "/to-https":
            return self.redirect(302, "https://%s:%d/pages/first.html" % (self.host_name(), Handler.tls_port))
        if path == "/cookie/secure-set":
            return self.redirect(302, "http://%s:%d/cookie/echo" % (self.host_name(), Handler.http_port),
                                 (("Set-Cookie", "s=1; Path=/; Secure"), ("Set-Cookie", "p=2; Path=/")))
        if path.startswith("/status/"):
            code = int(path.split("/")[2])
            return self.send_page("<!DOCTYPE html><title>status %d</title><p>status %d" % (code, code), code)
        return self.send_page("<!DOCTYPE html><title>not found</title><p>no such path", 404)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8074)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--tls-dir")
    parser.add_argument("--tls-port", type=int, default=8443)
    parser.add_argument("--tls-wrong-port", type=int, default=8444)
    parser.add_argument("--idle", type=float, default=1.0)
    args = parser.parse_args()
    Handler.timeout = args.idle
    server = Server((args.bind, args.port), Handler)
    Handler.http_port = server.server_address[1]
    Handler.tls_port = args.tls_port
    if args.tls_dir:
        for port, name in ((args.tls_port, "good"), (args.tls_wrong_port, "wrong")):
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(os.path.join(args.tls_dir, name + ".pem"), os.path.join(args.tls_dir, name + ".key"))
            secure = Server((args.bind, port), Handler)
            secure.socket = context.wrap_socket(secure.socket, server_side=True)
            threading.Thread(target=secure.serve_forever, daemon=True).start()
    print("http-server: listening on %s:%d" % (args.bind, server.server_address[1]), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
