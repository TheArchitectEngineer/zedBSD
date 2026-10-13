#!/usr/bin/env python3
# ws228-p001: host test of vi (userland/base/vi) through a pseudo-terminal.
# Builds vi with the host cc, types keys into it, and judges each case by the
# file it writes (the screen is not judged).  Output goes to a new directory
# under build/tmp (removing old runs is Q1's step).
#   python3 plan/ws228/tests/run-host-vi.py [--keep-going]
# PASS ends with "host-vi: ok (N cases)".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import codecs
import fcntl
import importlib.util
import os
import pty
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import threading
import time
import unicodedata

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
        os.write(fd, chunk if isinstance(chunk, bytes) else chunk.encode("utf-8"))
        # A lone ESC must stand apart from the next key.
        time.sleep(0.15 if chunk == ESC or (isinstance(chunk, str) and chunk.endswith(ESC)) else 0.05)
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


def run_then_close(command, keys, cwd, timeout=5.0):
    """Runs command on a pty, types the keys, then closes the pty as a hangup
    does, and returns the exit status (or "timeout" if the program keeps running)."""
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        env = dict(os.environ)
        env["TERM"] = "xterm"
        env["TERMINFO"] = os.path.join(ROOT, "userland/base/terminfo")
        os.execve("/bin/sh", ["sh", "-c", command], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    time.sleep(0.3)
    drain(fd, 0.0)
    os.write(fd, keys.encode("utf-8"))
    time.sleep(0.3)
    drain(fd, 0.0)
    os.close(fd)
    deadline = time.time() + timeout
    while time.time() < deadline:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            return os.waitstatus_to_exitcode(status)
        time.sleep(0.05)
    os.kill(pid, 9)
    os.waitpid(pid, 0)
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
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")

    def feed(self, data):
        text = self.decoder.decode(data)
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


def keys_split(text):
    """Like keys_of, but each byte of a character of several bytes is typed on
    its own, 50 ms apart, as a slow line (the serial console) delivers it."""
    chunks = []
    for chunk in keys_of(text):
        current = ""
        for char in chunk:
            data = char.encode("utf-8")
            if len(data) == 1:
                current += char
                continue
            if current:
                chunks.append(current)
                current = ""
            chunks.extend(bytes([byte]) for byte in data)
        if current:
            chunks.append(current)
    return chunks


# Cases whose characters of several bytes are typed a byte at a time (T1-595).
SPLIT_CASES = [
    ("split-insert", None, "ihello\r日本語" + ESC + ":wq\r", "hello\n日本語\n"),
    ("split-r", "日本語\n", "lrあ:wq\r", "日あ語\n"),
    ("split-f", "日本語テキスト\n", "fテD:wq\r", "日本語\n"),
    ("split-search", "日本\n日本語\n", "/語\rx:wq\r", "日本\n日本\n"),
]


def serial_cases(report, out):
    """The serial reader of vi-guest.py against a stand-in guest on a socket:
    a character split between reads, and a command line shown twice."""
    spec = importlib.util.spec_from_file_location("vi_guest", os.path.join(os.path.dirname(__file__), "vi-guest.py"))
    vi_guest = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vi_guest)
    path = os.path.join(out, "serial.sock")
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(path)
    server.listen(4)

    def guest(stream, after_line):
        connection, _ = server.accept()
        if after_line:
            typed = b""
            while not typed.endswith(b"\r"):
                typed += connection.recv(1)
        for byte in stream:
            connection.sendall(bytes([byte]))
            time.sleep(0.01)
        time.sleep(0.5)
        connection.close()

    # Each byte comes in a read of its own: the reader keeps 日本語 whole.
    text = "hello\n日本語\n" + vi_guest.serial.MARKER + "=0\n"
    pattern = vi_guest.re.compile(vi_guest.re.escape(vi_guest.serial.MARKER) + r"=\d")
    results = []
    for reader in (vi_guest.serial.Console, vi_guest.Console):
        thread = threading.Thread(target=guest, args=(text.encode("utf-8"), False))
        thread.start()
        console = reader(path, 10.0)
        results.append(console.read_until(pattern))
        thread.join()
        console.socket.close()
    old, new = results
    print("     (serial.py's own reader %s)" % ("loses the split characters" if "\ufffd" in old else "keeps them"))
    report("serial-split-utf8", new.startswith("hello\n日本語\n"), repr(new))

    # The line comes back from the terminal and again from the line editor: the output is what follows the last copy.
    line = "stty -g; echo %s=$?" % vi_guest.serial.MARKER
    stream = (line + "\r\nroot@kei:~$ " + line + "\r\n26:3:7:77\r\n" + vi_guest.serial.MARKER + "=0\r\n").encode()
    thread = threading.Thread(target=guest, args=(stream, True))
    thread.start()
    console = vi_guest.Console(path, 10.0)
    status, body = vi_guest.command(console, "stty -g")
    thread.join()
    console.socket.close()
    server.close()
    report("serial-double-echo", (status, body) == (0, "26:3:7:77\n"), repr((status, body)))


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
    ("count-insert", "ab\n", "5i-" + ESC + "3ax" + ESC + ":wq\r", "-----xxxab\n"),
    ("cw-one-letter", "a b\n", "cwx" + ESC + ":wq\r", "x b\n"),
    ("dollar-p", "ab\ncd\n", "lyljp:wq\r", "ab\ncdb\n"),
    ("f-t-semicolon", "a,b,c,d\n", "f,;x0t,D:wq\r", "\n"),
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

    cases = [case + (keys_of,) for case in CASES] + [case + (keys_split,) for case in SPLIT_CASES]
    for name, initial, keys, expected, typing in cases:
        work = tempfile.mkdtemp(prefix=name + ".", dir=out)
        path = os.path.join(work, "f.txt")
        if initial is not None:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(initial)
        status = run("%s f.txt" % vi, typing(keys), work)
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

    # A line taller than the screen shows the part with the cursor.
    work = tempfile.mkdtemp(prefix="screen-tall.", dir=out)
    with open(os.path.join(work, "f.txt"), "w", encoding="utf-8") as handle:
        handle.write("x" * 3000 + "END\n")
    output = []
    status = run("%s f.txt" % vi, keys_of("$") + [None] + keys_of(":q\r"), work, output=output)
    screen = screen_of(output)
    rows = [screen.line(row) for row in range(23)]
    under = screen.cells[screen.row][screen.column]
    report("screen-tall-line", status == 0 and any(row.endswith("END") for row in rows) and under == "D",
           "cursor=%r under=%r" % ((screen.row, screen.column), under))

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

    # A file that cannot be read is not opened at all, so :wq cannot empty it.
    work = tempfile.mkdtemp(prefix="unreadable.", dir=out)
    path = os.path.join(work, "f.txt")
    with open(path, "w") as handle:
        handle.write("secret\n")
    os.chmod(path, 0o200)
    status = run("%s f.txt" % vi, keys_of("ix" + ESC + ":wq\r"), work)
    os.chmod(path, 0o600)
    report("unreadable-refused", status == 1 and open(path).read() == "secret\n", "status=%r" % status)
    status = run("%s ." % vi, keys_of(":q\r"), work)
    report("directory-refused", status == 1, "status=%r" % status)

    # Writing another file leaves the buffer changed, so :q still refuses.
    work = tempfile.mkdtemp(prefix="w-other.", dir=out)
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write("abc\n")
    status = run("%s f.txt" % vi, keys_of("x:w g.txt\r:q\r$x:wq\r"), work)
    report("w-other-then-q", status == 0 and open(os.path.join(work, "f.txt")).read() == "b\n"
           and open(os.path.join(work, "g.txt")).read() == "bc\n")

    # Written, taken back, changed again: a different change, so :q refuses.
    work = tempfile.mkdtemp(prefix="saved-state.", dir=out)
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write("abc\n")
    status = run("%s f.txt" % vi, keys_of("x:w\ru$x:q\r:wq\r"), work)
    report("saved-state", status == 0 and open(os.path.join(work, "f.txt")).read() == "ab\n",
           repr(open(os.path.join(work, "f.txt")).read()))

    # Bytes that are not text come back as they were, with a newline added at the end.
    work = tempfile.mkdtemp(prefix="binary.", dir=out)
    data = b"a\x00b\r\n\xff\xfe\xe6\x97\n\x1b[31m\xc2\x9b\nlast"
    with open(os.path.join(work, "f.txt"), "wb") as handle:
        handle.write(data)
    status = run("%s f.txt" % vi, keys_of(":wq\r"), work)
    got = open(os.path.join(work, "f.txt"), "rb").read()
    report("binary-round-trip", status == 0 and got == data + b"\n", repr(got))

    # A file with two names is written in place: both names show the change.
    work = tempfile.mkdtemp(prefix="hardlink.", dir=out)
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write("abc\n")
    os.link(os.path.join(work, "f.txt"), os.path.join(work, "g.txt"))
    inode = os.stat(os.path.join(work, "f.txt")).st_ino
    status = run("%s f.txt" % vi, keys_of("x:wq\r"), work)
    report("hardlink-in-place", status == 0 and open(os.path.join(work, "g.txt")).read() == "bc\n"
           and os.stat(os.path.join(work, "f.txt")).st_ino == inode)

    # A big edit is one step: 20000 lines deleted and taken back at once.
    work = tempfile.mkdtemp(prefix="many-lines.", dir=out)
    lines = "".join("line %d\n" % number for number in range(20000))
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write(lines)
    began = time.time()
    status = run("%s f.txt" % vi, keys_of("dGuyGGp:wq\r"), work, timeout=20.0)
    took = time.time() - began
    report("many-lines", status == 0 and open(os.path.join(work, "f.txt")).read() == lines + lines and took < 15,
           "status=%r took=%.1f" % (status, took))

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

    # A terminal that goes away ends vi (status 1), and unwritten changes go to a recovery file in HOME.
    work = tempfile.mkdtemp(prefix="hangup-recover.", dir=out)
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write("abc\n")
    status = run_then_close("HOME=%s exec %s f.txt" % (work, vi), "x", work)
    recovered = [name for name in os.listdir(work) if name.startswith(".vi-recover.f.txt.")]
    content = open(os.path.join(work, recovered[0])).read() if recovered else None
    mode = os.stat(os.path.join(work, recovered[0])).st_mode & 0o777 if recovered else None
    report("hangup-recover", status == 1 and content == "bc\n" and mode == 0o600
           and open(os.path.join(work, "f.txt")).read() == "abc\n", "status=%r files=%r" % (status, recovered))

    # An unchanged buffer leaves no recovery file.
    work = tempfile.mkdtemp(prefix="hangup-clean.", dir=out)
    with open(os.path.join(work, "f.txt"), "w") as handle:
        handle.write("abc\n")
    status = run_then_close("HOME=%s exec %s f.txt" % (work, vi), "l", work)
    recovered = [name for name in os.listdir(work) if name.startswith(".vi-recover.")]
    report("hangup-clean", status == 1 and not recovered, "status=%r files=%r" % (status, recovered))

    serial_cases(report, out)

    if failures:
        print("host-vi: FAIL (%d of %d, output in %s)" % (failures, count, out))
        sys.exit(1)
    print("host-vi: ok (%d cases)" % count)


if __name__ == "__main__":
    main()
