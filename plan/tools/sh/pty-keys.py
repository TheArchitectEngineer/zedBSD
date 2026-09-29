#!/usr/bin/env python3
# ws087: types keys at the guest's /bin/sh through ssh -tt (a pty on both
# ends) and prints what the terminal receives after each key, so the line
# editor's reaction to arrow keys and Tab can be read.
# Usage: GUEST_RUNTIME=... pty-keys.py [KEYS...]   (KEYS as Python bytes literals)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import ast, json, os, pty, select, sys, time
root = os.path.dirname(os.path.abspath(__file__)) + "/../../.."
runtime = os.environ.get("GUEST_RUNTIME", root + "/build/guest")
session = json.load(open(os.path.join(runtime, "session.json")))
key = os.path.abspath(root + "/plan/tmp/guest/id_ed25519")
argv = ["ssh", "-tt", "-i", key, "-p", str(session["ssh_port"]),
        "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
        "-o", "LogLevel=ERROR", "-o", "BatchMode=yes", "root@127.0.0.1",
        os.environ.get("REMOTE", "PS1='$ ' exec /bin/sh -i")]
pid, fd = pty.fork()
if pid == 0:
    os.execvp("ssh", argv)
def readall(t):
    out = b""
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                out += os.read(fd, 65536)
            except OSError:
                break
    return out
print("start", readall(4.0))
keys = [ast.literal_eval(k) for k in sys.argv[1:]] or [
    b"echo one\r", b"echo two\r", b"\x1b[A", b"\x1b[A", b"\r"]
for k in keys:
    os.write(fd, k)
    print(repr(k), "->", readall(1.0))
os.write(fd, b"exit\r")
readall(1.0)
