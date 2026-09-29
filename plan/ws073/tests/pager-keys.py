#!/usr/bin/env python3
"""BUG-104: less pages with Ctrl-F and Ctrl-B (and f), more keeps its keys.

Runs a host build of less and more (the shared userland/base/common/pager.c)
on a pseudo terminal of 24 rows over a 200-line file, sends keys, and reads
the position from the prompt each key leaves ("NAME  TOP/200" for less,
"--More--(BOTTOM/200)" for more).

    python3 plan/ws073/tests/pager-keys.py LESS_BINARY MORE_BINARY
    python3 plan/ws073/tests/pager-keys.py --ssh PORT   (the guest's /bin/less and
        /bin/more over "ssh -tt root@127.0.0.1", the file made at /tmp/pager-keys.txt)

Prints PAGER-KEYS:PASS.  Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import fcntl, os, pty, re, select, struct, subprocess, sys, tempfile, termios, time

ROWS, COLS = 24, 80


def prompt_after(fd, pattern):
	"""Reads output until it goes quiet and returns the last prompt value."""
	data = b""
	deadline = time.time() + 5
	while time.time() < deadline:
		ready, _, _ = select.select([fd], [], [], 0.3)
		if not ready:
			if re.search(pattern, data):
				break
			continue
		try:
			chunk = os.read(fd, 65536)
		except OSError:
			break
		if not chunk:
			break
		data += chunk
	found = re.findall(pattern, data)
	return int(found[-1]) if found else None


KEY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../tmp/guest/id_ed25519")


def ssh_command(port):
	"""Returns the ssh words that reach the WS073 guest."""
	return ["ssh", "-i", KEY, "-p", str(port), "-o", "StrictHostKeyChecking=no",
		"-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR", "-o", "BatchMode=yes"]


def session(binary, path, keys, pattern):
	"""Runs one pager and returns the position after the start and each key."""
	pid, fd = pty.fork()
	if pid == 0:
		if isinstance(binary, list):
			os.execvp(binary[0], binary + [path])
		os.execv(binary, [binary, path])
	fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
	os.kill(pid, 28)  # SIGWINCH, in case the size was read before it was set
	time.sleep(1.5 if isinstance(binary, list) else 0)
	positions = [prompt_after(fd, pattern)]
	for key in keys:
		os.write(fd, key)
		positions.append(prompt_after(fd, pattern))
	os.write(fd, b"q")
	time.sleep(0.2)
	try:
		os.waitpid(pid, 0)
	except ChildProcessError:
		pass
	os.close(fd)
	return positions


def main():
	if sys.argv[1] == "--ssh":
		base = ssh_command(sys.argv[2])
		subprocess.run(base + ["root@127.0.0.1", "--",
			"i=1; while [ $i -le 200 ]; do echo line $i; i=$((i+1)); done > /tmp/pager-keys.txt"], check=True)
		less = base + ["-tt", "root@127.0.0.1", "--", "/bin/less"]
		more = base + ["-tt", "root@127.0.0.1", "--", "/bin/more"]
		path = "/tmp/pager-keys.txt"
		local = False
	else:
		less, more = sys.argv[1], sys.argv[2]
		with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
			for i in range(1, 201):
				f.write(f"line {i}\n")
			path = f.name
		local = True
	failures = 0
	# less: the prompt shows the top line; one screen is ROWS - 1 lines.
	keys = [b"\x06", b"\x06", b"\x02", b"f", b"b", b" ", b"\x02", b"\x02"]
	want = [1, 24, 47, 24, 47, 24, 47, 24, 1]
	got = session(less, path, keys, rb"  (\d+)/200")
	print("less", got)
	if got != want:
		print(f"PAGER-KEYS:FAIL less {got} expected {want}")
		failures += 1
	# more: the prompt shows the bottom line; Ctrl-F and f are not more's keys here, space is.
	keys = [b"\x06", b"f", b" ", b"b"]
	want = [23, 23, 23, 46, 23]
	got = session(more, path, keys, rb"--More--\((\d+)/200\)")
	print("more", got)
	if got != want:
		print(f"PAGER-KEYS:FAIL more {got} expected {want}")
		failures += 1
	if local:
		os.unlink(path)
	print("PAGER-KEYS:PASS" if failures == 0 else "PAGER-KEYS:FAIL")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
