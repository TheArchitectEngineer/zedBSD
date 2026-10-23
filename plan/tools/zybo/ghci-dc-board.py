#!/usr/bin/env python3
"""Checks the Amaterrace GPU's display controller (bitstream 2, "DC", WS288
p005) on the real ZYBO with nothing but fpgareg: no driver, no display
needed.  Over SSH it

  1. (with --blt-bit) loads bitstream 1 ("2D") and paints framebuffers in
     the GPU's reserved memory with the blitter (FILL rectangles: bars, a
     box, a border; 640x480 BGRA8, 1280x720 BGRA8, 640x480 RGB565), the
     memory then keeps them across the reload of the PL;
  2. loads the DC bitstream (--dc-bit) and reads the GHCI header (DEVICE
     "ATDC", CAPS [3] only, EXTCAP -> DISP at 0x200) and the DC's
     identification;
  3. for each mode (640x480 at 25.185 MHz, 720p at 74.219 MHz): the timing,
     the framebuffer and the format, ENABLE, then the vsync interrupt
     through fpgareg wait, FRAME_COUNT against the host's clock (60 Hz),
     SCAN_WORDS and SCAN_SUM (the sum of the pixel words the DMA read in
     the last frame, against the host's model of the painted memory), no
     underflow; a flip (FB_NEXT -> FB_BASE at a vsync, FLIP_DONE) to
     another framebuffer; TEST_PATTERN; ENABLE = 0 -> STATUS.idle;
  4. leaves the display as --leave says, for a look at the monitor:
     pattern (640x480 colour bars, the default), fb (the painted 640x480),
     fb720 (the painted 720p), pattern720, off.

  plan/tools/zybo/ghci-dc-board.py --dc-bit DC.bit [--blt-bit 2D.bit]
      [--host HOST] [--base 0x1C000000] [--leave pattern] [--log FILE]

Without --blt-bit the framebuffers are whatever the memory holds: the sums
are then not checked (only reported).  The login is root/root (sshpass);
the board's host key is not recorded.  The host's board lock
/tmp/zedbsd-zybo-board.lock is held for the run.  Prints one PASS or FAIL
line per check and exits 1 on any FAIL.  About two minutes.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""

import argparse
import fcntl
import os
import subprocess
import sys
import time

SSH_OPTIONS = ["-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
               "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=10"]
LOCK = "/tmp/zedbsd-zybo-board.lock"

# GHCI and the 2D bitstream's CP (amaterrace-gpu include/ghci_regs.h).
CTRL, STATUS, AP_BASE, AP_LIMIT, KICK, STATE = 0x030, 0x034, 0x060, 0x064, 0xF00, 0x1000
FILL, BPP16 = 0, 1 << 8
# The DC's registers (DISP at 0x200 in the DC-only window).
D = 0x200
DC_ID, DC_VERSION, DC_CAPS, DC_CTRL, DC_STATUS = D + 0x00, D + 0x04, D + 0x08, D + 0x10, D + 0x14
DC_IRQ_STATUS, DC_IRQ_ENABLE, FB_BASE, FB_NEXT = D + 0x18, D + 0x1C, D + 0x20, D + 0x24
FB_STRIDE, FB_SIZE, TIMING, SYNC_POL = D + 0x28, D + 0x2C, D + 0x30, D + 0x50
FRAME_COUNT, PCLK_SEL, SCAN_SUM, SCAN_WORDS = D + 0x54, D + 0x58, D + 0x70, D + 0x74
ENABLE, TEST_PATTERN = 1, 4

MODES = {
    # name: (pclk_sel, H active/fp/sync/bp, V ..., SYNC_POL, pixel clock)
    "640x480": (0, (640, 16, 96, 48), (480, 10, 2, 33), 0, 100e6 * 34 / 3 / 45),
    "1280x720": (1, (1280, 110, 40, 220), (720, 5, 5, 20), 3, 100e6 * 59.375 / 8 / 10),
}


class Memory:
    """The painted memory as the host knows it (words; unknown = None)."""

    def __init__(self):
        self.words = {}

    def fill(self, base, stride, x, y, w, h, colour, is16):
        ps = 2 if is16 else 4
        for yy in range(y, y + h):
            row = base + yy * stride
            for xx in range(x, x + w):
                a = row + xx * ps
                if is16:
                    k = a & ~3
                    old = self.words.get(k, 0)
                    if a & 2:
                        self.words[k] = (old & 0x0000FFFF) | (colour << 16)
                    else:
                        self.words[k] = (old & 0xFFFF0000) | colour
                else:
                    self.words[a] = colour

    def frame_sum(self, base, stride, w, h, is16):
        """The DC's SCAN_SUM of a frame, or None if a word is unknown."""
        total = 0
        for y in range(h):
            row = base + y * stride
            for x in range(w):
                a = row + x * (2 if is16 else 4)
                word = self.words.get(a & ~3)
                if word is None:
                    return None
                total += ((word >> 16) if a & 2 else word & 0xFFFF) if is16 else word
        return total & 0xFFFFFFFF


def paint_rects(w, h, is16):
    """Rectangles (x, y, w, h, colour) of a recognisable picture: eight
    vertical bars (a grey ramp from dark to light, distinct from the test
    pattern), a centred box, a one-pixel white border."""
    rects = []
    bw = w // 8
    for i in range(8):
        level = 0x20 + i * 0x1C
        if is16:
            c = ((level >> 3) << 11) | ((level >> 2) << 5) | (level >> 3)
        else:
            c = 0xFF000000 | (level << 16) | ((level ^ 0x40) << 8) | (0xFF - level)
        rects.append((i * bw, 0, bw, h, c))
    box = 0x07E0 if is16 else 0xFF20C040   # green
    rects.append((w // 4, h // 4, w // 2, h // 2, box))
    white = 0xFFFF if is16 else 0xFFFFFFFF
    rects += [(0, 0, w, 1, white), (0, h - 1, w, 1, white), (0, 0, 1, h, white),
              (w - 1, 0, 1, h, white)]
    return rects


class Script:
    def __init__(self):
        self.lines = [
            "r() { fpgareg read $1; }",
            "w() { fpgareg write $1 $2 >/dev/null; }",
            "st() { w $((0x1000 + $1 * 4)) $2; }",
            # Waits for the blitter: STATUS[1] clear.
            "bw() { n=0; while [ $(( $(r 0x34) & 2 )) -ne 0 ] && [ $n -lt 2000 ]; do n=$((n+1)); done; echo \"B $n\"; }",
            # n vsync interrupts.
            "vw() { n=0; m=0; while [ $n -lt $1 ]; do fpgareg wait --ack 0x218 500 >/dev/null 2>&1 || m=$((m+1)); n=$((n+1)); done; echo \"V $m\"; }",
        ]

    def add(self, text):
        self.lines.append(text)

    def reg(self, name, offset):
        self.add(f"echo \"R {name} $(r {offset:#x})\"")

    def write(self, offset, value):
        self.add(f"w {offset:#x} {value:#x}")

    def text(self):
        return "\n".join(self.lines) + "\necho END\n"


def ssh_run(args, env, script, timeout=600):
    ssh = ["sshpass", "-e", "ssh"] + SSH_OPTIONS + [f"root@{args.host}"]
    return subprocess.run(["timeout", str(timeout)] + ssh + ["sh -s"], input=script, env=env,
                          capture_output=True, text=True)


def read_count(args, env):
    """FRAME_COUNT and the host's time at the middle of the read."""
    ssh = ["sshpass", "-e", "ssh"] + SSH_OPTIONS + [f"root@{args.host}"]
    t0 = time.time()
    run = subprocess.run(["timeout", "30"] + ssh + [f"fpgareg read {FRAME_COUNT:#x}"],
                         env=env, capture_output=True, text=True)
    t1 = time.time()
    return int(run.stdout.split()[0], 0), (t0 + t1) / 2


def load(args, env, bit, name):
    ssh = ["sshpass", "-e", "ssh"] + SSH_OPTIONS + [f"root@{args.host}"]
    subprocess.run(["timeout", "120", "sshpass", "-e", "scp"] + SSH_OPTIONS +
                   [bit, f"root@{args.host}:/tmp/{name}.bit"], env=env, check=True)
    run = subprocess.run(["timeout", "60"] + ssh + [f"fpgaload /tmp/{name}.bit"],
                         env=env, capture_output=True, text=True)
    print(run.stdout.strip())
    return run.returncode == 0


def parse(out):
    regs, other = {}, []
    for line in out.splitlines():
        part = line.split()
        if part and part[0] == "R" and len(part) == 3:
            regs[part[1]] = int(part[2], 0)
        elif part:
            other.append(part)
    return regs, other


def mode_setup(s, name, fb, stride, fmt, extra_ctrl=0):
    sel, h, v, pol, _ = MODES[name]
    s.write(DC_CTRL, 0)
    s.write(PCLK_SEL, sel)
    for i, value in enumerate(h + v):
        s.write(TIMING + 4 * i, value)
    s.write(SYNC_POL, pol)
    s.write(FB_NEXT, fb)
    s.write(FB_STRIDE, stride)
    s.write(FB_SIZE, (v[0] << 16) | h[0])
    s.write(DC_IRQ_STATUS, 7)
    s.write(DC_IRQ_ENABLE, 1)
    s.write(DC_CTRL, ENABLE | (fmt << 4) | extra_ctrl)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dc-bit", required=True)
    parser.add_argument("--blt-bit")
    parser.add_argument("--host", default="10.0.30.2")
    parser.add_argument("--base", default="0x1C000000")
    parser.add_argument("--leave", default="pattern",
                        choices=["pattern", "fb", "fb720", "pattern720", "off"])
    parser.add_argument("--log")
    args = parser.parse_args()
    base = int(args.base, 0)
    fb640 = base + 0x100000
    fb720 = base + 0x400000
    fb565 = base + 0x900000
    env = dict(os.environ, SSHPASS="root")
    log = open(args.log, "w") if args.log else None
    fails = 0

    def result(ok, name, detail=""):
        nonlocal fails
        print(("PASS " if ok else "FAIL ") + name + (f": {detail}" if detail else ""))
        if not ok:
            fails += 1

    lock = open(LOCK, "a")
    fcntl.flock(lock, fcntl.LOCK_EX)

    mem = Memory()
    painted = False
    if args.blt_bit:
        if not load(args, env, args.blt_bit, "ghci-2d"):
            result(False, "fpgaload 2D")
            return 1
        s = Script()
        s.write(CTRL, 2)
        s.write(AP_BASE, base)
        s.write(AP_LIMIT, base + 0x4000000)
        for fb, stride, w, h, is16 in ((fb640, 2560, 640, 480, False),
                                       (fb720, 5120, 1280, 720, False),
                                       (fb565, 1280, 640, 480, True)):
            for x, y, rw, rh, c in paint_rects(w, h, is16):
                mem.fill(fb, stride, x, y, rw, rh, c, is16)
                op = FILL | (BPP16 if is16 else 0)
                for idx, value in ((0, op), (1, fb), (2, stride), (3, 0), (4, 0), (5, (y << 16) | x),
                                   (6, 0), (7, (rh << 16) | rw), (8, c), (9, 0), (10, 0)):
                    s.add(f"st {idx} {value:#x}")
                s.write(KICK, 1)
                s.add("bw")
        s.reg("status_2d", STATUS)
        run = ssh_run(args, env, s.text())
        if log:
            log.write(run.stdout + run.stderr)
        regs, other = parse(run.stdout)
        waits = [int(p[1]) for p in other if p[0] == "B"]
        ok = "END" in run.stdout and waits and max(waits) < 2000 and \
            (regs.get("status_2d", 0) >> 16) & 0xF == 0
        result(ok, "paint the framebuffers with the 2D bitstream",
               f"{len(waits)} fills, STATUS {regs.get('status_2d', 0):#x}")
        painted = ok

    if not load(args, env, args.dc_bit, "ghci-dc"):
        result(False, "fpgaload DC")
        return 1

    # Identification.
    s = Script()
    for name, off in (("magic", 0x000), ("version", 0x004), ("vendor", 0x008), ("device", 0x00C),
                      ("isa", 0x010), ("extcap", 0x018), ("gclk", 0x01C), ("caps", 0x020),
                      ("cap0", 0x100), ("cap1", 0x104), ("dc_id", DC_ID),
                      ("dc_version", DC_VERSION), ("dc_caps", DC_CAPS), ("status0", DC_STATUS)):
        s.reg(name, off)
    s.write(DC_CTRL, 2)   # RESET
    s.reg("count_reset", FRAME_COUNT)
    run = ssh_run(args, env, s.text(), 60)
    regs, _ = parse(run.stdout)
    want = dict(magic=0x47484349, version=0x00010000, vendor=0x414D4154, device=0x41544443,
                isa=0, extcap=0x100, gclk=100000000, caps=0x8, cap0=0x101, cap1=0x200,
                dc_id=0x41544443, dc_version=1, dc_caps=0x473, count_reset=0)
    bad = {k: hex(regs.get(k, -1)) for k, v in want.items() if regs.get(k) != v}
    result(not bad, "identification (GHCI header, EXTCAP DISP, DC_ID/CAPS)", str(bad) if bad else "")

    # The modes.
    for name, fb, stride, fmt, flip_to in (("640x480", fb640, 2560, 0, fb720),
                                          ("1280x720", fb720, 5120, 0, fb640),
                                          ("640x480", fb565, 1280, 2, None)):
        sel, h, v, pol, pclk = MODES[name]
        is16 = fmt == 2
        label = f"{name}{' RGB565' if is16 else ''}"
        s = Script()
        mode_setup(s, name, fb, stride, fmt)
        s.add("vw 5")
        ssh_run(args, env, s.text(), 60)
        fc0, t0 = read_count(args, env)
        time.sleep(10)
        fc1, t1 = read_count(args, env)
        s = Script()
        s.add("vw 20")
        for k, off in (("status", DC_STATUS), ("irq", DC_IRQ_STATUS), ("base", FB_BASE),
                       ("sum", SCAN_SUM), ("words", SCAN_WORDS)):
            s.reg(k, off)
        if flip_to is not None:
            s.write(DC_IRQ_STATUS, 7)
            s.write(FB_NEXT, flip_to)
            # FLIP_DONE without fpgareg wait (whose --ack would clear it),
            # then three frames of the new framebuffer for the sum.
            s.add("n=0; while [ $(( $(r 0x218) & 4 )) -eq 0 ] && [ $n -lt 100 ]; do n=$((n+1)); done")
            s.reg("f_irq", DC_IRQ_STATUS)
            s.add("vw 3")
            for k, off in (("f_base", FB_BASE), ("f_sum", SCAN_SUM),
                           ("f_words", SCAN_WORDS), ("f_status", DC_STATUS)):
                s.reg(k, off)
        s.write(DC_CTRL, ENABLE | TEST_PATTERN)
        s.add("vw 3")
        s.reg("p_status", DC_STATUS)
        s.write(DC_CTRL, 0)
        s.add("vw 1")
        s.reg("off_status", DC_STATUS)
        run = ssh_run(args, env, s.text(), 120)
        if log:
            log.write(run.stdout + run.stderr)
        r1, other = parse(run.stdout)
        missed = [int(p[1]) for p in other if p[0] == "V"]
        fps = (fc1 - fc0) / (t1 - t0)
        want_fps = pclk / (sum(h) * sum(v))
        result(abs(fps - want_fps) < 0.5, f"{label}: frame rate", f"{fps:.2f} Hz, expected {want_fps:.3f}")
        result(missed and missed[0] == 0 and r1.get("irq", 0) & 1,
               f"{label}: vsync interrupt (20 fpgareg wait)", f"missed {missed}")
        st = r1.get("status", 0)
        result(st & 3 == 1 and not r1.get("irq", 0) & 2, f"{label}: running, no underflow",
               f"STATUS {st:#x} IRQ {r1.get('irq', 0):#x}")
        result(r1.get("base") == fb, f"{label}: FB_BASE", f"{r1.get('base', 0):#x}")
        result(r1.get("words") == h[0] * v[0], f"{label}: SCAN_WORDS", f"{r1.get('words')}")
        want_sum = mem.frame_sum(fb, stride, h[0], v[0], is16) if painted else None
        if want_sum is None:
            print(f"note {label}: SCAN_SUM {r1.get('sum', 0):#x} (memory not painted: not checked)")
        else:
            result(r1.get("sum") == want_sum, f"{label}: SCAN_SUM = the painted framebuffer",
                   f"{r1.get('sum', 0):#x}, expected {want_sum:#x}")
        if flip_to is not None:
            ok = r1.get("f_irq", 0) & 4 and r1.get("f_base") == flip_to and \
                r1.get("f_words") == h[0] * v[0] and r1.get("f_status", 0) & 3 == 1
            want_f = mem.frame_sum(flip_to, stride, h[0], v[0], is16) if painted else None
            if want_f is not None:
                ok = ok and r1.get("f_sum") == want_f
            result(ok, f"{label}: flip to {flip_to:#x} (FLIP_DONE, FB_BASE, SCAN_SUM)",
                   f"IRQ {r1.get('f_irq', 0):#x} base {r1.get('f_base', 0):#x} sum "
                   f"{r1.get('f_sum', 0):#x} expected {want_f if want_f is None else hex(want_f)}")
        result(r1.get("p_status", 0) & 3 == 1, f"{label}: TEST_PATTERN runs without underflow",
               f"STATUS {r1.get('p_status', 0):#x}")
        result(r1.get("off_status", 0) & 7 == 4, f"{label}: ENABLE 0 -> STATUS.idle",
               f"STATUS {r1.get('off_status', 0):#x}")

    # What the monitor shows afterwards.
    s = Script()
    if args.leave == "pattern":
        mode_setup(s, "640x480", fb640, 2560, 0, TEST_PATTERN)
    elif args.leave == "pattern720":
        mode_setup(s, "1280x720", fb720, 5120, 0, TEST_PATTERN)
    elif args.leave == "fb":
        mode_setup(s, "640x480", fb640, 2560, 0)
    elif args.leave == "fb720":
        mode_setup(s, "1280x720", fb720, 5120, 0)
    else:
        s.write(DC_CTRL, 0)
    s.write(DC_IRQ_ENABLE, 0)
    s.reg("leave_ctrl", DC_CTRL)
    run = ssh_run(args, env, s.text(), 60)
    regs, _ = parse(run.stdout)
    print(f"left: {args.leave} (CTRL {regs.get('leave_ctrl', 0):#x})")
    print("RESULT", "PASS" if fails == 0 else f"FAIL ({fails})")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
