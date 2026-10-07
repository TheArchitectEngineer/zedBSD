#!/usr/bin/env python3
"""Takes a picture of one head of the Venus guest's virtio-gpu through QMP's screendump (ws113-p006).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

With QEMU's D-Bus display (VENUS_DISPLAY=dbus) the guest has no VNC socket for
plan/ws035/tests/zdesktop-check.py; QMP's screendump writes each head of the
device (id venus in plan/ws035/tests/zdesktop-guest.sh) as a PNG instead.

    qmp-head-shot.py SOCKET HEAD OUT.png [--device venus]
"""
import argparse
import json
import os
import socket
import sys


def command(stream, name, arguments):
	"""Sends one command and returns its reply (events that come first are skipped)."""
	stream.write(json.dumps({"execute": name, "arguments": arguments}) + "\n")
	stream.flush()
	while True:
		reply = json.loads(stream.readline())
		if "return" in reply or "error" in reply:
			return reply


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("socket")
	parser.add_argument("head", type=int)
	parser.add_argument("out")
	parser.add_argument("--device", default="venus")
	arguments = parser.parse_args()

	# One connection, its greeting and the capabilities first.
	connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	connection.settimeout(30.0)
	connection.connect(arguments.socket)
	stream = connection.makefile("rw")
	stream.readline()
	command(stream, "qmp_capabilities", {})

	# The head's picture, written by QEMU (an absolute path: QEMU's working directory is its own).
	path = os.path.abspath(arguments.out)
	reply = command(stream, "screendump", {"filename": path, "device": arguments.device, "head": arguments.head, "format": "png"})
	if "error" in reply:
		print(f"qmp-head-shot: {reply['error']}", file=sys.stderr)
		return 1
	print(f"qmp-head-shot: head {arguments.head} {path}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
