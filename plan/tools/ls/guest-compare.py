#!/usr/bin/env python3
"""ws086-p002: compares ls in an amd64 guest with GNU ls on the host.

The same trees of names are made in the guest (over SSH, plan/tools/guest)
and on the host.  The guest's ls runs on a pseudo-terminal (ssh -tt, with
stty setting its width) and into a pipe; the host's GNU ls runs the same
way (plan/tools/ls/tty-run.py's method) in C.UTF-8.  The terminal turns
\\n into \\r\\n on the way out of the guest, which is undone before comparing.

  GUEST_RUNTIME=... python3 plan/tools/ls/guest-compare.py [--gnu /bin/ls]

The guest must be running (plan/tools/guest/guest.sh start IMAGE).
Prints one line per difference and a count; exits 1 when anything differs.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

import argparse
import fcntl
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import termios
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "guest"))
import guest  # noqa: E402

GREEK = ("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi "
         "omicron pi rho sigma tau upsilon phi chi psi omega").split()

ODD = [b"plain", b"with space", b"it's", b"dollar$x", b"ctl\x01x", b"nl\nx", b"#hash",
       b"mid#hash", b"~tilde", b"a=b", b"q?x", b"{", b"b]", b"tab\tx", b"\"dq\"",
       b"both'\"", b"a\x01'b", b"a@b", b"back\\slash", b"del\x7f", b"'lead",
       "あいう".encode(), "日本語のファイル".encode(), "café au lait".encode(),
       b"bad\xffx", "中 文".encode(), "it's 日本".encode()]


def octal(name: bytes) -> str:
	"""Writes a name as a printf format of octal escapes."""
	return "".join("\\%03o" % byte for byte in name)


def tree_script(root: str) -> str:
	"""Returns a sh script that makes the trees under root."""
	lines = ["set -e", "rm -rf %s" % shlex.quote(root), "mkdir -p %s/many/sub %s/odd %s/empty" % ((shlex.quote(root),) * 3)]
	for name in GREEK + ["a-very-long-file-name-that-is-wide"]:
		lines.append(": > %s/many/%s" % (shlex.quote(root), name))
	lines.append("chmod 755 %s/many/beta" % shlex.quote(root))
	lines.append("ln -s alpha %s/many/link" % shlex.quote(root))
	for name in ODD:
		lines.append(": > \"%s/odd/$(printf '%sX')\"" % (root, octal(name)))
		lines.append("mv \"%s/odd/$(printf '%sX')\" \"%s/odd/$(printf '%s')\"" % (root, octal(name), root, octal(name)))
	return "\n".join(lines) + "\n"


def host_run(command: str, directory: str, width) -> bytes:
	"""Runs GNU ls on the host, on a terminal of the given width or into a pipe."""
	environment = {"PATH": "/usr/bin:/bin", "LC_ALL": "C.UTF-8", "TZ": "UTC"}
	if width is None:
		return subprocess.run(["sh", "-c", command], cwd=directory, env=environment,
		                      stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=20).stdout
	master, slave = os.openpty()
	fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, width, 0, 0))
	attributes = termios.tcgetattr(slave)
	attributes[1] &= ~termios.OPOST
	termios.tcsetattr(slave, termios.TCSANOW, attributes)
	child = subprocess.Popen(["sh", "-c", command], cwd=directory, env=environment,
	                         stdin=subprocess.DEVNULL, stdout=slave, stderr=subprocess.DEVNULL)
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
	child.wait(timeout=20)
	os.close(master)
	return bytes(output)


def guest_run(session: dict, command: str, directory: str, width) -> bytes:
	"""Runs ls in the guest, on its terminal of the given width or into a pipe."""
	if width is None:
		remote = "cd %s && %s | cat" % (shlex.quote(directory), command)
		ssh = ["ssh", *guest.ssh_options(session), "root@127.0.0.1", "--", remote]
	else:
		# ls is not the last command, so that the shell waits for it: when the
		# shell execs it, its output is lost now and then as the session ends
		# (seen 1 time in 8 to 25, and never with a command after it).
		remote = "stty cols %d rows 24; cd %s && %s; true" % (width, shlex.quote(directory), command)
		ssh = ["ssh", "-tt", *guest.ssh_options(session), "root@127.0.0.1", "--", remote]
	result = subprocess.run(ssh, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
	                        stderr=subprocess.DEVNULL, timeout=60)
	output = result.stdout
	if width is not None:
		output = output.replace(b"\r\n", b"\n")
	return output


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--gnu", default=shutil.which("ls"))
	parser.add_argument("--verbose", action="store_true")
	arguments = parser.parse_args()
	session = guest.read_session()
	guest_root = "/tmp/ws086"
	host_root = tempfile.mkdtemp(prefix="ws086-guest-")
	failed = 0
	count = 0
	try:
		subprocess.run(["sh", "-c", tree_script(host_root)], check=True)
		made = subprocess.run(["ssh", *guest.ssh_options(session), "root@127.0.0.1", "--", "sh -s"],
		                      input=tree_script(guest_root).encode(), timeout=120)
		if made.returncode != 0:
			print("could not make the trees in the guest")
			return 1
		for tree in ("many", "odd", "empty"):
			for options in ["", "-1", "-C", "-x", "-m", "-F", "-w0", "-w30", "-N", "-q", "-xF", "-a"]:
				for width in [None, 20, 40, 80, 132]:
					count += 1
					gnu = host_run("exec %s %s" % (arguments.gnu, options), os.path.join(host_root, tree), width)
					kei = guest_run(session, "ls %s" % options, "%s/%s" % (guest_root, tree), width)
					if gnu != kei:
						failed += 1
						print("DIFF tree=%s width=%s: ls %s" % (tree, width, options))
						if arguments.verbose:
							print("  gnu: %r" % gnu[:500])
							print("  kei: %r" % kei[:500])
	finally:
		shutil.rmtree(host_root)
	print("compared=%d differ=%d" % (count, failed))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
