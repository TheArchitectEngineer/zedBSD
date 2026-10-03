#!/usr/bin/env python3
"""ws001-p036: checks zedBSD stty (host build) on a pseudo terminal.

Each check runs stty with a fresh pty as its standard input and reads the
result back with the termios module (the host build uses the host's
termios, so the constants are the host's), or with GNU stty -a.  Settings
a Linux pty refuses (parity, character sizes but cs8, -cread, differing
speeds) are checked against GNU stty on another fresh pty instead: the
same status and the same resulting settings.

  python3 plan/ws001/tests/stty-host-test.py [--bin build/ws001/bin]
"""

import argparse
import fcntl
import os
import struct
import subprocess
import sys
import termios

T = termios
MODES = [
	("parenb", 2, T.PARENB), ("parodd", 2, T.PARODD), ("hupcl", 2, T.HUPCL),
	("cstopb", 2, T.CSTOPB), ("cread", 2, T.CREAD), ("clocal", 2, T.CLOCAL),
	("ignbrk", 0, T.IGNBRK), ("brkint", 0, T.BRKINT), ("ignpar", 0, T.IGNPAR),
	("parmrk", 0, T.PARMRK), ("inpck", 0, T.INPCK), ("istrip", 0, T.ISTRIP),
	("inlcr", 0, T.INLCR), ("igncr", 0, T.IGNCR), ("icrnl", 0, T.ICRNL),
	("ixon", 0, T.IXON), ("ixoff", 0, T.IXOFF), ("ixany", 0, T.IXANY),
	("opost", 1, T.OPOST), ("onlcr", 1, T.ONLCR), ("ocrnl", 1, T.OCRNL),
	("onocr", 1, T.ONOCR), ("onlret", 1, T.ONLRET), ("ofill", 1, T.OFILL),
	("ofdel", 1, T.OFDEL), ("isig", 3, T.ISIG), ("icanon", 3, T.ICANON),
	("iexten", 3, T.IEXTEN), ("echo", 3, T.ECHO), ("echoe", 3, T.ECHOE),
	("echok", 3, T.ECHOK), ("echonl", 3, T.ECHONL), ("noflsh", 3, T.NOFLSH),
	("tostop", 3, T.TOSTOP),
]
GROUPS = [
	("cs5", 2, T.CSIZE, T.CS5), ("cs6", 2, T.CSIZE, T.CS6), ("cs7", 2, T.CSIZE, T.CS7), ("cs8", 2, T.CSIZE, T.CS8),
	("nl1", 1, T.NLDLY, T.NL1), ("nl0", 1, T.NLDLY, T.NL0),
	("cr2", 1, T.CRDLY, T.CR2), ("cr0", 1, T.CRDLY, T.CR0),
	("tab3", 1, T.TABDLY, T.TAB3), ("tab1", 1, T.TABDLY, T.TAB1), ("tab0", 1, T.TABDLY, T.TAB0),
	("bs1", 1, T.BSDLY, T.BS1), ("bs0", 1, T.BSDLY, T.BS0),
	("vt1", 1, T.VTDLY, T.VT1), ("vt0", 1, T.VTDLY, T.VT0),
	("ff1", 1, T.FFDLY, T.FF1), ("ff0", 1, T.FFDLY, T.FF0),
]


class Pty:
	def __init__(self):
		self.master, self.slave = os.openpty()

	def close(self):
		os.close(self.master)
		os.close(self.slave)

	def run(self, program, *arguments):
		result = subprocess.run([program] + list(arguments), stdin=self.slave, capture_output=True, text=True)
		return result.returncode, result.stdout, result.stderr

	def attributes(self):
		return termios.tcgetattr(self.slave)


def byte(value):
	"""A control character as termios gives it: bytes, or an int for min and time."""
	if isinstance(value, bytes):
		return value[0]
	return value


def same_as_gnu(stty, arguments):
	"""Runs GNU stty and zedBSD stty on fresh ptys; returns whether both agree."""
	results = []
	for program in ("/usr/bin/stty", stty):
		pty = Pty()
		status, _, _ = pty.run(program, *arguments)
		results.append((status != 0, pty.attributes()))
		pty.close()
	return results[0] == results[1], results


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--bin", default="build/ws001/bin")
	options = parser.parse_args()
	stty = os.path.abspath(os.path.join(options.bin, "stty"))
	passed = 0
	failed = 0

	def check(name, condition, detail=""):
		nonlocal passed, failed
		if condition:
			passed += 1
		else:
			failed += 1
			print("FAIL %s %s" % (name, detail))

	# The settings a Linux pty refuses, against GNU stty.
	for arguments in (["parenb"], ["-parenb"], ["parodd"], ["cread"], ["-cread"], ["cs5"], ["cs6"], ["cs7"],
			  ["evenp"], ["oddp"], ["-parity"], ["ospeed", "4800"], ["ispeed", "600"]):
		same, results = same_as_gnu(stty, arguments)
		check("as GNU " + " ".join(arguments), same, str(results))

	# Every single mode off and on.
	for name, field, bit in MODES:
		if name in ("parenb", "parodd", "cread"):
			continue
		pty = Pty()
		status, _, error = pty.run(stty, "-" + name)
		off = pty.attributes()[field] & bit
		status2, _, error2 = pty.run(stty, name)
		on = pty.attributes()[field] & bit
		check("mode " + name, status == 0 and status2 == 0 and off == 0 and on == bit, error + error2)
		pty.close()

	# Every member of a group, and that a member cannot be negated.
	for name, field, mask, value in GROUPS:
		if name in ("cs5", "cs6", "cs7"):
			continue
		pty = Pty()
		status, _, error = pty.run(stty, name)
		check("group " + name, status == 0 and pty.attributes()[field] & mask == value, error)
		pty.close()
	pty = Pty()
	status, _, _ = pty.run(stty, "-cs8")
	check("-cs8 refused", status == 1)
	pty.close()

	# Speeds.
	pty = Pty()
	status, _, _ = pty.run(stty, "19200")
	attributes = pty.attributes()
	check("speed", status == 0 and attributes[4] == T.B19200 and attributes[5] == T.B19200)
	status, out, _ = pty.run(stty, "-a")
	check("-a one speed", out.startswith("speed 19200 baud;"), out.split("\n")[0])
	status, _, _ = pty.run(stty, "12345")
	check("bad speed", status == 1)
	pty.close()

	# Control characters, min and time.
	pty = Pty()
	status, _, error = pty.run(stty, "intr", "^A", "erase", "^?", "kill", "undef", "eof", "x", "quit", "^-", "min", "5", "time", "3")
	cc = pty.attributes()[6]
	check("characters", status == 0 and byte(cc[T.VINTR]) == 1 and byte(cc[T.VERASE]) == 0x7f and byte(cc[T.VEOF]) == ord("x") and byte(cc[T.VMIN]) == 5 and byte(cc[T.VTIME]) == 3 and byte(cc[T.VKILL]) == byte(cc[T.VQUIT]), error + str(cc))
	status, out, _ = pty.run(stty, "-a")
	check("-a characters", "intr = ^A;" in out and "erase = ^?;" in out and "eof = x;" in out and "min = 5; time = 3;" in out, out)
	status, _, _ = pty.run(stty, "intr", "abc")
	check("bad character", status == 1)
	status, _, _ = pty.run(stty, "min")
	check("missing value", status == 1)
	status, _, _ = pty.run(stty, "min", "300")
	check("min too big", status == 1)
	pty.close()

	# Combinations.
	pty = Pty()
	pty.run(stty, "raw")
	a = pty.attributes()
	check("raw", a[0] & (T.ICRNL | T.IXON | T.BRKINT) == 0 and a[1] & T.OPOST == 0 and a[3] & (T.ISIG | T.ICANON | T.IEXTEN) == 0 and byte(a[6][T.VMIN]) == 1 and byte(a[6][T.VTIME]) == 0)
	pty.run(stty, "cooked")
	a = pty.attributes()
	check("cooked", a[0] & (T.ICRNL | T.IXON | T.BRKINT) == (T.ICRNL | T.IXON | T.BRKINT) and a[1] & T.OPOST and a[3] & (T.ISIG | T.ICANON) == T.ISIG | T.ICANON)
	pty.run(stty, "raw")
	pty.run(stty, "-raw")
	a = pty.attributes()
	check("-raw", a[3] & T.ICANON and a[1] & T.OPOST)
	pty.run(stty, "nl")
	a = pty.attributes()
	check("nl", a[0] & T.ICRNL == 0 and a[1] & T.ONLCR == 0)
	pty.run(stty, "inlcr", "igncr", "ocrnl")
	pty.run(stty, "-nl")
	a = pty.attributes()
	check("-nl", a[0] & (T.ICRNL | T.INLCR | T.IGNCR) == T.ICRNL and a[1] & (T.ONLCR | T.OCRNL) == T.ONLCR)
	pty.run(stty, "erase", "a", "kill", "b")
	pty.run(stty, "ek")
	a = pty.attributes()
	check("ek", a[6][T.VERASE] == b"\x7f" and a[6][T.VKILL] == b"\x15")
	pty.run(stty, "-tabs")
	check("-tabs", pty.attributes()[1] & T.TABDLY == T.TAB3)
	pty.run(stty, "tabs")
	check("tabs", pty.attributes()[1] & T.TABDLY == T.TAB0)
	pty.run(stty, "hup")
	check("hup", pty.attributes()[2] & T.HUPCL)
	pty.run(stty, "-hup")
	check("-hup", pty.attributes()[2] & T.HUPCL == 0)
	pty.run(stty, "raw", "-echo", "tostop", "intr", "^B", "ocrnl", "tab3")
	pty.run(stty, "sane")
	a = pty.attributes()
	check("sane", a[3] & (T.ECHO | T.ICANON | T.ISIG | T.TOSTOP) == T.ECHO | T.ICANON | T.ISIG and a[1] & (T.OPOST | T.ONLCR | T.OCRNL | T.TABDLY) == T.OPOST | T.ONLCR and a[0] & T.ICRNL and a[6][T.VINTR] == b"\x03")
	pty.close()

	# -g gives the settings back.
	pty = Pty()
	before = pty.attributes()
	status, saved, _ = pty.run(stty, "-g")
	pty.run(stty, "raw", "-echo", "intr", "^B", "ocrnl", "tab3", "1200")
	status2, _, error = pty.run(stty, saved.strip())
	after = pty.attributes()
	check("-g round trip", status == 0 and status2 == 0 and before == after, error + saved)
	status, _, _ = pty.run(stty, "1:2:3")
	check("bad saved", status == 1)
	pty.close()

	# Window size.
	pty = Pty()
	status, _, _ = pty.run(stty, "rows", "30", "cols", "100")
	rows, columns = struct.unpack("HHHH", fcntl.ioctl(pty.slave, termios.TIOCGWINSZ, b"\0" * 8))[:2]
	check("rows cols", status == 0 and rows == 30 and columns == 100)
	status, out, _ = pty.run(stty, "columns", "90", "size")
	check("columns size", status == 0 and out == "30 90\n", out)
	status, out, _ = pty.run(stty, "-a")
	check("-a size", "rows 30; columns 90;" in out, out.split("\n")[0])
	pty.close()

	# -a shows each mode, and GNU stty reads what zedBSD stty set.
	pty = Pty()
	pty.run(stty, "-echo", "tostop", "-opost")
	status, out, _ = pty.run(stty, "-a")
	tokens = out.split()
	check("-a modes", "-echo" in tokens and "tostop" in tokens and "-opost" in tokens and "cs8" in tokens, out)
	status, out, _ = pty.run(stty)
	check("no operand", "-echo" in out.split())
	status, out, _ = pty.run("/usr/bin/stty", "-a")
	tokens = out.split()
	check("GNU reads", "-echo" in tokens and "tostop" in tokens and "-opost" in tokens, out)
	pty.close()

	# Errors.
	pty = Pty()
	status, _, error = pty.run(stty, "bogus")
	check("unknown operand", status == 1 and "bogus" in error, error)
	status, _, _ = pty.run(stty, "-a", "-g")
	check("-a -g", status == 1)
	pty.close()
	result = subprocess.run([stty], stdin=subprocess.DEVNULL, capture_output=True, text=True)
	check("not a terminal", result.returncode == 1 and result.stderr != "", result.stderr)

	print("TOTAL %d/%d" % (passed, passed + failed))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
