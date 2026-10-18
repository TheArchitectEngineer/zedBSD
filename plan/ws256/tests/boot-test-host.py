#!/usr/bin/env python3
"""Host check of boot-test.py's patterns and steps, without an emulator.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

A fake QMP monitor answers each screendump with the next of a list of
screens drawn in the console font, and a fake serial socket records what is
typed.  The checks: the single default pattern behaves as before, several
patterns may be seen on different screens, and the steps type in order and
wait for their screens.

    python3 -I plan/ws256/tests/boot-test-host.py OUTDIR
"""
from __future__ import annotations

import importlib.util
import json
import socket
import subprocess
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "plan/tools/boot-test.py"


def load_tool():
    """Loads boot-test.py as a module, for its font reader."""
    spec = importlib.util.spec_from_file_location("boot_test", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def draw(font_rows: dict[str, bytes], lines: list[str], path: Path) -> None:
    """Draws an 80x25 screen of text, 640x400, as a binary PPM."""
    width, height = 640, 400
    pixels = bytearray(width * height * 3)
    for row, text in enumerate(lines):
        for column, char in enumerate(text[:80]):
            bitmap = font_rows[char]
            for y in range(16):
                for x in range(8):
                    if bitmap[y] & (0x80 >> x):
                        offset = ((row * 16 + y) * width + column * 8 + x) * 3
                        pixels[offset:offset + 3] = b"\xff\xff\xff"
    path.write_bytes(b"P6\n640 400\n255\n" + bytes(pixels))


def fake_monitor(path: Path, screens: list[Path], gate: list) -> None:
    """Answers QMP with the screens in turn; the last one repeats.

    A screen whose entry in `gate` is an Event waits for that event first,
    so a step's screen appears only after the line was typed.
    """
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(path))
    server.listen(1)
    connection, _ = server.accept()
    stream = connection.makefile("rwb")
    stream.write(b'{"QMP": {}}\n')
    stream.flush()
    index = 0
    for line in stream:
        request = json.loads(line)
        if request["execute"] == "screendump":
            event = gate[min(index, len(gate) - 1)]
            if event is not None:
                event.wait(10.0)
            source = screens[min(index, len(screens) - 1)]
            Path(request["arguments"]["filename"]).write_bytes(
                source.read_bytes())
            index += 1
        stream.write(b'{"return": {}}\n')
        stream.flush()


def fake_serial(path: Path, typed: list[str], events: dict[str, object]) -> None:
    """Records each typed line and fires the event waiting on it."""
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(path))
    server.listen(1)
    connection, _ = server.accept()
    connection.sendall(b"guest output that must be dropped\r\n" * 100)
    buffer = b""
    while True:
        chunk = connection.recv(4096)
        if not chunk:
            return
        buffer += chunk
        while b"\r" in buffer:
            text, buffer = buffer.split(b"\r", 1)
            typed.append(text.decode())
            event = events.get(text.decode())
            if event is not None:
                event.set()


def run(out: Path, name: str, screens_text: list[list[str]], args: list[str],
        gate_on: list[str | None], font_rows) -> tuple[int, list[str], Path]:
    """Runs boot-test.py against the fakes and returns its status."""
    work = out / name
    work.mkdir()
    screens = []
    for index, lines in enumerate(screens_text):
        path = work / f"screen{index}.ppm"
        draw(font_rows, lines, path)
        screens.append(path)
    events = {text: threading.Event() for text in gate_on if text}
    gate = [events.get(text) if text else None for text in gate_on]
    typed: list[str] = []
    threading.Thread(target=fake_monitor,
                     args=(work / "qmp.sock", screens, gate),
                     daemon=True).start()
    threading.Thread(target=fake_serial,
                     args=(work / "serial.sock", typed, events),
                     daemon=True).start()
    command = [sys.executable, "-I", str(TOOL),
               "--monitor", str(work / "qmp.sock"),
               "--screenshot", str(work / "login.png"),
               "--frame", str(work / "frame.ppm"),
               "--timeout", "8", "--then-timeout", "8",
               "--interval", "0.05"] + args
    status = subprocess.run(command, env={"PYTHONDONTWRITEBYTECODE": "1"},
                            timeout=60).returncode
    return status, typed, work


def main() -> int:
    """Runs the checks and prints PASS or FAIL for each."""
    out = Path(sys.argv[1])
    tool = load_tool()
    table = tool.load_font(ROOT / "src/drivers/platform/pcat/graphics/vgafont.c")
    font_rows = {char: rows for rows, char in table.items()}
    failures = 0

    def check(name: str, condition: bool) -> None:
        nonlocal failures
        print(("PASS " if condition else "FAIL ") + name)
        if not condition:
            failures += 1

    # 1. The default pattern alone: a login prompt after a boot screen.
    status, _, work = run(out, "default",
                          [["booting"], ["zedbsd login: "]], [], [None],
                          font_rows)
    check("default pattern", status == 0 and (work / "login.png").exists()
          and "zedbsd login:" in (work / "login.txt").read_text())
    check("default keeps no per-pattern screens",
          not (work / "login-1.png").exists())

    # 2. The default pattern never shown fails and keeps the screen.
    status, _, work = run(out, "never", [["booting"]], [], [None], font_rows)
    check("missing prompt fails", status == 1
          and (work / "login.png").exists())

    # 3. Several patterns seen on different screens (the first scrolls away).
    status, _, work = run(out, "several",
                          [["RISCV64 PAGING PASS"], ["RISCV64 SMP 4 ONLINE"],
                           ["vfs: no root"]],
                          ["--pattern", "^RISCV64 PAGING PASS",
                           "--pattern", "^RISCV64 SMP 4 ONLINE",
                           "--pattern", "^vfs: "], [None], font_rows)
    check("several patterns", status == 0
          and (work / "login-1.png").exists()
          and (work / "login-3.png").exists()
          and "PAGING" in (work / "login-1.txt").read_text())

    # 4. One of several patterns missing fails.
    status, _, work = run(out, "several-missing",
                          [["RISCV64 PAGING PASS"], ["vfs: no root"]],
                          ["--pattern", "^RISCV64 PAGING PASS",
                           "--pattern", "^RISCV64 SMP 4 ONLINE"], [None],
                          font_rows)
    check("several patterns, one missing", status == 1)

    # 5. Steps: log in and type uname -m; each screen comes after its line.
    status, typed, work = run(
        out, "steps",
        [["zedbsd login: "], ["Password:"], ["# "], ["# uname -m", "riscv64", "# "]],
        ["--serial", str(out / "steps/serial.sock"),
         "--then-type", "root", "--then-expect", "[Pp]assword",
         "--then-type", "root", "--then-expect", "^# ?$",
         "--then-type", "uname -m", "--then-expect", "^riscv64$"],
        [None, "root", "root", "uname -m"], font_rows)
    check("steps type in order", typed == ["root", "root", "uname -m"])
    check("steps see the last screen", status == 0
          and "riscv64" in (work / "login-after.txt").read_text())

    # 6. Steps without a serial socket are refused.
    status, _, _ = run(out, "no-serial", [["zedbsd login: "]],
                       ["--then-type", "root"], [None], font_rows)
    check("steps need --serial", status != 0)

    print("FAIL" if failures else "PASS", f"{failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
