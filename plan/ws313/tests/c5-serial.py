#!/usr/bin/env python3
"""Follows the ODROID-C5's boot on its live debug UART, stage by stage.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    c5-serial.py [--device PATH] [--baud 921600] [--login USER:PASSWORD]
        [--commands] [--gpu] [--scale N]

The boot is read until the login prompt; each stage's line is looked for
in every line, and the run ends when the next stage is overdue.  The two
U-Boot stages may be missing (a silent U-Boot).  With --login it logs in
and, with --commands, runs each block's check and judges its output by
what it must show.  The last lines are the summary: one PASS, FAIL,
MISSING, SKIP or NOTE a line.  Nothing is kept in a file; the judgement is made on the live
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

# The stages of a boot: a name, a pattern, how long it may take after the
# stage before it (seconds), and whether it may be missing.  The U-Boot
# lines are the boot script's echoes, which a silent U-Boot build does not
# print, so their absence alone ends nothing.
STAGES = [
	("u-boot ran boot.scr", r"zedBSD: boot\.scr on", 120, True),
	("u-boot loaded the kernel", r"zedBSD: booti", 60, True),
	("HAL entry", r"ODROID-C5 ENTRY", 150, False),
	("HAL EL1", r"ODROID-C5 EL1 PASS", 10, False),
	("device tree parsed", r"ODROID-C5 FDT PASS", 10, False),
	("early framebuffer", r"ODROID-C5 (FRAMEBUFFER PASS|framebuffer unavailable)", 10, False),
	("memory map", r"ARM64 MEMORY MAP PASS", 30, False),
	("interrupts", r"ARM64 IRQ READY", 30, False),
	("all four CPUs", r"ARM64 SMP cpu 3 online", 60, False),
	("login prompt", r"login: ", 300, False),
]

# Lines worth a mark in the summary when they appear (the first of each).
NOTABLE = [
	r"FDT parse error.*",
	r"ARM64 SMP cpu \d .*(did not come up|was not released).*",
	r"s905x5m-usb: about to read the controller.*",
	r"mali: .*(fault|did not|failed).*",
	r"(?i)panic.*",
]

# The checks run after login: a name, the command, what its output must
# show, and whether the image is expected to have it at all ("gpu" only
# with --gpu).  Output without the pattern is FAIL; a check the image does
# not carry is SKIP.
COMMANDS = [
	("kernel", "uname -a", r"zedBSD", True),
	("sd card and root", "df /", r"mmc", True),
	("soc drivers", "dmesg | grep -i -e s905x5m -e meson-mmc -e dw-eqos", r"s905x5m", True),
	("usb devices", "lsusb", r"(?i)(hid|keyboard|mouse)", True),
	("ethernet", "ifconfig -a", r"inet \d+\.\d+\.\d+\.\d+", True),
	("gpu", "dmesg | grep -i mali", r"Mali-G310", False),
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

	def next_line(self, deadline: float) -> str | None:
		"""Returns the next whole line, or None when the deadline passes first.

		A prompt (a line that ends in ": " and waits for input, the login
		prompt) is returned as it stands, without its newline.
		"""
		while "\n" not in self.pending:
			if self.pending.endswith(": "):
				text, self.pending = self.pending, ""
				self.show(text)
				return text
			if time.monotonic() >= deadline:
				return None
			try:
				chunk = os.read(self.fd, 4096)
			except (BlockingIOError, InterruptedError):
				time.sleep(0.02)
				continue
			self.pending += chunk.decode("utf-8", "replace").replace("\r", "")
		text, self.pending = self.pending.split("\n", 1)
		self.show(text)
		return text

	def collect(self, pattern: re.Pattern, seconds: float) -> str | None:
		"""Reads until the pattern (a prompt) shows; returns what came before it."""
		deadline = time.monotonic() + seconds
		gathered: list[str] = []
		while time.monotonic() < deadline:
			found = pattern.search(self.pending)
			if found is not None:
				gathered.append(self.pending[:found.start()])
				self.flush_until(found.end())
				return "\n".join(gathered)
			try:
				chunk = os.read(self.fd, 4096)
			except (BlockingIOError, InterruptedError):
				time.sleep(0.02)
				continue
			self.pending += chunk.decode("utf-8", "replace").replace("\r", "")
			while "\n" in self.pending:
				text, self.pending = self.pending.split("\n", 1)
				self.show(text)
				gathered.append(text)
		return None

	def flush_until(self, end: int) -> None:
		self.show(self.pending[:end])
		self.pending = self.pending[end:]

	def type(self, text: str) -> None:
		time.sleep(0.5)
		os.write(self.fd, (text + "\r").encode())


def follow_boot(line: Line, scale: float) -> tuple[list[tuple[str, str]], bool]:
	"""Reads the boot until the login prompt, or until a stage is overdue.

	Every stage's pattern is looked for in every line, so a stage whose
	line is missing (a silent U-Boot) does not hide the ones after it.
	"""
	seen: dict[int, float] = {}
	notable: dict[str, str] = {}
	patterns = [re.compile(pattern) for _, pattern, _, _ in STAGES]
	marks = [re.compile(pattern) for pattern in NOTABLE]
	last = time.monotonic()
	login = len(STAGES) - 1
	while login not in seen:
		# The next stage not yet seen sets how long the line may stay quiet.
		waiting = next(index for index in range(len(STAGES)) if index not in seen)
		allowance = STAGES[waiting][2] * scale
		if waiting < 2:
			allowance += STAGES[2][2] * scale
		text = line.next_line(last + allowance)
		if text is None:
			break
		for index, pattern in enumerate(patterns):
			if index not in seen and pattern.search(text):
				seen[index] = time.monotonic()
				last = seen[index]
		for mark in marks:
			found = mark.search(text)
			if found and mark.pattern not in notable:
				notable[mark.pattern] = found.group(0)

	results: list[tuple[str, str]] = []
	failed = False
	for index, (name, _, _, optional) in enumerate(STAGES):
		if index in seen:
			result = "PASS"
			if index == 5:
				result = "PASS (see the HDMI photo)"
		elif optional:
			result = "MISSING (silent U-Boot?)"
		else:
			result = "FAIL"
			failed = True
		results.append((name, result))
	for text in notable.values():
		results.append((f"seen: {text[:60]}", "NOTE"))
	return results, failed


def run_checks(line: Line, login: str, gpu: bool) -> list[tuple[str, str]]:
	"""Logs in and runs each block's check, judging what it prints."""
	results: list[tuple[str, str]] = []
	user, _, password = login.partition(":")
	line.type(user)
	if line.wait(re.compile(r"[Pp]assword: ?"), 20) is not None:
		line.type(password)
	prompt = re.compile(r"[#$] $")
	if line.wait(prompt, 30) is None:
		results.append(("shell after login", "FAIL"))
		return results
	results.append(("shell after login", "PASS"))
	for name, command, expect, always in COMMANDS:
		if not always and not gpu:
			results.append((f"ran: {name}", "SKIP (no --gpu)"))
			continue
		line.type(command)
		output = line.collect(prompt, 30)
		if output is not None:
			# The echo of the command is not its output.
			output = output.replace(command, "")
		if output is None:
			results.append((f"ran: {name}", "FAIL (no prompt)"))
		elif re.search(expect, output):
			results.append((f"ran: {name}", "PASS"))
		else:
			results.append((f"ran: {name}", "FAIL (output lacks " + expect + ")"))
	return results


def main() -> int:
	parser = argparse.ArgumentParser(prog="c5-serial")
	parser.add_argument("--device", default="/dev/ttyUSB0")
	parser.add_argument("--baud", type=int, default=921600, choices=sorted(SPEEDS))
	parser.add_argument("--login")
	parser.add_argument("--commands", action="store_true")
	parser.add_argument("--gpu", action="store_true",
			    help="the image has the Mali driver (the keiland image)")
	parser.add_argument("--scale", type=float, default=1.0,
			    help="multiplies every stage's time")
	arguments = parser.parse_args()

	line = Line(open_line(arguments.device, arguments.baud))
	print("c5-serial: listening; power the board on now", flush=True)
	results, failed = follow_boot(line, arguments.scale)
	if not failed and arguments.login:
		checks = run_checks(line, arguments.login, arguments.gpu) if arguments.commands else []
		results.extend(checks)
		failed = any(result.startswith("FAIL") for _, result in checks)

	print("c5-serial: summary", flush=True)
	for name, result in results:
		print(f"c5-serial: {result:<34} {name}", flush=True)
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
