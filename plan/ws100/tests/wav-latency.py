#!/usr/bin/env python3
"""ws100-p008: the time from an input to its sound in QEMU's WAV, on the host's clock.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

record: sends pointer steps through QMP (as qmp-pointer.py), noting the host's monotonic time of each wheel notch and
each button release, while a thread notes the WAV's size every 2 ms (QEMU's wav backend writes the sound as it
plays it, so the size says which frame was played when).
    wav-latency.py record SOCKET WAV EVENTS [--width 1280 --height 800] STEPS...
      (the steps of qmp-pointer.py; wheel-down, wheel-up and up are timed)
analyse: finds the sounds' starts in the WAV (a frame above a threshold after 150 ms of quiet), turns their frames
into host times (the earliest time each written size was seen, less the frames' duration), and pairs each timed input
with the first start after it.
    wav-latency.py analyse WAV EVENTS [TARGET_MS]
"""
import json
import os
import socket
import statistics
import struct
import sys
import threading
import time

RATE = 48000
FRAME_BYTES = 4
HEADER = 44


def send(stream, command, arguments):
	"""Sends one QMP command and waits for its reply."""
	stream.write(json.dumps({"execute": command, "arguments": arguments}) + "\n")
	stream.flush()
	while True:
		reply = json.loads(stream.readline())
		if "return" in reply:
			return
		if "error" in reply:
			raise RuntimeError(reply["error"])


def record(argv):
	"""Runs the steps, timing the inputs, while the WAV's size is sampled."""
	socket_path, wav, events_path = argv[0], argv[1], argv[2]
	width, height = 1280, 800
	steps = argv[3:]
	if steps[:1] == ["--width"]:
		width, height, steps = int(steps[1]), int(steps[3]), steps[4:]
	samples = []
	running = [True]

	def sampler():
		while running[0]:
			try:
				samples.append((time.monotonic(), os.stat(wav).st_size))
			except OSError:
				pass
			time.sleep(0.002)

	thread = threading.Thread(target=sampler)
	thread.start()
	connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	connection.connect(socket_path)
	stream = connection.makefile("rw")
	stream.readline()
	send(stream, "qmp_capabilities", {})
	events = []
	index = 0
	while index < len(steps):
		word = steps[index]
		if word == "move":
			x, y = int(steps[index + 1]), int(steps[index + 2])
			vx = (x * 32767 + width - 2) // (width - 1)
			vy = (y * 32767 + height - 2) // (height - 1)
			send(stream, "input-send-event", {"events": [
				{"type": "abs", "data": {"axis": "x", "value": vx}},
				{"type": "abs", "data": {"axis": "y", "value": vy}}]})
			index += 3
		elif word in ("down", "up"):
			moment = time.monotonic()
			send(stream, "input-send-event", {"events": [{"type": "btn", "data": {"down": word == "down", "button": "left"}}]})
			if word == "up":
				events.append(("release", moment))
			index += 1
		elif word in ("wheel-down", "wheel-up"):
			moment = time.monotonic()
			for down in (True, False):
				send(stream, "input-send-event", {"events": [{"type": "btn", "data": {"down": down, "button": word}}]})
			events.append(("wheel", moment))
			index += 1
		elif word == "sleep":
			time.sleep(int(steps[index + 1]) / 1000.0)
			index += 2
		else:
			print("wav-latency: unknown step %s" % word, file=sys.stderr)
			return 2
	time.sleep(1.0)
	running[0] = False
	thread.join()
	with open(events_path, "w") as out:
		json.dump({"events": events, "samples": samples}, out)
	return 0


def analyse(argv):
	"""Pairs the timed inputs with the sounds' starts in the WAV, on the host's clock."""
	wav, events_path = argv[0], argv[1]
	target = int(argv[2]) if len(argv) > 2 else 50
	data = open(wav, "rb").read()[HEADER:]
	frames = len(data) // FRAME_BYTES
	left = struct.unpack("<%dh" % (frames * 2), data[:frames * FRAME_BYTES])[0::2]
	recorded = json.load(open(events_path))
	# The host time of frame 0: each sampled size's frames were all played by the time it was seen.
	offsets = [moment - ((size - HEADER) // FRAME_BYTES) / RATE for moment, size in recorded["samples"] if size > HEADER]
	if not offsets:
		print("RESULT no WAV sizes sampled")
		return 1
	zero = min(offsets)
	starts = []
	quiet = RATE
	for index, value in enumerate(left):
		if abs(value) > 300:
			if quiet > RATE * 0.15:
				starts.append(zero + index / RATE)
			quiet = 0
		else:
			quiet += 1
	rows = []
	for kind, moment in recorded["events"]:
		after = [s for s in starts if s >= moment - 0.005]
		if not after or after[0] - moment > 0.5:
			print("input %s at %.3f: no sound within 500 ms" % (kind, moment))
			continue
		latency = (after[0] - moment) * 1000.0
		rows.append((kind, latency))
		print("input %s latency_ms=%.0f" % (kind, latency))
	if not rows:
		print("RESULT no inputs paired")
		return 1
	values = [r[1] for r in rows]
	print("RESULT wav inputs=%d median_ms=%.0f max_ms=%.0f target=%d %s (frame 0 at host %.3f, the size's step %.1f ms at most)" % (
		len(rows), statistics.median(values), max(values), target, "within" if statistics.median(values) <= target else "OVER",
		zero, max(o - zero for o in offsets[-50:]) * 1000.0))
	return 0


if __name__ == "__main__":
	if len(sys.argv) > 1 and sys.argv[1] == "record":
		sys.exit(record(sys.argv[2:]))
	if len(sys.argv) > 1 and sys.argv[1] == "analyse":
		sys.exit(analyse(sys.argv[2:]))
	print(__doc__)
	sys.exit(2)
