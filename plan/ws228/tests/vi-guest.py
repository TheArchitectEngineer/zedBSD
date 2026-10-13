#!/usr/bin/env python3
# ws228-p002: edits files with vi on a zedBSD guest over its serial console
# (an image built from plan/ws228/tests/config-amd64-vi.mk, CONFIG_PCAT_SERIAL_MIRROR=y).
# Each step types keys into vi and judges the file vi wrote, read back with cat;
# the screen is not judged.
#   python3 plan/ws228/tests/vi-guest.py --socket SERIAL_SOCKET
# PASS ends with "vi-guest: ok (N steps)".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "guest"))
import serial  # noqa: E402  (plan/tools/guest/serial.py)

ESC = "\x1b"
FILE = "/tmp/ws228-vi.txt"


def command(console, line):
    """Runs a shell command and returns (status, output) with \\r removed."""
    console.forget()
    console.send("%s; echo %s=$?" % (line, serial.MARKER))
    pattern = re.compile(re.escape(serial.MARKER) + r"=(\d+)")
    output = console.read_until(pattern)
    match = pattern.search(output)
    body = output[:match.start()].replace("\r", "")
    newline = body.find("\n")
    if newline >= 0:
        body = body[newline + 1:]
    return int(match.group(1)), body


def edit(console, keys):
    """Opens the file in vi, types the keys, and waits for the shell to come back."""
    console.forget()
    console.send("TERM=vt100 vi %s" % FILE)
    time.sleep(2.0)
    for chunk in re.split("(%s)" % ESC, keys):
        if not chunk:
            continue
        console.socket.sendall(chunk.encode("utf-8"))
        # A lone ESC must stand apart from the next key.
        time.sleep(0.5 if chunk == ESC else 0.3)
    console.read_until(serial.PROMPT, timeout=30.0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", required=True)
    arguments = parser.parse_args()
    console = serial.Console(arguments.socket, 60.0)
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
    edit(console, "ihello\r日本語" + ESC + ":wq\r")
    status, text = command(console, "cat %s" % FILE)
    check("insert-write", text == "hello\n日本語\n", repr(text))

    # x on a wide character deletes the whole character.
    edit(console, "jx:wq\r")
    status, text = command(console, "cat %s" % FILE)
    check("x-japanese", text == "hello\n本語\n", repr(text))

    # dd taken back by u leaves the file as it was.
    edit(console, "ddu:wq\r")
    status, text = command(console, "cat %s" % FILE)
    check("dd-undo", text == "hello\n本語\n", repr(text))

    # . repeats the last change.
    edit(console, "x..:wq\r")
    status, text = command(console, "cat %s" % FILE)
    check("dot", text == "lo\n本語\n", repr(text))

    # :q on an unchanged file ends vi, and the terminal modes are as before.
    edit(console, ":q\r")
    _, after = command(console, "stty -g")
    check("stty-restored", before == after, "%r != %r" % (before, after))

    if failures:
        print("vi-guest: FAIL (%s)" % ", ".join(failures))
        return 1
    print("vi-guest: ok (%d steps)" % steps)
    return 0


if __name__ == "__main__":
    sys.exit(main())
