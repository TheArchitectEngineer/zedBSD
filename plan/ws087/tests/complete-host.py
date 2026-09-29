#!/usr/bin/env python3
# ws087-p003: Tab completion of the host build of sh, through a pty.
# Command names (builtins, aliases, functions, PATH, directories here),
# paths (quoting, directories, dot files, ~), the common part and the list
# on a second Tab, the question for a long list, vi insert mode, and no
# completion inside an open quote.
# Usage: complete-host.py SHELL   (build it with plan/tools/sh/build-host-sh.sh)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os, pty, select, sys, tempfile, time

SH = os.path.abspath(sys.argv[1])
results = []


class Shell:
    """One interactive shell on a pty."""

    def __init__(self, env, cwd):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(cwd)
            os.execve(SH, [SH], env)
        self.read(0.5)

    def read(self, t=0.4):
        out = b""
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    out += os.read(self.fd, 65536)
                except OSError:
                    break
        return out

    def send(self, data, t=0.4):
        os.write(self.fd, data)
        return self.read(t)

    def clear(self):
        # Ctrl-E, then Ctrl-U: an empty line for the next case.
        self.send(b"\x05\x15", 0.2)

    def exit(self):
        self.send(b"\x05\x15exit\r", 0.3)
        os.waitpid(self.pid, 0)
        os.close(self.fd)


def check(name, ok, detail):
    results.append(bool(ok))
    print(("ok " if ok else "FAIL ") + name + ("" if ok else ": " + repr(detail)[-400:]))


with tempfile.TemporaryDirectory() as base:
    base = os.path.realpath(base)
    home = base + "/home"
    bin_dir = base + "/bin"
    work = home + "/work"
    for d in (bin_dir, work + "/data", work + "/beta dir"):
        os.makedirs(d)
    for name, mode in (("zzfoo", 0o755), ("zzfoobar", 0o755), ("zzfoonx", 0o644)):
        with open(bin_dir + "/" + name, "w") as f:
            f.write("#!/bin/sh\necho %s\n" % name)
        os.chmod(bin_dir + "/" + name, mode)
    for name in ("alpha.txt", "alpine.txt", ".hidden"):
        open(work + "/" + name, "w").close()
    with open(work + "/exe.sh", "w") as f:
        f.write("#!/bin/sh\n")
    os.chmod(work + "/exe.sh", 0o755)
    env = {"PATH": bin_dir + ":/usr/bin:/bin", "HOME": home, "PS1": "$ ", "TERM": "xterm", "HISTFILE": ""}

    s = Shell(env, work)

    out = s.send(b"zzf\t")
    check("command: common part of zzfoo and zzfoobar", out.endswith(b"zzfoo\a"), out)
    out = s.send(b"\t")
    check("command: second Tab lists both, not the file that is not executable",
          b"zzfoo" in out and b"zzfoobar" in out and b"zzfoonx" not in out and out.rstrip().endswith(b"$ zzfoo"), out)
    s.clear()

    out = s.send(b"zzfoob\t")
    check("command: one match gets a blank", out.endswith(b"zzfoobar "), out)
    s.clear()

    out = s.send(b"ech\t")
    check("builtin", out.endswith(b"echo "), out)
    s.clear()

    s.send(b"alias qqal='echo AL'\r")
    out = s.send(b"qqa\t")
    check("alias", out.endswith(b"qqal "), out)
    out = s.send(b"\r")
    check("alias completed runs", b"\r\nAL\r\n" in out, out)

    s.send(b"qqfn() { echo FN; }\r")
    out = s.send(b"qqf\t")
    check("function", out.endswith(b"qqfn "), out)
    s.clear()

    out = s.send(b"cat alp\t")
    check("path: common part", out.endswith(b"cat alp\a") or out.endswith(b"\a"), out)
    out = s.send(b"h\t")
    check("path: one match", out.endswith(b"a.txt "), out)
    s.clear()

    out = s.send(b"cd da\t")
    check("directory: slash and no blank", out.endswith(b"data/"), out)
    s.clear()

    out = s.send(b"ls bet\t")
    check("name with a blank is quoted", out.endswith(b"beta\\ dir/"), out)
    out = s.send(b"\r")
    check("quoted name runs", b"\r\n" in out and b"No such" not in out and b"cannot" not in out, out)

    out = s.send(b"ls .h\t")
    check("dot file when asked for", out.endswith(b".hidden "), out)
    s.clear()

    s.send(b"ls \t")
    out = s.send(b"\t")
    check("list of a directory leaves out dot files, shows directories with a slash",
          b"alpha.txt" in out and b"data/" in out and b"beta dir/" in out and b".hidden" not in out, out)
    s.clear()

    out = s.send(b"./ex\t")
    check("command with a slash", out.endswith(b"./exe.sh "), out)
    s.clear()

    out = s.send(b"ls ~/wo\t")
    check("tilde", out.endswith(b"~/work/"), out)
    s.clear()

    out = s.send(b"echo x | zzfoob\t")
    check("command after a pipe", out.endswith(b"zzfoobar "), out)
    s.clear()

    out = s.send(b"echo x > alph\t")
    check("redirection file is a path", out.endswith(b"a.txt "), out)
    s.clear()

    out = s.send(b"X=1 zzfoob\t")
    check("command after an assignment", out.endswith(b"zzfoobar "), out)
    s.clear()

    out = s.send(b"if zzfoob\t")
    check("command after a reserved word", out.endswith(b"zzfoobar "), out)
    s.clear()

    out = s.send(b"X=dat\t")
    check("assignment value is a path", out.endswith(b"data/"), out)
    s.clear()

    out = s.send(b"zzzzq\t")
    check("no match rings the bell", out == b"zzzzq\a", out)
    s.clear()

    out = s.send(b"cat 'alp\t")
    check("no completion inside an open quote", out == b"cat 'alp\a", out)
    s.clear()

    s.send(b"\t")
    out = s.send(b"\t")
    check("long list asks first", b"Display all " in out and b"possibilities? (y or n)" in out, out)
    out = s.send(b"n")
    check("answer n lists nothing and gives the line back", out.endswith(b"$ ") and b"zzfoobar" not in out, out)
    s.clear()

    s.send(b"\t")
    s.send(b"\t")
    out = s.send(b"y", 1.0)
    check("answer y lists them in columns and gives the line back",
          b"zzfoobar" in out and b"echo" in out and out.endswith(b"\r\n$ "), out[-200:])
    s.clear()

    out = s.send(b"; echo x\x01zzfoob\t")
    check("completion before the end of the line", b"zzfoobar " in out, out)
    out = s.send(b"\r")
    check("the completed line runs", b"zzfoobar\r\nx\r\n" in out, out)

    s.send(b"set -o vi\r")
    out = s.send(b"cat alpha\t")
    check("vi insert mode", out.endswith(b".txt "), out)
    s.send(b"\x15exit\r", 0.3)
    os.waitpid(s.pid, 0)

print("complete-host: %d/%d" % (sum(results), len(results)))
sys.exit(0 if all(results) else 1)
