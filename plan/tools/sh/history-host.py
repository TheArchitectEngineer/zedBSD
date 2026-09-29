#!/usr/bin/env python3
# ws087-p002: the history file of the host build of sh, through a pty.
# A new shell recalls with the up arrow what an earlier shell ran
# (HISTFILE, or $HOME/.sh_history), HISTSIZE bounds what is loaded and the
# line editor's history (stifle_history), and shells that are not
# interactive on a terminal neither read nor write the file.
# Usage: history-host.py SHELL   (build it with plan/tools/sh/build-host-sh.sh)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os, pty, select, stat, subprocess, sys, tempfile, time

SH = os.path.abspath(sys.argv[1])
results = []


class Shell:
    """One interactive shell on a pty, with its own environment."""

    def __init__(self, env):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execve(SH, [SH], env)
        self.read(0.4)

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

    def send(self, data, t=0.3):
        os.write(self.fd, data)
        return self.read(t)

    def exit(self):
        # Ctrl-U clears whatever a test left on the line.
        self.send(b"\x15exit\r", 0.3)
        os.waitpid(self.pid, 0)
        os.close(self.fd)


def environment(home, **extra):
    env = {"PATH": "/usr/bin:/bin", "HOME": home, "PS1": "$ ", "TERM": "xterm"}
    env.update(extra)
    return env


def check(name, ok, detail=""):
    results.append(ok)
    print(("ok " if ok else "FAIL ") + name + ("" if ok else ": " + repr(detail)[-300:]))


def lines_of(path):
    if not os.path.exists(path):
        return None
    # The exit each test ends a shell with is kept too; it is left out here.
    with open(path) as f:
        return [line for line in f.read().splitlines() if line != "exit"]


with tempfile.TemporaryDirectory() as home:
    history = os.path.join(home, ".sh_history")

    # 1. A first shell writes the default file, owner-only.
    a = Shell(environment(home))
    a.send(b"echo one\r")
    a.send(b"echo two\r")
    check("default file gets each line as it is read", lines_of(history) == ["echo one", "echo two"], lines_of(history))
    a.exit()
    mode = stat.S_IMODE(os.stat(history).st_mode)
    check("file mode 0600", mode == 0o600, oct(mode))

    # 2. A new shell recalls them with the up arrow (the newest is the first shell's exit).
    b = Shell(environment(home))
    out = b.send(b"\x1b[A")
    check("new shell: up shows the newest line", out.endswith(b"exit"), out)
    out = b.send(b"\x1b[A")
    check("new shell: second up shows the line before", out.endswith(b"echo two"), out)
    b.send(b"\x1b[A")
    out = b.send(b"\r")
    check("new shell: third up and Enter runs the oldest line", b"\r\none\r\n" in out, out)
    b.exit()
    check("recalled line is appended again", lines_of(history)[-1] == "echo one", lines_of(history))

    # 3. Two shells side by side append in the order the lines are read.
    open(history, "w").close()
    a = Shell(environment(home))
    b = Shell(environment(home))
    a.send(b"echo a1\r")
    b.send(b"echo b1\r")
    a.send(b"echo a2\r")
    a.exit()
    b.exit()
    check("two shells interleave", lines_of(history) == ["echo a1", "echo b1", "echo a2"], lines_of(history))

    # 4. HISTFILE names another file; an empty HISTFILE keeps none.
    custom = os.path.join(home, "custom-history")
    open(history, "w").close()
    a = Shell(environment(home, HISTFILE=custom))
    a.send(b"echo custom\r")
    a.exit()
    check("HISTFILE is used", lines_of(custom) == ["echo custom"] and lines_of(history) == [], (lines_of(custom), lines_of(history)))
    a = Shell(environment(home, HISTFILE=""))
    a.send(b"echo none\r")
    out = a.send(b"\x1b[A")
    a.exit()
    check("empty HISTFILE writes nothing", lines_of(history) == [], lines_of(history))
    check("empty HISTFILE still recalls in the shell", out.endswith(b"echo none"), out)

    # 5. HISTSIZE bounds what is loaded, and a file past twice it is cut down.
    with open(history, "w") as f:
        for i in range(1, 21):
            f.write("echo line%d\n" % i)
    a = Shell(environment(home, HISTSIZE="5"))
    for i in range(6):
        a.send(b"\x1b[A", 0.15)
    out = a.send(b"\r")
    check("HISTSIZE=5 loads the newest 5", b"\r\nline16\r\n" in out, out)
    a.exit()
    lines = lines_of(history)
    check("file past 2*HISTSIZE is cut to HISTSIZE (+ the new line)", lines == ["echo line%d" % i for i in range(16, 21)] + ["echo line16"], lines)

    # 6. A file under twice HISTSIZE is left whole.
    with open(history, "w") as f:
        for i in range(1, 9):
            f.write("echo keep%d\n" % i)
    a = Shell(environment(home, HISTSIZE="5"))
    a.exit()
    check("file under 2*HISTSIZE is not rewritten", len(lines_of(history)) == 8, lines_of(history))

    # 7. The line editor keeps more than its default 32 lines (stifle_history).
    with open(history, "w") as f:
        for i in range(1, 61):
            f.write("echo deep%d\n" % i)
    a = Shell(environment(home, HISTSIZE="100"))
    for i in range(50):
        os.write(a.fd, b"\x1b[A")
    a.read(0.5)
    out = a.send(b"\r")
    check("up 50 times reaches the 11th line", b"\r\ndeep11\r\n" in out, out)
    # HISTSIZE lowered in the shell bounds the editor too: it keeps the
    # newest 3 (deep60, echo deep11 and the assignment), so the oldest is deep60.
    a.send(b"HISTSIZE=3\r")
    for i in range(10):
        os.write(a.fd, b"\x1b[A")
    a.read(0.5)
    out = a.send(b"\r")
    check("HISTSIZE=3 set in the shell bounds the editor", b"\r\ndeep60\r\n" in out, out)
    a.exit()

    # 8. vi mode recalls loaded lines too.
    with open(history, "w") as f:
        f.write("echo vi-old\n")
    a = Shell(environment(home))
    a.send(b"set -o vi\r")
    # The newest line is set -o vi itself; two steps back is the loaded one.
    a.send(b"\x1b2k")
    out = a.send(b"\r")
    check("vi mode: ESC k recalls the loaded lines", b"\r\nvi-old\r\n" in out, out)
    a.exit()

    # 9. Shells that are not interactive on a terminal leave the file alone.
    os.remove(history)
    env = environment(home)
    subprocess.run([SH, "-c", "echo x"], env=env, stdout=subprocess.DEVNULL)
    subprocess.run([SH], input=b"echo y\n", env=env, stdout=subprocess.DEVNULL)
    subprocess.run([SH, "-i"], input=b"echo z\n", env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    check("-c, piped and -i on a pipe write no file", not os.path.exists(history))

print("history-host: %d/%d" % (sum(results), len(results)))
sys.exit(0 if all(results) else 1)
