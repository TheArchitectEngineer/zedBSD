#!/usr/bin/env python3
"""Waits on a live serial line for a pattern, printing the lines as they come.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    uart-expect.py [--device PATH] [--baud N] [--timeout S] REGEX...

Every REGEX must match some line, in any order; it exits 0 when all have,
and 1 when the time runs out.  Nothing is kept in a file: the judgement is
made on the line while it is live (the U3 exception, design §7.4).  This is
the ZYBO stand-in until plan/tools/guest/serial.py takes --device.
"""
from __future__ import annotations

import argparse
import os
import re
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


def main() -> int:
	"""Reads the line until every pattern has matched or time runs out."""
	parser = argparse.ArgumentParser(prog="uart-expect")
	parser.add_argument("--device", default=DEFAULT_DEVICE)
	parser.add_argument("--baud", type=int, default=115200, choices=sorted(SPEEDS))
	parser.add_argument("--timeout", type=float, default=30.0)
	parser.add_argument("patterns", nargs="+")
	arguments = parser.parse_args()

	waiting = [re.compile(pattern) for pattern in arguments.patterns]
	fd = open_line(arguments.device, arguments.baud)
	# Tell a caller that starts this in the background that the line is open.
	print("uart-expect: listening", flush=True)
	deadline = time.monotonic() + arguments.timeout
	pending = ""
	while waiting and time.monotonic() < deadline:
		try:
			chunk = os.read(fd, 4096)
		except BlockingIOError:
			time.sleep(0.02)
			continue
		pending += chunk.decode("utf-8", "replace").replace("\r", "")
		while "\n" in pending:
			line, pending = pending.split("\n", 1)
			print(f"uart| {line}", flush=True)
			waiting = [p for p in waiting if not p.search(line)]
	if pending:
		print(f"uart| {pending}", flush=True)
		waiting = [p for p in waiting if not p.search(pending)]
	for pattern in waiting:
		print(f"uart-expect: never saw {pattern.pattern!r}", flush=True)
	return 1 if waiting else 0


if __name__ == "__main__":
	sys.exit(main())
