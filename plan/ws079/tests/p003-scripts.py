#!/usr/bin/env python3
"""Writes the peninject scripts of the WS079 p003 guest test.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

The pen is declared 21600 x 13500 and the compositor runs at 1280 x 800, so a
screen pixel (x, y) is the pen position (x * 21600 / 1279, y * 13500 / 799).
Every script starts with a pause so the compositor's input scan (every 2 s)
finds the new node before the pen moves.

    p003-scripts.py OUTDIR
"""
import sys
from pathlib import Path

WIDTH = 1280
HEIGHT = 800
PEN_X = 21600
PEN_Y = 13500


def raw(x: float, y: float) -> tuple[int, int]:
	"""Returns the pen position of a screen pixel."""
	return round(x * PEN_X / (WIDTH - 1)), round(y * PEN_Y / (HEIGHT - 1))


def line(command: str, x: float, y: float, *rest: int) -> str:
	"""Returns one positional command at a screen pixel."""
	rx, ry = raw(x, y)
	return " ".join([command, str(rx), str(ry)] + [str(value) for value in rest])


def header() -> list[str]:
	"""Returns the declaration and the pause for the compositor's scan."""
	return ["size %d %d" % (PEN_X, PEN_Y), "wait 3000"]


def tablet_script() -> str:
	"""The tablet client: pressure and tilt, a barrel button, the eraser, the grab, leaving."""
	lines = header()
	lines.append("tool pen")
	# Hover over the window: proximity_in.
	lines.append(line("hover", 640, 300, -30, -20))
	lines.append("wait 300")
	# A stroke left to right: pressure 0 -> 4095, tilt -30 -> +30 degrees.
	steps = 40
	lines.append(line("down", 500, 380, 0, -30, -20))
	for step in range(1, steps + 1):
		x = 500 + 280 * step / steps
		pressure = round(4095 * step / steps)
		tilt = round(-30 + 60 * step / steps)
		lines.append(line("move", x, 380, pressure, tilt, 20))
		lines.append("wait 20")
	# A barrel button while touching.
	lines.append("button stylus 1")
	lines.append("button stylus 0")
	lines.append("up")
	lines.append("wait 300")
	# The eraser end: its own tool.
	lines.append("tool rubber")
	lines.append(line("down", 520, 450, 2048))
	for step in range(1, 21):
		lines.append(line("move", 520 + 240 * step / 20, 450, 2048))
		lines.append("wait 20")
	lines.append("up")
	lines.append("wait 300")
	# The implicit grab: a touch that starts on the window keeps it off the window.
	lines.append("tool pen")
	lines.append(line("down", 900, 520, 3000))
	for step in range(1, 11):
		lines.append(line("move", 900 + 25 * step, 520, 3000))
		lines.append("wait 20")
	lines.append("lift")
	lines.append("wait 300")
	# Off the window after the lift: proximity_out; back on it: proximity_in; then out of range.
	lines.append(line("hover", 640, 300))
	lines.append("wait 300")
	lines.append("up")
	lines.append("hold 500")
	return "\n".join(lines) + "\n"


def pointer_script() -> str:
	"""A client without the tablet: the touch is BTN_LEFT, the barrels BTN_RIGHT and BTN_MIDDLE."""
	lines = header()
	lines.append("tool pen")
	lines.append(line("hover", 640, 360))
	lines.append("wait 300")
	lines.append(line("down", 560, 400, 2000))
	for step in range(1, 21):
		lines.append(line("move", 560 + 160 * step / 20, 400, 2000))
		lines.append("wait 20")
	lines.append("lift")
	lines.append("wait 200")
	lines.append("button stylus 1")
	lines.append("button stylus 0")
	lines.append("wait 200")
	lines.append("button stylus2 1")
	lines.append("button stylus2 0")
	lines.append("up")
	lines.append("hold 500")
	return "\n".join(lines) + "\n"


def home_script() -> str:
	"""zdesktop's own UI takes the pen as the pointer: a tap on the launcher opens App Home."""
	lines = header()
	lines.append("tool pen")
	lines.append(line("hover", 16, 16))
	lines.append("wait 300")
	lines.append(line("down", 16, 16, 2000))
	lines.append("wait 150")
	lines.append("lift")
	lines.append("up")
	lines.append("hold 1500")
	return "\n".join(lines) + "\n"


def select_script(x0: float, x1: float, y: float) -> str:
	"""A drag with the tip from x0 to x1 on row y, then the second barrel button (BTN_MIDDLE: paste)."""
	lines = header()
	lines.append("tool pen")
	lines.append(line("hover", x0, y))
	lines.append("wait 300")
	lines.append(line("down", x0, y, 2000))
	for step in range(1, 21):
		lines.append(line("move", x0 + (x1 - x0) * step / 20, y, 2000))
		lines.append("wait 20")
	lines.append("lift")
	lines.append("wait 300")
	lines.append("button stylus2 1")
	lines.append("button stylus2 0")
	lines.append("up")
	lines.append("hold 1000")
	return "\n".join(lines) + "\n"


def main() -> int:
	out = Path(sys.argv[1])
	out.mkdir(parents=True, exist_ok=True)
	(out / "tablet.pen").write_text(tablet_script())
	(out / "pointer.pen").write_text(pointer_script())
	(out / "home.pen").write_text(home_script())
	x0 = float(sys.argv[2]) if len(sys.argv) > 4 else 300.0
	x1 = float(sys.argv[3]) if len(sys.argv) > 4 else 500.0
	y = float(sys.argv[4]) if len(sys.argv) > 4 else 200.0
	(out / "select.pen").write_text(select_script(x0, x1, y))
	return 0


if __name__ == "__main__":
	sys.exit(main())
