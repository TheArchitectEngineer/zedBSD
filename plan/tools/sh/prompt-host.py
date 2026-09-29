#!/usr/bin/env python3
# ws087-p006: the default prompt of the host build of sh (no PS1) shows the
# home directory as ~, as bash's \w does, through a pty.
# Usage: prompt-host.py SHELL   (build it with plan/tools/sh/build-host-sh.sh)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os, pty, re, select, sys, tempfile, time

SH = os.path.abspath(sys.argv[1])
results = []


def prompt_after(home, command):
    """Runs one command in a new shell and returns the directory its next prompt shows."""
    env = {"PATH": "/usr/bin:/bin", "TERM": "xterm", "HISTFILE": ""}
    if home is not None:
        env["HOME"] = home
    pid, fd = pty.fork()
    if pid == 0:
        os.execve(SH, [SH], env)
    out = b""
    os.write(fd, command.encode() + b"\r")
    time.sleep(0.4)
    os.write(fd, b"exit\r")
    end = time.time() + 2
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                data = os.read(fd, 65536)
            except OSError:
                break
            if not data:
                break
            out += data
    os.waitpid(pid, 0)
    prompts = re.findall(rb"@[^:\r\n]*:([^\r\n]*?)\$ ", out)
    return prompts[1].decode() if len(prompts) > 1 else repr(out)


def check(name, got, want):
    ok = got == want
    results.append(ok)
    print(("ok " if ok else "FAIL ") + name + ("" if ok else ": got %r, want %r" % (got, want)))


with tempfile.TemporaryDirectory() as base:
    home = os.path.realpath(base) + "/kei"
    os.makedirs(home + "/docs/deep")
    os.makedirs(home + "2")
    check("in HOME", prompt_after(home, "cd " + home), "~")
    check("under HOME", prompt_after(home, "cd " + home + "/docs/deep"), "~/docs/deep")
    check("HOME with a trailing slash", prompt_after(home + "/", "cd " + home + "/docs"), "~/docs")
    check("a sibling that shares the prefix", prompt_after(home, "cd " + home + "2"), home + "2")
    check("outside HOME", prompt_after(home, "cd /usr"), "/usr")
    check("HOME=/ is not replaced", prompt_after("/", "cd /usr"), "/usr")
    check("empty HOME is not replaced", prompt_after("", "cd /usr"), "/usr")
    check("unset HOME is not replaced", prompt_after(None, "cd /usr"), "/usr")

print("prompt-host: %d/%d" % (sum(results), len(results)))
sys.exit(0 if all(results) else 1)
