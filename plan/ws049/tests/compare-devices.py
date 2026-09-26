#!/usr/bin/env python3
"""Compares device evaluations between aml-host and acpiexec.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    plan/ws049/tests/compare-devices.py NAME TABLE...

Evaluates _HID _CID _UID _ADR _STA _CRS of every device with aml-host
--devices, then the same paths in the same order with acpiexec (one
process, so side effects happen in the same order), and compares the
results.  Both simulate the address spaces as memory that reads zero until
written.  Writes build/ws049/devices/NAME-{ours,oracle}.txt and NAME.diff;
exits 0 when every result is the same.
"""
import difflib
import os
import re
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "plan", "ws049", "tests"))


def parse_value(lines, index):
	"""Parses one acpiexec value starting at lines[index]."""
	line = lines[index].strip()
	match = re.match(r"\[Integer\] = ([0-9A-F]+)", line)
	if match:
		return "Integer 0x%X" % int(match.group(1), 16), index + 1
	match = re.match(r'\[String\] Length [0-9A-F]+ = "(.*)"$', line)
	if match:
		return 'String "%s"' % match.group(1), index + 1
	match = re.match(r"\[Buffer\] Length ([0-9A-F]+) =(.*)$", line)
	if match:
		length = int(match.group(1), 16)
		data = []
		# A short buffer's one dump line follows the "=" on the same line.
		same_line = re.match(r"\s+[0-9A-F]{4}: ((?:[0-9A-F]{2} )+)", match.group(2))
		if same_line:
			data.extend(same_line.group(1).split())
		index += 1
		while len(data) < length and index < len(lines):
			dump = re.match(r"\s+[0-9A-F]{4}: ((?:[0-9A-F]{2} )+)", lines[index])
			if not dump:
				break
			data.extend(dump.group(1).split())
			index += 1
		return " ".join(["Buffer [%d]" % length] + data[:length]), index
	match = re.match(r"\[Package\] Contains (\d+) Elements:", line)
	if match:
		count = int(match.group(1))
		elements = []
		index += 1
		for _ in range(count):
			value, index = parse_value(lines, index)
			elements.append(value)
		return "Package [%d] {%s }" % (count, ",".join(" " + element for element in elements)), index
	match = re.match(r"\[Object Reference\] = .*Name (\S+)", line)
	if match:
		return "Reference " + match.group(1), index + 1
	return "Unknown(%s)" % line, index + 1


def oracle(paths, tables):
	"""Evaluates the paths with acpiexec in one process, fed on standard input."""
	commands = "".join("evaluate %s\n" % path for path in paths) + "quit\n"
	output = subprocess.run(
		["acpiexec", "-l", "-di", "-dr"] + tables,
		input=commands, capture_output=True, text=True).stdout
	lines = output.split("\n")
	results = []
	index = 0
	while index < len(lines):
		match = re.match(r"Evaluation of (\S+) returned object", lines[index])
		if match:
			value, index = parse_value(lines, index + 1)
			results.append("%s = %s" % (match.group(1), value))
			continue
		match = re.match(r"Evaluation of (\S+) failed", lines[index])
		if match:
			results.append("%s = error" % match.group(1))
		index += 1
	return results


def main():
	if len(sys.argv) < 3:
		print(__doc__, file=sys.stderr)
		return 2
	name = sys.argv[1]
	tables = sys.argv[2:]
	out = os.path.join(REPO, "build", "ws049", "devices")
	os.makedirs(out, exist_ok=True)
	host = os.path.join(REPO, "build", "ws049", "host", "aml-host")
	raw = subprocess.run([host, "--quiet", "--devices"] + tables, capture_output=True, text=True).stdout
	ours = []
	paths = []
	for line in raw.split("\n"):
		match = re.match(r"(\\\S+) = (.*)$", line)
		if not match:
			continue
		paths.append(match.group(1))
		value = match.group(2)
		value = re.sub(r"^error -?\d+$", "error", value)
		value = re.sub(r"Reference \\\S*?([A-Z_][A-Z0-9_]{3})\b", r"Reference \1", value)
		ours.append("%s = %s" % (match.group(1), value))
	theirs = oracle(paths, tables)
	with open(os.path.join(out, name + "-ours.txt"), "w") as stream:
		stream.write("\n".join(ours) + "\n")
	with open(os.path.join(out, name + "-oracle.txt"), "w") as stream:
		stream.write("\n".join(theirs) + "\n")
	diff = list(difflib.unified_diff(theirs, ours, "acpiexec", "aml-host", lineterm="", n=0))
	with open(os.path.join(out, name + ".diff"), "w") as stream:
		stream.write("\n".join(diff) + "\n")
	if not diff:
		print("%s: same (%d evaluations)" % (name, len(ours)))
		return 0
	changed = len([line for line in diff if line.startswith("+") and not line.startswith("+++")])
	print("%s: DIFFERENT (%d of %d evaluations), see %s" % (name, changed, len(ours), os.path.join(out, name + ".diff")))
	return 1


if __name__ == "__main__":
	sys.exit(main())
