#!/bin/sh
# ws309: on QEMU's Venus guest, the handwriting face's ink is recognized on the recognition pool (hand-1 to hand-N)
# and the event loop takes the answer.  The guest is started from IMAGE in OUT/runtime; WAYLAND is put in as
# /bin/wayland; a compositor of its own (--testing --glass, 1280x800) is driven by aat-input's pointer: the bottom-left
# corner's swipe opens the QWERTY panel, its band button (1186,486) the handwriting face (writing area 962x288 at
# 6,506), three strokes are written and, 1.5 s later, a fourth (the same ink, more strokes), then the clear key
# (1047,665) after a fifth stroke.  The guest is ended by QEMU's monitor.
# Prints "hand-guest: PASS" when the compositor was ready, the pool started with 1 to 4 threads (KWL OSK hand pool
# threads=N, its names hand-1.. on the KWL THREADS line after it), the templates were read once (one KWL OSK hand
# templates line, count=228 error=0), the three strokes and the four were recognized with 1 to 4 candidates and no
# note (KWL OSK hand recognize strokes=3 and strokes=4), the clear left no recognition of the fifth stroke, and no KWL
# FAILED or VULKAN_ERROR line came.  The pool's size, stale answers dropped (KWL OSK hand stale) and the worst
# pass_max_us of KWL PERF pass are printed, not judged.
#   usage: plan/ws309/tests/hand-guest.sh OUT IMAGE WAYLAND
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: hand-guest.sh OUT IMAGE WAYLAND}
image=${2:?}
wl=${3:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
G_TIMEOUT=300 g wait > "$out/wait.log" 2>&1 || { echo "hand-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	G_TIMEOUT=200 g run 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p011dev; mkdir -p $HOME; rm -f /tmp/wayland-0; /bin/wayland --testing --glass --timeout=300 --width=1280 --height=800 > /tmp/hand.log 2>&1 </dev/null & sleep 5; aat-input start --width 1280 --height 800 > /tmp/aat.log 2>&1; a() { aat-input "$@" >> /tmp/aat.log 2>&1; }; a drag 6 792 150 650 8; sleep 1; a move-to 1186 486; sleep 0.3; a click; sleep 1; a drag 200 580 400 700 12; sleep 0.2; a drag 420 560 402 740 12; sleep 0.2; a drag 500 600 650 640 12; sleep 1.5; a drag 700 560 700 740 12; sleep 1.5; a drag 300 600 360 620 6; sleep 0.2; a move-to 1047 665; sleep 0.2; a click; sleep 1.5; echo done' > "$out/guest-start.out" 2>&1
	G_TIMEOUT=60 g run 'grep -E "KWL (READY|THREADS|FAILED|VULKAN_ERROR|PERF pass|OSK (open|hand))" /tmp/hand.log; echo "== cpus $(sysctl -n hw.ncpu 2>/dev/null)"' > "$out/guest.out" 2>&1
	g run "aat-input stop >/dev/null 2>&1; $stop_all" > /dev/null 2>&1
fi
python3 -I - "$GUEST_RUNTIME/qmp.sock" <<'PY' > "$out/quit.log" 2>&1
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.settimeout(10)
s.connect(sys.argv[1])
s.recv(4096)
s.sendall(b'{"execute":"qmp_capabilities"}\n')
s.recv(4096)
s.sendall(b'{"execute":"quit"}\n')
print(s.recv(4096))
PY
python3 -I - "$out/guest.out" "$failed" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read().splitlines()
ok = sys.argv[2] == "0"
if not ok:
	print("hand-guest: the guest or the compositor did not start")
def fail(why):
	global ok
	ok = False
	print("hand-guest: " + why)
if not any(line.startswith("KWL READY") for line in text):
	fail("the compositor was not ready")
if any(re.match(r"KWL (FAILED|VULKAN_ERROR)", line) for line in text):
	fail("a KWL FAILED or VULKAN_ERROR line")
pool = [line for line in text if line.startswith("KWL OSK hand pool")]
match = re.match(r"KWL OSK hand pool threads=(\d+)$", pool[0]) if pool else None
if not match or not 1 <= int(match.group(1)) <= 4:
	fail("the pool did not start with 1 to 4 threads (%s)" % (pool[0] if pool else "no line"))
else:
	count = int(match.group(1))
	after = text[text.index(pool[0]) + 1:]
	names = [line for line in after if line.startswith("KWL THREADS")]
	if not names or not all(("hand-%d" % n) in names[0] for n in range(1, count + 1)):
		fail("the pool's threads are not in the register")
	print("hand-guest: pool threads=%d (%s)" % (count, [line for line in text if line.startswith("== cpus")]))
reads = [line for line in text if line.startswith("KWL OSK hand templates")]
if len(reads) != 1 or "count=228 error=0" not in reads[0]:
	fail("the templates were not read once (%d lines)" % len(reads))
recognized = [line for line in text if line.startswith("KWL OSK hand recognize")]
for strokes in (3, 4):
	if not any(re.match(r"KWL OSK hand recognize strokes=%d .* candidates=[1-4] first=\S+ note=$" % strokes, line) for line in recognized):
		fail("%d strokes were not recognized with candidates" % strokes)
if any(re.match(r"KWL OSK hand recognize strokes=5 ", line) for line in recognized):
	fail("the cleared fifth stroke was recognized")
if not any(line.startswith("KWL OSK hand clear") for line in text):
	fail("the clear key was not taken")
for line in recognized:
	print("hand-guest: " + line)
print("hand-guest: stale answers dropped %d" % sum(1 for line in text if line.startswith("KWL OSK hand stale")))
passes = [line for line in text if line.startswith("KWL PERF pass")]
if passes:
	print("hand-guest: pass_max_us(max)=%d" % max(int(re.search(r"pass_max_us=(\d+)", line).group(1)) for line in passes))
print("hand-guest: " + ("PASS" if ok else "FAIL"))
PY
