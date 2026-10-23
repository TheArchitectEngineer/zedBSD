#!/usr/bin/env python3
"""Checks the Amaterrace GPU's bitstream 1 ("2D", WS288 p003/p004) on the real
ZYBO with nothing but fpgareg: no GPU driver.  Over SSH it (optionally)
loads the bitstream with fpgaload, then runs one shell script on the board
that reads the GHCI identification and capability registers, writes a
source pattern into the GPU's memory through the debug port
(DBG_ADDR/DATA), reads every destination window before and after each
blit through the same port, and drives the interrupt through
fpgareg wait.  The host compares the windows against a model of the
blitter (the testbench's, sim/tb/atg_top_tb.v of amaterrace-gpu).  Since
p004 (DEVICE_VERSION 0.3) it also drives the command ring: a FILL and a
FENCE written into the ring through the debug port, the doorbell, the
fence's interrupt through fpgareg wait; the debug port in HALTED; a class V
packet (no firmware: fault kind 2) -> RESET -> the ring again from 0.

  plan/tools/zybo/ghci-blt-board.py [--bit FILE.bit] [--host HOST]
      [--base 0x1C000000] [--seed N] [--log FILE]

The GPU's memory is the 64 MiB at --base that the board's device tree
reserves (plan/ws288/tests/zybo-gpu-sd.dts); the aperture is set to it.
--bit copies the bitstream to the board's /tmp and loads it first.  The
source pattern and the colours change with --seed (by default the time),
so that a run never finds the results of an earlier one already in place.  The
login is root/root (sshpass); the board's host key is not recorded.  The
host's board lock /tmp/zedbsd-zybo-board.lock is held for the run.
Prints one PASS or FAIL line per check and exits 1 on any FAIL.  Runs in
about a minute (each fpgareg is a process on the board, about 10 ms).

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

# GHCI registers (amaterrace-gpu include/ghci_regs.h).
MAGIC, VERSION, VENDOR, DEVICE, DEVVER, EXTCAP, CAPS = 0x000, 0x004, 0x008, 0x00C, 0x014, 0x018, 0x020
CTRL, STATUS, IRQ_STATUS, IRQ_ENABLE, IRQ_SET = 0x030, 0x034, 0x038, 0x03C, 0x040
AP_BASE, AP_LIMIT, FAULT_ADDR, FAULT_INFO = 0x060, 0x064, 0x068, 0x06C
RING_BASE, RING_SIZE, RING_WPTR, RING_RPTR, FENCE_LAST = 0x050, 0x054, 0x058, 0x05C, 0x070
PERF0, PERF1, PERF_CTRL = 0x080, 0x084, 0x090
DBG_ADDR, DBG_DATA, DBG_STATUS, KICK, STATE = 0x0A0, 0x0A4, 0x0A8, 0xF00, 0x1000
FILL, COPY, TILE, UNTILE = 0, 1, 2, 3
BLEND, BPP16, REVERSE = 1 << 4, 1 << 8, 1 << 12


SEED = 0


def pattern(addr):
    """The source pattern's word at a byte address."""
    x = ((addr ^ SEED) * 2654435761) & 0xFFFFFFFF
    return x ^ (x >> 13) ^ 0x5A5A0000


def colour(value):
    """A colour of the run: the alpha byte kept, the others varied."""
    return value ^ (pattern(SEED & 0xFFFC) & 0x00FFFFFF)


class Board:
    """Collects the board script and the names of what it prints."""

    def __init__(self):
        self.lines = [
            "r() { fpgareg read $1; }",
            "w() { fpgareg write $1 $2 >/dev/null; }",
            # A debug read: start it, the status, the value.
            "dr() { w 0xa0 $1; s=$(r 0xa8); echo \"D $1 $(r 0xa4) $s\"; }",
            "dw() { w 0xa0 $1; w 0xa4 $2; }",
            "st() { w $((0x1000 + $1 * 4)) $2; }",
            # Waits for the blitter: STATUS[1] clear.
            "bw() { n=0; while [ $(( $(r 0x34) & 2 )) -ne 0 ] && [ $n -lt 200 ]; do n=$((n+1)); done; echo \"B $n\"; }",
        ]

    def add(self, text):
        self.lines.append(text)

    def reg(self, name, offset):
        self.add(f"echo \"R {name} $(r {offset:#x})\"")

    def write(self, offset, value):
        self.add(f"w {offset:#x} {value:#x}")

    def read_words(self, addrs):
        for a in addrs:
            self.add(f"dr {a:#x}")

    def script(self):
        return "\n".join(self.lines) + "\necho END\n"


class Model:
    """The GPU memory as the host knows it: words read through the port."""

    def __init__(self):
        self.mem = {}

    def get(self, addr, is16):
        word = self.mem[addr & ~3]
        if not is16:
            return word
        return (word >> 16) & 0xFFFF if addr & 2 else word & 0xFFFF

    def put(self, addr, is16, value):
        k = addr & ~3
        if not is16:
            self.mem[k] = value & 0xFFFFFFFF
        elif addr & 2:
            self.mem[k] = (self.mem[k] & 0x0000FFFF) | ((value & 0xFFFF) << 16)
        else:
            self.mem[k] = (self.mem[k] & 0xFFFF0000) | (value & 0xFFFF)


def pix_addr(base, stride, x, y, tiled, ps):
    if tiled:
        return base + (y // 4) * stride + (x // 4) * 16 * ps + ((y % 4) * 4 + x % 4) * ps
    return base + y * stride + x * ps


def over(src, dst):
    out = 0
    alpha = src >> 24
    for ch in range(4):
        t = ((dst >> (8 * ch)) & 0xFF) * (255 - alpha)
        q = ((src >> (8 * ch)) & 0xFF) + (t + 127) // 255
        out |= min(q, 255) << (8 * ch)
    return out


def model_blit(m, b):
    """The blitter's result in the model (sources read before writes)."""
    op = b["op"]
    is16 = bool(op & BPP16)
    ps = 2 if is16 else 4
    mask = ~(ps - 1)
    dtile = (op & 3) == TILE
    stile = (op & 3) == UNTILE
    dx, dy, sx, sy, w, h = b["dx"], b["dy"], b["sx"], b["sy"], b["w"], b["h"]
    values = []
    for y in range(dy, dy + h):
        for x in range(dx, dx + w):
            if op & 3 == FILL:
                v = b["color"] & (0xFFFF if is16 else 0xFFFFFFFF)
            else:
                v = m.get(pix_addr(b["src"] & mask, b["sstride"] & mask, sx + x - dx,
                                   sy + y - dy, stile, ps), is16)
            values.append(v)
    i = 0
    for y in range(dy, dy + h):
        for x in range(dx, dx + w):
            v = values[i]
            i += 1
            a = pix_addr(b["dst"] & mask, b["dstride"] & mask, x, y, dtile, ps)
            if op & BLEND:
                v = over(v, m.get(a, is16))
            m.put(a, is16, v)


def window(base, stride, cols_bytes, rows):
    """The words of a rectangle of bytes."""
    return [base + y * stride + c for y in range(rows) for c in range(0, cols_bytes, 4)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bit")
    parser.add_argument("--host", default="10.0.30.2")
    parser.add_argument("--base", default="0x1C000000")
    parser.add_argument("--seed", type=int)
    parser.add_argument("--log")
    args = parser.parse_args()
    base = int(args.base, 0)
    global SEED
    SEED = (args.seed if args.seed is not None else int(time.time())) & 0xFFFFFFFF
    print(f"seed {SEED}")

    board = Board()
    # Identification and capabilities.
    for name, off in (("magic", MAGIC), ("version", VERSION), ("vendor", VENDOR),
                      ("device", DEVICE), ("devver", DEVVER), ("extcap", EXTCAP),
                      ("caps", CAPS), ("cap0", 0x100), ("cap1", 0x104), ("psrc0", 0x110),
                      ("psrc1", 0x114), ("status0", STATUS)):
        board.reg(name, off)
    # A clean start: RESET, the aperture over the reserved memory.
    board.write(CTRL, 2)
    board.write(IRQ_STATUS, 0x7F)
    board.write(AP_BASE, base)
    board.write(AP_LIMIT, base + 0x4000000)
    board.reg("ap_base", AP_BASE)
    board.reg("ap_limit", AP_LIMIT)

    # The source pattern: 24 x 8 words at base + 0x1000, stride 128.
    src32 = base + 0x1000
    src_words = window(src32, 128, 24 * 4, 8)
    for a in src_words:
        board.add(f"dw {a:#x} {pattern(a):#x}")
    board.read_words(src_words[:4])
    board.reg("dbg_status", DBG_STATUS)

    # The blits: each with the window it may change.
    blits = [
        dict(name="fill32", op=FILL, dst=base + 0x10000, dstride=128, src=0, sstride=0,
             dx=3, dy=1, sx=0, sy=0, w=13, h=3, color=colour(0xAABBCCDD), win=(base + 0x10000, 128, 80, 5)),
        dict(name="copy32", op=COPY, dst=base + 0x11004, dstride=128, src=src32 + 8, sstride=128,
             dx=2, dy=2, sx=5, sy=1, w=15, h=4, color=0, win=(base + 0x11000, 128, 88, 7)),
        dict(name="fill16", op=FILL | BPP16, dst=base + 0x12002, dstride=64, src=0, sstride=0,
             dx=3, dy=1, sx=0, sy=0, w=21, h=3, color=colour(0xBEEF), win=(base + 0x12000, 64, 64, 5)),
        dict(name="copy16", op=COPY | BPP16, dst=base + 0x13006, dstride=64, src=src32, sstride=128,
             dx=1, dy=1, sx=5, sy=2, w=25, h=3, color=0, win=(base + 0x13000, 64, 64, 5)),
        dict(name="fill-blend", op=FILL | BLEND, dst=base + 0x11004, dstride=128, src=0, sstride=0,
             dx=4, dy=3, sx=0, sy=0, w=9, h=2, color=colour(0x80402010), win=(base + 0x11000, 128, 88, 7)),
        dict(name="copy-blend", op=COPY | BLEND, dst=base + 0x10000, dstride=128, src=src32, sstride=128,
             dx=1, dy=0, sx=1, sy=2, w=10, h=3, color=0, win=(base + 0x10000, 128, 80, 5)),
        dict(name="tile", op=TILE, dst=base + 0x14000, dstride=128, src=src32, sstride=128,
             dx=0, dy=0, sx=2, sy=0, w=8, h=8, color=0, win=(base + 0x14000, 128, 128, 2)),
        dict(name="untile", op=UNTILE, dst=base + 0x15000, dstride=128, src=base + 0x14000, sstride=128,
             dx=1, dy=1, sx=0, sy=0, w=8, h=8, color=0, win=(base + 0x15000, 128, 40, 10)),
        dict(name="overlap-reverse", op=COPY | REVERSE, dst=base + 0x11004, dstride=128,
             src=base + 0x11004, sstride=128, dx=3, dy=2, sx=1, sy=1, w=12, h=4, color=0,
             win=(base + 0x11000, 128, 88, 7)),
        dict(name="overlap-forward16", op=COPY | BPP16, dst=base + 0x13006, dstride=64,
             src=base + 0x13006, sstride=64, dx=0, dy=0, sx=3, sy=1, w=20, h=3, color=0,
             win=(base + 0x13000, 64, 64, 5)),
    ]
    for i, b in enumerate(blits):
        w = window(*b["win"])
        b["words"] = w
        board.add(f"echo \"P {i}\"")
        board.read_words(w)
        for idx, key in ((0, "op"), (1, "dst"), (2, "dstride"), (3, "src"), (4, "sstride")):
            board.add(f"st {idx} {b[key]:#x}")
        board.add(f"st 5 {(b['dy'] << 16) | b['dx']:#x}")
        board.add(f"st 6 {(b['sy'] << 16) | b['sx']:#x}")
        board.add(f"st 7 {(b['h'] << 16) | b['w']:#x}")
        board.add(f"st 8 {b['color']:#x}")
        board.add("st 9 0; st 10 0")
        board.write(KICK, 1)
        board.add("bw")
        board.add(f"echo \"A {i}\"")
        board.read_words(w)
        board.reg(f"status_{i}", STATUS)
    board.reg("perf0", PERF0)
    board.reg("perf1", PERF1)
    board.write(PERF_CTRL, 1)
    board.reg("perf0_clear", PERF0)

    # The interrupt: SOFT, then no second one; HALTED from a background
    # wait; an aperture fault, RESET and a fill that works again.
    board.write(IRQ_STATUS, 0x7F)
    board.write(IRQ_ENABLE, 1 << 5)
    board.write(IRQ_SET, 1 << 5)
    board.add("echo \"W soft $(fpgareg wait --ack 0x38 1000 2>&1)\"")
    board.add("echo \"W again $(fpgareg wait --ack 0x38 300 2>&1)\"")
    board.write(IRQ_ENABLE, 1 << 6)
    board.add("fpgareg wait --ack 0x38 3000 > /tmp/ghci-wait.txt 2>&1 &")
    board.add("r 0x2c >/dev/null; r 0x2c >/dev/null")
    board.write(CTRL, 1 << 2)
    board.add("wait")
    board.add("echo \"W halted $(cat /tmp/ghci-wait.txt)\"")
    board.reg("status_halted", STATUS)
    board.write(CTRL, 0)
    board.write(IRQ_STATUS, 0x7F)
    board.write(IRQ_ENABLE, 1 << 1)
    board.add(f"st 0 0; st 1 {base - 0x100:#x}; st 2 128; st 5 0; st 7 0x10002; st 9 0; st 10 0")
    board.write(KICK, 1)
    board.add("echo \"W fault $(fpgareg wait --ack 0x38 1000 2>&1)\"")
    board.reg("status_fault", STATUS)
    board.reg("fault_info", FAULT_INFO)
    board.reg("fault_addr", FAULT_ADDR)
    board.write(CTRL, 2)
    board.reg("status_reset", STATUS)
    board.reg("fault_info_reset", FAULT_INFO)
    refill = dict(name="fill-after-reset", op=FILL, dst=base + 0x16000, dstride=64, src=0, sstride=0,
                  dx=1, dy=1, sx=0, sy=0, w=6, h=2, color=colour(0x0BADF00D), win=(base + 0x16000, 64, 32, 4))
    refill["words"] = window(*refill["win"])
    board.add(f"echo \"P {len(blits)}\"")
    board.read_words(refill["words"])
    board.add(f"st 0 0; st 1 {refill['dst']:#x}; st 2 64; st 5 0x10001; st 7 0x20006; st 8 {refill['color']:#x}")
    board.write(KICK, 1)
    board.add("bw")
    board.add(f"echo \"A {len(blits)}\"")
    board.read_words(refill["words"])
    blits.append(refill)

    # The ring (p004): 1 KiB at base + 0x20000, the fence word at + 0x21000.
    ring = base + 0x20000
    fence_addr = base + 0x21000
    fence1 = 0x1000 + (SEED & 0xFFF)
    ringfill = dict(name="ring-fill", op=FILL, dst=base + 0x17000, dstride=64, src=0, sstride=0,
                    dx=2, dy=1, sx=0, sy=0, w=5, h=2, color=colour(0x00C0FFEE),
                    win=(base + 0x17000, 64, 32, 4))
    ringfill["words"] = window(*ringfill["win"])
    pkts = [(0 << 30) | (1 << 24) | (0x11 << 16) | 22]
    for idx, val in ((0, FILL), (1, ringfill["dst"]), (2, 64), (3, 0), (4, 0), (5, 0x10002),
                     (6, 0), (7, 0x20005), (8, ringfill["color"]), (9, 0), (10, 0)):
        pkts += [idx, val]
    pkts += [(2 << 24) | (0x12 << 16), (5 << 24) | (0x13 << 16) | (1 << 8) | 2, fence_addr, fence1]
    board.add(f"echo \"P {len(blits)}\"")
    board.read_words(ringfill["words"])
    board.add(f"dw {fence_addr:#x} 0")
    for i, v in enumerate(pkts):
        board.add(f"dw {ring + 4 * i:#x} {v:#x}")
    board.write(RING_BASE, ring)
    board.write(RING_SIZE, 10)
    board.write(FENCE_LAST, 0)
    board.write(IRQ_STATUS, 0x1FF)
    board.write(IRQ_ENABLE, 1 << 0)
    board.write(CTRL, 1)
    board.write(RING_WPTR, len(pkts))
    board.add("echo \"W fence $(fpgareg wait --ack 0x38 1000 2>&1)\"")
    board.reg("fence_last", FENCE_LAST)
    board.reg("ring_rptr", RING_RPTR)
    board.reg("status_ring", STATUS)
    board.add(f"echo \"A {len(blits)}\"")
    board.read_words(ringfill["words"])
    blits.append(ringfill)
    board.add("echo \"F 0\"")
    board.read_words([fence_addr])
    # The debug port in HALTED (it waited for HALT = 0 before p004).
    board.write(CTRL, 5)
    board.reg("status_halt2", STATUS)
    board.add("echo \"F 1\"")
    board.read_words([src32])
    board.write(CTRL, 1)
    # A class V packet without firmware: fault kind 2, then RESET and the ring from 0.
    wp = len(pkts)
    for i, v in enumerate([(2 << 30) | (0x5C << 16) | 1, 0]):
        board.add(f"dw {ring + 4 * (wp + i):#x} {v:#x}")
    board.write(IRQ_STATUS, 0x1FF)
    board.write(IRQ_ENABLE, 1 << 1)
    board.write(RING_WPTR, wp + 2)
    board.add("echo \"W vfault $(fpgareg wait --ack 0x38 1000 2>&1)\"")
    board.reg("fault_info_v", FAULT_INFO)
    board.reg("fault_addr_v", FAULT_ADDR)
    board.write(CTRL, 2)
    board.reg("status_reset2", STATUS)
    board.reg("ring_wptr_reset", RING_WPTR)
    fence2 = fence1 + 1
    for i, v in enumerate([(5 << 24) | (0x14 << 16) | (1 << 8) | 2, fence_addr, fence2]):
        board.add(f"dw {ring + 4 * i:#x} {v:#x}")
    board.write(FENCE_LAST, fence1)
    board.write(IRQ_STATUS, 0x1FF)
    board.write(IRQ_ENABLE, 1 << 0)
    board.write(CTRL, 1)
    board.write(RING_WPTR, 3)
    board.add("echo \"W fence2 $(fpgareg wait --ack 0x38 1000 2>&1)\"")
    board.reg("fence_last2", FENCE_LAST)
    board.add("echo \"F 2\"")
    board.read_words([fence_addr])
    board.write(CTRL, 0)

    script = board.script()

    # Runs it on the board under the host's board lock.
    lock = open(LOCK, "a")
    fcntl.flock(lock, fcntl.LOCK_EX)
    env = dict(os.environ, SSHPASS="root")
    ssh = ["sshpass", "-e", "ssh"] + SSH_OPTIONS + [f"root@{args.host}"]
    if args.bit:
        subprocess.run(["timeout", "120", "sshpass", "-e", "scp"] + SSH_OPTIONS +
                       [args.bit, f"root@{args.host}:/tmp/ghci-test.bit"], env=env, check=True)
        load = subprocess.run(["timeout", "60"] + ssh + ["fpgaload /tmp/ghci-test.bit; fpgaload --status"],
                              env=env, capture_output=True, text=True)
        print(load.stdout.strip())
        if load.returncode != 0:
            print("FAIL fpgaload", load.stderr.strip())
            return 1
    run = subprocess.run(["timeout", "600"] + ssh + ["sh -s"], input=script, env=env,
                         capture_output=True, text=True)
    out = run.stdout
    if args.log:
        with open(args.log, "w") as log:
            log.write(out)
            log.write(run.stderr)
    if "END" not in out:
        print("FAIL the board script did not finish:", run.stderr.strip()[-400:])
        return 1

    # Parses what the board printed.
    regs = {}
    phase = {}
    current = None
    waits = {}
    busy_loops = []
    for line in out.splitlines():
        part = line.split()
        if not part:
            continue
        if part[0] == "R":
            regs[part[1]] = int(part[2], 0)
        elif part[0] == "F":
            current = ("F", int(part[1]))
            phase[current] = {}
        elif part[0] in ("P", "A"):
            current = (part[0], int(part[1]))
            phase[current] = {}
        elif part[0] == "D":
            value, status = int(part[2], 0), int(part[3], 0)
            if current is None:
                phase.setdefault(("S", 0), {})[int(part[1], 0)] = (value, status)
            else:
                phase[current][int(part[1], 0)] = (value, status)
        elif part[0] == "W":
            waits[part[1]] = " ".join(part[2:])
        elif part[0] == "B":
            busy_loops.append(int(part[1]))

    failures = 0

    def check(name, ok, detail=""):
        nonlocal failures
        print(("PASS " if ok else "FAIL ") + name + ("" if ok else ": " + detail))
        if not ok:
            failures += 1

    check("identification", regs.get("magic") == 0x47484349 and regs.get("version") == 0x00010000
          and regs.get("vendor") == 0x414D4154 and regs.get("device") == 0x41544731,
          str({k: hex(regs.get(k, -1)) for k in ("magic", "version", "vendor", "device")}))
    check("device version 0.3", regs.get("devver") == 3, hex(regs.get("devver", -1)))
    check("CAPS (CP+ring, BLT, 4x4 tiles, state 1, 32-byte lines)", regs.get("caps") == 0x00501803,
          hex(regs.get("caps", -1)))
    check("EXTCAP PERF", regs.get("extcap") == 0x100 and regs.get("cap0") == 0x103 and
          regs.get("cap1") == 0x110 and regs.get("psrc0") == 1 and regs.get("psrc1") == 8,
          str({k: hex(regs.get(k, -1)) for k in ("extcap", "cap0", "cap1", "psrc0", "psrc1")}))
    check("aperture", regs.get("ap_base") == base and regs.get("ap_limit") == base + 0x4000000)
    first = phase.get(("S", 0), {})
    check("DBG write/read", all(first.get(a) == (pattern(a), 0) for a in src_words[:4]),
          str({hex(a): first.get(a) for a in src_words[:4]}))
    check("DBG_STATUS after the writes", regs.get("dbg_status") == 0, hex(regs.get("dbg_status", -1)))

    model = Model()
    for a in src_words:
        model.mem[a] = pattern(a)
    for i, b in enumerate(blits):
        before = phase.get(("P", i), {})
        after = phase.get(("A", i), {})
        if len(before) != len(b["words"]) or len(after) != len(b["words"]):
            check(b["name"], False, "missing words")
            continue
        for a, (value, status) in before.items():
            model.mem[a] = value
        model_blit(model, b)
        bad = [(a, after[a][0], model.mem[a]) for a in b["words"]
               if after[a][0] != model.mem[a] or after[a][1] != 0]
        for a in b["words"]:
            model.mem[a] = after[a][0]
        st = regs.get(f"status_{i}", 0)
        changed = sum(1 for a in b["words"] if before[a][0] != after[a][0])
        detail = ", ".join(f"{a:#x} = {g:#010x} not {e:#010x}" for a, g, e in bad[:4])
        check(f"{b['name']} ({len(b['words'])} words, {changed} changed)",
              not bad and (b is blits[-1] or (st & 0x20F) == 0), detail or f"STATUS {st:#x}")
    check("PERF counters", regs.get("perf0", 0) > 0 and regs.get("perf1", 0) > 0 and
          regs.get("perf0_clear", 1) < regs.get("perf0", 0),
          str({k: regs.get(k) for k in ("perf0", "perf1", "perf0_clear")}))
    check("IRQ_SET -> fpgareg wait", waits.get("soft", "").endswith("status 0x00000020"), waits.get("soft", ""))
    check("no second interrupt", "irq count" not in waits.get("again", "irq count") and
          "wait:" in waits.get("again", ""), waits.get("again", ""))
    check("HALT -> HALTED interrupt", waits.get("halted", "").endswith("status 0x00000040") and
          (regs.get("status_halted", 0) >> 16) & 0xF == 3,
          f"{waits.get('halted', '')} STATUS {regs.get('status_halted', 0):#x}")
    check("aperture fault -> interrupt, FAULTED, FAULT_INFO",
          "status 0x00000002" in waits.get("fault", "") and (regs.get("status_fault", 0) >> 16) & 0xF == 4
          and regs.get("fault_info") == 0x00011101 and regs.get("fault_addr") == base - 0x100,
          f"{waits.get('fault', '')} STATUS {regs.get('status_fault', 0):#x} "
          f"INFO {regs.get('fault_info', 0):#x} ADDR {regs.get('fault_addr', 0):#x}")
    check("RESET -> IDLE", (regs.get("status_reset", -1) >> 16) & 0xF == 0 and
          regs.get("fault_info_reset") == 0, hex(regs.get("status_reset", -1)))
    fword = {k: list(phase.get(("F", k), {}).values()) for k in range(3)}
    check("ring FENCE -> interrupt, FENCE_LAST, fence word, RPTR",
          "irq count" in waits.get("fence", "") and int(waits.get("fence", "status 0").split()[-1], 0) & 1
          and regs.get("fence_last") == fence1 and regs.get("ring_rptr") == len(pkts)
          and fword[0] == [(fence1, 0)],
          f"{waits.get('fence', '')} FENCE_LAST {regs.get('fence_last', 0):#x} "
          f"RPTR {regs.get('ring_rptr', -1)} word {fword[0]}")
    check("DBG in HALTED", (regs.get("status_halt2", 0) >> 16) & 0xF == 3 and
          fword[1] == [(pattern(src32), 0)], f"STATUS {regs.get('status_halt2', 0):#x} {fword[1]}")
    check("class V without firmware -> fault kind 2", "status" in waits.get("vfault", "") and
          regs.get("fault_info_v") == (0x5C << 24) | (2 << 12) and
          regs.get("fault_addr_v") == ring + 4 * len(pkts),
          f"{waits.get('vfault', '')} INFO {regs.get('fault_info_v', 0):#x} "
          f"ADDR {regs.get('fault_addr_v', 0):#x}")
    check("RESET -> the ring from 0 -> FENCE", (regs.get("status_reset2", -1) >> 16) & 0xF == 0 and
          regs.get("ring_wptr_reset") == 0 and regs.get("fence_last2") == fence2 and
          fword[2] == [(fence2, 0)],
          f"STATUS {regs.get('status_reset2', -1):#x} FENCE_LAST {regs.get('fence_last2', 0):#x} "
          f"{fword[2]}")
    print(f"blit waits (STATUS polls): {busy_loops}")
    print("PASS ghci-blt-board" if failures == 0 else f"FAIL ghci-blt-board: {failures} checks")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
