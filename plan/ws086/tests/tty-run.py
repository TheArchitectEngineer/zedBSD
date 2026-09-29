#!/usr/bin/env python3
"""ws086: runs a command with its standard output on a pseudo-terminal of a
given width and writes what it printed, byte for byte, to standard output.
Standard error stays on the caller's standard error.

  python3 plan/ws086/tests/tty-run.py COLUMNS COMMAND [ARG...]

COLUMNS 0 leaves the terminal's window size at 0x0 (as a serial line may).
The exit status is the command's.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

import fcntl
import os
import struct
import subprocess
import sys
import termios


def main() -> int:
	columns = int(sys.argv[1])
	command = sys.argv[2:]
	master, slave = os.openpty()
	fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, columns, 0, 0))
	attributes = termios.tcgetattr(slave)
	# Leaves the output as the program wrote it (no \n -> \r\n).
	attributes[1] &= ~termios.OPOST
	termios.tcsetattr(slave, termios.TCSANOW, attributes)
	child = subprocess.Popen(command, stdout=slave, stdin=subprocess.DEVNULL)
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
	status = child.wait()
	os.close(master)
	sys.stdout.buffer.write(output)
	return status


if __name__ == "__main__":
	sys.exit(main())
