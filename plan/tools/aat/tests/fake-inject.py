#!/usr/bin/env python3
"""The self-test's stand-in for the target's injector (plan/tools/aat/README.md, the protocol).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

It reads "SEQ VERB ARGS" lines from the FIFO as the real daemon does,
checks each as the real one would (a known verb, its arguments, the point
on the screen), writes what it took to DONE.record and answers
"done SEQ STATUS" (0, or 22 for a refused line) in the done file.
"""
import argparse
import signal
import sys

KEY_MAX = 0x2ff
BUTTONS = (0x110, 0x111, 0x112)


def check(verb: str, words: list[str], width: int, height: int) -> bool:
	"""Tells whether a command is one the injector takes."""
	try:
		numbers = [int(word) for word in words]
	except ValueError:
		return False
	if verb == "abs":
		return len(numbers) == 2 and 0 <= numbers[0] < width and 0 <= numbers[1] < height
	if verb == "rel":
		return len(numbers) == 2
	if verb == "button":
		return len(numbers) == 2 and numbers[0] in BUTTONS and numbers[1] in (0, 1)
	if verb == "wheel":
		return len(numbers) == 2
	if verb == "key":
		return len(numbers) == 2 and 0 < numbers[0] <= KEY_MAX and numbers[1] in (0, 1)
	if verb == "wait":
		return len(numbers) == 1 and 0 <= numbers[0] <= 10000
	return False


def main() -> int:
	"""Serves the FIFO until it is killed."""
	parser = argparse.ArgumentParser()
	parser.add_argument("--fifo", required=True)
	parser.add_argument("--done", required=True)
	parser.add_argument("--width", type=int, required=True)
	parser.add_argument("--height", type=int, required=True)
	arguments = parser.parse_args()
	signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
	while True:
		with open(arguments.fifo, encoding="utf-8") as fifo:
			for line in fifo:
				words = line.split()
				if len(words) < 2:
					continue
				sequence, verb = words[0], words[1]
				status = 0 if check(verb, words[2:], arguments.width, arguments.height) else 22
				with open(arguments.done + ".record", "a", encoding="utf-8") as record:
					record.write(" ".join(words[1:]) + "\n")
				with open(arguments.done, "a", encoding="utf-8") as done:
					done.write(f"done {sequence} {status}\n")


if __name__ == "__main__":
	sys.exit(main())
