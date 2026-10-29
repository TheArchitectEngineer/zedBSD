#!/usr/bin/env python3
"""Samples where a running guest's CPUs are: each vCPU's RIP through QMP (info registers -a), many times, mapped to
the kernel's functions with VMUNIX's symbols.  For finding where a slow load spends its time (a kernel that waits
shows its idle loop; one that works shows the functions).

    python3 plan/ws307/tests/rip-sample.py QMP_SOCKET VMUNIX [SAMPLES [INTERVAL]]

Prints the functions by share of the samples, and the share of samples outside the kernel's text.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import bisect
import collections
import json
import re
import socket
import subprocess
import sys
import time


def symbols(vmunix):
	out = subprocess.run(["nm", "-n", "--defined-only", vmunix], capture_output=True, text=True, check=True).stdout
	addrs, names = [], []
	for line in out.splitlines():
		f = line.split()
		if len(f) == 3 and f[1] in "tTwW":
			addrs.append(int(f[0], 16))
			names.append(f[2])
	return addrs, names


class Qmp:
	def __init__(self, path):
		self.sock = socket.socket(socket.AF_UNIX)
		self.sock.connect(path)
		self.file = self.sock.makefile("rw")
		self.file.readline()
		self.call("qmp_capabilities")

	def call(self, command, arguments=None):
		message = {"execute": command}
		if arguments:
			message["arguments"] = arguments
		self.file.write(json.dumps(message) + "\n")
		self.file.flush()
		while True:
			reply = json.loads(self.file.readline())
			if "return" in reply or "error" in reply:
				return reply


def main():
	if len(sys.argv) < 3:
		print(__doc__, file=sys.stderr)
		return 2
	qmp = Qmp(sys.argv[1])
	addrs, names = symbols(sys.argv[2])
	samples = int(sys.argv[3]) if len(sys.argv) > 3 else 300
	interval = float(sys.argv[4]) if len(sys.argv) > 4 else 0.05
	count = collections.Counter()
	total = 0
	for _ in range(samples):
		text = qmp.call("human-monitor-command", {"command-line": "info registers -a"}).get("return", "")
		for rip in re.findall(r"RIP=([0-9a-f]{16})", text):
			value = int(rip, 16)
			i = bisect.bisect_right(addrs, value) - 1
			name = names[i] if i >= 0 and value < (1 << 64) and value >= addrs[0] else "(outside)"
			if value < 0xffff800000000000:
				name = "(user)"
			count[name] += 1
			total += 1
		time.sleep(interval)
	for name, n in count.most_common(40):
		print(f"{100.0 * n / total:6.2f}% {n:6d} {name}")
	print(f"total {total}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
