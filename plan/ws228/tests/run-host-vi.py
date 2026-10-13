#!/usr/bin/env python3
# ws228-p001: host test of vi (userland/base/vi) through a pseudo-terminal.
# Builds vi with the host cc, types keys into it, and judges each case by the
# file it writes (the screen is not judged).  Output goes to a new directory
# under build/tmp (removing old runs is Q1's step).
#   python3 plan/ws228/tests/run-host-vi.py [--keep-going]
# PASS ends with "host-vi: ok (N cases)".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import fcntl
import os
import pty
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata
import termios
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
ESC = "\x1b"


def build(out):
    inc = os.path.join(out, ".inc")
    os.makedirs(inc)
    shutil.copy(os.path.join(ROOT, "include/libc/terminfo.h"), inc)
    sources = sorted(
        os.path.join(ROOT, "userland/base/vi", name)
        for name in os.listdir(os.path.join(ROOT, "userland/base/vi"))
        if name.endswith(".c"))
    sources.append(os.path.join(ROOT, "userland/base/common/terminfo.c"))
    binary = os.path.join(out, "vi")
    extra = os.environ.get("EXTRA_CFLAGS", "").split()
    subprocess.run(["cc", "-std=c99", "-D_GNU_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-I" + inc, "-I" + ROOT] + extra + sources + ["-o", binary], check=True)
    return binary


def run(command, keys, cwd, timeout=8.0, output=None):
    """Runs command on a new pty of 24x80, types the keys, and returns the exit status.
    The terminal output is added to the output list when one is given."""
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        env = dict(os.environ)
        env["TERM"] = "xterm"
        env["TERMINFO"] = os.path.join(ROOT, "userland/base/terminfo")
        os.execve("/bin/sh", ["sh", "-c", command], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    time.sleep(0.3)
    for chunk in keys:
        drain(fd, 0.0, output)
        if chunk is None:
            # A snapshot mark: the screen up to here is what a check reads.
            drain(fd, 0.3, output)
            output.append(None)
            continue
        os.write(fd, chunk.encode("utf-8"))
        # A lone ESC must stand apart from the next key.
        time.sleep(0.15 if chunk.endswith(ESC) else 0.05)
    deadline = time.time() + timeout
    while time.time() < deadline:
        drain(fd, 0.05, output)
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            drain(fd, 0.0, output)
            os.close(fd)
            return os.waitstatus_to_exitcode(status)
    os.kill(pid, 9)
    os.waitpid(pid, 0)
    os.close(fd)
    return "timeout"


def drain(fd, wait, output=None):
    while True:
        ready, _, _ = select.select([fd], [], [], wait)
        if not ready:
            return
        try:
            data = os.read(fd, 65536)
        except OSError:
            return
        if not data:
            return
        if output is not None:
            output.append(data)
        wait = 0.0


class Screen:
    """Just enough of an xterm to read back what vi drew: cursor addressing,
    erase in line, clear, and UTF-8 text with wide characters."""

    def __init__(self, rows=24, columns=80):
        self.rows = rows
        self.columns = columns
        self.cells = [[" "] * columns for _ in range(rows)]
        self.row = 0
        self.column = 0

    def feed(self, data):
        text = data.decode("utf-8", errors="replace")
        index = 0
        while index < len(text):
            char = text[index]
            if char == ESC:
                index = self.escape(text, index + 1)
                continue
            index += 1
            if char == "\r":
                self.column = 0
            elif char == "\n":
                self.row = min(self.row + 1, self.rows - 1)
            elif char in "\a\b":
                pass
            else:
                self.put(char)

    def put(self, char):
        width = 2 if unicodedata.east_asian_width(char) in "WF" else 1
        if unicodedata.combining(char):
            return
        if self.column + width > self.columns:
            self.column = 0
            self.row = min(self.row + 1, self.rows - 1)
        self.cells[self.row][self.column] = char
        if width == 2:
            self.cells[self.row][self.column + 1] = ""
        self.column += width

    def escape(self, text, index):
        if text[index] != "[":
            return index + 1
        end = index + 1
        while not ("@" <= text[end] <= "~"):
            end += 1
        parameters = text[index + 1:end]
        final = text[end]
        if parameters.startswith("?"):
            return end + 1
        numbers = [int(part) if part else 0 for part in parameters.split(";")] if parameters else []
        if final == "H":
            self.row = (numbers[0] if len(numbers) > 0 and numbers[0] else 1) - 1
            self.column = (numbers[1] if len(numbers) > 1 and numbers[1] else 1) - 1
        elif final == "K":
            for column in range(self.column, self.columns):
                self.cells[self.row][column] = " "
        elif final == "J":
            self.cells = [[" "] * self.columns for _ in range(self.rows)]
        return end + 1

    def line(self, row):
        return "".join(self.cells[row]).rstrip()


def screen_of(output):
    """The screen at the first snapshot mark."""
    screen = Screen()
    for data in output:
        if data is None:
            break
        screen.feed(data)
    return screen


def keys_of(text):
    """Cuts a key string into chunks, each ESC ending its chunk."""
    chunks = []
    current = ""
    for char in text:
        current += char
        if char == ESC:
            chunks.append(current)
            current = ""
    if current:
        chunks.append(current)
    return chunks


CASES = [
    # name, initial file contents (None: no file), keys, expected contents
    ("insert-and-write", None, "ihello\rworld" + ESC + ":wq\r", "hello\nworld\n"),
    ("x-dw-dd", "one two three\nline2\nline3\n", "xdwjdd:wq\r", "two three\nline3\n"),
    ("D", "abc def\n", "wD:wq\r", "abc \n"),
    ("yy-p-P", "a\nb\n", "yypjP:wq\r", "a\na\na\nb\n"),
    ("undo-redo", "abc\n", "xxuu\x12:wq\r", "bc\n"),
    ("undo-insert", "abc\n", "AXYZ" + ESC + "u:wq\r", "abc\n"),
    ("japanese-x-r", "日本語テキスト\n", "lxrあ:wq\r", "日あテキスト\n"),
    ("japanese-w-D", "日本語テキスト\n", "wD:wq\r", "日本語\n"),
    ("japanese-insert", "", "i漢字かな" + ESC + "x:wq\r", "漢字か\n"),
    ("search-n", "foo\nbar\nbaz\n", "/ba\rxnx:wq\r", "foo\nar\naz\n"),
    ("q-refused", "abc\n", "x:q\r:wq\r", "bc\n"),
    ("q-force", "abc\n", "x:q!\r", "abc\n"),
    ("dot-delete", "a b c d\n", "dw..:wq\r", "d\n"),
    ("dot-count", "a b c d e\n", "dw3.:wq\r", "e\n"),
    ("dot-insert", "", "ix" + ESC + "..:wq\r", "xxx\n"),
    ("cw", "foo bar\n", "cwbaz" + ESC + ":wq\r", "baz bar\n"),
    ("J", "a\n  b\n", "J:wq\r", "a b\n"),
    ("o-O", "b\n", "oc" + ESC + "kOa" + ESC + ":wq\r", "a\nb\nc\n"),
    ("e-reload", "abc\n", "x:e!\r:wq\r", "abc\n"),
    ("G-count", "1\n2\n3\n4\n", "3Gdd:wq\r", "1\n2\n4\n"),
    ("incomplete-last-line", "abc", ":wq\r", "abc\n"),
    ("tilde-dollar", "abc\n", "3~$x:wq\r", "AB\n"),
    ("ZZ", "abc\n", "xZZ", "bc\n"),
]


def main():
    keep_going = "--keep-going" in sys.argv
    tmp_root = os.path.join(ROOT, "build", "tmp")
    os.makedirs(tmp_root, exist_ok=True)
    out = tempfile.mkdtemp(prefix="ws228-host-vi.", dir=tmp_root)
    vi = build(out)
    failures = 0
    count = 0

    def report(name, ok, detail=""):
        nonlocal failures, count
        count += 1
        if ok:
            print("PASS", name)
        else:
            failures += 1
            print("FAIL", name, detail)
            if not keep_going:
                print("host-vi: FAIL (output in %s)" % out)
                sys.exit(1)

    for name, initial, keys, expected in CASES:
        work = tempfile.mkdtemp(prefix=name + ".", dir=out)
        path = os.path.join(work, "f.txt")
        if initial is not None:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(initial)
        status = run("%s f.txt" % vi, keys_of(keys), work)
        got = None
        if os.path.exists(path):
            with open(path, encoding="utf-8", errors="replace") as handle:
                got = handle.read()
        report(name, status == 0 and got == expected, "status=%r got=%r expected=%r" % (status, got, expected))

    # What the screen shows: lines, a wrapped long line, a wide character
    # that does not fit the last column, ~ below the end, and the cursor.
    work = tempfile.mkdtemp(prefix="screen.", dir=out)
    with open(os.path.join(work, "f.txt"), "w", encoding="utf-8") as handle:
        handle.write("abc\n日本語\n" + "x" * 100 + "\n" + "a" * 79 + "日\n\tT\n")
    output = []
    status = run("%s f.txt" % vi, keys_of("jll") + [None] + keys_of(":q\r"), work, output=output)
    screen = screen_of(output)
    expected = ["abc", "日本語", "x" * 80, "x" * 20, "a" * 79, "日", "        T", "~"]
    got = [screen.line(row) for row in range(8)]
    report("screen", status == 0 and got == expected and (screen.row, screen.column) == (1, 4),
           "got=%r cursor=%r" % (got, (screen.row, screen.column)))

    # The bottom line: the message after a write, and the cursor on a long line's second row.
    work = tempfile.mkdtemp(prefix="screen-status.", dir=out)
    with open(os.path.join(work, "f.txt"), "w", encoding="utf-8") as handle:
        handle.write("x" * 100 + "\n")
    output = []
    status = run("%s f.txt" % vi, keys_of("$:w\r") + [None] + keys_of(":q\r"), work, output=output)
    screen = screen_of(output)
    report("screen-status", status == 0 and screen.line(23) == '"f.txt" 1 lines, 101 bytes written'
           and (screen.row, screen.column) == (1, 19),
           "status line=%r cursor=%r" % (screen.line(23), (screen.row, screen.column)))

    # The permissions of an existing file are kept.
    work = tempfile.mkdtemp(prefix="mode.", dir=out)
    path = os.path.join(work, "f.txt")
    with open(path, "w") as handle:
        handle.write("abc\n")
    os.chmod(path, 0o640)
    status = run("%s f.txt" % vi, keys_of("x:wq\r"), work)
    mode = os.stat(path).st_mode & 0o7777
    report("mode-kept", status == 0 and mode == 0o640 and open(path).read() == "bc\n", "mode=%o" % mode)

    # Writing through a symbolic link changes the file it points to.
    work = tempfile.mkdtemp(prefix="symlink.", dir=out)
    target = os.path.join(work, "target.txt")
    with open(target, "w") as handle:
        handle.write("abc\n")
    os.symlink("target.txt", os.path.join(work, "link.txt"))
    status = run("%s link.txt" % vi, keys_of("x:wq\r"), work)
    report("symlink-target", status == 0 and os.path.islink(os.path.join(work, "link.txt"))
           and open(target).read() == "bc\n", "status=%r" % status)

    # Another existing file is not overwritten without !.
    work = tempfile.mkdtemp(prefix="w-exists.", dir=out)
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write("one\n")
    with open(os.path.join(work, "g.txt"), "w") as handle:
        handle.write("two\n")
    status = run("%s f.txt" % vi, keys_of(":w g.txt\r:q\r"), work)
    report("w-exists-refused", status == 0 and open(os.path.join(work, "g.txt")).read() == "two\n")

    # The terminal modes after vi are the modes before it.
    work = tempfile.mkdtemp(prefix="stty.", dir=out)
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write("abc\n")
    status = run("stty -g > before; %s f.txt; stty -g > after" % vi, keys_of(":q\r"), work)
    before = open(os.path.join(work, "before")).read() if os.path.exists(os.path.join(work, "before")) else None
    after = open(os.path.join(work, "after")).read() if os.path.exists(os.path.join(work, "after")) else None
    report("stty-restored", status == 0 and before is not None and before == after, "%r %r" % (before, after))

    # A hangup ends vi with status 1 and gives the terminal back.
    work = tempfile.mkdtemp(prefix="hup.", dir=out)
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write("abc\n")
    status = run("stty -g > before; %s f.txt </dev/tty & child=$!; sleep 0.5; kill -HUP $child; wait $child; echo $? > code; stty -g > after" % vi, [], work)
    code = open(os.path.join(work, "code")).read().strip() if os.path.exists(os.path.join(work, "code")) else None
    before = open(os.path.join(work, "before")).read() if os.path.exists(os.path.join(work, "before")) else None
    after = open(os.path.join(work, "after")).read() if os.path.exists(os.path.join(work, "after")) else None
    report("hangup", code == "1" and before == after, "code=%r" % code)

    if failures:
        print("host-vi: FAIL (%d of %d, output in %s)" % (failures, count, out))
        sys.exit(1)
    print("host-vi: ok (%d cases)" % count)


if __name__ == "__main__":
    main()
