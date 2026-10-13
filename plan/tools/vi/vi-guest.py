#!/usr/bin/env python3
# ws228-p002: edits files with vi on a zedBSD guest over its serial console
# (an image built from plan/tools/vi/config-amd64-vi.mk, CONFIG_PCAT_SERIAL_MIRROR=y).
# Each step types keys into vi and judges the file vi wrote, read back with cat;
# the screen is not judged.
# The guest writes the serial line a byte at a time, so a character of several
# bytes can come in pieces; the reader here decodes across reads (T1-595: a
# decode per read turned each piece of 本語 into U+FFFD).
#   python3 plan/tools/vi/vi-guest.py --socket SERIAL_SOCKET
# PASS ends with "vi-guest: ok (N steps)".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import argparse
import codecs
import os
import re
import socket
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "guest"))
import serial  # noqa: E402  (plan/tools/guest/serial.py)

ESC = "\x1b"
FILE = "/tmp/ws228-vi.txt"

# The shell echoes this with vi's exit status once vi has ended.
DONE = "ZEDBSD-VI-STATUS"


class Console(serial.Console):
    """serial.Console with a reader that keeps a character split between reads whole."""

    def __init__(self, path, timeout):
        super().__init__(path, timeout)
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")

    def receive(self):
        """Adds what has arrived to pending; False when nothing came within the socket's wait."""
        try:
            chunk = self.socket.recv(4096)
        except socket.timeout:
            return False
        if not chunk:
            raise SystemExit("serial: the line closed")
        self.pending += self.decoder.decode(chunk)
        return True

    def read_until(self, pattern, timeout=None):
        """Reads until the output matches, returning everything read."""
        deadline = time.monotonic() + (timeout or self.timeout)
        while time.monotonic() < deadline:
            if pattern.search(self.pending):
                return self.pending
            self.receive()
        raise SystemExit("serial: never saw %r\n--- last output ---\n%s" % (pattern.pattern, self.pending[-2000:]))

    def quiet(self, limit=15.0):
        """Reads until the line has been silent for a while (the socket's 0.5 s wait, twice),
        so that what is typed next reaches a shell or a vi that is waiting for it."""
        deadline = time.monotonic() + limit
        silent = 0
        while silent < 2 and time.monotonic() < deadline:
            silent = 0 if self.receive() else silent + 1


def command(console, line):
    """Runs a shell command and returns (status, output) with \\r removed."""
    console.quiet()
    console.forget()
    typed = "%s; echo %s=$?" % (line, serial.MARKER)
    console.send(typed)
    pattern = re.compile(re.escape(serial.MARKER) + r"=(\d+)")
    output = console.read_until(pattern)
    match = pattern.search(output)
    body = output[:match.start()].replace("\r", "")

    # What the command printed follows the line as it was typed back (the
    # last copy, should the terminal and the line editor both have shown it).
    typed_at = body.rfind(typed)
    if typed_at >= 0:
        body = body[typed_at + len(typed):]
    newline = body.find("\n")
    if newline >= 0:
        body = body[newline + 1:]
    return int(match.group(1)), body


def edit(console, keys):
    """Opens the file in vi, types the keys, and returns vi's exit status."""
    console.quiet()
    console.forget()
    console.send("TERM=vt100 vi %s; echo %s=$?" % (FILE, DONE))

    # vi has started and drawn its screen once the line is quiet.
    time.sleep(2.0)
    console.quiet()
    for chunk in re.split("(%s)" % ESC, keys):
        if not chunk:
            continue
        console.socket.sendall(chunk.encode("utf-8"))
        # A lone ESC must stand apart from the next key.
        time.sleep(0.5 if chunk == ESC else 0.3)

    # vi has ended when the shell runs the echo after it (the prompt could be an old one).
    pattern = re.compile(re.escape(DONE) + r"=(\d+)")
    match = pattern.search(console.read_until(pattern, timeout=30.0))
    return int(match.group(1))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", required=True)
    arguments = parser.parse_args()
    console = Console(arguments.socket, 60.0)
    serial.login(console, "root")
    failures = []
    steps = 0

    def check(name, ok, detail):
        nonlocal steps
        steps += 1
        print(("PASS " if ok else "FAIL ") + name + ("" if ok else " " + detail))
        if not ok:
            failures.append(name)

    command(console, "rm -f %s" % FILE)
    _, before = command(console, "stty -g")

    # Typing two lines, one of them Japanese, and writing them.
    ended = edit(console, "ihello\r日本語" + ESC + ":wq\r")
    status, text = command(console, "cat %s" % FILE)
    check("insert-write", ended == 0 and text == "hello\n日本語\n", "vi=%d %r" % (ended, text))

    # x on a wide character deletes the whole character.
    ended = edit(console, "jx:wq\r")
    status, text = command(console, "cat %s" % FILE)
    check("x-japanese", ended == 0 and text == "hello\n本語\n", "vi=%d %r" % (ended, text))

    # dd taken back by u leaves the file as it was.
    ended = edit(console, "ddu:wq\r")
    status, text = command(console, "cat %s" % FILE)
    check("dd-undo", ended == 0 and text == "hello\n本語\n", "vi=%d %r" % (ended, text))

    # . repeats the last change.
    ended = edit(console, "x..:wq\r")
    status, text = command(console, "cat %s" % FILE)
    check("dot", ended == 0 and text == "lo\n本語\n", "vi=%d %r" % (ended, text))

    # :q on an unchanged file ends vi, and the terminal modes are as before.
    ended = edit(console, ":q\r")
    _, after = command(console, "stty -g")
    check("stty-restored", ended == 0 and before == after, "vi=%d %r != %r" % (ended, before, after))

    if failures:
        print("vi-guest: FAIL (%s)" % ", ".join(failures))
        return 1
    print("vi-guest: ok (%d steps)" % steps)
    return 0


if __name__ == "__main__":
    sys.exit(main())
