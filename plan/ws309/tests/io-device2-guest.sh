#!/bin/sh
# ws309: on QEMU's Venus guest, the system extension's readings on the io-device thread: the power's state, the
# network's details and the machine's monitor (its samples while a monitor object exists, the monitor closed after
# the last and opened again for the next).  The guest is started from IMAGE in OUT/runtime; WAYLAND is put in as
# /bin/wayland, LIBKEILAND as /lib/libkeiland.so (by a rename) and PROBE as /bin/keiland-system; a compositor of its own
# (--testing --glass, HOME=/tmp/p004dev) runs while the probe dumps the state, asks for the details and watches the
# monitor for 3 s, twice, and the compositor's own KWL lines are collected.  The guest is ended by QEMU's monitor.
# Prints "io-device2-guest: PASS" when io-device is in KWL THREADS, the dump has the power, the details' result is
# error=0 with an interface, each monitor run had its info and at least 5 frames, and the compositor counted the
# subscribers 1 and 0 twice.
#   usage: plan/ws309/tests/io-device2-guest.sh OUT IMAGE WAYLAND LIBKEILAND PROBE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: io-device2-guest.sh OUT IMAGE WAYLAND LIBKEILAND PROBE}
image=${2:?}
wl=${3:?}
lib=${4:?}
probe=${5:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-300}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
g wait > "$out/wait.log" 2>&1 || { echo "io-device2-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	g put "$lib" /tmp/libkeiland.so > "$out/put-lib.log" 2>&1 || failed=1
	g put "$probe" /bin/keiland-system > "$out/put-probe.log" 2>&1 || failed=1
	G_TIMEOUT=180 g run 'chmod 755 /bin/wayland /bin/keiland-system; cp /tmp/libkeiland.so /lib/libkeiland.so.new && mv /lib/libkeiland.so.new /lib/libkeiland.so; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p004dev; mkdir -p $HOME; rm -f /tmp/wayland-0; /bin/wayland --testing --glass --timeout=60000 --width=1280 --height=800 > /tmp/io-dev.log 2>&1 </dev/null & sleep 6; echo "== run 1"; keiland-system --timeout-ms=3000 dump details monitor 2>&1; sleep 1; echo "== run 2"; keiland-system --timeout-ms=3000 monitor 2>&1; sleep 1; echo "== compositor"; grep -E "KWL (READY|THREADS|IO|POWER|SYSTEM (monitor|power|network details))" /tmp/io-dev.log' > "$out/guest.out" 2>&1
	g run "$stop_all" > /dev/null 2>&1
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
cat "$out/guest.out"
python3 -I - "$out/guest.out" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read().splitlines()
ok = True
def fail(why):
	global ok
	ok = False
	print("io-device2-guest: " + why)
if not any(re.search(r"KWL THREADS .*io-device", line) for line in text):
	fail("no io-device in KWL THREADS")
if not any(line.startswith("KEILAND-SYSTEM power") for line in text):
	fail("the dump has no power")
if not any(re.match(r"KEILAND-SYSTEM (link|details?) ", line) for line in text):
	fail("the details have no interface")
results = [line for line in text if line.startswith("KEILAND-SYSTEM result")]
if not results or any("error=0" not in line for line in results):
	fail("a result is not error=0: %r" % results)
runs = []
current = None
for line in text:
	if line.startswith("== run"):
		current = {"info": 0, "frames": 0}
		runs.append(current)
	elif line.startswith("== compositor"):
		current = None
	elif current is not None:
		if line.startswith("KEILAND-SYSTEM monitor-info"):
			current["info"] += 1
		if line.startswith("KEILAND-SYSTEM monitor-frame"):
			current["frames"] += 1
if len(runs) != 2:
	fail("%d runs" % len(runs))
for number, run in enumerate(runs):
	if run["info"] < 1 or run["frames"] < 5:
		fail("run %d had %d infos and %d frames" % (number + 1, run["info"], run["frames"]))
counts = [m.group(1) for m in (re.search(r"KWL SYSTEM monitor subscribers=(\d+)", line) for line in text) if m]
if counts[:4] != ["1", "0", "1", "0"]:
	fail("the subscribers were counted %r" % counts)
print("io-device2-guest: " + ("PASS" if ok else "FAIL"))
PY
