#!/usr/bin/env python3
"""BUG-173: the shell's line editor (userland/base/libedit/readline.c) with long Japanese lines.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Runs the host build of /bin/sh (plan/tools/sh/build-host-sh.sh) in a pseudo terminal 40 columns wide and
draws what it writes on a small terminal model (UTF-8, East Asian wide characters in two cells, autowrap with
the pending wrap at the right edge, CR, LF, BS, CSI A B C D J K), as the system's Terminal does.  Checks:
  1. A typed line of Japanese wider than the terminal wraps, and the prompt stays on its row.
  2. The line recalled from the history (Up) is drawn from the prompt's row, the prompt still there (the bug:
     the cursor was moved back by bytes, ran into the left edge and the line was drawn over the prompt).
  3. Ctrl-A and Ctrl-E move across the rows; a character typed at the start goes after the prompt.
  4. Backspace erases one whole character (three bytes), Left steps one character.
  5. The line accepted with Enter runs with the edited text.

    python3 plan/ws073/tests/host-bug173.py [SH]      (default build/bug173/host-sh)
"""
import fcntl
import os
import pty
import select
import struct
import sys
import termios
import time

WIDTH = 40
HEIGHT = 20


def wide(code):
	"""Mirrors src/libc/wide.c's wcwidth for the characters the test uses."""
	ranges = ((0x1100, 0x115f), (0x2e80, 0xa4cf), (0xac00, 0xd7a3), (0xf900, 0xfaff), (0xfe10, 0xfe6f),
		  (0xff01, 0xff60), (0xffe0, 0xffe6), (0x1f300, 0x1faff), (0x20000, 0x3fffd))
	return any(low <= code <= high for low, high in ranges)


class Screen:
	"""A terminal model: cells of characters, a cursor, the pending wrap."""

	def __init__(self):
		self.cells = [[" "] * WIDTH for _ in range(HEIGHT)]
		self.row = 0
		self.col = 0
		self.pending = False
		self.text = b""

	def scroll(self):
		self.cells.pop(0)
		self.cells.append([" "] * WIDTH)

	def newline(self):
		self.row += 1
		if self.row >= HEIGHT:
			self.scroll()
			self.row = HEIGHT - 1

	def put(self, character):
		cells = 2 if wide(ord(character)) else 1
		if self.pending or self.col + cells > WIDTH:
			self.col = 0
			self.newline()
			self.pending = False
		self.cells[self.row][self.col] = character
		if cells == 2:
			self.cells[self.row][self.col + 1] = ""
		self.col += cells
		if self.col >= WIDTH:
			self.col = WIDTH - 1
			self.pending = True

	def feed(self, data):
		self.text += data
		text = data.decode("utf-8", "replace")
		index = 0
		while index < len(text):
			character = text[index]
			index += 1
			if character == "\x1b" and index < len(text) and text[index] == "[":
				end = index + 1
				while end < len(text) and not ("@" <= text[end] <= "~"):
					end += 1
				parameter = text[index + 1:end]
				final = text[end] if end < len(text) else ""
				index = end + 1
				count = int(parameter) if parameter.isdigit() else 1
				self.pending = False
				if final == "A":
					self.row = max(0, self.row - count)
				elif final == "B":
					self.row = min(HEIGHT - 1, self.row + count)
				elif final == "C":
					self.col = min(WIDTH - 1, self.col + count)
				elif final == "D":
					self.col = max(0, self.col - count)
				elif final == "K":
					for col in range(self.col, WIDTH):
						self.cells[self.row][col] = " "
				elif final == "J":
					for col in range(self.col, WIDTH):
						self.cells[self.row][col] = " "
					for row in range(self.row + 1, HEIGHT):
						self.cells[row] = [" "] * WIDTH
				continue
			if character == "\r":
				self.col = 0
				self.pending = False
			elif character == "\n":
				self.newline()
				self.pending = False
			elif character == "\b":
				self.col = max(0, self.col - 1)
				self.pending = False
			elif character == "\a":
				pass
			elif ord(character) >= 0x20:
				self.put(character)

	def line(self, row):
		return "".join(self.cells[row]).rstrip()


def run(shell):
	"""Runs the checks; returns the number that failed."""
	failures = 0
	checks = 0
	pid, master = pty.fork()
	if pid == 0:
		os.environ["PS1"] = "$ "
		os.environ["HISTFILE"] = "/dev/null"
		os.environ["LANG"] = "C.UTF-8"
		os.execv(shell, [shell, "-i"])
	fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", HEIGHT, WIDTH, 0, 0))
	screen = Screen()

	def drain(seconds=0.4):
		end = time.time() + seconds
		while time.time() < end:
			ready, _, _ = select.select([master], [], [], 0.05)
			if ready:
				try:
					data = os.read(master, 4096)
				except OSError:
					return
				screen.feed(data)

	def send(data):
		os.write(master, data)
		drain()

	def check(condition, what):
		nonlocal failures, checks
		checks += 1
		if not condition:
			failures += 1
			print("FAIL: " + what)
			for row in range(HEIGHT):
				print("  |" + screen.line(row) + "|")

	drain(1.0)
	japanese = "日本語の長い行" * 3
	typed = "echo " + japanese
	send(typed.encode("utf-8"))
	start = screen.row
	while start > 0 and not screen.line(start).startswith("$ "):
		start -= 1
	check(screen.line(start).startswith("$ echo 日本語"), "1: the typed line starts after the prompt")
	check(screen.line(start + 1) != "", "1: the line wraps to a second row")

	# Enter, then the line recalled.
	send(b"\r")
	drain(0.5)
	send(b"\x1b[A")
	rows = [row for row in range(HEIGHT) if screen.line(row).startswith("$ echo 日本語")]
	check(len(rows) >= 2, "2: the recalled line is drawn from a prompt row (the prompt kept)")
	recall = rows[-1] if rows else 0
	check(screen.line(recall + 1).startswith("長い行") or "日本" in screen.line(recall + 1) or "行" in screen.line(recall + 1),
	      "2: the recalled line goes on on the next row")

	# Ctrl-A, a character at the start.
	send(b"\x01X")
	check(screen.line(recall).startswith("$ Xecho 日本語"), "3: Ctrl-A goes to the line's start, after the prompt")
	send(b"\x05")
	check(screen.row > recall, "3: Ctrl-E goes to the line's last row")

	# Backspace erases one character; Left steps one character.
	send(b"\x7f")
	send(b"\x1b[D\x1b[DZ")
	send(b"\r")
	drain(0.5)
	expected = ("X" + "echo " + japanese)[1:]
	output = screen.text.decode("utf-8", "replace")
	check("command not found" in output or "Xecho" in output, "5: the edited line ran")
	edited = japanese[:-1]
	edited = edited[:-2] + "Z" + edited[-2:]
	check(("Xecho: not found" in output) or ("Xecho" in output and edited in output), "4: Backspace and Left worked by characters")

	os.write(master, b"exit\r")
	drain(0.5)
	try:
		os.kill(pid, 9)
	except OSError:
		pass
	os.waitpid(pid, 0)
	print("host-bug173: %s (%d checks)" % ("ok" if failures == 0 else "%d FAILED" % failures, checks))
	return failures


if __name__ == "__main__":
	sys.exit(1 if run(sys.argv[1] if len(sys.argv) > 1 else "build/bug173/host-sh") else 0)
