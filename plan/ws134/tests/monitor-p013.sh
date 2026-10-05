#!/bin/sh
# ws134-p013: the System Monitor on the machine's own values (its system source: libkeiland's kl_system_monitor_*,
# zdesktop's kl_system_monitor_v1, libkeiland-backend-zedbsd's monitor area) on the Venus guest (the System Monitor's
# image: plan/ws134/tests/build-monitor-image.sh BUILD, then plan/tools/files/files-guest.sh start BUILD/hdd-image.img;
# 4 CPUs, NVMe, Venus).  zdesktop --glass at 1280x800.  Judged by the monitor's log (/tmp/monitor.log, read over SSH)
# and a picture, not the console.
#  1. The monitor without --source (the default, auto): ZMON READY source=system cpus=4, ZMON SYSTEM open; its samples
#     have the CPUs, the memory, the swap, the network and the disks as the system's (not in simulated: simulated
#     has none of 0x3f) and the memory in use above 0; a busy loop lifts the CPU to 20% or more;
#     a disk read shows 1 MB/s or more read.  system.png.
#  2. --source=sim still simulates: ZMON READY source=sim.
#  3. The compositor stops sampling after the monitor ends (ZWL SYSTEM monitor subscribers=0); no ERROR.
#
#   plan/ws134/tests/monitor-p013.sh [OUTDIR]
# Prints "monitor-p013: PASS" or "monitor-p013: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws134-p013}
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
stop_monitor='for p in $(ps -A -o pid,args | grep -E "[/]bin/monitor" | awk "{print \$1}"); do kill $p; done; sleep 2'
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[/]bin/monitor" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

# zdesktop.
guest "$stop_all" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=400 --width=1280 --height=800 --glass \$picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; echo started" >/dev/null

# 1. The default source, quiet for 6 s, then a busy loop and a disk read for 8 s.
guest "export XDG_RUNTIME_DIR=/tmp; /bin/monitor --timeout-s=120 --token=p013 > /tmp/monitor.log 2>&1 </dev/null & echo started" >/dev/null
sleep 8
guest "awk 'BEGIN { for (;;) ; }' & p=\$!; (sleep 2; dd if=/dev/nvme0n1 of=/dev/null bs=65536 skip=18000 count=1024 >/dev/null 2>&1) & sleep 8; kill \$p" >/dev/null
sleep 1
check "$out/system.png" >/dev/null
guest 'grep -E "ZMON (READY|SYSTEM|SAMPLE|FAILED|DISCONNECTED)" /tmp/monitor.log' > "$out/system.log"
guest "$stop_monitor" >/dev/null

# 2. The simulation by name.
guest "export XDG_RUNTIME_DIR=/tmp; /bin/monitor --timeout-s=60 --source=sim --token=sim > /tmp/monitor-sim.log 2>&1 </dev/null & sleep 6; grep 'ZMON READY' /tmp/monitor-sim.log" > "$out/sim.log"
guest "$stop_monitor" >/dev/null

# 3. The compositor's sampling and errors.
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
lines = open(out + "/system.log").read().splitlines()
ready = [l for l in lines if l.startswith("ZMON READY")]
expect(ready and " source=system cpus=4 " in ready[0], "the default source is the system's, 4 CPUs: %s" % (ready[:1]))
expect(any(l.startswith("ZMON SYSTEM open") for l in lines), "ZMON SYSTEM open")
expect(not any("FAILED" in l or "DISCONNECTED" in l for l in lines), "no failure")
samples = [fields(l) for l in lines if l.startswith("ZMON SAMPLE")]
expect(len(samples) >= 8, "samples: %d" % len(samples))
if samples:
    expect(all(int(s["simulated"], 0) & 0x3f == 0 for s in samples), "the CPUs, memory, swap, network and disks are the system's (simulated %s)" % samples[-1]["simulated"])
    used = max(int(s["used"]) for s in samples)
    expect(used > 0, "memory in use %d" % used)
    expect(any(float(s["cpu"]) >= 0.2 for s in samples), "the busy loop: CPU up to %.3f" % max(float(s["cpu"]) for s in samples))
    expect(any(float(s["read"]) >= 1e6 for s in samples), "the disk read: up to %.0f B/s" % max(float(s["read"]) for s in samples))
sim = open(out + "/sim.log").read()
expect(" source=sim " in sim, "--source=sim simulates")
log = open(out + "/zdesktop-monitor.txt").read()
expect("subscribers=1" in log and "subscribers=0" in log, "the compositor sampled while the monitor ran, and stopped")
expect(open(out + "/errors.txt").read().strip() == "0", "zdesktop: no ERROR")
sys.exit(0 if ok else 1)
EOF

[ $status -eq 0 ] && echo "monitor-p013: PASS" || echo "monitor-p013: FAIL"
exit $status
