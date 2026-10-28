#!/bin/sh
# ws309: on QEMU's Venus guest, displays.conf is written by the compositor's io-file thread and a displays request's
# result comes once the file is written: the guest is started from IMAGE in OUT/runtime, WAYLAND is put in as
# /bin/wayland and PROBE as /bin/keiland-system, a compositor of its own (--testing, HOME=/tmp/p005home) runs while
# the probe asks for the extended and mirror modes and turns the display to 90 and back to 0, and the compositor's
# own output's KWL THREADS and KWL DISPLAYS lines, the probe's results and the file are collected.  The guest is ended
# by QEMU's monitor.  Prints "io-file-guest: PASS" when io-file is in the register, every held result's ticket was
# written by io-file before its answer, the probe got every result with error=0, and displays.conf holds the last mode.
#   usage: plan/ws309/tests/io-file-guest.sh OUT IMAGE WAYLAND PROBE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: io-file-guest.sh OUT IMAGE WAYLAND PROBE}
image=${2:?}
wl=${3:?}
probe=${4:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-300}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
g wait > "$out/wait.log" 2>&1 || { echo "io-file-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	g put "$probe" /bin/keiland-system > "$out/put-probe.log" 2>&1 || failed=1
	G_TIMEOUT=180 g run 'chmod 755 /bin/wayland /bin/keiland-system; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p005home; mkdir -p $HOME; rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=60000 --width=1280 --height=800 > /tmp/io-file.log 2>&1 </dev/null & sleep 5; keiland-system --timeout-ms=20000 displays display-mode extended display-mode mirror display-mode extended > /tmp/probe.out 2>&1; key=$(sed -n "s/.*display key=\(.*\) label=.*/\1/p" /tmp/probe.out | head -1); keiland-system --timeout-ms=20000 display-rotate "$key" 90 display-rotate "$key" 0 >> /tmp/probe.out 2>&1; sleep 2; cat /tmp/probe.out; echo "== file"; cat $HOME/.config/keiland/displays.conf; echo "== compositor"; grep -E "KWL (READY|THREADS|DISPLAYS|OUTPUT rotate key|IO)" /tmp/io-file.log' > "$out/guest.out" 2>&1
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
	print("io-file-guest: " + why)
if not any(re.search(r"KWL THREADS .*io-file", line) for line in text):
	fail("no io-file in KWL THREADS")
results = [line for line in text if line.startswith("KEILAND-SYSTEM result")]
if len(results) < 5:
	fail("%d probe results, 5 wanted" % len(results))
if any("error=0" not in line for line in results):
	fail("a probe result is not error=0")
written = 0
held = 0
for line in text:
	m = re.search(r"KWL DISPLAYS saved by=io-file ticket=(\d+) errno=(-?\d+)", line)
	if m:
		if m.group(2) != "0":
			fail("a writing failed: " + line)
		written = max(written, int(m.group(1)))
	m = re.search(r"KWL DISPLAYS result held request=(\d+) ticket=(\d+)", line)
	if m:
		held += 1
		pending = (int(m.group(1)), int(m.group(2)))
		if pending[1] <= written:
			fail("a result held for a ticket written already: " + line)
if held == 0:
	fail("no result was held for its writing")
if written == 0:
	fail("io-file wrote nothing")
if "== file" not in "\n".join(text) or not any(re.match(r"mode\s*=?\s*extended|mode extended", line) for line in text):
	fail("displays.conf does not hold the extended mode")
print("io-file-guest: " + ("PASS" if ok else "FAIL"))
PY
