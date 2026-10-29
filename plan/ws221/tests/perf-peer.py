#!/usr/bin/env python3
"""The host's end of tcp-perf (userland/tests/tcp-perf): sinks or sources a TCP stream.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    perf-peer.py ADDRESS PORT LOG [CONNECTIONS]

Listens on ADDRESS:PORT (an IPv4 or IPv6 address, a link-local one with %INTERFACE) and serves CONNECTIONS connections one after another
(default 1).  The first byte of each says the direction: 'S' (the machine sends:
this end reads to the end of the stream and closes) or 'R' (the machine
receives: this end writes until the machine closes).  Each connection appends a
line to LOG: "done DIRECTION seconds=S bytes=N mbps=RATE" as this end saw it.
"""
from __future__ import annotations

import socket
import sys
import time

CHUNK = 1 << 20


def serve_one(listener: socket.socket, log) -> None:
	connection, peer = listener.accept()
	connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
	direction = connection.recv(1)
	moved = 0
	start = time.monotonic()
	if direction == b"S":
		buffer = bytearray(CHUNK)
		view = memoryview(buffer)
		while True:
			got = connection.recv_into(view)
			if got == 0:
				break
			moved += got
		label = "sink"
	elif direction == b"R":
		data = bytes((i * 131 + 7) & 0xFF for i in range(256)) * (CHUNK // 256)
		try:
			while True:
				moved += connection.send(data)
		except (BrokenPipeError, ConnectionResetError):
			pass
		label = "source"
	else:
		label = "bad-direction"
	elapsed = max(time.monotonic() - start, 1e-9)
	connection.close()
	log.write(f"done {label} peer={peer[0]} seconds={elapsed:.3f} bytes={moved} mbps={moved * 8 / elapsed / 1e6:.1f}\n")
	log.flush()


def main() -> int:
	if len(sys.argv) not in (4, 5):
		print(__doc__.strip().splitlines()[3], file=sys.stderr)
		return 2
	address, port, log_path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
	count = int(sys.argv[4]) if len(sys.argv) == 5 else 1
	family, kind, protocol, _, where = socket.getaddrinfo(address, port, socket.AF_UNSPEC, socket.SOCK_STREAM, 0,
	    socket.AI_NUMERICHOST | socket.AI_PASSIVE)[0]
	listener = socket.socket(family, kind, protocol)
	listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	listener.bind(where)
	listener.listen(1)
	with open(log_path, "a", buffering=1) as log:
		log.write("ready\n")
		for _ in range(count):
			serve_one(listener, log)
	listener.close()
	return 0


if __name__ == "__main__":
	sys.exit(main())
