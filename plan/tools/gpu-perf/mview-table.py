#!/usr/bin/env python3
"""The table of measure.sh bench: one row a run from mview's MVIEW SPIN and MVIEW STAGES lines.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    mview-table.py BENCH.LOG

BENCH.LOG holds "RUN LABEL" lines, each followed by that run's mview output.
"""
import re
import sys


def main() -> int:
	rows = []
	label = None
	values = {}
	with open(sys.argv[1], encoding="utf-8", errors="replace") as log:
		for line in log:
			if line.startswith("RUN "):
				if label is not None:
					rows.append((label, values))
				label = line.split()[1]
				values = {}
				continue
			for key in ("fps", "frame", "record", "submit", "present", "fence", "acquire"):
				if line.startswith("MVIEW SPIN") and key == "fps" or line.startswith("MVIEW STAGES") and key != "fps":
					match = re.search(r"\b%s=([0-9.]+)" % key, line)
					if match:
						values[key] = match.group(1)
			if line.startswith("MVIEW FAILED"):
				values["failed"] = re.sub(r"^MVIEW FAILED run=\S+ ", "", line.strip())
	if label is not None:
		rows.append((label, values))
	print("%-12s %9s %9s %9s %9s %9s %9s %9s" % ("run", "fps", "frame_ms", "acquire", "record", "submit", "present", "fence"))
	for label, values in rows:
		if "failed" in values:
			print("%-12s FAILED %s" % (label, values["failed"]))
			continue
		print("%-12s %9s %9s %9s %9s %9s %9s %9s" % (label, values.get("fps", "-"), values.get("frame", "-").rstrip("ms"),
			values.get("acquire", "-"), values.get("record", "-"), values.get("submit", "-"), values.get("present", "-"),
			values.get("fence", "-")))
	return 0


if __name__ == "__main__":
	sys.exit(main())
