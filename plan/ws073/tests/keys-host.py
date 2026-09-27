#!/usr/bin/env python3
"""Presses keys on a guest's keyboard through QMP (BUG-070).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    keys-host.py QMP_SOCKET [DEVICE]

Presses and releases, in order, the keys whose USB HID usages BUG-070 is
about, with a pause after each, and prints the evdev code each should
become (EXPECTED lines), for comparison with plan/ws073/tests/evdev-keys.c
in the guest.  DEVICE is the QOM path of the keyboard to send to (default:
whichever keyboard QEMU routes to).
"""
import json
import socket
import sys
import time

# QEMU qcode, and the evdev code the USB HID driver should report for it.
KEYS = [
	("num_lock", 69), ("kp_divide", 98), ("kp_multiply", 55),
	("kp_subtract", 74), ("kp_add", 78), ("kp_enter", 96),
	("kp_1", 79), ("kp_2", 80), ("kp_3", 81), ("kp_4", 75), ("kp_5", 76),
	("kp_6", 77), ("kp_7", 71), ("kp_8", 72), ("kp_9", 73), ("kp_0", 82),
	("kp_decimal", 83), ("print", 99), ("scroll_lock", 70), ("pause", 119),
	("less", 86), ("menu", 127), ("ro", 89), ("yen", 124),
	("henkan", 92), ("muhenkan", 94), ("katakanahiragana", 93),
	("f13", 183), ("f24", 194), ("a", 30), ("caps_lock", 58),
]


def command(stream, execute, arguments=None):
	"""Sends one QMP command and returns its reply."""
	request = {"execute": execute}
	if arguments is not None:
		request["arguments"] = arguments
	stream.write(json.dumps(request) + "\n")
	stream.flush()
	while True:
		reply = json.loads(stream.readline())
		if "return" in reply or "error" in reply:
			return reply


def main() -> int:
	"""Presses every key once."""
	connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	connection.connect(sys.argv[1])
	stream = connection.makefile("rw")
	stream.readline()
	command(stream, "qmp_capabilities")
	for qcode, code in KEYS:
		for down in (True, False):
			arguments = {"events": [{"type": "key", "data": {
				"down": down, "key": {"type": "qcode", "data": qcode}}}]}
			if len(sys.argv) > 2:
				arguments["device"] = sys.argv[2]
			reply = command(stream, "input-send-event", arguments)
			if "error" in reply:
				print("ERROR %s %s" % (qcode, reply["error"]))
			time.sleep(0.05)
		print("EXPECTED %s %d" % (qcode, code))
		time.sleep(0.1)
	return 0


if __name__ == "__main__":
	sys.exit(main())
