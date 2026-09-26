#!/usr/bin/env python3
"""ws001: runs the cases whose expected output is written down rather than
taken from the host's GNU utilities, because GNU differs from POSIX there
or zedBSD chose otherwise on purpose (each case says why).

The cases are plan/ws001/tests/pinned/*.sh, split at lines "#### name";
each has its code, then a line "## expect" and the exact expected output.
A line "## status N" before "## expect" expects status N (0 otherwise).
Each case runs under dash in an empty directory with the utilities under
test first on PATH, as plan/tools/utils/util-diff.py runs its cases.

  python3 plan/ws001/tests/pinned-cases.py [--bin build/ws001/bin]
      [--export DIR]

--export writes the cases in the form plan/ws001/tests/guest-cases.sh
reads, for the guest.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def parse(path: Path) -> list[tuple[str, str, bytes, int]]:
	"""Splits a file into (name, code, expected output, status)."""
	cases = []
	name = None
	code: list[str] = []
	expect: list[str] = []
	status = 0
	in_expect = False

	def close() -> None:
		if name is not None:
			text = "\n".join(expect)
			if expect:
				text += "\n"
			cases.append((name, "\n".join(code).strip() + "\n", text.encode(), status))

	for line in path.read_text().split("\n"):
		if line.startswith("#### "):
			close()
			name = line[5:].strip()
			code, expect, status, in_expect = [], [], 0, False
			continue
		if line.startswith("## status "):
			status = int(line[len("## status "):])
			continue
		if line.strip() == "## expect":
			in_expect = True
			continue
		if name is None:
			continue
		if in_expect:
			expect.append(line)
		else:
			code.append(line)
	close()
	# A blank line separates cases; it is not part of the expected output.
	return [(n, c, e.rstrip(b"\n") + b"\n" if e.strip() else b"", s) for n, c, e, s in cases]


def run(code: str, path: str) -> tuple[bytes, int]:
	work = Path(tempfile.mkdtemp(prefix="ws001-pinned-"))
	environment = {"PATH": path, "HOME": str(work), "LC_ALL": "C", "TZ": "UTC"}
	try:
		result = subprocess.run(["dash", "-c", code], cwd=work, env=environment,
					stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
					stderr=subprocess.DEVNULL, timeout=20)
		return result.stdout, result.returncode
	finally:
		for directory, _, _ in os.walk(work):
			os.chmod(directory, 0o755)
		shutil.rmtree(work, ignore_errors=True)


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--bin", default="build/ws001/bin")
	parser.add_argument("--export")
	options = parser.parse_args()
	ours = str(Path(options.bin).resolve())
	cases = []
	for path in sorted((HERE / "pinned").glob("*.sh")):
		for name, code, expect, status in parse(path):
			cases.append((path.name, name, code, expect, status))

	if options.export:
		out = Path(options.export) / "00"
		out.mkdir(parents=True, exist_ok=True)
		for index, (label, name, code, expect, status) in enumerate(cases):
			(out / ("pinned-%04d.sh" % index)).write_text(code)
			(out / ("pinned-%04d.exp" % index)).write_bytes(
			    ("%d\npinned %s :: %s\n" % (status, label, name)).encode() + expect)
		print("exported %d cases" % len(cases))
		return 0

	failed = 0
	for label, name, code, expect, status in cases:
		output, got = run(code, ours + ":/usr/bin:/bin")
		if output == expect and got == status:
			continue
		failed += 1
		print("FAIL %s :: %s\n--- expected (status %d)\n%s--- got (status %d)\n%s"
		      % (label, name, status, expect.decode(errors="replace"), got,
			 output.decode(errors="replace")))
	print("TOTAL %d/%d" % (len(cases) - failed, len(cases)))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
