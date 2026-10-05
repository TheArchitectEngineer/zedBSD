#!/usr/bin/env python3
"""runner: runs a suite (or scenarios) of tests/ against a target with the automatic helpers (WS173 p004).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    plan/tools/aat/run-aat.sh TARGET OUTDIR [SUITE|ID|PATTERN|area:NAME|changed:RANGE ...] [--record PATH] [--no-samples]

TARGET is 5330, qemu (the guest of GUEST_RUNTIME/session.json) or
user@host[:port].  The default is the smoke suite.  For each scenario in
order: a helper of the same id (helpers_*.py) runs it and records its
steps (OUTDIR/records/ID.md); a scenario for the real machine on QEMU is
not-run; one without a helper is needs-person when it needs hands, and
by-agent otherwise (an agent carries it out from its document).  Before
them: aat check, the session's ZWL READY, aat-input started, the samples
put.  After them: OUTDIR/summary.md (the run's record: the target, the
image, every verdict with its note, screenshots and record) and
OUTDIR/summary.tsv; --record copies the record to PATH (the plan's
plan/ws173/runs/DATE-SUITE.md).
"""
from __future__ import annotations

import datetime
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))

import aatlib  # noqa: E402

CHECK = HERE.parent / "check-scenarios.py"
HELPERS = sorted(HERE.glob("helpers_*.py"))

# How long one scenario's helper may take.
SCENARIO_SECONDS = 900


def load_checker():
	"""check-scenarios.py as a module (its suite and header readers)."""
	import importlib.util
	spec = importlib.util.spec_from_file_location("check_scenarios", CHECK)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


def selector():
	"""select-scenarios.py as a module."""
	import importlib.util
	spec = importlib.util.spec_from_file_location("select_scenarios", HERE.parent / "select-scenarios.py")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


def helper_ids() -> dict[str, Path]:
	"""Each helper's ids: the scenario's id -> its helper's file."""
	found = {}
	for path in HELPERS:
		for ident in re.findall(r'run\.define\("([a-z0-9.-]+)"\)', path.read_text(encoding="utf-8")):
			found[ident] = path
	# The open-from-home helpers are made in a loop over the applications.
	if (HERE / "helpers_apps.py").exists():
		text = (HERE / "helpers_apps.py").read_text(encoding="utf-8")
		block = re.search(r"DIRECTORIES = \{(.*?)\}", text, re.S)
		for directory in re.findall(r'"([a-z]+)": "', block.group(1) if block else ""):
			found[f"apps.{directory}.open-from-home"] = HERE / "helpers_apps.py"
	return found


def chosen(checker, words: list[str], found: dict[str, dict]) -> tuple[list[str], str]:
	"""The scenarios the words name, in order (suites first by their order), and the run's name."""
	members: list[str] = []
	names = []
	for word in words or ["smoke"]:
		if word.startswith("changed:"):
			# The scenarios a git range needs (select-scenarios.py: paths, and smoke).
			hits, _, _ = selector().select(word[8:] or "main...HEAD")
		elif (checker.SUITES / f"{word}.suite").exists():
			hits, _ = checker.suite_members(word, found)
		elif word.startswith("area:"):
			hits = [ident for ident, fields in found.items() if word[5:] in checker.listed(fields.get("areas", ""))]
		else:
			import fnmatch
			hits = [ident for ident in found if fnmatch.fnmatchcase(ident, word)]
		if not hits:
			sys.exit(f"run-aat: {word!r} names no scenario")
		names.append(word.replace("*", "x").replace(":", "-").replace(".", "_").replace("/", "-"))
		for ident in hits:
			if ident not in members and found[ident].get("status") == "active":
				members.append(ident)
	return members, "+".join(names)


def aat(target: list[str], *words: str, timeout: float = 300) -> subprocess.CompletedProcess:
	"""One aat command."""
	return subprocess.run([sys.executable, str(HERE.parent / "aat"), *target, *words], capture_output=True, text=True,
		timeout=timeout)


def preflight(target: list[str], outdir: Path, samples: bool) -> list[str]:
	"""Reaches the target, waits for the session, starts the input and puts the samples; returns what failed."""
	problems = []
	# The host itself has no injector or capture: its fakes stand in (the runner's own test), so no check.
	if target == ["--local"]:
		check = subprocess.CompletedProcess([], 0, "local: no check\n", "")
	else:
		check = aat(target, "check", timeout=120)
	(outdir / "check.txt").write_text(check.stdout + check.stderr)
	print(check.stdout.strip())
	if check.returncode != 0:
		problems.append(f"aat check: {(check.stdout + check.stderr).strip().splitlines()[-1:]}")
		return problems
	ready = aat(target, "wait-log", r"ZWL READY socket=\S+ .* role=normal", "--timeout", "180", timeout=400)
	if ready.returncode != 0:
		problems.append("no session (no ZWL READY ... role=normal)")
		return problems
	size = re.search(r"width=(\d+) height=(\d+)", ready.stdout)
	start = aat(target, "start", "--size", f"{size.group(1)}x{size.group(2)}", timeout=120)
	print(start.stdout.strip() or start.stderr.strip())
	if start.returncode != 0:
		problems.append(f"aat start: {start.stderr.strip()}")
	if samples:
		made = subprocess.run([sys.executable, str(HERE / "samples.py"), str(outdir), "--", *target], capture_output=True,
			text=True, timeout=900)
		if made.returncode != 0:
			problems.append(f"samples: {made.stderr.strip()[-200:]}")
	return problems


def target_words(name: str) -> list[str]:
	"""aat's target options for TARGET."""
	if name == "qemu":
		return ["--qemu"]
	# The host itself, for the runner's own test (tests/run-runner-host.sh).
	if name == "local":
		return ["--local"]
	return ["--target", name]


def describe(target: list[str]) -> str:
	"""The image the target runs: its os-release name and uname."""
	result = aat(target, "run", ". /etc/os-release 2>/dev/null; echo \"$PRETTY_NAME\"; uname -a", timeout=60)
	return " / ".join(line.strip() for line in result.stdout.splitlines() if line.strip())


def summary(outdir: Path, run_name: str, target_name: str, image: str, members: list[str], found: dict, problems: list[str],
	started: datetime.datetime) -> str:
	"""The run's record (Markdown) from OUTDIR/verdicts.tsv."""
	verdicts = {}
	tsv = outdir / "verdicts.tsv"
	if tsv.exists():
		for line in tsv.read_text(encoding="utf-8").splitlines():
			fields = line.split("\t")
			if len(fields) >= 5:
				verdicts[fields[0]] = fields
	counts: dict[str, int] = {}
	rows = []
	for ident in members:
		fields = verdicts.get(ident, [ident, "not-run", found[ident].get("title", ""), "", "no verdict (the run stopped?)"])
		verdict = fields[1]
		counts[verdict] = counts.get(verdict, 0) + 1
		pictures = " ".join(f"[{Path(p).stem.split('-')[-1]}]({outdir.resolve() / p})" for p in fields[3].split(",") if p)
		record = outdir / "records" / f"{ident}.md"
		link = f"[record]({record.resolve()})" if record.exists() else ""
		note = fields[4].replace("|", "\\|")
		rows.append(f"| `{ident}` | **{verdict}** | {note} | {pictures} | {link} |")
	lines = [
		f"# AAT {started:%Y-%m-%d %H:%M} — {run_name}",
		"",
		f"- target: {target_name}（{'QEMU' if target_name == 'qemu' else '実機・SSH'}）",
		f"- image: {image}",
		f"- scenarios: {len(members)}（{', '.join(f'{k} {v}' for k, v in sorted(counts.items()))}）",
		f"- output: {outdir.resolve()}",
	]
	if problems:
		lines.append(f"- preflight: {'; '.join(problems)}")
	lines += ["", "| scenario | verdict | note | screenshots | record |", "| --- | --- | --- | --- | --- |", *rows, ""]
	lines += [
		"verdict: pass（確かめた）・fail（どの操作で何が違ったか）・needs-person（撮影を人が見る、または人の手）・",
		"by-agent（自動の補助が無い。エージェントが文書を読んで行う）・not-run（この target では流さない）。",
		"",
	]
	return "\n".join(lines)


def main() -> int:
	"""Runs the scenarios named and writes the record."""
	arguments = sys.argv[1:]
	record = None
	samples = True
	if "--record" in arguments:
		index = arguments.index("--record")
		record = Path(arguments[index + 1])
		del arguments[index:index + 2]
	if "--no-samples" in arguments:
		arguments.remove("--no-samples")
		samples = False
	if len(arguments) < 2:
		print(__doc__.split("\n\n")[2], file=sys.stderr)
		return 2
	target_name, outdir = arguments[0], Path(arguments[1])
	outdir.mkdir(parents=True, exist_ok=True)
	# A run starts its verdicts afresh (an earlier run's in the same directory are kept aside).
	if (outdir / "verdicts.tsv").exists():
		(outdir / "verdicts.tsv").replace(outdir / "verdicts.tsv.old")
	target = target_words(target_name)
	checker = load_checker()
	found = checker.scenarios()
	members, run_name = chosen(checker, arguments[2:], found)
	helpers = helper_ids()
	started = datetime.datetime.now()
	qemu = target_name == "qemu"
	print(f"run-aat: {run_name}: {len(members)} scenarios on {target_name}, output {outdir}")
	problems = preflight(target, outdir, samples)
	image = describe(target) if not problems or "aat check" not in problems[0] else "-"
	for ident in members:
		fields = found[ident]
		title = fields.get("title", "")
		verdict = None
		if fields.get("machine") == "hardware" and qemu:
			verdict, note = "not-run", "the real machine's scenario"
		elif problems and problems[0].startswith(("aat check", "no session")):
			verdict, note = "not-run", problems[0]
		elif ident not in helpers:
			if fields.get("human") == "hands":
				verdict, note = "needs-person", "a person's hands (UAT)"
			else:
				verdict, note = "by-agent", "no helper: an agent carries it out from the document"
		if verdict:
			with open(outdir / "verdicts.tsv", "a", encoding="utf-8") as verdicts:
				verdicts.write("\t".join([ident, verdict, title, "", note, "0", "runner"]) + "\n")
			print(f"AAT {ident} {verdict} {note}")
			continue
		try:
			subprocess.run([sys.executable, str(helpers[ident]), "--outdir", str(outdir), "--only", f"^{re.escape(ident)}$",
				"--", *target], timeout=SCENARIO_SECONDS)
		except subprocess.TimeoutExpired:
			with open(outdir / "verdicts.tsv", "a", encoding="utf-8") as verdicts:
				verdicts.write("\t".join([ident, "fail", title, "", f"the helper ran over {SCENARIO_SECONDS} s", "", "runner"]) + "\n")
	text = summary(outdir, run_name, target_name, image, members, found, problems, started)
	(outdir / "summary.md").write_text(text, encoding="utf-8")
	if (outdir / "verdicts.tsv").exists():
		(outdir / "summary.tsv").write_text((outdir / "verdicts.tsv").read_text(encoding="utf-8"), encoding="utf-8")
	if record:
		record.parent.mkdir(parents=True, exist_ok=True)
		record.write_text(text, encoding="utf-8")
	print(text)
	return 0


if __name__ == "__main__":
	sys.exit(main())
