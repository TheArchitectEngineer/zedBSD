#!/usr/bin/env python3
"""Turns the samples of one long transfer into a time series and a summary (ws220-p001).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    analyze.py DIRECTION PEER SERIES SS CSV

DIRECTION is down (the zedBSD machine receives) or up (it sends).  PEER is the
host end, ADDRESS:PORT (down: the server, 10.77.0.1:8080; up: the SSH client,
ADDRESS:* for any port).  SERIES is the machine's samples (the lines of
`sysctl net.tcpstats`, about one a second), SS the host's (`ss -tin` with a
"time=" line before each).  CSV gets a row per sample: seconds, the MB/s since
the last one, the machine's send and receive state and the change of its
counts, and the host's cwnd, ssthresh, rtt and retransmissions.  The summary
line at the end says: mean MB/s, the lowest and the median one-second rate,
the seconds below 1 MB/s and at 0, the longest run without progress, and the
totals of the counts that tell why.
"""
from __future__ import annotations

import re
import statistics
import sys

FIELD = re.compile(r"(\w+)=(\S+)")
COUNTS = ("retransmit_segments", "immediate_resends", "duplicate_acks_in", "acks_outside", "segment_send_failures",
	"send_refused", "send_buffer_sleeps", "out_of_order_in", "out_of_order_kept", "old_segments_in",
	"receive_refused", "zero_windows_in", "zero_windows_out", "window_updates_out")
STATE = ("send_window", "in_flight_bytes", "send_segments", "backoff", "rto_ms", "receive_window", "receive_bytes",
	"reorder_segments")


def connection_samples(path: str, direction: str, peer: str) -> list[dict]:
	"""The samples of the transfer's connection, with the pool's use of the same reading."""
	address, _, port = peer.partition(":")
	samples = []
	pool = {}
	for line in open(path, errors="replace"):
		if not line.startswith("net.tcpstats:"):
			continue
		fields = dict(FIELD.findall(line))
		if "sockets" in fields:
			pool = fields
			continue
		remote = fields.get("remote", "")
		remote_address, _, remote_port = remote.rpartition(":")
		if remote_address != address or (port not in ("*", "") and remote_port != port):
			continue
		fields["packets_used"] = pool.get("packets_used", "")
		fields["packet_alloc_failures"] = pool.get("packet_alloc_failures", "")
		samples.append(fields)

	# Several connections to the peer (an earlier run, the SSH session): the one that moved the most.
	by_local: dict[str, list[dict]] = {}
	for sample in samples:
		by_local.setdefault(sample["local"], []).append(sample)
	key = "data_bytes_in" if direction == "down" else "data_bytes_out"
	best = max(by_local.values(), key=lambda run: int(run[-1].get(key, 0)), default=[])
	return best


def host_samples(path: str) -> list[tuple[float, dict]]:
	"""The host's TCP state of the transfer at each time= mark (the first socket ss lists)."""
	result = []
	now = None
	taken = True
	try:
		lines = open(path, errors="replace").read().splitlines()
	except OSError:
		return result
	for line in lines:
		if line.startswith("time="):
			now = float(line[5:])
			taken = False
			continue
		if now is None or taken or "cwnd:" not in line:
			continue
		values = {}
		for name in ("cwnd", "ssthresh", "rtt", "retrans", "unacked", "rcv_space"):
			match = re.search(rf"\b{name}:(\S+)", line)
			if match:
				values[name] = match.group(1)
		result.append((now, values))
		taken = True
	return result


def main() -> int:
	if len(sys.argv) != 6:
		print(__doc__, file=sys.stderr)
		return 2
	direction, peer, series, ss, csv_path = sys.argv[1:]
	samples = connection_samples(series, direction, peer)
	if len(samples) < 2:
		print("analyze: summary samples=0 (no connection to the peer in the series)")
		return 1
	hosts = host_samples(ss)
	host_start = hosts[0][0] if hosts else 0.0

	rows = []
	start = int(samples[0]["time_ms"])
	moved_key = "data_bytes_in" if direction == "down" else "send_unacknowledged"
	for previous, current in zip(samples, samples[1:]):
		seconds = (int(current["time_ms"]) - int(previous["time_ms"])) / 1000.0
		if seconds <= 0:
			continue
		moved = int(current[moved_key]) - int(previous[moved_key])
		if direction == "up":
			moved %= 1 << 32
		row = {"t": (int(current["time_ms"]) - start) / 1000.0, "mbs": moved / 1048576.0 / seconds}
		for name in STATE:
			row[name] = current.get(name, "")
		for name in COUNTS:
			row["d_" + name] = int(current.get(name, 0)) - int(previous.get(name, 0))
		row["packets_used"] = current["packets_used"]
		# The host's sample nearest in time from the start of each series (the clocks are not the same).
		host = {}
		if hosts:
			target = host_start + row["t"]
			host = min(hosts, key=lambda item: abs(item[0] - target))[1]
		for name in ("cwnd", "ssthresh", "rtt", "retrans", "unacked"):
			row["host_" + name] = host.get(name, "")
		rows.append(row)

	with open(csv_path, "w") as output:
		names = list(rows[0].keys())
		output.write(",".join(names) + "\n")
		for row in rows:
			output.write(",".join(str(row[name]) for name in names) + "\n")

	# The summary.
	rates = [row["mbs"] for row in rows]
	total_seconds = (int(samples[-1]["time_ms"]) - start) / 1000.0
	first = int(samples[0][moved_key])
	last = int(samples[-1][moved_key])
	moved_total = (last - first) % (1 << 32) if direction == "up" else last - first
	longest = 0.0
	run = 0.0
	for row, previous_t in zip(rows, [0.0] + [row["t"] for row in rows]):
		if row["mbs"] == 0.0:
			run += row["t"] - previous_t
			longest = max(longest, run)
		else:
			run = 0.0
	totals = {name: sum(row["d_" + name] for row in rows) for name in COUNTS}
	print(f"analyze: summary direction={direction} seconds={total_seconds:.1f} mbytes={moved_total / 1048576.0:.1f} "
		f"mean_mbs={moved_total / 1048576.0 / max(total_seconds, 0.001):.2f} min_mbs={min(rates):.2f} "
		f"median_mbs={statistics.median(rates):.2f} below_1mbs={sum(1 for rate in rates if rate < 1.0)} "
		f"zero={sum(1 for rate in rates if rate == 0.0)} longest_zero_s={longest:.1f} "
		+ " ".join(f"{name}={value}" for name, value in totals.items()))
	return 0


if __name__ == "__main__":
	sys.exit(main())
