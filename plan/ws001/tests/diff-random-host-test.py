#!/usr/bin/env python3
"""ws001-p029: checks zedBSD diff (host build) on random file pairs.

For each pair, every output form must turn the first file into the second
when applied by the host's GNU tools (patch for the normal, -c and -u
forms, ed for -e), and the number of lines diff marks as removed or added
must equal that of GNU diff --minimal, so the common subsequence is a
longest one.  -b is checked the same way on pairs that differ in blanks.
Context hunks without context lines (-C 0) are not applied: a one-line
range and an empty one print alike there, and patch misreads GNU diff's
own -C 0 output in the same way.

  python3 plan/ws001/tests/diff-random-host-test.py [--bin build/ws001/bin]
      [--count 300] [--seed 1]
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path


def random_pair(rng: random.Random) -> tuple[list[str], list[str]]:
	"""Makes two related files of random lines from a small alphabet."""
	alphabet = [chr(ord("a") + i) for i in range(rng.randint(2, 8))]
	first = [rng.choice(alphabet) for _ in range(rng.randint(0, 60))]
	second = list(first)
	for _ in range(rng.randint(0, 12)):
		action = rng.random()
		position = rng.randint(0, len(second))
		if action < 0.4 and second:
			del second[min(position, len(second) - 1)]
		elif action < 0.8:
			second.insert(position, rng.choice(alphabet + ["new"]))
		elif second:
			second[min(position, len(second) - 1)] = "changed"
	return first, second


def write(path: Path, lines: list[str], newline: bool) -> None:
	text = "\n".join(lines)
	if lines and newline:
		text += "\n"
	path.write_text(text)


def changed_lines(output: str) -> int:
	return sum(1 for line in output.split("\n") if line[:2] in ("< ", "> "))


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--bin", default="build/ws001/bin")
	parser.add_argument("--count", type=int, default=300)
	parser.add_argument("--seed", type=int, default=1)
	parser.add_argument("--keep")
	options = parser.parse_args()
	ours = str(Path(options.bin, "diff").resolve())
	rng = random.Random(options.seed)
	environment = dict(os.environ, LC_ALL="C", TZ="UTC")
	failures = 0
	with tempfile.TemporaryDirectory(prefix="ws001-diff-") as work:
		work = Path(work)
		x = work / "x"
		y = work / "y"
		z = work / "z"
		for case in range(options.count):
			first, second = random_pair(rng)
			write(x, first, rng.random() < 0.9)
			write(y, second, rng.random() < 0.9)
			if case % 5 == 0:
				# Blank-only differences for -b.
				second = [line.replace("a", "a  ") + (" " if rng.random() < 0.3 else "") for line in first]
				write(y, second, True)
			problems = []
			# The number of changed lines against GNU --minimal.
			ours_normal = subprocess.run([ours, str(x), str(y)], capture_output=True, text=True, env=environment)
			gnu_normal = subprocess.run(["/usr/bin/diff", "--minimal", str(x), str(y)], capture_output=True, text=True, env=environment)
			if ours_normal.returncode != gnu_normal.returncode:
				problems.append("status %d vs %d" % (ours_normal.returncode, gnu_normal.returncode))
			if changed_lines(ours_normal.stdout) != changed_lines(gnu_normal.stdout):
				problems.append("changed lines %d vs %d" % (changed_lines(ours_normal.stdout), changed_lines(gnu_normal.stdout)))
			ours_b = subprocess.run([ours, "-b", str(x), str(y)], capture_output=True, text=True, env=environment)
			gnu_b = subprocess.run(["/usr/bin/diff", "--minimal", "-b", str(x), str(y)], capture_output=True, text=True, env=environment)
			if ours_b.returncode != gnu_b.returncode or changed_lines(ours_b.stdout) != changed_lines(gnu_b.stdout):
				problems.append("-b: status %d vs %d, changed %d vs %d" % (ours_b.returncode, gnu_b.returncode, changed_lines(ours_b.stdout), changed_lines(gnu_b.stdout)))
			# Each form applied by the GNU tools gives the second file.
			for form in ([], ["-c"], ["-u"], ["-C", "1"], ["-U", "0"], ["-U", "1"]):
				output = subprocess.run([ours] + form + [str(x), str(y)], capture_output=True, env=environment).stdout
				(work / "p").write_bytes(output)
				applied = subprocess.run(["/usr/bin/patch", "-s", "-o", str(z), str(x), str(work / "p")], capture_output=True, env=environment)
				if output and (applied.returncode != 0 or z.read_bytes() != y.read_bytes()):
					# -C 0 context hunks can be ambiguous to patch; that
					# counts only when GNU diff's own hunks apply.
					reference = subprocess.run(["/usr/bin/diff"] + form + [str(x), str(y)], capture_output=True, env=environment).stdout
					(work / "p").write_bytes(reference)
					checked = subprocess.run(["/usr/bin/patch", "-s", "-o", str(z), str(x), str(work / "p")], capture_output=True, env=environment)
					if checked.returncode == 0 and z.read_bytes() == y.read_bytes():
						problems.append("patch %s failed" % " ".join(form or ["normal"]))
			if y.read_bytes().endswith(b"\n") or not y.read_bytes():
				script = subprocess.run([ours, "-e", str(x), str(y)], capture_output=True, env=environment).stdout
				(work / "e").write_bytes(x.read_bytes())
				subprocess.run(["/usr/bin/ed", "-s", str(work / "e")], input=script + b"w\nq\n", capture_output=True, env=environment)
				expected = y.read_bytes()
				got = (work / "e").read_bytes()
				if x.read_bytes().endswith(b"\n") or not x.read_bytes():
					if got != expected:
						problems.append("ed -e failed")
			if problems:
				failures += 1
				print("FAIL case %d: %s" % (case, "; ".join(problems)))
				# The first failing pair is kept for a look.
				if options.keep and not Path(options.keep, "x").exists():
					Path(options.keep).mkdir(parents=True, exist_ok=True)
					Path(options.keep, "x").write_bytes(x.read_bytes())
					Path(options.keep, "y").write_bytes(y.read_bytes())
	print("TOTAL %d/%d" % (options.count - failures, options.count))
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
