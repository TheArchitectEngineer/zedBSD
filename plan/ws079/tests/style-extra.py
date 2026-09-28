#!/usr/bin/env python3
"""Checks rules of plan/coding-style.md that plan/tools/style-check.py leaves to reading (ws079-p009).

  python3 plan/ws079/tests/style-extra.py FILE... [--summary]

Each finding is printed as FILE:LINE: RULE: text.  Exits 1 when there is any.  The rules (with the section of
coding-style.md):

  return-error        11  a function whose last statement is a bare return of an error-like variable
                          (return error;): the failure and the success return separately
  return-call         11  a return of a function call's result (return name(...);)
  initializer-call     4  a local declaration whose initializer calls a function
  function-comment   3,10 a function definition without a comment right above its return type
  type-comment         2  a struct, union or enum definition, or a typedef, at file scope without a comment above
  variable-comment     2  a file-scope variable without a comment above
  banned-comment      10  a comment of the banned kinds (Handles the ..., Checks the operation ..., ...)
  joined-check         9  one if that tests two results for NULL (allocations checked together)
  copyright           13  a file that does not start with the copyright header
  braces               8  an if and its terminal else braced differently, a loop whose unbraced body is an if, a
                          comment between a control statement and its unbraced body, a block that starts blank

What it finds is a candidate: a macro called in a return, or a cast that looks like a call, is read and kept when the
rule does not apply.
"""

import argparse
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location("style_check", os.path.join(HERE, "../../tools/style-check.py"))
STYLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STYLE)

ERROR_NAMES = {"error", "status", "err", "result", "failed", "rc", "ret"}
NOT_CALLS = STYLE.KEYWORDS | {"sizeof"}
BANNED = [
	r"Handles the \w+ condition", r"Handles the \w+ availability", r"Checks the operation status",
	r"Checks the operation result", r"Returns the computed result", r"Computes the function result",
	r"Reports successful completion", r"Process each remaining element",
]
DECLARED = re.compile(
	r"^\t(const\s+|static\s+|volatile\s+|unsigned\s+|signed\s+)*"
	r"(struct\s+\w+|union\s+\w+|enum\s+\w+|int|char|long|short|double|float|size_t|ssize_t|off_t|FILE|\w+_t)"
	r"(\s+|\s*\*+\s*)\**\w+(\s*\[[^\]]*\])?\s*=(.*);\s*$")
COPYRIGHT = "/*\n * zedBSD\n * Copyright (C) 2026 Awe Morris\n *\n * SPDX-License-Identifier: Zlib\n */\n"


def check_file(path):
	"""Returns the findings of a file as (line, rule, text)."""
	with open(path, encoding="utf-8", errors="replace") as stream:
		source = stream.read()
	raw = source.split("\n")
	stripped = []
	in_comment = False
	for line in raw:
		text, in_comment = STYLE.strip_line(line, in_comment)
		stripped.append(text)
	findings = []

	if not source.startswith(COPYRIGHT):
		findings.append((1, "copyright", raw[0] if raw else ""))

	# Function definitions: the name and its open parenthesis at the start of a line, the return type on the line above.
	for index, line in enumerate(raw):
		if not re.match(r"^[A-Za-z_]\w*\((void\))?$", line):
			continue
		if index < 2:
			continue
		type_line = raw[index - 1]
		if not type_line or type_line[0] in " \t#/*}":
			continue
		above = raw[index - 2].rstrip()
		if not above.endswith("*/"):
			findings.append((index, "function-comment", type_line + " " + line))

		# The body: from the { after the arguments to the } at the start of a line.
		start = index
		while start < len(raw) and raw[start] != "{":
			start += 1
		end = start + 1
		while end < len(raw) and raw[end] != "}":
			end += 1
		if end >= len(raw):
			continue

		# The last statement.
		last = end - 1
		while last > start and raw[last].strip() == "":
			last -= 1
		match = re.match(r"^\treturn\s+(\w+)\s*;\s*$", raw[last])
		if match and match.group(1) in ERROR_NAMES:
			findings.append((last + 1, "return-error", raw[last].strip()))

		# Returns of calls, initializers with calls and joined checks inside the body.
		for body in range(start + 1, end):
			text = stripped[body]
			call = re.match(r"^\s*return\s+\(?\s*([A-Za-z_]\w*)\s*\(", text)
			if call and call.group(1) not in NOT_CALLS:
				findings.append((body + 1, "return-call", raw[body].strip()))
			declared = DECLARED.match(text)
			if declared:
				names = [name for name in STYLE.calls_in(declared.group(5)) if name not in NOT_CALLS]
				if names:
					findings.append((body + 1, "initializer-call", raw[body].strip()))
			if re.search(r"\bif\s*\(.*== NULL\s*(\|\||&&).*== NULL", text):
				findings.append((body + 1, "joined-check", raw[body].strip()))

	# File-scope types and variables.
	depth = 0
	for index, line in enumerate(raw):
		text = stripped[index]
		if depth == 0 and index > 0:
			above = raw[index - 1].rstrip()
			is_type = re.match(r"^(typedef\b|(struct|union|enum)\s+\w+\s*\{)", line)
			is_variable = (re.match(r"^(static\s+)?(const\s+)?(unsigned\s+|signed\s+)?(struct\s+\w+|\w+)[\s\*]+\w+(\[[^\]]*\])*\s*(=.*)?[;{]?\s*$", line)
			               and "(" not in line.split("=")[0] and not line.startswith("return")
			               and not re.match(r"^(struct|union|enum)\s+\w+;", line)
			               and not re.match(r"^(static\s+)?(inline\s+)?\w[\w\s\*]*$", line))
			if is_type and not above.endswith("*/"):
				findings.append((index + 1, "type-comment", line.strip()))
			elif is_variable and not above.endswith("*/") and not line.startswith("extern"):
				findings.append((index + 1, "variable-comment", line.strip()))
		depth += text.count("{") - text.count("}")

	# The braces of decisions and loops (section 8).
	for index, line in enumerate(raw):
		stripped_line = line.strip()
		previous = raw[index - 1].strip() if index > 0 else ""
		following = raw[index + 1].strip() if index + 1 < len(raw) else ""
		if stripped_line == "} else":
			findings.append((index + 1, "braces", "a braced if with an unbraced else"))
		if stripped_line == "else {" and previous != "}" and not previous.startswith("/*"):
			findings.append((index + 1, "braces", "an unbraced if with a braced else"))
		if re.match(r"^(for|while)\s*\(.*\)$", stripped_line) and following.startswith("if ("):
			findings.append((index + 1, "braces", "a loop whose body is an if, without braces"))
		if re.match(r"^(if|for|while|else if)\b.*\)$", stripped_line) and following.startswith("/*"):
			findings.append((index + 1, "braces", "a comment between a control statement and its unbraced body"))
		if (stripped_line.endswith("{") and following == "" and "=" not in stripped_line and
		    not stripped_line.startswith(("struct", "enum", "union", "static", "const", "extern"))):
			findings.append((index + 1, "braces", "a block that starts with a blank line"))

	# Comments of the banned kinds.
	for index, line in enumerate(raw):
		for pattern in BANNED:
			if re.search(pattern, line):
				findings.append((index + 1, "banned-comment", line.strip()))

	return findings


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("files", nargs="+")
	parser.add_argument("--summary", action="store_true")
	arguments = parser.parse_args()
	total = 0
	for path in arguments.files:
		findings = sorted(check_file(path))
		total += len(findings)
		if arguments.summary:
			counts = {}
			for _, rule, _ in findings:
				counts[rule] = counts.get(rule, 0) + 1
			if counts:
				print(path, " ".join("%s=%d" % item for item in sorted(counts.items())))
		else:
			for number, rule, text in findings:
				print("%s:%d: %s: %s" % (path, number, rule, text))
	if arguments.summary:
		print("total", total)
	return 1 if total else 0


if __name__ == "__main__":
	sys.exit(main())
