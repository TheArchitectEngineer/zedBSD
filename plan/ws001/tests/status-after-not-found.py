#!/usr/bin/env python3
"""ws001-p031: checks, on a guest's serial console, that $? is 127 in the
next interactive command after a command that was not found (the q136
observation in ws.md), with each command typed as a line of its own.

  python3 plan/ws001/tests/status-after-not-found.py SERIAL_SOCKET

The guest must be booted to the login prompt (plan/tools/guest/guest.py
start); the account is logged in first.  Prints the two answers and exits
0 when they are 127 and 126.
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/guest"))
import serial  # noqa: E402


def main() -> int:
	console = serial.Console(sys.argv[1], 60.0)
	serial.login(console, "root")
	answers = []
	for missing in ("/bin/nosuch_ws001", "/etc/passwd"):
		# The failing command, then its status on a line of its own.
		console.send(missing)
		console.read_until(serial.PROMPT)
		console.forget()
		console.send("echo WS001STATUS=$?")
		output = console.read_until(re.compile(r"WS001STATUS=(\d+)\r?\n"))
		match = re.search(r"WS001STATUS=(\d+)\r?\n", output)
		answers.append(int(match.group(1)))
		console.read_until(serial.PROMPT)
		console.forget()
	print("not found: %d, not executable: %d" % (answers[0], answers[1]))
	return 0 if answers == [127, 126] else 1


if __name__ == "__main__":
	sys.exit(main())
