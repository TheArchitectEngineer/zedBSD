#!/usr/bin/env python3
"""Waits on a live serial line for a pattern, printing the lines as they come.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    uart-expect.py [--device PATH | --socket PATH] [--baud N] [--timeout S]
        [--stamp] [--then STEP]... REGEX...

Every REGEX must match some line, in any order; it exits 0 when all have,
and 1 when the time runs out.  A line still being written (a prompt such
as "login: ", which ends without a newline) is matched too.  --then gives
a dialogue to hold, in order, once every REGEX has matched: "type:TEXT"
types TEXT and a carriage return after a short pause (login throws away
what was typed before it asks), "expect:REGEX" waits for REGEX in what
came after the step before it, and "run:COMMAND" runs COMMAND on the host
with sh, showing what it prints, and fails the dialogue when it exits
nonzero (an ssh or scp to the guest while the line stays watched).  A
named group of an expect step's REGEX, (?P<name>...), is kept, and
{name} in a later step's TEXT or COMMAND is replaced by what it matched
(the address of a DHCP lease, read from ifconfig, for the ssh that
follows).  Nothing is kept in a file: the judgement is
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
import subprocess
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
	parser.add_argument("--then", action="append", default=[])
	parser.add_argument("patterns", nargs="+")
	arguments = parser.parse_args()
	for step in arguments.then:
		if not step.startswith(("type:", "expect:", "run:")):
			parser.error(f"--then takes type:TEXT, expect:REGEX or run:COMMAND, not {step!r}")

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

	def send(text: str) -> None:
		"""Types a line on the serial port, as a terminal sends it."""
		data = (text + "\r").encode()
		if client is not None:
			client.sendall(data)
		else:
			os.write(fd, data)

	# The line being written, and whether its text has been shown and matched.
	state = {"pending": "", "closed": False}
	# What the named groups of the expect steps matched, for later steps.
	values: dict[str, str] = {}

	def matched(pattern: re.Pattern, text: str) -> bool:
		"""Tests one pattern on a text, keeping what its named groups took."""
		found = pattern.search(text)
		if found is None:
			return False
		for name, value in found.groupdict().items():
			if value is not None:
				values[name] = value
		return True

	def substitute(text: str) -> str:
		"""Puts what the named groups matched in place of their {name}."""
		for name, value in values.items():
			text = text.replace("{" + name + "}", value)
		return text

	def run(command: str) -> bool:
		"""Runs one host command within the time left; reports its success."""
		left = max(1.0, deadline - time.monotonic())
		print(f"uart-expect: run {command!r}", flush=True)
		try:
			finished = subprocess.run(["sh", "-c", command], stdout=subprocess.PIPE,
				stderr=subprocess.STDOUT, timeout=left, check=False)
		except subprocess.TimeoutExpired:
			print("uart-expect: the command ran out of time", flush=True)
			return False
		for line in finished.stdout.decode("utf-8", "replace").splitlines():
			print(f"host| {line}", flush=True)
		if finished.returncode != 0:
			print(f"uart-expect: the command exited {finished.returncode}", flush=True)
			return False
		return True

	def read_lines(patterns: list[re.Pattern]) -> list[re.Pattern]:
		"""Reads what comes until the patterns match; returns those left."""
		while patterns and not state["closed"] and time.monotonic() < deadline:
			# A line still being written may be the prompt waited for.
			patterns = [p for p in patterns if not matched(p, state["pending"])]
			if not patterns:
				break
			try:
				if client is not None:
					chunk = client.recv(4096)
					if not chunk:
						state["closed"] = True
						break
				else:
					chunk = os.read(fd, 4096)
			except (BlockingIOError, InterruptedError):
				time.sleep(0.02)
				continue
			state["pending"] += chunk.decode("utf-8", "replace").replace("\r", "")
			while "\n" in state["pending"]:
				line, state["pending"] = state["pending"].split("\n", 1)
				show(line)
				patterns = [p for p in patterns if not matched(p, line)]
		return patterns

	waiting = read_lines(waiting)
	failed = False
	for step in arguments.then:
		if waiting or failed:
			break
		kind, _, text = step.partition(":")
		text = substitute(text)
		if kind == "run":
			# What the line printed so far is shown before the host's output.
			if state["pending"]:
				show(state["pending"])
				state["pending"] = ""
			failed = not run(text)
		elif kind == "type":
			# The prompt is shown before the text that answers it.
			if state["pending"]:
				show(state["pending"])
				state["pending"] = ""
			time.sleep(0.5)
			print(f"uart-expect: type {text!r}", flush=True)
			send(text)
		else:
			# Only what follows the step before is searched.
			if state["pending"]:
				show(state["pending"])
				state["pending"] = ""
			waiting = read_lines([re.compile(text)])
	if state["pending"]:
		show(state["pending"])
	for pattern in waiting:
		print(f"uart-expect: never saw {pattern.pattern!r}", flush=True)
	return 1 if waiting or failed else 0


if __name__ == "__main__":
	sys.exit(main())
