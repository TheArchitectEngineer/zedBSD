#!/usr/bin/env python3
"""Follows the ODROID-C5's boot on its live debug UART, stage by stage.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    c5-serial.py [--device PATH] [--baud 921600] [--login USER:PASSWORD]
        [--stage-timeout S] [--commands]

Each stage waits for its line (in order) within its own time; the first
stage that does not come ends the run.  With --login it logs in at the
login prompt and, with --commands, runs the checks of every block and
shows what they print.  The last lines are the summary: one PASS/FAIL/SKIP
per stage.  Nothing is kept in a file; the judgement is made on the live
line (a guest's console log is not read afterwards).
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import termios
import time

SPEEDS = {115200: termios.B115200, 921600: termios.B921600}

# The stages of a boot: a name, a pattern, and how long it may take after
# the stage before it (seconds).  The U-Boot lines are the boot script's.
STAGES = [
	("u-boot ran boot.scr", r"zedBSD: boot\.scr on", 120),
	("u-boot loaded the kernel", r"zedBSD: booti", 60),
	("HAL entry", r"ODROID-C5 ENTRY", 30),
	("HAL EL1", r"ODROID-C5 EL1 PASS", 10),
	("device tree parsed", r"ODROID-C5 FDT PASS", 10),
	("early framebuffer", r"ODROID-C5 (FRAMEBUFFER PASS|framebuffer unavailable)", 10),
	("memory map", r"ARM64 MEMORY MAP PASS", 30),
	("interrupts", r"ARM64 IRQ READY", 30),
	("all four CPUs", r"ARM64 SMP cpu 3 online", 60),
	("login prompt", r"login: ", 300),
]

# The checks run after login: what each block reports about itself.
COMMANDS = [
	("kernel", "uname -a"),
	("sd card and root", "df /"),
	("soc drivers", "dmesg | grep -i -e s905x5m -e odroid-c5"),
	("usb", "lsusb"),
	("ethernet", "ifconfig -a"),
	("gpu", "dmesg | grep -i mali"),
]


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


class Line:
	"""The live serial line: what has come, shown as it comes."""

	def __init__(self, fd: int) -> None:
		self.fd = fd
		self.pending = ""
		self.start = time.monotonic()

	def show(self, text: str) -> None:
		print(f"uart| [{time.monotonic() - self.start:8.2f}] {text}", flush=True)

	def wait(self, pattern: re.Pattern, seconds: float) -> re.Match | None:
		"""Reads until the pattern matches a line (or the line being written)."""
		deadline = time.monotonic() + seconds
		while time.monotonic() < deadline:
			found = pattern.search(self.pending)
			if found is not None:
				self.flush_until(found.end())
				return found
			try:
				chunk = os.read(self.fd, 4096)
			except (BlockingIOError, InterruptedError):
				time.sleep(0.02)
				continue
			self.pending += chunk.decode("utf-8", "replace").replace("\r", "")
			while "\n" in self.pending:
				line, rest = self.pending.split("\n", 1)
				found = pattern.search(line)
				self.show(line)
				self.pending = rest
				if found is not None:
					return found
		return None

	def flush_until(self, end: int) -> None:
		self.show(self.pending[:end])
		self.pending = self.pending[end:]

	def type(self, text: str) -> None:
		time.sleep(0.5)
		os.write(self.fd, (text + "\r").encode())


def main() -> int:
	parser = argparse.ArgumentParser(prog="c5-serial")
	parser.add_argument("--device", default="/dev/ttyUSB0")
	parser.add_argument("--baud", type=int, default=921600, choices=sorted(SPEEDS))
	parser.add_argument("--login")
	parser.add_argument("--commands", action="store_true")
	parser.add_argument("--scale", type=float, default=1.0,
			    help="multiplies every stage's time")
	arguments = parser.parse_args()

	line = Line(open_line(arguments.device, arguments.baud))
	print("c5-serial: listening; power the board on now", flush=True)
	results: list[tuple[str, str]] = []
	failed = False
	for name, pattern, seconds in STAGES:
		if failed:
			results.append((name, "SKIP"))
			continue
		found = line.wait(re.compile(pattern), seconds * arguments.scale)
		if found is None:
			results.append((name, "FAIL"))
			failed = True
		elif "unavailable" in found.group(0):
			results.append((name, "FAIL (UART only)"))
		else:
			results.append((name, "PASS"))

	if not failed and arguments.login:
		user, _, password = arguments.login.partition(":")
		line.type(user)
		if line.wait(re.compile(r"[Pp]assword: ?"), 20) is not None:
			line.type(password)
		prompt = line.wait(re.compile(r"[#$] $"), 30)
		results.append(("shell after login", "PASS" if prompt else "FAIL"))
		if prompt and arguments.commands:
			for name, command in COMMANDS:
				line.type(command)
				done = line.wait(re.compile(r"[#$] $"), 30)
				results.append((f"ran: {name}", "PASS (read the output above)" if done else "FAIL"))

	print("c5-serial: summary", flush=True)
	for name, result in results:
		print(f"c5-serial: {result:<30} {name}", flush=True)
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
