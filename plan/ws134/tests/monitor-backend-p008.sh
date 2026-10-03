#!/bin/sh
# ws134-p008: the System Monitor's backend area on zedBSD (libkeiland-backend-zedbsd/monitor-zedbsd.c) on a running
# guest, through the test probe /bin/monitor-probe (the System Monitor's image: plan/ws134/tests/build-monitor-image.sh
# BUILD, then plan/tools/files/files-guest.sh start BUILD/hdd-image.img; 4 CPUs, an NVMe disk, Venus).  Over SSH, never
# the console.  The probe samples twice 3 s apart while one busy loop runs and 32 MiB are read from the disk:
#  1. The info: as many CPUs as hw.ncpu, the host's name, nvme0n1 of kind 2, a link or more (no loopback), no GPU on
#     Venus.
#  2. The samples: valid has the CPUs, the memory, the swap, the links and the disks (0x1f), not the GPU's bits; the
#     CPUs' ticks grew by 3 s x hz x CPUs within 15%, the user ticks by 60% of one CPU's 3 s or more; the memory's total
#     is above 0 with the free, the caches and the reclaimable within it; the disks' read bytes grew by 8 MiB or more;
#     the links carried bytes (the SSH session itself); the time grew by about 3 s.
#
#   plan/ws134/tests/monitor-backend-p008.sh [OUTDIR]
# Prints "monitor-backend-p008: PASS" or "monitor-backend-p008: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws134-p008}
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }

# The probe under a busy loop and a disk read, and what the system says of itself.
guest "sysctl hw.ncpu; hostname; awk 'BEGIN { for (;;) ; }' & p=\$!; (sleep 1; dd if=/dev/nvme0n1 of=/dev/null bs=65536 skip=12000 count=512 >/dev/null 2>&1) & /bin/monitor-probe 2 3000; echo exit=\$?; kill \$p" > "$out/probe.txt"
python3 - "$out/probe.txt" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
ok = True
def expect(condition, what):
    global ok
    print(("ok: " if condition else "FAILED: ") + what)
    ok = ok and condition
def fields(line):
    return dict(f.split("=", 1) for f in line.split()[2:] if "=" in f)
ncpu = next((int(l.split(":")[1]) for l in lines if l.startswith("hw.ncpu:")), None)
host = next((l for l in lines[1:3] if not l.startswith(("hw.", "MPROBE"))), "").strip()
info = next((fields(l) for l in lines if l.startswith("MPROBE INFO")), None)
disks = [fields(l) for l in lines if l.startswith("MPROBE DISK")]
links = [fields(l) for l in lines if l.startswith("MPROBE LINK")]
samples = [fields(l) for l in lines if l.startswith("MPROBE SAMPLE")]
expect("exit=0" in lines and "MPROBE DONE" in lines, "the probe ran to its end")
expect(info is not None, "the info")
if info:
    expect(int(info["cpus"]) == ncpu, "CPUs %s (hw.ncpu %s)" % (info["cpus"], ncpu))
    expect(info["host"] == host, "host %s (hostname %s)" % (info["host"], host))
    expect(int(info["gpus"]) == 0, "no GPU on Venus")
expect(any(d["name"] == "nvme0n1" and d["kind"] == "2" for d in disks), "nvme0n1 as NVMe")
expect(len(links) >= 1 and not any(l["name"].startswith("lo") for l in links), "links without the loopback: %s" % [l["name"] for l in links])
expect(len(samples) == 2, "two samples")
if len(samples) == 2:
    a, b = samples
    n = lambda s, k: int(s[k], 0)
    expect(n(a, "valid") & 0x1f == 0x1f and n(b, "valid") & 0x1f == 0x1f, "valid has the CPUs, memory, swap, links, disks: %s" % b["valid"])
    expect(n(b, "valid") & 0xe0 == 0, "no GPU bits on Venus")
    seconds = (n(b, "time_ns") - n(a, "time_ns")) / 1e9
    expect(2.8 <= seconds <= 4.0, "3 s apart: %.2f" % seconds)
    hz, cpus = n(b, "cpu_hz"), n(b, "cpus")
    ticks = sum(n(b, k) - n(a, k) for k in ("user", "system", "idle", "other"))
    want = seconds * hz * cpus
    expect(abs(ticks - want) <= 0.15 * want, "the CPUs' ticks %d (want %.0f)" % (ticks, want))
    user = n(b, "user") - n(a, "user")
    expect(user >= 0.6 * 3 * hz, "user ticks %d (one busy loop)" % user)
    total = n(b, "mem_total")
    expect(total > 0, "memory total %d" % total)
    expect(n(b, "mem_free") <= total and n(b, "cache") <= total and n(b, "reclaimable") <= n(b, "cache"), "free %s, caches %s, reclaimable %s within it" % (b["mem_free"], b["cache"], b["reclaimable"]))
    read = n(b, "read_bytes") - n(a, "read_bytes")
    expect(read >= 8 << 20, "disks read %d bytes" % read)
    carried = n(b, "rx") + n(b, "tx") - n(a, "rx") - n(a, "tx")
    expect(carried > 0, "links carried %d bytes" % carried)
sys.exit(0 if ok else 1)
EOF

[ $status -eq 0 ] && echo "monitor-backend-p008: PASS" || echo "monitor-backend-p008: FAIL"
exit $status
