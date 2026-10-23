#!/usr/bin/env python3
"""Waits on a live serial line for a pattern, printing the lines as they come.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    uart-expect.py [--device PATH | --socket PATH] [--baud N] [--timeout S]
        [--stamp] REGEX...

Every REGEX must match some line, in any order; it exits 0 when all have,
and 1 when the time runs out.  Nothing is kept in a file: the judgement is
made on the line while it is live (the U3 exception, design §7.4).  This is
the ZYBO stand-in until plan/tools/guest/serial.py takes --device.
--socket reads an emulator's serial port from its Unix socket instead
(qemu-zynq.sh), connecting as soon as the socket is there.  --stamp puts
the host's seconds since the start before each line, so that the time
between two lines (ARMV7 TIMER 0S and 10S) can be read off the clock of
the host.
"""
from __future__ import annotations

import argparse
import os
import re
import socket
import sys
import termios
import time

DEFAULT_DEVICE = ("/dev/serial/by-id/"
	"usb-Digilent_Digilent_Adept_USB_Device_210279785182-if01-port0")
SPEEDS = {9600: termios.B9600, 115200: termios.B115200}


def open_line(path: str, baud: int) -> int:
	"""Opens the line raw, 8N1, without modem control."""
	fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
	attrs = termios.tcgetattr(fd)
	attrs[0] = 0
	attrs[1] = 0
	attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
	attrs[3] = 0
	attrs[4] = SPEEDS[baud]
	attrs[5] = SPEEDS[baud]
	termios.tcsetattr(fd, termios.TCSANOW, attrs)
	termios.tcflush(fd, termios.TCIFLUSH)
	return fd


def open_socket(path: str, deadline: float) -> socket.socket:
	"""Connects to the emulator's serial socket, waiting for it to appear."""
	while True:
		client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		try:
			client.connect(path)
			client.setblocking(False)
			return client
		except OSError:
			client.close()
			if time.monotonic() >= deadline:
				raise
			time.sleep(0.05)


def main() -> int:
	"""Reads the line until every pattern has matched or time runs out."""
	parser = argparse.ArgumentParser(prog="uart-expect")
	parser.add_argument("--device", default=DEFAULT_DEVICE)
	parser.add_argument("--socket")
	parser.add_argument("--stamp", action="store_true")
	parser.add_argument("--baud", type=int, default=115200, choices=sorted(SPEEDS))
	parser.add_argument("--timeout", type=float, default=30.0)
	parser.add_argument("patterns", nargs="+")
	arguments = parser.parse_args()

	waiting = [re.compile(pattern) for pattern in arguments.patterns]
	start = time.monotonic()
	deadline = start + arguments.timeout
	client = None
	fd = -1
	if arguments.socket:
		client = open_socket(arguments.socket, deadline)
	else:
		fd = open_line(arguments.device, arguments.baud)
	# Tell a caller that starts this in the background that the line is open.
	print("uart-expect: listening", flush=True)

	def show(line: str) -> None:
		"""Prints one line as it came, with the host's time when asked."""
		if arguments.stamp:
			print(f"uart| [{time.monotonic() - start:9.3f}] {line}", flush=True)
		else:
			print(f"uart| {line}", flush=True)

	pending = ""
	while waiting and time.monotonic() < deadline:
		try:
			if client is not None:
				chunk = client.recv(4096)
				if not chunk:
					break
			else:
				chunk = os.read(fd, 4096)
		except (BlockingIOError, InterruptedError):
			time.sleep(0.02)
			continue
		pending += chunk.decode("utf-8", "replace").replace("\r", "")
		while "\n" in pending:
			line, pending = pending.split("\n", 1)
			show(line)
			waiting = [p for p in waiting if not p.search(line)]
	if pending:
		show(pending)
		waiting = [p for p in waiting if not p.search(pending)]
	for pattern in waiting:
		print(f"uart-expect: never saw {pattern.pattern!r}", flush=True)
	return 1 if waiting else 0


if __name__ == "__main__":
	sys.exit(main())
