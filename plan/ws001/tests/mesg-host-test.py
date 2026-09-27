#!/usr/bin/env python3
"""ws001-p040: checks zedBSD mesg (host build) on a pseudo terminal.

mesg works on the first of descriptors 0, 1 and 2 that is a terminal; the
checks give it the pty slave there and read the mode back with os.stat.

  python3 plan/ws001/tests/mesg-host-test.py [--bin build/ws001/bin]
"""

import argparse
import os
import stat
import subprocess
import sys


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--bin", default="build/ws001/bin")
	options = parser.parse_args()
	mesg = os.path.abspath(os.path.join(options.bin, "mesg"))
	passed = 0
	failed = 0

	def check(name, condition, detail=""):
		nonlocal passed, failed
		if condition:
			passed += 1
		else:
			failed += 1
			print("FAIL %s %s" % (name, detail))

	master, slave = os.openpty()
	path = os.ttyname(slave)

	def run(arguments, stdin=None, stdout=None, stderr=None):
		result = subprocess.run([mesg] + arguments, stdin=slave if stdin is None else stdin,
					stdout=subprocess.PIPE if stdout is None else stdout,
					stderr=subprocess.PIPE if stderr is None else stderr, text=True)
		return result.returncode, result.stdout or "", result.stderr or ""

	# y and n, and the state they leave.
	os.chmod(path, 0o600 | stat.S_IWOTH)
	status, _, _ = run(["y"])
	mode = os.stat(path).st_mode & 0o7777
	check("y", status == 0 and mode & stat.S_IWGRP and mode & 0o600 == 0o600, oct(mode))
	status, out, _ = run([])
	check("is y", status == 0 and out == "is y\n", out)
	status, _, _ = run(["n"])
	mode = os.stat(path).st_mode & 0o7777
	check("n", status == 1 and mode & (stat.S_IWGRP | stat.S_IWOTH) == 0 and mode & 0o600 == 0o600, oct(mode))
	status, out, _ = run([])
	check("is n", status == 1 and out == "is n\n", out)
	status, _, _ = run(["--", "y"])
	check("-- y", status == 0 and os.stat(path).st_mode & stat.S_IWGRP)

	# The terminal is found on standard error when the others are not one.
	os.chmod(path, 0o600)
	with open(os.devnull, "rb") as null_input, open(os.devnull, "wb") as null_output:
		result = subprocess.run([mesg, "y"], stdin=null_input, stdout=null_output, stderr=slave)
	check("stderr terminal", result.returncode == 0 and os.stat(path).st_mode & stat.S_IWGRP)

	# Errors.
	status, _, error = run(["x"])
	check("bad operand", status == 2 and "usage" in error, error)
	status, _, error = run(["y", "n"])
	check("two operands", status == 2)
	with open(os.devnull, "rb") as null_input:
		result = subprocess.run([mesg], stdin=null_input, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	check("no terminal", result.returncode == 2 and result.stderr != b"")

	os.close(master)
	os.close(slave)
	print("TOTAL %d/%d" % (passed, passed + failed))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
