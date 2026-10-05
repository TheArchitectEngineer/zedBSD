#!/usr/bin/env python3
"""select-scenarios: the scenario tests a change needs, from a git range (WS173 p006, plan/tests.md §5).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    plan/tools/aat/select-scenarios.py [RANGE] [--no-smoke] [--all-status] [--explain] [--gaps]

RANGE is what git diff takes (default main...HEAD: the branch's own
changes; a commit alone means that commit, A..B the commits between).  A
scenario is chosen when a changed file is under one of its paths (a path
ending in / is a directory, otherwise a file or its prefix), or when its
own document changed; the smoke suite is added unless --no-smoke.  Only
active scenarios unless --all-status.  One id a line, in the order of the
full suite.  --explain says why each was chosen; --gaps lists the changed
source files no scenario covers (outside tests/, userland/tests/, plan/ and
docs/), on the
standard error.  run-aat.sh takes the same choice as changed:RANGE.
"""
from __future__ import annotations

import importlib.util
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CHECK = Path(__file__).resolve().parent / "check-scenarios.py"

# Changes there are records and documents, not behaviour: they choose nothing and are no gap.
NOT_SOURCE = ("plan/", "docs/", "tests/suites/", ".claude/", "AGENTS.md", "README", "LICENSE")


def checker():
	"""check-scenarios.py as a module (its readers of scenarios and suites)."""
	spec = importlib.util.spec_from_file_location("check_scenarios", CHECK)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


def changed_files(range_: str) -> list[str]:
	"""The files a git range changed (a single commit: that commit's)."""
	if ".." not in range_:
		command = ["git", "-C", str(ROOT), "diff-tree", "--no-commit-id", "--name-only", "-r", "-m", range_]
	else:
		command = ["git", "-C", str(ROOT), "diff", "--name-only", range_]
	result = subprocess.run(command, capture_output=True, text=True)
	if result.returncode != 0:
		sys.exit(f"select-scenarios: git: {result.stderr.strip()}")
	return sorted(set(line.strip() for line in result.stdout.splitlines() if line.strip()))


def covers(path: str, changed: str) -> bool:
	"""Whether a scenario's path covers a changed file: a directory (…/) holds it, or the path is the file or its
	prefix (a source's stem names its siblings, userland/desktop/wayland/keyboard covers keyboard.c and .h)."""
	if path.endswith("/"):
		return changed.startswith(path)
	return changed == path or changed.startswith(path)


def select(range_: str, smoke: bool = True, all_status: bool = False) -> tuple[list[str], dict[str, list[str]], list[str]]:
	"""The ids chosen (in the full suite's order), why each was, and the changed source files nothing covers."""
	module = checker()
	found = module.scenarios()
	files = changed_files(range_)
	reasons: dict[str, list[str]] = {}
	covered = set()
	for ident, fields in found.items():
		if not all_status and fields.get("status") != "active":
			continue
		document = str(fields["_path"].relative_to(ROOT))
		for changed in files:
			if changed == document:
				reasons.setdefault(ident, []).append(f"its document {changed}")
				covered.add(changed)
				continue
			for path in module.listed(fields.get("paths", "")):
				if covers(path, changed):
					reasons.setdefault(ident, []).append(f"{changed} (paths {path})")
					covered.add(changed)
					break
	if smoke and (module.SUITES / "smoke.suite").exists():
		for ident in module.suite_members("smoke", found)[0]:
			reasons.setdefault(ident, []).append("the smoke suite")
	order = module.suite_members("full", found)[0] if (module.SUITES / "full.suite").exists() else []
	order += [ident for ident in found if ident not in order]
	chosen = [ident for ident in order if ident in reasons]
	gaps = [changed for changed in files if changed not in covered and not changed.startswith(NOT_SOURCE)
		and not changed.startswith(("tests/", "userland/tests/"))]
	return chosen, reasons, gaps


def main() -> int:
	"""Prints the scenarios a range needs."""
	words = [word for word in sys.argv[1:] if not word.startswith("--")]
	options = {word for word in sys.argv[1:] if word.startswith("--")}
	unknown = options - {"--no-smoke", "--all-status", "--explain", "--gaps", "--help"}
	if unknown or "--help" in options or len(words) > 1:
		print(__doc__.split("\n\n")[2], file=sys.stderr)
		return 2
	range_ = words[0] if words else "main...HEAD"
	chosen, reasons, gaps = select(range_, "--no-smoke" not in options, "--all-status" in options)
	for ident in chosen:
		if "--explain" in options:
			print(f"{ident}\t{'; '.join(reasons[ident][:3])}{' ...' if len(reasons[ident]) > 3 else ''}")
		else:
			print(ident)
	if "--gaps" in options:
		for changed in gaps:
			print(f"gap: {changed}", file=sys.stderr)
	return 0


if __name__ == "__main__":
	sys.exit(main())
