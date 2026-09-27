#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""The HTTP test server of zdesktop-browser (ws074-p016, p017): serves the test pages and the cases the HTTP client meets.

  http-server.py [--port N] [--bind ADDRESS] [--tls-dir DIR --tls-port N --tls-wrong-port N]
      (default 8074 on 0.0.0.0; the guest reaches the host as 10.0.2.2)

With --tls-dir (made by make-test-ca.sh) the same paths are also served over HTTPS: on --tls-port with DIR/good.pem
(localhost, 127.0.0.1, 10.0.2.2) and on --tls-wrong-port with DIR/wrong.pem (a name that matches nothing).

  /pages/NAME            a page of plan/ws074/tests/pages, with Content-Length
  /redirect/N            N redirects (302, 301, 307 in turn) and then /pages/first.html
  /chunked               an HTML page sent in chunks of a few bytes (Transfer-Encoding: chunked)
  /close                 an HTML page without a length, ended by closing the connection
  /cookie/set            sets two cookies (one for /cookie only) and redirects to /cookie/echo
  /cookie/echo           a page showing the Cookie header it was sent
  /script                a page whose script (/script.js, relative) writes into it
  /status/N              a page with status N
  /to-https              a redirect to the same host's https port, /pages/first.html
  /cookie/secure-set     sets s=1 (Secure) and p=2, then redirects to the http port's /cookie/echo
"""

import argparse
import http.server
import os
import socketserver
import ssl
import sys
import threading

PAGES = os.path.join(os.path.abspath(os.path.dirname(__file__)), "pages")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    http_port = 0
    tls_port = 0

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
        if path.startswith("/pages/"):
            name = os.path.basename(path)
            full = os.path.join(PAGES, name)
            if not os.path.isfile(full):
                return self.send_page("<!DOCTYPE html><title>not found</title><p>no such page", 404)
            with open(full, "rb") as stream:
                return self.send_page(stream.read())
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
    args = parser.parse_args()
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
