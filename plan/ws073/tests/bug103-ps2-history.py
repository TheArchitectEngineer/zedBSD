#!/usr/bin/env python3
# ws073-p048 (BUG-103): the up arrow of a PS/2-only keyboard recalls the
# previous command in /bin/sh on the framebuffer console.
#
# The guest has no USB keyboard: QEMU's q35 i8042 is the only keyboard, the way
# the Latitude 5330's built-in keyboard is.  The script logs in as root on the
# console, runs "echo ps2hist103", presses Up and Return, and reads the screen
# (the console font, as plan/tools/boot-test.py reads it).  PASS when the output
# line "ps2hist103" is on the screen twice: once for the typed command and once
# for the recalled one.  Before ws087-p005 the PS/2 driver dropped the E0 keys,
# Up did nothing and the second Return ran an empty line (one output line).
#
#   plan/ws073/tests/bug103-ps2-history.py IMAGE OUTDIR
#
# Writes OUTDIR/login.png, OUTDIR/history.png and OUTDIR/history.txt.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import importlib.util
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("boottest", ROOT / "plan/tools/boot-test.py")
boottest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boottest)

MARK = "ps2hist103"


def accel():
    """Returns the accelerator options of plan/tools/guest/qemu-accel.sh."""
    out = subprocess.run(
        ["sh", "-c", ". plan/tools/guest/qemu-accel.sh; qemu_accel_args max"],
        cwd=ROOT, capture_output=True, text=True, check=True)
    return shlex.split(out.stdout)


def keys(monitor, text):
    """Types text with QMP send-key, one key (with shift when needed) at a time."""
    named = {" ": "spc", "\n": "ret", ".": "dot", "-": "minus", "/": "slash"}
    for ch in text:
        if ch in named:
            codes = [named[ch]]
        elif ch.isupper():
            codes = ["shift", ch.lower()]
        else:
            codes = [ch]
        monitor.command("send-key", keys=[{"type": "qcode", "data": c} for c in codes])
        time.sleep(0.08)


def screen(monitor, font, frame):
    monitor.screendump(frame)
    return boottest.read_text(frame, font)


def wait_for(monitor, font, frame, predicate, timeout):
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        lines = screen(monitor, font, frame)
        if predicate(lines):
            return lines, True
        time.sleep(1.0)
    return lines, False


def main():
    image = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    font = boottest.load_font(ROOT / "src/drivers/platform/pcat/graphics/vgafont.c")
    work = Path(tempfile.mkdtemp(prefix="bug103-"))
    disk = work / "disk.img"
    nvram = work / "vars.fd"
    qmp = work / "qmp.sock"
    frame = work / "screen.ppm"
    shutil.copy(image, disk)
    shutil.copy("/usr/share/OVMF/OVMF_VARS_4M.fd", nvram)
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-m", "4G", "-smp", "2", *accel(),
        "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
        "-drive", f"if=pflash,format=raw,file={nvram}",
        "-drive", f"if=none,id=boot,file={disk},format=raw",
        "-device", "nvme,serial=zedbsd-boot,drive=boot,bootindex=1",
        # No USB keyboard: the i8042 of q35 is the only keyboard.
        "-vga", "std", "-display", "none", "-no-reboot",
        "-qmp", f"unix:{qmp},server,nowait",
    ]
    log = open(out / "qemu.log", "wb")
    qemu = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, start_new_session=True)
    status = 1
    try:
        monitor = boottest.Monitor(str(qmp), timeout=30.0)
        prompt = boottest.re.compile(r"^(?:\S+ )?login: ?")
        lines, ok = wait_for(monitor, font, frame,
                             lambda ls: any(prompt.search(l) for l in ls), 240)
        boottest.write_png(frame, out / "login.png")
        if not ok:
            print("bug103: never saw the login prompt", file=sys.stderr)
            return 1
        keys(monitor, "root\n")
        lines, ok = wait_for(monitor, font, frame,
                             lambda ls: any("Password" in l for l in ls), 30)
        keys(monitor, "root\n")
        time.sleep(4.0)
        keys(monitor, f"echo {MARK}\n")
        time.sleep(1.5)
        monitor.command("send-key", keys=[{"type": "qcode", "data": "up"}])
        time.sleep(0.5)
        keys(monitor, "\n")
        time.sleep(1.5)
        lines = screen(monitor, font, frame)
        boottest.write_png(frame, out / "history.png")
        (out / "history.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        count = sum(1 for l in lines if l.strip() == MARK)
        if count >= 2:
            print(f"BUG103-PS2-HISTORY:PASS ({count} output lines)")
            status = 0
        else:
            print(f"BUG103-PS2-HISTORY:FAIL ({count} output lines; see {out}/history.png)")
    finally:
        qemu.terminate()
        try:
            qemu.wait(timeout=10)
        except subprocess.TimeoutExpired:
            qemu.kill()
        shutil.rmtree(work, ignore_errors=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
