#!/usr/bin/env python3
"""Turns the samples of one long transfer into a time series and a summary (ws220-p001, p002).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    analyze.py DIRECTION PEER SERIES SS CSV [--pcap FILE (--port PORT | --sender-port PORT)]

DIRECTION is down (the zedBSD machine receives) or up (it sends).  PEER is the
host end, ADDRESS:PORT (ADDRESS:* for any port).  SERIES is the machine's
samples (the lines of `sysctl net.tcpstats`, about one a second), SS the
host's (`ss -tin` with a "time=" line before each).  CSV gets a row per sample:
seconds, the MB/s since the last one, the machine's send and receive state and
the change of its counts, and the host's cwnd, ssthresh, rtt and
retransmissions.  The summary line "analyze: summary ..." counts only the
transfer itself: from the first sample that moved data to the last one (the
key exchange before and the close after are left out), and its bytes are the
sum of the samples' (so a transfer past 4 GiB of sequence space is counted
whole).

With --pcap (a capture of the transfer on the host's side, tcpdump -w, PORT
the host's port of the connection, or --sender-port the data sender's port
when the host's is not known, as for the upload over SSH) the line "analyze: wire ..." gives what a
one-second sample cannot see: the receiver's acknowledgements over time, cut
into 100 ms steps, the longest time the acknowledged byte did not move
(longest_stall_ms, the measure of a stall of half a second or two), the
100 ms steps without progress, the one-second rates (lowest, the 1st
percentile and the median, the seconds below 1 MB/s after the first five),
and the data segments the sender sent again.  Sequence numbers are unwrapped,
so a transfer across the 2^32 wrap is measured whole (crossed_wrap=1 says
the receiver's acknowledgements crossed it).
"""
from __future__ import annotations

import re
import statistics
import struct
import sys

FIELD = re.compile(r"(\w+)=(\S+)")
COUNTS = ("retransmit_segments", "immediate_resends", "duplicate_acks_in", "acks_outside", "segment_send_failures",
	"send_refused", "send_buffer_sleeps", "out_of_order_in", "out_of_order_kept", "old_segments_in",
	"receive_refused", "zero_windows_in", "zero_windows_out", "window_updates_out", "fast_retransmits")
STATE = ("send_window", "in_flight_bytes", "send_segments", "backoff", "rto_ms", "srtt_us", "rttvar_us", "rtt_samples",
	"receive_window", "receive_bytes", "reorder_segments")
STEP = 0.1
HAVE_SEQUENCE = 0x20
HALF = 1 << 31
WRAP = 1 << 32


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


def series(direction: str, peer: str, series_path: str, ss: str, csv_path: str) -> int:
	"""The machine's one-second samples: the CSV and the summary line of the transfer."""
	samples = connection_samples(series_path, direction, peer)
	if len(samples) < 2:
		print("analyze: summary samples=0 (no connection to the peer in the series)")
		return 1
	hosts = host_samples(ss)
	host_start = hosts[0][0] if hosts else 0.0

	# The bytes moved: the in-order bytes taken (down), the sequence space acknowledged (up, modulo 2^32 per sample).
	moved_key = "data_bytes_in" if direction == "down" else "send_unacknowledged"
	if direction == "up" and int(samples[0].get("features", "0"), 0) & HAVE_SEQUENCE == 0:
		print("analyze: note: the samples have no sequence numbers (sysctl not run as root); up counts data_bytes_out")
		moved_key = "data_bytes_out"
	rows = []
	start = int(samples[0]["time_ms"])
	for previous, current in zip(samples, samples[1:]):
		seconds = (int(current["time_ms"]) - int(previous["time_ms"])) / 1000.0
		if seconds <= 0:
			continue
		moved = int(current[moved_key]) - int(previous[moved_key])
		if moved_key == "send_unacknowledged":
			moved %= WRAP
		row = {"t": (int(current["time_ms"]) - start) / 1000.0, "seconds": seconds, "bytes": moved,
			"mbs": moved / 1048576.0 / seconds}
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
	if not rows:
		print("analyze: summary samples=0 (no interval between samples)")
		return 1

	with open(csv_path, "w") as output:
		names = list(rows[0].keys())
		output.write(",".join(names) + "\n")
		for row in rows:
			output.write(",".join(str(row[name]) for name in names) + "\n")

	# The transfer: from the first row that moved data to the last; the rows before and after are not its.
	moving = [index for index, row in enumerate(rows) if row["bytes"] > 0]
	if not moving:
		print(f"analyze: summary direction={direction} mbytes=0 (no sample moved data)")
		return 1
	transfer = rows[moving[0]:moving[-1] + 1]
	rates = [row["mbs"] for row in transfer]
	total_seconds = sum(row["seconds"] for row in transfer)
	moved_total = sum(row["bytes"] for row in transfer)
	longest = 0.0
	run = 0.0
	for row in transfer:
		if row["bytes"] == 0:
			run += row["seconds"]
			longest = max(longest, run)
		else:
			run = 0.0
	after_head = [row["mbs"] for row in transfer if row["t"] - transfer[0]["t"] >= 5.0]
	totals = {name: sum(row["d_" + name] for row in transfer) for name in COUNTS}
	print(f"analyze: summary direction={direction} seconds={total_seconds:.1f} mbytes={moved_total / 1048576.0:.1f} "
		f"mean_mbs={moved_total / 1048576.0 / max(total_seconds, 0.001):.2f} min_mbs={min(rates):.2f} "
		f"median_mbs={statistics.median(rates):.2f} below_1mbs={sum(1 for rate in rates if rate < 1.0)} "
		f"below_1mbs_after_5s={sum(1 for rate in after_head if rate < 1.0)} "
		f"zero={sum(1 for rate in rates if rate == 0.0)} longest_zero_s={longest:.1f} "
		+ " ".join(f"{name}={value}" for name, value in totals.items()))
	# The round-trip estimate over the transfer (ws220-p004), from the samples that have one.
	estimates = [row for row in transfer if str(row.get("rtt_samples", "")).isdigit() and int(row["rtt_samples"]) > 0]
	if estimates:
		srtts = [int(row["srtt_us"]) for row in estimates]
		rtos = [int(row["rto_ms"]) for row in estimates if str(row.get("rto_ms", "")).isdigit()]
		print(f"analyze: rtt samples={len(estimates)} srtt_us_median={int(statistics.median(srtts))} "
			f"srtt_us_max={max(srtts)} rto_ms_median={int(statistics.median(rtos)) if rtos else 0} "
			f"rto_ms_max={max(rtos) if rtos else 0} backoff_max={max(int(row['backoff'] or 0) for row in estimates)}")
	return 0


def pcap_packets(path: str):
	"""Yields (time, source port, destination port, sequence, acknowledgement, flags, payload length) of each TCP/IPv4 packet."""
	with open(path, "rb") as capture:
		head = capture.read(24)
		if len(head) < 24:
			return
		order = "<"
		magic = struct.unpack("<I", head[:4])[0]
		if magic not in (0xa1b2c3d4, 0xa1b23c4d):
			order = ">"
			magic = struct.unpack(">I", head[:4])[0]
		divisor = 1e9 if magic == 0xa1b23c4d else 1e6
		link = struct.unpack(order + "I", head[20:24])[0]
		while True:
			record = capture.read(16)
			if len(record) < 16:
				return
			seconds, fraction, captured, _ = struct.unpack(order + "IIII", record)
			data = capture.read(captured)
			if len(data) < captured:
				return
			# The IPv4 header after the link header: Ethernet (1) or Linux cooked (113).
			if link == 1:
				if len(data) < 14 or data[12:14] != b"\x08\x00":
					continue
				ip = data[14:]
			elif link == 113:
				if len(data) < 16 or data[14:16] != b"\x08\x00":
					continue
				ip = data[16:]
			else:
				return
			if len(ip) < 20 or ip[0] >> 4 != 4 or ip[9] != 6:
				continue
			ip_header = (ip[0] & 0x0f) * 4
			total = struct.unpack(">H", ip[2:4])[0]
			tcp = ip[ip_header:]
			if len(tcp) < 20:
				continue
			source, destination, sequence, acknowledgement = struct.unpack(">HHII", tcp[:12])
			tcp_header = (tcp[12] >> 4) * 4
			payload = total - ip_header - tcp_header
			yield (seconds + fraction / divisor, source, destination, sequence, acknowledgement, tcp[13], max(payload, 0))


def wire(direction: str, path: str, port: int, port_is_sender: bool) -> int:
	"""The receiver's acknowledgements on the wire: the stalls and rates a one-second sample cannot see."""
	acknowledgements = []
	sent_end = None
	resent = 0
	data_segments = 0
	base_ack = None
	last_ack = 0
	unwrapped = 0
	for time, source, destination, sequence, acknowledgement, flags, payload in pcap_packets(path):
		# The data comes from the host's port (down) or goes to it (up); the acknowledgements go the other way.
		if source != port and destination != port:
			continue
		if port_is_sender or direction == "down":
			from_sender = source == port
		else:
			from_sender = destination == port
		if from_sender:
			# A data segment that ends at or before the farthest end sent so far was sent again.
			if payload > 0:
				data_segments += 1
				end = (sequence + payload) % WRAP
				ahead = (end - sent_end) % WRAP if sent_end is not None else 1
				if ahead == 0 or ahead >= HALF:
					resent += 1
				else:
					sent_end = end
			continue
		if not flags & 0x10:
			continue
		# The receiver's acknowledgement, unwrapped: only a step forward (less than half the space) counts.
		if base_ack is None:
			base_ack = acknowledgement
			last_ack = acknowledgement
			acknowledgements.append((time, 0))
			continue
		step = (acknowledgement - last_ack) % WRAP
		if step == 0 or step >= HALF:
			continue
		unwrapped += step
		last_ack = acknowledgement
		acknowledgements.append((time, unwrapped))
	if len(acknowledgements) < 3:
		print(f"analyze: wire direction={direction} port={port} acknowledgements={len(acknowledgements)} (too few)")
		return 1

	# The transfer: from the first acknowledgement that moved (the handshake's left out) to the last.
	first = acknowledgements[1]
	last = acknowledgements[-1]
	longest = 0.0
	for (before, _), (after, _) in zip(acknowledgements[1:], acknowledgements[2:]):
		longest = max(longest, after - before)
	duration = max(last[0] - first[0], 0.001)
	moved = last[1] - first[1]

	# Progress in 100 ms steps and in whole seconds, from the first acknowledgement that moved.
	steps = int(duration / STEP) + 1
	per_step = [0] * steps
	previous = first[1]
	for time, value in acknowledgements[2:]:
		index = min(int((time - first[0]) / STEP), steps - 1)
		per_step[index] += value - previous
		previous = value
	idle_steps = sum(1 for amount in per_step if amount == 0)
	longest_idle = 0
	run = 0
	for amount in per_step:
		run = run + 1 if amount == 0 else 0
		longest_idle = max(longest_idle, run)
	per_second = [sum(per_step[index:index + 10]) / 1048576.0 for index in range(0, steps - 9, 10)]
	if not per_second:
		per_second = [moved / 1048576.0 / duration]
	ordered = sorted(per_second)
	percentile_1 = ordered[int(len(ordered) * 0.01)]
	after_head = per_second[5:]
	crossed = int(base_ack + unwrapped >= WRAP)
	print(f"analyze: wire direction={direction} port={port} seconds={duration:.2f} mbytes={moved / 1048576.0:.1f} "
		f"mean_mbs={moved / 1048576.0 / duration:.2f} min_1s_mbs={ordered[0]:.2f} p1_1s_mbs={percentile_1:.2f} "
		f"median_1s_mbs={statistics.median(per_second):.2f} "
		f"below_1mbs_after_5s={sum(1 for rate in after_head if rate < 1.0)} "
		f"longest_stall_ms={longest * 1000.0:.0f} idle_100ms_steps={idle_steps} longest_idle_100ms_steps={longest_idle} "
		f"data_segments={data_segments} resent_segments={resent} crossed_wrap={crossed}")
	return 0


def main() -> int:
	arguments = sys.argv[1:]
	pcap = None
	port = None
	port_is_sender = False
	if "--pcap" in arguments:
		index = arguments.index("--pcap")
		pcap = arguments[index + 1]
		del arguments[index:index + 2]
	if "--port" in arguments:
		index = arguments.index("--port")
		port = int(arguments[index + 1])
		del arguments[index:index + 2]
	if "--sender-port" in arguments:
		index = arguments.index("--sender-port")
		port = int(arguments[index + 1])
		port_is_sender = True
		del arguments[index:index + 2]
	if len(arguments) != 5 or (pcap is not None and port is None):
		print(__doc__, file=sys.stderr)
		return 2
	direction, peer, series_path, ss, csv_path = arguments
	status = series(direction, peer, series_path, ss, csv_path)
	if pcap is not None:
		status = max(status, wire(direction, pcap, port, port_is_sender))
	return status


if __name__ == "__main__":
	sys.exit(main())
