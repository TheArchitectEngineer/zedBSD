#!/usr/bin/env python3
"""Sends one QMP command to a running guest and prints the answer as JSON.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    plan/ws049/tests/qmp-send.py SOCKET COMMAND [ARGUMENTS-JSON]

SOCKET is the guest's QMP socket (the "monitor" of plan/tools/guest/guest.py's
session.json).  Events QEMU sends meanwhile are skipped; the exit status is 1
when QEMU answers with an error.
"""
import json
import socket
import sys


def receive(stream):
	"""Reads one answer, skipping asynchronous events."""
	while True:
		line = stream.readline()
		if not line:
			raise SystemExit("qmp: the connection closed")
		message = json.loads(line)
		if "event" not in message:
			return message


def main():
	if len(sys.argv) < 3:
		raise SystemExit(__doc__)
	path = sys.argv[1]
	request = {"execute": sys.argv[2]}
	if len(sys.argv) > 3:
		request["arguments"] = json.loads(sys.argv[3])
	connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	connection.settimeout(30)
	connection.connect(path)
	stream = connection.makefile("rw")
	stream.readline()
	stream.write(json.dumps({"execute": "qmp_capabilities"}) + "\n")
	stream.flush()
	receive(stream)
	stream.write(json.dumps(request) + "\n")
	stream.flush()
	answer = receive(stream)
	print(json.dumps(answer))
	return 1 if "error" in answer else 0


if __name__ == "__main__":
	sys.exit(main())
