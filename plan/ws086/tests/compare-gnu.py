#!/usr/bin/env python3
"""ws086-p002: compares Kei's ls with GNU ls (coreutils) byte for byte.

Both run over the same trees of names, with the same options, written to
a pseudo-terminal of several widths and to a pipe, in the locales C and
C.UTF-8.  Standard output and the exit status must be the same; standard
error is compared for the cases that test a message (marked below).

Kei's ls takes names as UTF-8 in every locale (user's decision of
2026-09-29), while GNU ls escapes every byte past ASCII in the C locale, so
the trees with such names are compared in C.UTF-8 only.

  python3 plan/ws086/tests/compare-gnu.py OUR_LS [--gnu /bin/ls] [--verbose]

Prints one line per difference and a count; exits 1 when anything differs.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

import argparse
import fcntl
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import termios

GREEK = ("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi "
         "omicron pi rho sigma tau upsilon phi chi psi omega").split()

ASCII_ODD = ["plain", "with space", "it's", "dollar$x", "ctl\x01x", "nl\nx", "#hash",
             "mid#hash", "~tilde", "x~", "a=b", "q?x", "{", "x{y}", "b]", "tab\tx",
             "\"dq\"", "both'\"", "a\x01'b", "\x01", "a@b", "it's x", "#", "a\x1b[1m",
             "back\\slash", "a:b", "star*", "pipe|", "semi;", "paren(", "brace{x",
             "caret^", "tick`", "amp&", "lt<", "gt>", "bang!", "del\x7f", "per%cent",
             "plus+", "comma,", "under_score", "dot.dot", "'lead", "trail'",
             # ws086-p003: an apostrophe with # ~ { } decides between double and single quotes.
             "it's#x", "#it's", "it's{x", "~it's", "it's~", "it's@", "{it's}"]

UTF8_ODD = ["あいう", "日本語のファイル", "é", "café au lait", "emoji\U0001f600",
            "bad\xffx", "cut\xe3\x81", "zero​width", "comb́ining", "中 文",
            "fullＡ", "it's 日本"]


def make_tree(root: str) -> None:
	"""Builds the trees the cases list."""
	def touch(path: str) -> None:
		with open(path, "wb"):
			pass

	many = os.path.join(root, "many")
	os.makedirs(os.path.join(many, "sub"))
	for name in GREEK:
		touch(os.path.join(many, name))
	touch(os.path.join(many, "a-very-long-file-name-that-is-wide"))
	os.chmod(os.path.join(many, "beta"), 0o755)
	os.symlink("alpha", os.path.join(many, "link"))
	os.symlink("sub", os.path.join(many, "dirlink"))
	os.symlink("nowhere", os.path.join(many, "dangling"))
	os.mkfifo(os.path.join(many, "fifo"))
	os.makedirs(os.path.join(many, "sub", "deep"))
	touch(os.path.join(many, "sub", "inner"))

	odd = os.path.join(root, "odd")
	os.makedirs(odd)
	for name in ASCII_ODD:
		touch(os.path.join(odd, name).encode("utf-8", "surrogateescape"))
	os.symlink("with space", os.path.join(odd, "lnk"))
	os.makedirs(os.path.join(odd, "dir:colon"))
	os.makedirs(os.path.join(odd, "dir space"))

	utf = os.path.join(root, "utf")
	os.makedirs(utf)
	for name in UTF8_ODD:
		raw = name.encode("utf-8", "surrogateescape") if "\xff" not in name and "\xe3\x81" not in name \
			else name.encode("latin-1")
		touch(os.path.join(utf.encode(), raw))
	for name in GREEK[:10]:
		touch(os.path.join(utf, name))

	few = os.path.join(root, "few")
	os.makedirs(few)
	for name in ["a", "b", "c"]:
		touch(os.path.join(few, name))

	os.makedirs(os.path.join(root, "empty"))
	one = os.path.join(root, "one")
	os.makedirs(one)
	touch(os.path.join(one, "only"))

	# Many short names: the column count is decided by the width, not the names.
	short = os.path.join(root, "short")
	os.makedirs(short)
	for index in range(200):
		touch(os.path.join(short, "f%d" % (index * 7 % 1000)))


def run(command: list, directory: str, width, environment: dict) -> tuple:
	"""Runs a command on a terminal of the given width (None: a pipe)."""
	if width is None:
		result = subprocess.run(command, cwd=directory, env=environment, stdin=subprocess.DEVNULL,
		                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
		return result.stdout, result.stderr, result.returncode
	master, slave = os.openpty()
	fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, width, 0, 0))
	attributes = termios.tcgetattr(slave)
	attributes[1] &= ~termios.OPOST
	termios.tcsetattr(slave, termios.TCSANOW, attributes)
	child = subprocess.Popen(command, cwd=directory, env=environment, stdin=subprocess.DEVNULL,
	                         stdout=slave, stderr=subprocess.PIPE)
	os.close(slave)
	output = bytearray()
	while True:
		try:
			chunk = os.read(master, 65536)
		except OSError:
			break
		if not chunk:
			break
		output += chunk
	error = child.stderr.read()
	status = child.wait(timeout=20)
	os.close(master)
	return bytes(output), error, status


def cases():
	"""Yields (directory, options, operands, widths, locales, extra environment, compare stderr)."""
	terminal_widths = [0, 20, 40, 80, 132]
	both = ["C", "C.UTF-8"]
	utf_only = ["C.UTF-8"]
	option_sets = ["", "-1", "-C", "-x", "-m", "-l", "-i", "-F", "-iF", "-lF", "-li", "-w0", "-w30",
	               "-w 010", "-T0", "-T4", "-N", "-q", "-a", "-r", "-Cl", "-lC", "-xm", "-C1", "-Fx",
	               "-mi", "-Nl", "-qC", "-xw0", "-mw0", "-w1", "-w2", "-w3", "-w4", "-ld", "-lL", "-LF"]
	for tree, locales in (("many", both), ("odd", both), ("utf", utf_only), ("few", both),
	                      ("empty", both), ("one", both), ("short", both)):
		for options in option_sets:
			yield tree, options, "", terminal_widths + [None], locales, {}, False
	# Operands: files and directories together, headers, -d, -R.
	for options in ["", "-C", "-x", "-l", "-d", "-R", "-CR", "-F", "-iC"]:
		yield ".", options, "many/alpha many/beta few one empty", [80, None], both, {}, False
		yield ".", options, "odd/dir:colon odd/dir\\ space few", [80, None], both, {}, False
		yield ".", options, "many", [80, None], both, {}, False
		yield "many", options, "sub link dirlink dangling fifo", [80, None], both, {}, False
	yield ".", "-R", "", [80, 40, None], both, {}, False
	yield ".", "-d", "", [80, None], both, {}, False
	yield ".", "", "many -l", [80, None], both, {}, False
	yield ".", "", "many -x", [80, None], both, {}, False
	yield ".", "", "many -- -l", [80, None], both, {}, False
	# The width from the environment, which a terminal's own width overrides.
	for columns in ["50", "0", " 50", "abc", "", "0x28", "99999999999999999999999"]:
		for options in ["-C", "-x", "-m", "-1", "-l"]:
			yield "many", options, "", [None, 0, 80], both, {"COLUMNS": columns}, True
	for tabsize in ["4", "0", "x", "", "16"]:
		for options in ["-C", "-x", "-1"]:
			yield "many", options, "", [None, 80], both, {"TABSIZE": tabsize}, True
	# Messages of bad values.
	for options in ["-w abc", "-w -1", "-w ''", "-T x", "-T 99999999999999999999999", "-w 99999999999999999999999 -C"]:
		yield "many", options, "", [None], both, {}, True
	# The serial numbers of /, where mount points have small ones.
	for options in ["-i", "-i1", "-ix", "-im", "-li", "-iF"]:
		yield "/", options, "", [80, None], both, {}, False
	# POSIX order.
	yield ".", "", "many -l", [None, 80], both, {"POSIXLY_CORRECT": "1"}, False


COLUMN_OPTIONS = ("-C", "-x", "-m", "-Cl", "-lC", "-xm", "-Fx", "-mi", "-qC", "-w0", "-w30", "-w 010",
                  "-T0", "-T4", "-w1", "-w2", "-w3", "-w4", "-xw0", "-mw0", "-iF")


def known_difference(tree: str, options: str, operands: str, width, locale: str) -> str:
	"""Names the known, accepted difference a case falls under, or ''."""
	# Kei takes names as UTF-8 in the C locale too (user, 2026-09-29): a name
	# with a control character has GNU's unmeasurable width in columns to a pipe.
	if tree == "odd" and locale == "C" and width is None and options in COLUMN_OPTIONS:
		return "utf8-in-c-locale"
	# -R over every tree reaches the UTF-8 names, which GNU escapes in C.
	if tree == "." and "R" in options and operands == "" and locale == "C":
		return "utf8-in-c-locale"
	# -L keeps listing a dangling link as a link (the POSIX ls before ws086),
	# where GNU ls reports it and ends with 1.
	if tree == "many" and "L" in options and operands == "":
		return "dangling-with-L"
	return ""


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("ours")
	parser.add_argument("--gnu", default=shutil.which("ls"))
	parser.add_argument("--verbose", action="store_true")
	arguments = parser.parse_args()
	ours = os.path.abspath(arguments.ours)
	root = tempfile.mkdtemp(prefix="ws086-")
	failed = 0
	count = 0
	skipped = {}
	try:
		make_tree(root)
		for tree, options, operands, widths, locales, extra, with_error in cases():
			directory = root if tree == "." else (tree if tree.startswith("/") else os.path.join(root, tree))
			for locale in locales:
				for width in widths:
					environment = {"PATH": "/usr/bin:/bin", "LC_ALL": locale, "TZ": "UTC"}
					environment.update(extra)
					line = "%s %s %s" % ("ls", options, operands)
					gnu = run(["sh", "-c", "exec %s %s %s" % (arguments.gnu, options, operands)],
					          directory, width, environment)
					kei = run(["sh", "-c", "exec %s %s %s" % (ours, options, operands)],
					          directory, width, environment)
					count += 1
					known = known_difference(tree, options, operands, width, locale)
					if known:
						skipped[known] = skipped.get(known, 0) + 1
						continue
					same = gnu[0] == kei[0] and gnu[2] == kei[2]
					# GNU quotes a value in a message with ‘ ’ in a UTF-8 locale; Kei with '.
					if with_error and locale == "C" and gnu[1] != kei[1]:
						same = False
					if not same:
						failed += 1
						print("DIFF tree=%s locale=%s width=%s env=%s: %s" % (tree, locale, width, extra, line))
						if arguments.verbose:
							print("  gnu: %r status=%d err=%r" % (gnu[0][:600], gnu[2], gnu[1][:200]))
							print("  kei: %r status=%d err=%r" % (kei[0][:600], kei[2], kei[1][:200]))
	finally:
		shutil.rmtree(root)
	print("compared=%d differ=%d known=%s" % (count, failed, skipped))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
