#!/usr/bin/env python3
"""ws092: types text and key chords into the guest through QMP (US layout).

    qmp-keys.py SOCKET STEP...

Steps, in order:
    text:STRING      each character typed (\\n is Enter, \\t is Tab)
    key:CHORD        a chord such as ctrl-s, shift-left, ctrl-shift-z, up, f3, esc
    click:X,Y        the left button at a place of the 1280x800 output
    move:X,Y         the pointer to a place
    drag:X,Y,X2,Y2   the left button pressed at one place and let go at another
    middle:X,Y       the middle button at a place
    right:X,Y        the right button at a place
    sleep:MS         a pause
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import json
import socket
import sys
import time

WIDTH = 1280
HEIGHT = 800

# The qcodes of the characters without shift, and those typed with shift.
PLAIN = {" ": "spc", "\n": "ret", "\t": "tab", "-": "minus", "=": "equal", "[": "bracket_left",
         "]": "bracket_right", ";": "semicolon", "'": "apostrophe", "`": "grave_accent",
         "\\": "backslash", ",": "comma", ".": "dot", "/": "slash"}
SHIFTED = {"!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7", "*": "8",
           "(": "9", ")": "0", "_": "minus", "+": "equal", "{": "bracket_left", "}": "bracket_right",
           ":": "semicolon", '"': "apostrophe", "~": "grave_accent", "|": "backslash",
           "<": "comma", ">": "dot", "?": "slash"}
NAMES = {"esc": "esc", "enter": "ret", "tab": "tab", "backspace": "backspace", "delete": "delete",
         "left": "left", "right": "right", "up": "up", "down": "down", "home": "home", "end": "end",
         "pgup": "pgup", "pgdn": "pgdn", "f3": "f3", "insert": "insert"}


def send(stream, command, arguments):
	"""Sends one command and waits for its reply."""
	stream.write(json.dumps({"execute": command, "arguments": arguments}) + "\n")
	stream.flush()
	while True:
		reply = json.loads(stream.readline())
		if "return" in reply:
			return
		if "error" in reply:
			raise RuntimeError(reply["error"])


def key(stream, code, down):
	"""Presses or lets go of a key."""
	send(stream, "input-send-event", {"events": [{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": code}}}]})
	time.sleep(0.02)


def tap(stream, codes):
	"""Presses the modifiers and the key, then lets them go in reverse."""
	for code in codes:
		key(stream, code, True)
	for code in reversed(codes):
		key(stream, code, False)
	time.sleep(0.03)


def character(stream, value):
	"""Types one character."""
	if value in PLAIN:
		tap(stream, [PLAIN[value]])
	elif value in SHIFTED:
		tap(stream, ["shift", SHIFTED[value]])
	elif value.isupper():
		tap(stream, ["shift", value.lower()])
	else:
		tap(stream, [value])


def chord(stream, text):
	"""Types a chord such as ctrl-shift-z."""
	parts = text.split("-")
	codes = []
	for part in parts[:-1]:
		codes.append({"ctrl": "ctrl", "shift": "shift", "alt": "alt"}[part])
	last = parts[-1]
	codes.append(NAMES.get(last, PLAIN.get(last, last)))
	tap(stream, codes)


def pointer(stream, x, y):
	"""Moves the pointer (usb-tablet) to a place."""
	send(stream, "input-send-event", {"events": [
		{"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (WIDTH - 1))}},
		{"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (HEIGHT - 1))}}]})
	time.sleep(0.05)


def button(stream, name, down):
	"""Presses or lets go of a button."""
	send(stream, "input-send-event", {"events": [{"type": "btn", "data": {"down": down, "button": name}}]})
	time.sleep(0.05)


def main():
	connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	connection.connect(sys.argv[1])
	stream = connection.makefile("rw")
	stream.readline()
	send(stream, "qmp_capabilities", {})
	for step in sys.argv[2:]:
		kind, _, value = step.partition(":")
		if kind == "text":
			for item in value.encode().decode("unicode_escape"):
				character(stream, item)
		elif kind == "key":
			chord(stream, value)
		elif kind in ("click", "middle", "right", "move"):
			x, y = (int(part) for part in value.split(","))
			pointer(stream, x, y)
			if kind != "move":
				name = {"click": "left", "middle": "middle", "right": "right"}[kind]
				button(stream, name, True)
				button(stream, name, False)
		elif kind == "drag":
			x, y, x2, y2 = (int(part) for part in value.split(","))
			pointer(stream, x, y)
			button(stream, "left", True)
			for index in range(1, 9):
				pointer(stream, x + (x2 - x) * index / 8, y + (y2 - y) * index / 8)
			button(stream, "left", False)
		elif kind == "sleep":
			time.sleep(int(value) / 1000.0)
		else:
			raise SystemExit("unknown step " + step)


if __name__ == "__main__":
	main()
