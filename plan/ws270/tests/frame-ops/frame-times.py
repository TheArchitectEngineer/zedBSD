#!/usr/bin/env python3
"""The time of each of Keiland's frames, operation by operation, from its session log.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    frame-times.py SESSION.LOG MARKS [--frames]

MARKS is the file frame-ops.sh writes on the board: one line per step,
"NAME LINE", LINE being the number of lines the session log had when the
step began.  A step's frames are those whose "KWL LAT draw" line lies
between its mark and the next.

For each frame (the compositor's KWL LAT lines, times in ms):
    pace     acquired -> draw: the wait for the clients told of the last frame
    cpu      draw -> recorded: the walk and the recording
    submit   recorded -> queued: vkQueueSubmit (the GPU's run of the frame)
    present  queued -> submit: vkQueuePresentKHR
    work     draw -> submit: the frame's own time
    period   from the previous frame's draw, when that was less than 100 ms
             before (frames of one movement; a frame after a pause has none)
    shown    submit -> the frame's completion on the main loop
The KWL PASSES line gives the pixels the frame drew (area) and loaded (load);
dirty 1 is a frame drawn whole.

The summary counts the frames whose work, or whose period, is over 16 ms.
--frames prints every frame too.  The step's KWL PERF compose and rec lines
(5 s windows that end in the step) are printed under it.
"""
from __future__ import annotations

import re
import sys

BUDGET_MS = 16.0
MOVEMENT_MS = 100.0

LAT = re.compile(r"KWL LAT (acquired|pace|draw|recorded|queued|submit|shown) frame=(\d+)(.*)")
FIELD = re.compile(r"(\w+)=(-?[\d.]+)")
PASSES = re.compile(r"KWL PASSES frame=(\d+) .*?area_px=(\d+) load_px=(\d+)")


def fields(text: str) -> dict[str, float]:
	"""The name=number pairs of a line's rest."""
	return {name: float(value) for name, value in FIELD.findall(text)}


def read_frames(lines: list[str]) -> dict[int, dict]:
	"""Each frame's times (ms) and the line of its draw."""
	frames: dict[int, dict] = {}
	for number, line in enumerate(lines, 1):
		match = LAT.search(line)
		if match:
			kind, frame, rest = match.group(1), int(match.group(2)), match.group(3)
			entry = frames.setdefault(frame, {"frame": frame})
			values = fields(rest)
			if "at_us" in values:
				entry[kind] = values["at_us"] / 1000.0
			if kind == "draw":
				entry["line"] = number
				entry["dirty"] = int(values.get("dirty", 0))
			if kind == "pace":
				entry["waited"] = values.get("waited_ms", 0.0)
			continue
		match = PASSES.search(line)
		if match:
			entry = frames.setdefault(int(match.group(1)), {"frame": int(match.group(1))})
			entry["area"] = int(match.group(2))
			entry["load"] = int(match.group(3))
	return frames


def span(entry: dict, start: str, end: str) -> float | None:
	"""The time from one of a frame's points to another, when both were logged."""
	if start in entry and end in entry:
		return entry[end] - entry[start]
	return None


def text(value: float | None) -> str:
	"""A time for the table."""
	return "-" if value is None else f"{value:.0f}"


def main() -> int:
	args = [word for word in sys.argv[1:] if not word.startswith("--")]
	every = "--frames" in sys.argv
	if len(args) != 2:
		print(__doc__.strip().splitlines()[2], file=sys.stderr)
		return 2
	with open(args[0], encoding="utf-8", errors="replace") as log:
		lines = log.read().splitlines()
	marks = []
	with open(args[1], encoding="utf-8") as file:
		for row in file:
			words = row.split()
			if len(words) >= 2 and words[-1].isdigit():
				marks.append((" ".join(words[:-1]), int(words[-1])))
	frames = read_frames(lines)
	ordered = sorted((entry for entry in frames.values() if "line" in entry), key=lambda entry: entry["line"])
	previous = None
	for entry in ordered:
		entry["period"] = None
		if previous is not None and "draw" in previous and "draw" in entry:
			gap = entry["draw"] - previous["draw"]
			if gap < MOVEMENT_MS:
				entry["period"] = gap
		previous = entry

	print(f"{'step':<14} {'frames':>6} {'whole':>5} {'work>16':>7} {'period>16':>9} "
		f"{'work p50/max':>12} {'submit p50/max':>14} {'period p50/max':>14} {'fps':>5}")
	for index, (name, start) in enumerate(marks):
		end = marks[index + 1][1] if index + 1 < len(marks) else len(lines) + 1
		chosen = [entry for entry in ordered if start < entry["line"] <= end]
		works = [value for value in (span(entry, "draw", "submit") for entry in chosen) if value is not None]
		submits = [value for value in (span(entry, "recorded", "queued") for entry in chosen) if value is not None]
		periods = [entry["period"] for entry in chosen if entry["period"] is not None]
		whole = sum(1 for entry in chosen if entry.get("dirty"))

		def stat(values: list[float]) -> str:
			if not values:
				return "-"
			ordered_values = sorted(values)
			return f"{ordered_values[len(ordered_values) // 2]:.0f}/{ordered_values[-1]:.0f}"

		fps = "-"
		if periods:
			fps = f"{1000.0 * len(periods) / sum(periods):.0f}"
		print(f"{name:<14} {len(chosen):>6} {whole:>5} {sum(1 for value in works if value > BUDGET_MS):>7} "
			f"{sum(1 for value in periods if value > BUDGET_MS + 0.7):>9} {stat(works):>12} {stat(submits):>14} "
			f"{stat(periods):>14} {fps:>5}")
		if every:
			print(f"  {'frame':>6} {'dirty':>5} {'area':>8} {'load':>8} {'pace':>4} {'cpu':>4} {'submit':>6} "
				f"{'present':>7} {'work':>4} {'period':>6} {'shown':>5}")
			for entry in chosen:
				print(f"  {entry['frame']:>6} {entry.get('dirty', '-'):>5} {entry.get('area', '-'):>8} "
					f"{entry.get('load', '-'):>8} {text(span(entry, 'acquired', 'draw')):>4} "
					f"{text(span(entry, 'draw', 'recorded')):>4} {text(span(entry, 'recorded', 'queued')):>6} "
					f"{text(span(entry, 'queued', 'submit')):>7} {text(span(entry, 'draw', 'submit')):>4} "
					f"{text(entry['period']):>6} {text(span(entry, 'submit', 'shown')):>5}")
			for line in lines[start:end - 1]:
				if line.startswith("KWL PERF compose") or line.startswith("KWL PERF rec "):
					print(f"  {line}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
