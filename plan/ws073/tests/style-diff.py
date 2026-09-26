#!/usr/bin/env python3
"""Shows the style-check findings on the lines a change added or modified.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    python3 plan/ws073/tests/style-diff.py [--base REV] FILE...

The legacy files carry many findings of their own; the rule for a fix in
them is "not worse", which is checked on the changed lines.  BASE defaults
to HEAD (the working tree against the last commit).
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def changed_lines(base: str, path: str) -> set[int]:
	"""Returns the line numbers of the new file that the diff touches."""
	diff = subprocess.run(["git", "-C", str(ROOT), "diff", "-U0", base, "--", path],
			      check=True, capture_output=True, text=True).stdout
	lines = set()
	for match in re.finditer(r"^@@ -\S+ \+(\d+)(?:,(\d+))? @@", diff, re.M):
		start = int(match.group(1))
		count = int(match.group(2)) if match.group(2) is not None else 1
		lines.update(range(start, start + count))
	return lines


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--base", default="HEAD")
	parser.add_argument("files", nargs="+")
	arguments = parser.parse_args()
	found = 0
	for path in arguments.files:
		lines = changed_lines(arguments.base, path)
		report = subprocess.run([sys.executable, str(ROOT / "plan/tools/style-check.py"), path],
					capture_output=True, text=True, cwd=ROOT).stdout
		for line in report.splitlines():
			parts = line.split(":", 2)
			if len(parts) == 3 and parts[1].isdigit() and int(parts[1]) in lines:
				print(line)
				found += 1
	print(f"findings on changed lines: {found}")
	return 1 if found else 0


if __name__ == "__main__":
	sys.exit(main())
