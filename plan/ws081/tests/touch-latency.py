#!/usr/bin/env python3
"""ws081-p016 (for p017, L3): the touch's latency and the inertial scroll's frame gaps from zdesktop's log.

zdesktop must run with --log-frames: it logs each finger's report ("KWL TOUCH report contact=N ... stamp_ms=S
host_ms=H", H the time the report was stamped, the same clock as at_ms) and each composed frame ("KWL COMPOSE
frame=F ... at_ms=A").

  latency   for each finger put down (a contact's first report, or its first after 300 ms without one), the time
            from its report to the first frame composed after it: the first frame the screen can show the answer in.
            It is the compositor's side of "a finger down to the screen answering"; a client that answers later
            shows in the frames after (p017 compares the pictures).
  inertia   after each stroke that moved (a drag or a fling), from its last report, the frames composed while the content still moves (frames closer than
            GAP_END ms to the one before, within FOLLOW ms): the longest interval between them.

  touch-latency.py LOG [--follow-ms=1500] [--gap-end-ms=250]
Prints one line per finger put down and a RESULT line: downs, p50, p95 and the longest latency, and the longest
inertial frame interval.
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import argparse
import re
import sys

REPORT = re.compile(r"KWL TOUCH report contact=(\d+) x=(-?\d+) y=(-?\d+) stamp_ms=(\d+) host_ms=(\d+)")
COMPOSE = re.compile(r"KWL COMPOSE frame=(\d+) .*at_ms=(\d+)")


def percentile(values, share):
	"""The value below which a share of the values lie (nearest rank)."""
	if not values:
		return None
	ordered = sorted(values)
	index = max(0, -(-int(share * 1000) * len(ordered) // 1000) - 1)
	return ordered[min(index, len(ordered) - 1)]


def moved(first, last):
	"""Tells whether a stroke moved (more than 40 pixels): a drag or a fling, not a tap."""
	return abs(last[1] - first[1]) + abs(last[2] - first[2]) > 40


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("log")
	parser.add_argument("--follow-ms", type=int, default=1500)
	parser.add_argument("--gap-end-ms", type=int, default=250)
	arguments = parser.parse_args()

	# The reports and the frames, in the log's order.
	reports = []
	frames = []
	with open(arguments.log, errors="replace") as log:
		for line in log:
			found = REPORT.search(line)
			if found:
				reports.append((int(found.group(1)), int(found.group(5)), int(found.group(2)), int(found.group(3))))
				continue
			found = COMPOSE.search(line)
			if found:
				frames.append(int(found.group(2)))
	frames.sort()

	# The fingers put down: a contact's first report, or its first after a pause; a stroke that moved ends in a glide.
	last_seen = {}
	started_at = {}
	downs = []
	ends = []
	for contact, host, x, y in reports:
		previous = last_seen.get(contact)
		if previous is None or host - previous[0] > 300:
			downs.append(host)
			if previous is not None and moved(started_at[contact], previous):
				ends.append(previous[0])
			started_at[contact] = (host, x, y)
		last_seen[contact] = (host, x, y)
	for contact, last in last_seen.items():
		if moved(started_at[contact], last):
			ends.append(last[0])

	# The latency: from each down to the first frame composed at or after it.
	latencies = []
	position = 0
	for down in sorted(downs):
		while position < len(frames) and frames[position] < down:
			position += 1
		if position == len(frames):
			break
		latency = frames[position] - down
		latencies.append(latency)
		print("down host_ms=%d frame_ms=%d latency_ms=%d" % (down, frames[position], latency))

	# The inertia: after each last report, the frames that keep coming closer than gap_end apart.
	longest_gap = 0
	for end in ends:
		following = [frame for frame in frames if end <= frame <= end + arguments.follow_ms]
		for before, after in zip(following, following[1:]):
			gap = after - before
			if gap > arguments.gap_end_ms:
				break
			longest_gap = max(longest_gap, gap)

	# The result.
	if latencies:
		print("RESULT downs=%d p50_ms=%d p95_ms=%d max_ms=%d inertia_max_gap_ms=%d frames=%d" %
		      (len(latencies), percentile(latencies, 0.5), percentile(latencies, 0.95), max(latencies), longest_gap,
		       len(frames)))
	else:
		print("RESULT downs=0 frames=%d (no finger's report: is zdesktop run with --log-frames?)" % len(frames))
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
