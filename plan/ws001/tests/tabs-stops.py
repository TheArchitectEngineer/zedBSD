#!/usr/bin/env python3
"""ws001-p043: compares zedBSD's tabs with the host's (ncurses) by the tab
stops each sets, not by the bytes (ncurses moves with blanks, zedBSD with
cuf).  Each case runs both with TERM=vt100 (zedBSD's entry from
userland/base/terminfo) and COLUMNS, decodes the output as a VT100 would
(CR, blank, ESC [ n C, ESC [ 3 g, ESC H) into the stops past column 1, and
compares them and the exit status.

  python3 plan/ws001/tests/tabs-stops.py BUILD/tabs
"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

CASES = [
	[], ["-8"], ["-1"], ["-2"], ["-4"], ["-9"], ["-0"],
	["-a"], ["-a2"], ["-c"], ["-c2"], ["-c3"], ["-f"], ["-p"], ["-s"], ["-u"],
	["1,10,20"], ["5,10,20"], ["1,+5,+5"], ["3 7 11"], ["3", "7", "11"],
	["4,+4,20,+2"], ["-T", "vt100", "6,12"], ["-c", "-4"], ["-4", "-c"],
]

# Where zedBSD differs from ncurses on purpose, its own result at 80
# columns: a stop past the width is not set (ncurses sends the cursor to
# the margin and sets one there), and a list that is not ascending, has a
# zero or starts with +n is refused (XCU; ncurses takes them).
OURS_ONLY = [
	(["10,100"], ("10", False)),
	(["10,5"], ("", True)),
	(["0,4"], ("", True)),
	(["+4"], ("", True)),
	(["4,x"], ("", True)),
]


def decode(data: bytes) -> str:
	"""The stops a VT100 has after the bytes, past column 1."""
	position = 1
	stops = set()
	index = 0
	while index < len(data):
		byte = data[index]
		if byte == 13:
			position = 1
			index += 1
			continue
		if byte == 32:
			position += 1
			index += 1
			continue
		match = re.match(rb"\x1b\[(\d*)C", data[index:])
		if match:
			position += max(int(match.group(1) or b"1"), 1)
			index += match.end()
			continue
		if data[index:index + 4] == b"\x1b[3g":
			stops.clear()
			index += 4
			continue
		if data[index:index + 2] == b"\x1bH":
			stops.add(position)
			index += 2
			continue
		return "unknown " + repr(data[index:index + 8])
	return ",".join(str(stop) for stop in sorted(stops) if stop > 1)


def run(command: list[str], width: str, terminfo: bool) -> tuple[str, int]:
	"""Runs one side; its stops (or nothing on failure) and status."""
	env = {"PATH": "/usr/bin:/bin", "TERM": "vt100", "COLUMNS": width, "LC_ALL": "C"}
	if terminfo:
		env["TERMINFO"] = str(ROOT / "userland/base/terminfo")
	result = subprocess.run(command, env=env, stdin=subprocess.DEVNULL,
				stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=10)
	stops = ""
	if result.returncode == 0:
		stops = decode(result.stdout)
	return stops, result.returncode != 0


def main() -> int:
	ours = sys.argv[1]
	passed = 0
	total = 0
	for width in ("80", "120"):
		for case in CASES:
			total += 1
			mine = run([ours] + case, width, True)
			theirs = run(["/usr/bin/tabs"] + case, width, False)
			if mine == theirs:
				passed += 1
				continue
			print(f"FAIL COLUMNS={width} tabs {' '.join(case)}")
			print(f"  ours   {mine}")
			print(f"  ncurses {theirs}")
	for case, expected in OURS_ONLY:
		total += 1
		mine = run([ours] + case, "80", True)
		if mine == expected:
			passed += 1
			continue
		print(f"FAIL tabs {' '.join(case)}: ours {mine}, expected {expected}")
	print(f"tabs {passed}/{total}")
	return 0 if passed == total else 1


if __name__ == "__main__":
	sys.exit(main())
