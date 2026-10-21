#!/usr/bin/env python3
"""Each typed key's way to the screen, step by step, from a session log.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    key-latency.py SESSION.LOG MARKS [STEP] [--keys]

The keys of a frame-ops.sh step (keys by default: one key every 200 ms)
need Keiland's --log-frames and the Terminal's TERMINAL_PERF=1.  For each
key press (not Shift) the first of each step after it, in that order and
by time (keys typed quickly share a step, which may come after the next
press), in ms from the press reaching the compositor's seat (KWL KEY in):
    ime       the key sent to the input method (KWL KEY to=ime), or
    client    the key sent to the window's client (KWL KEY to=client)
    commit    the input method's text sent to the client (KWL KEY ime_commit)
    term      the Terminal took the text or the key (ZTERM LAT commit / key)
    write     the Terminal wrote it to the shell (ZTERM LAT write)
    read      the Terminal read the shell's echo (ZTERM LAT read)
    present   the Terminal presented the frame showing it (ZTERM LAT present)
    adopt     the compositor took the Terminal's commit (KWL LAT adopt)
    shown     the compositor's frame with it completed (KWL LAT shown)
The Terminal's times have the monotonic clock's tick (1 ms); the
compositor's are finer.  The table has the median and the largest of each
step; --keys prints every key too.
"""
from __future__ import annotations

import re
import sys

STEPS = ("ime", "client", "commit", "term", "write", "read", "present", "adopt", "shown")
SHIFT_KEYS = {42, 54}
AT = re.compile(r"at_us=(\d+)")
PATTERNS = (
	("ime", re.compile(r"KWL KEY to=ime key=\d+ state=1 ")),
	("client", re.compile(r"KWL KEY to=client key=\d+ state=1 ")),
	("commit", re.compile(r"KWL KEY ime_commit ")),
	("term", re.compile(r"ZTERM LAT (commit|key) bytes=[1-9]")),
	("write", re.compile(r"ZTERM LAT write ")),
	("read", re.compile(r"ZTERM LAT read ")),
	("present", re.compile(r"ZTERM LAT present ")),
	("adopt", re.compile(r"KWL LAT adopt ")),
	("shown", re.compile(r"KWL LAT shown ")),
)
PRESS = re.compile(r"KWL KEY in key=(\d+) state=1 .*at_us=(\d+)")


def main() -> int:
	args = [word for word in sys.argv[1:] if not word.startswith("--")]
	every = "--keys" in sys.argv
	if len(args) not in (2, 3):
		print(__doc__.strip().splitlines()[2], file=sys.stderr)
		return 2
	step_name = args[2] if len(args) == 3 else "keys"
	with open(args[0], encoding="utf-8", errors="replace") as log:
		lines = log.read().splitlines()
	marks = []
	with open(args[1], encoding="utf-8") as file:
		for row in file:
			words = row.split()
			if len(words) >= 2 and words[-1].isdigit():
				marks.append((" ".join(words[:-1]), int(words[-1])))
	span = None
	for index, (name, start) in enumerate(marks):
		if name == step_name:
			end = marks[index + 1][1] if index + 1 < len(marks) else len(lines)
			span = lines[start:end]
	if span is None:
		print(f"no step {step_name} in the marks", file=sys.stderr)
		return 1

	# The presses, and every step's lines with their times (ms).
	keys = []
	events = {name: [] for name in STEPS}
	for line in span:
		match = PRESS.search(line)
		if match and int(match.group(1)) not in SHIFT_KEYS:
			keys.append({"key": int(match.group(1)), "in": int(match.group(2)) / 1000.0})
			continue
		for name, pattern in PATTERNS:
			if pattern.search(line):
				found = AT.search(line)
				if found is not None:
					events[name].append(int(found.group(1)) / 1000.0)

	# For each press, each step's first time at or after the step before it (keys typed quickly
	# share a write, a frame and a present); the compositor's ways (ime, client, commit) from the press.
	for entry in keys:
		after = entry["in"]
		for name in STEPS:
			start = entry["in"] if name in ("ime", "client", "commit") else after
			later = [time for time in events[name] if time >= start]
			if not later:
				continue
			entry[name] = later[0] - entry["in"]
			if name not in ("ime", "client", "commit"):
				after = later[0]

	if every:
		print("  key " + " ".join(f"{name:>7}" for name in STEPS))
		for entry in keys:
			print(f"  {entry['key']:>3} " + " ".join(f"{entry[name]:7.1f}" if name in entry else f"{'-':>7}" for name in STEPS))
	print(f"{len(keys)} keys; ms after the press reached the seat, median / largest:")
	for name in STEPS:
		values = sorted(entry[name] for entry in keys if name in entry)
		if values:
			print(f"  {name:<8} {values[len(values) // 2]:7.1f} {values[-1]:7.1f}  ({len(values)} keys)")
		else:
			print(f"  {name:<8} {'-':>7}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
