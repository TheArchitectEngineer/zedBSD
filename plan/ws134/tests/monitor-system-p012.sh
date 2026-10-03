#!/bin/sh
# ws134-p012: the system extension's monitor end to end on the Venus guest (the System Monitor's image:
# plan/ws134/tests/build-monitor-image.sh BUILD, then plan/tools/files/files-guest.sh start BUILD/hdd-image.img; 4
# CPUs, NVMe, Venus): zdesktop's kl_system_monitor_v1 (its sampling thread, the info, the samples and the acks)
# through libkeiland's kl_system_monitor_* in the probe /bin/keiland-system ("monitor" at 250 ms).  Over SSH, never
# the console.
#  1. Quiet, 4 s: the manager offers the monitor (capabilities has 0x20); the info: 4 CPUs, the host's name,
#     nvme0n1 of kind 2, a link, no GPU (Venus); 10 frames or more, about 0.25 s apart, valid with the CPUs, the
#     memory, the swap, the links and the disks (0x1f); the memory's total above 0; the compositor's log has the
#     sampling start (subscribers=1) and, after the probe ended, stop (subscribers=0).
#  2. One busy loop and a disk read, 4 s: some frame has the CPUs 20% busy or more (one of four), and some frame reads
#     1 MB/s or more.
#
#   plan/ws134/tests/monitor-system-p012.sh [OUTDIR]
# Prints "monitor-system-p012: PASS" or "monitor-system-p012: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws134-p012}
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[m]onitor|[k]eiland-system" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

# zdesktop alone (the probe runs as root, as zdesktop does: the same user).
guest "$stop_all" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --timeout=300 --width=1280 --height=800 > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; echo started" >/dev/null

# 1. Quiet.
guest "export XDG_RUNTIME_DIR=/tmp; hostname; /bin/keiland-system --timeout-ms=4000 monitor; sleep 1" > "$out/quiet.txt"

# 2. A busy loop and a disk read.
guest "export XDG_RUNTIME_DIR=/tmp; awk 'BEGIN { for (;;) ; }' & p=\$!; (sleep 1; dd if=/dev/nvme0n1 of=/dev/null bs=65536 skip=15000 count=512 >/dev/null 2>&1) & /bin/keiland-system --timeout-ms=4000 monitor; kill \$p; sleep 1" > "$out/busy.txt"
guest 'grep "ZWL SYSTEM monitor" /tmp/zdesktop.log' > "$out/zdesktop-monitor.txt"
guest "grep -c ERROR /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null

python3 - "$out" <<'EOF' || status=1
import sys
out = sys.argv[1]
ok = True
def expect(condition, what):
    global ok
    print(("ok: " if condition else "FAILED: ") + what)
    ok = ok and condition
def fields(line):
    return dict(f.split("=", 1) for f in line.split()[2:] if "=" in f)
quiet = open(out + "/quiet.txt").read().splitlines()
busy = open(out + "/busy.txt").read().splitlines()
host = quiet[0].strip() if quiet else ""
opened = [fields(l) for l in quiet if l.startswith("KEILAND-SYSTEM open")]
expect(opened and int(opened[0]["capabilities"], 0) & 0x20, "the manager offers the monitor: %s" % (opened and opened[0]["capabilities"]))
info = [fields(l) for l in quiet if l.startswith("KEILAND-SYSTEM monitor-info")]
expect(len(info) >= 1, "the info came")
if info:
    expect(info[0]["cpus"] == "4" and info[0]["host"] == host, "4 CPUs and the host %s (%s)" % (info[0]["host"], host))
    expect(info[0]["gpus"] == "0", "no GPU on Venus")
disks = [fields(l) for l in quiet if l.startswith("KEILAND-SYSTEM monitor-disk")]
links = [fields(l) for l in quiet if l.startswith("KEILAND-SYSTEM monitor-link")]
expect(any(d["name"] == "nvme0n1" and d["kind"] == "2" for d in disks), "nvme0n1 as NVMe")
expect(len(links) >= 1, "links: %s" % [l["name"] for l in links])
frames = [fields(l) for l in quiet if l.startswith("KEILAND-SYSTEM monitor-frame")]
expect(len(frames) >= 10, "10 frames or more in 4 s: %d" % len(frames))
if frames:
    seconds = [float(f["seconds"]) for f in frames]
    expect(all(0.15 <= s <= 0.6 for s in seconds), "about 0.25 s apart: %.3f to %.3f" % (min(seconds), max(seconds)))
    expect(all(int(f["valid"], 0) & 0x1f == 0x1f for f in frames), "valid has the CPUs, memory, swap, links, disks")
    expect(all(int(f["mem_total"]) > 0 for f in frames), "the memory's total")
    expect(all(0.0 <= float(f["cpu"]) <= 1.0 for f in frames), "the CPUs' shares within 0 and 1")
heavy = [fields(l) for l in busy if l.startswith("KEILAND-SYSTEM monitor-frame")]
expect(any(float(f["cpu"]) >= 0.2 for f in heavy), "a busy loop: the CPUs %s" % [f["cpu"] for f in heavy][:8])
expect(any(float(f["read"]) >= 1e6 for f in heavy), "a disk read: up to %.0f B/s" % max([float(f["read"]) for f in heavy] or [0]))
log = open(out + "/zdesktop-monitor.txt").read()
expect("subscribers=1" in log and "subscribers=0" in log, "the sampling started and stopped")
expect(open(out + "/errors.txt").read().strip() == "0", "zdesktop: no ERROR")
sys.exit(0 if ok else 1)
EOF

[ $status -eq 0 ] && echo "monitor-system-p012: PASS" || echo "monitor-system-p012: FAIL"
exit $status
