#!/usr/bin/env python3
"""The host's end of a long TCP transfer with a zedBSD machine (ws220-p001, p002).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    stream.py serve ADDRESS PORT LOG     an HTTP server: any GET is answered with
                                         /BYTES bytes of a fixed pattern (GET /1073741824)
    stream.py sink LOG                   reads standard input to its end
    stream.py listen ADDRESS PORT LOG    takes one connection, reads it to its end and
                                         closes it (the machine's tcp-stream sender, p002)

Either end writes LOG, a line every half second: the host's monotonic time in
seconds and the bytes moved so far ("t=12.500 bytes=123456789"), and a last
line "done t=... bytes=...".  The rate over time is the change of bytes; for
the server it is what the host's TCP took, which runs ahead of the guest by at
most the host's socket buffer.
"""
from __future__ import annotations

import socket
import sys
import threading
import time

CHUNK = 65536
PATTERN = bytes((i * 131 + 7) & 0xFF for i in range(CHUNK))


class Progress:
	"""The bytes moved, logged every half second by a thread of its own."""

	def __init__(self, path: str) -> None:
		self.bytes = 0
		self.done = False
		self.log = open(path, "w", buffering=1)
		self.thread = threading.Thread(target=self.run, daemon=True)
		self.thread.start()

	def run(self) -> None:
		while not self.done:
			self.log.write(f"t={time.monotonic():.3f} bytes={self.bytes}\n")
			time.sleep(0.5)

	def finish(self) -> None:
		self.done = True
		self.thread.join()
		self.log.write(f"done t={time.monotonic():.3f} bytes={self.bytes}\n")
		self.log.close()


def serve(address: str, port: int, log: str) -> int:
	"""Answers GET /N with N bytes, one request after another, logging the bytes sent."""
	listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	listener.bind((address, port))
	listener.listen(4)
	progress = Progress(log)
	try:
		while True:
			connection, _ = listener.accept()
			with connection:
				request = b""
				while b"\r\n\r\n" not in request:
					data = connection.recv(4096)
					if not data:
						break
					request += data
				try:
					path = request.split(b" ", 2)[1].decode()
					total = int(path.strip("/"))
				except (IndexError, ValueError):
					total = 0
				connection.sendall(
					b"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
					+ f"Content-Length: {total}\r\nConnection: close\r\n\r\n".encode())
				left = total
				while left > 0:
					piece = PATTERN if left >= CHUNK else PATTERN[:left]
					try:
						connection.sendall(piece)
					except OSError:
						break
					left -= len(piece)
					progress.bytes += len(piece)
	except KeyboardInterrupt:
		pass
	finally:
		progress.finish()
	return 0


def sink(log: str) -> int:
	"""Reads standard input to its end, logging the bytes read."""
	progress = Progress(log)
	stream = sys.stdin.buffer
	while True:
		data = stream.read1(CHUNK)
		if not data:
			break
		progress.bytes += len(data)
	progress.finish()
	return 0


def listen(address: str, port: int, log: str) -> int:
	"""Takes one connection, reads it to its end, then closes it, logging the bytes read."""
	listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	listener.bind((address, port))
	listener.listen(1)
	progress = Progress(log)
	connection, _ = listener.accept()
	listener.close()
	with connection:
		while True:
			data = connection.recv(CHUNK)
			if not data:
				break
			progress.bytes += len(data)
	progress.finish()
	return 0


def main() -> int:
	if len(sys.argv) == 5 and sys.argv[1] == "serve":
		return serve(sys.argv[2], int(sys.argv[3]), sys.argv[4])
	if len(sys.argv) == 5 and sys.argv[1] == "listen":
		return listen(sys.argv[2], int(sys.argv[3]), sys.argv[4])
	if len(sys.argv) == 3 and sys.argv[1] == "sink":
		return sink(sys.argv[2])
	print(__doc__, file=sys.stderr)
	return 2


if __name__ == "__main__":
	sys.exit(main())
