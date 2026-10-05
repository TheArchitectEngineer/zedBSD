#!/usr/bin/env python3
"""check-scenarios: checks the scenario tests and suites under tests/ (plan/tests.md, WS173 p004).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    plan/tools/aat/check-scenarios.py [--list]

Each tests/scenarios/**/*.md has its header (id, title, status, areas, paths,
machine, human, since) and the sections 目的, 準備, 操作と確認, 合格; the id
is its path; every path named exists in the tree; status, machine and human
take their words; each step has 操作, 確認事項, 正解 and 確認方法.  Each
tests/suites/*.suite names scenarios that exist (an id, a pattern with *,
or area:NAME).  --list prints one line a scenario: id, status, machine,
human, title.  The last line is "check-scenarios: PASS" or "... FAIL".
"""
from __future__ import annotations

import fnmatch
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCENARIOS = ROOT / "tests/scenarios"
SUITES = ROOT / "tests/suites"

KEYS = ("id", "title", "status", "areas", "paths", "machine", "human", "since")
WORDS = {
	"status": ("draft", "active", "retired"),
	"machine": ("qemu", "hardware", "either"),
	"human": ("none", "look", "hands"),
}
SECTIONS = ("## 目的", "## 準備", "## 操作と確認", "## 合格")
STEP_PARTS = ("操作:", "確認事項:", "正解:", "確認方法:")


def header(text: str) -> dict[str, str] | None:
	"""The header block's fields, or None without one."""
	match = re.match(r"---\n(.*?)\n---\n", text, re.S)
	if match is None:
		return None
	fields = {}
	for line in match.group(1).splitlines():
		key, _, value = line.partition(":")
		fields[key.strip()] = value.strip()
	return fields


def listed(value: str) -> list[str]:
	"""A header's [a, b] list."""
	return [word.strip() for word in value.strip("[]").split(",") if word.strip()]


def scenarios() -> dict[str, dict]:
	"""Every scenario by its id: its header and its file."""
	found = {}
	for path in sorted(SCENARIOS.rglob("*.md")):
		text = path.read_text(encoding="utf-8")
		fields = header(text) or {}
		fields["_path"] = path
		fields["_text"] = text
		ident = ".".join(path.relative_to(SCENARIOS).with_suffix("").parts)
		found[ident] = fields
	return found


def problems_of(ident: str, fields: dict) -> list[str]:
	"""What is wrong with one scenario."""
	problems = []
	text = fields["_text"]
	if header(text) is None:
		return ["no header block"]
	for key in KEYS:
		if not fields.get(key):
			problems.append(f"no {key}")
	if fields.get("id") and fields["id"] != ident:
		problems.append(f"id {fields['id']} is not its path's {ident}")
	for key, words in WORDS.items():
		if fields.get(key) and fields[key] not in words:
			problems.append(f"{key} {fields[key]} is not one of {', '.join(words)}")
	for path in listed(fields.get("paths", "")):
		if not (ROOT / path).exists():
			problems.append(f"no such path {path}")
	for section in SECTIONS:
		if section not in text:
			problems.append(f"no section {section}")
	steps = re.findall(r"^\d+\. 操作:.*?(?=^\d+\. |^## |\Z)", text, re.M | re.S)
	if not steps:
		problems.append("no numbered steps (N. 操作: ...)")
	for number, step in enumerate(steps, start=1):
		# A step that only restores or ends may leave its check out; one that checks gives all four parts.
		if "確認事項:" in step:
			for part in STEP_PARTS:
				if part not in step:
					problems.append(f"step {number}: no {part}")
	return problems


def suite_members(name: str, found: dict[str, dict]) -> tuple[list[str], list[str]]:
	"""A suite's scenarios in order, and the lines that name none."""
	members, dead = [], []
	for raw in (SUITES / f"{name}.suite").read_text(encoding="utf-8").splitlines():
		line = raw.split("#", 1)[0].strip()
		if not line:
			continue
		if line.startswith("area:"):
			area = line[5:]
			hits = [ident for ident, fields in found.items() if area in listed(fields.get("areas", ""))]
		elif line.startswith("suite:"):
			hits, _ = suite_members(line[6:], found)
		else:
			hits = [ident for ident in found if fnmatch.fnmatchcase(ident, line)]
		if not hits:
			dead.append(line)
		for ident in hits:
			if ident not in members:
				members.append(ident)
	return members, dead


def main() -> int:
	"""Checks everything; with --list, lists the scenarios."""
	found = scenarios()
	failed = False
	if "--list" in sys.argv:
		for ident, fields in found.items():
			print(f"{ident}\t{fields.get('status')}\t{fields.get('machine')}\t{fields.get('human')}\t{fields.get('title')}")
	for ident, fields in found.items():
		for problem in problems_of(ident, fields):
			print(f"{fields['_path'].relative_to(ROOT)}: {problem}")
			failed = True
	for suite in sorted(SUITES.glob("*.suite")):
		members, dead = suite_members(suite.stem, found)
		for line in dead:
			print(f"{suite.relative_to(ROOT)}: {line!r} names no scenario")
			failed = True
		print(f"suite {suite.stem}: {len(members)} scenarios")
	print(f"scenarios: {len(found)}")
	print("check-scenarios: FAIL" if failed else "check-scenarios: PASS")
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
