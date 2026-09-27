#!/usr/bin/env python3
"""ws071: drives the guest's pointer and keyboard together through one QMP connection.

    qmp-input.py SOCKET [--width 1280 --height 800] STEP...

Steps, run in order:
    move X Y             the pointer (usb-tablet) to output pixel (X, Y)
    down [BUTTON]        a button pressed (left, right or middle; left when omitted)
    up [BUTTON]          a button let go
    click [BUTTON]       pressed and let go
    hold KEY / free KEY  a key (a QEMU qcode: ctrl, shift, alt, ...) pressed and held / let go
    key KEY              a key pressed and let go
    sleep MS             a pause
It adds to plan/ws035/tests/qmp-pointer.py what that one lacks: the right button and a
modifier held while the pointer clicks (Ctrl+click, Shift+click).
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import argparse
import json
import socket
import sys
import time


def send(stream, command, arguments):
	"""Sends one command and waits for its reply (events may come first)."""
	stream.write(json.dumps({"execute": command, "arguments": arguments}) + "\n")
	stream.flush()
	while True:
		reply = json.loads(stream.readline())
		if "return" in reply:
			return
		if "error" in reply:
			raise RuntimeError(reply["error"])


def button(stream, name, down):
	"""Presses or lets go of a pointer button."""
	send(stream, "input-send-event", {"events": [{"type": "btn", "data": {"down": down, "button": name}}]})


def key(stream, code, down):
	"""Presses or lets go of a key."""
	send(stream, "input-send-event", {"events": [{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": code}}}]})
	time.sleep(0.03)


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("socket")
	parser.add_argument("--width", type=int, default=1280)
	parser.add_argument("--height", type=int, default=800)
	parser.add_argument("steps", nargs=argparse.REMAINDER)
	arguments = parser.parse_args()
	connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	connection.connect(arguments.socket)
	stream = connection.makefile("rw")
	stream.readline()
	send(stream, "qmp_capabilities", {})
	steps = arguments.steps
	index = 0
	while index < len(steps):
		word = steps[index]
		index += 1
		name = "left"
		if word in ("down", "up", "click") and index < len(steps) and steps[index] in ("left", "right", "middle"):
			name = steps[index]
			index += 1
		if word == "move":
			x = int(steps[index])
			y = int(steps[index + 1])
			index += 2
			vx = (x * 32767 + arguments.width - 2) // (arguments.width - 1)
			vy = (y * 32767 + arguments.height - 2) // (arguments.height - 1)
			send(stream, "input-send-event", {"events": [
				{"type": "abs", "data": {"axis": "x", "value": vx}},
				{"type": "abs", "data": {"axis": "y", "value": vy}}]})
		elif word == "down":
			button(stream, name, True)
		elif word == "up":
			button(stream, name, False)
		elif word == "click":
			button(stream, name, True)
			time.sleep(0.06)
			button(stream, name, False)
		elif word == "hold":
			key(stream, steps[index], True)
			index += 1
		elif word == "free":
			key(stream, steps[index], False)
			index += 1
		elif word == "key":
			key(stream, steps[index], True)
			key(stream, steps[index], False)
			index += 1
		elif word == "sleep":
			time.sleep(int(steps[index]) / 1000.0)
			index += 1
		else:
			print("qmp-input: unknown step %s" % word, file=sys.stderr)
			return 2
	return 0


if __name__ == "__main__":
	sys.exit(main())
