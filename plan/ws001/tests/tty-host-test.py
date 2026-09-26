#!/usr/bin/env python3
"""ws001: runs the terminal-dependent cases that util-diff.py cannot (its
cases have no terminal) under a pseudo-terminal, once with the host's GNU
utility and once with zedBSD's host build, and compares what each case
reports.

  python3 plan/ws001/tests/tty-host-test.py [--bin build/ws001/bin]

Each case is a dash script run in a fresh directory as the session leader
of a new pseudo-terminal, so that /dev/tty is that terminal.  The case
writes what it wants compared to the file "result"; the text typed to the
terminal is given per case.  The terminal output itself is not compared,
because the diagnostics of GNU and zedBSD differ in wording.
"""

import argparse
import os
import pty
import shutil
import sys
import tempfile
import time
from pathlib import Path

CASES = [
	("nohup appends terminal output to nohup.out",
	 "nohup sh -c 'echo out; echo err >&2' ; echo st=$? > result; "
	 "cat nohup.out >> result; ls -l nohup.out | cut -c1-10 >> result",
	 b""),
	("nohup appends to an existing nohup.out",
	 "echo old > nohup.out; nohup echo new; cat nohup.out > result",
	 b""),
	("nohup with stdout on a file sends stderr there",
	 "nohup sh -c 'echo out; echo err >&2' > o; echo st=$? > result; "
	 "cat o >> result; test -e nohup.out && echo made >> result",
	 b""),
	("nohup falls back to HOME",
	 "mkdir h ro; chmod 555 ro; cd ro; HOME=../h nohup echo x; "
	 "echo st=$? > ../result; cat ../h/nohup.out >> ../result",
	 b""),
	("nohup fails when no nohup.out can be opened",
	 "mkdir ro; chmod 555 ro; cd ro; HOME=/nonexistent nohup echo x; "
	 "echo st=$? > ../result",
	 b""),
	("nohup replaces terminal input",
	 "nohup sh -c 'cat 2>/dev/null; echo done' ; cat nohup.out > result",
	 b""),
	("mv asks about an unwritable destination on a terminal (no)",
	 "printf new > a; printf old > b; chmod 444 b; mv a b; "
	 "echo st=$? > result; ls >> result; cat b >> result",
	 b"n\n"),
	("mv asks about an unwritable destination on a terminal (yes)",
	 "printf new > a; printf old > b; chmod 444 b; mv a b; "
	 "echo st=$? > result; ls >> result; cat b >> result",
	 b"y\n"),
	("mv -f does not ask on a terminal",
	 "printf new > a; printf old > b; chmod 444 b; mv -f a b; "
	 "echo st=$? > result; ls >> result; cat b >> result",
	 b""),
	("xargs -p runs on y and skips on n",
	 "printf 'a\\nb\\nc\\n' | xargs -p -n 1 sh -c 'echo \"$0\" >> result'; "
	 "echo st=$? >> result",
	 b"y\nn\ny\n"),
]


def run_case(code: str, typed: bytes, path: str) -> bytes:
	"""Runs one case under a new pseudo-terminal; returns its result."""
	work = Path(tempfile.mkdtemp(prefix="ws001-tty-"))
	environment = {
		"PATH": path,
		"HOME": str(work),
		"LC_ALL": "C",
		"POSIXLY_CORRECT": "1",
		"TERM": "dumb",
	}
	pid, master = pty.fork()
	if pid == 0:
		os.chdir(work)
		os.execve("/bin/dash", ["dash", "-c", code], environment)
	# Types the answers a little after the start, then drains the output
	# until the case ends.
	time.sleep(0.3)
	if typed:
		os.write(master, typed)
	while True:
		try:
			chunk = os.read(master, 4096)
		except OSError:
			break
		if not chunk:
			break
	os.waitpid(pid, 0)
	os.close(master)
	result = work / "result"
	data = result.read_bytes() if result.exists() else b"<no result>"
	# Directories the case made read-only must be writable to remove.
	for directory, _, _ in os.walk(work):
		os.chmod(directory, 0o755)
	shutil.rmtree(work, ignore_errors=True)
	return data


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--bin", default="build/ws001/bin")
	options = parser.parse_args()
	ours = str(Path(options.bin).resolve())
	failed = 0
	for name, code, typed in CASES:
		reference = run_case(code, typed, "/usr/bin:/bin")
		result = run_case(code, typed, ours + ":/usr/bin:/bin")
		if reference == result:
			print("PASS %s" % name)
		else:
			failed += 1
			print("FAIL %s\n--- ref\n%s--- ours\n%s" % (
			    name, reference.decode(errors="replace"),
			    result.decode(errors="replace")))
	print("TOTAL %d/%d" % (len(CASES) - failed, len(CASES)))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
