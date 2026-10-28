#!/bin/sh
# ws309: on QEMU's Venus guest, the bar's daemons' connections on threads of their own (audiod, networkd, bluetoothd,
# the phone, volumed).  The guest is started from IMAGE in OUT/runtime; WAYLAND is put in as /bin/wayland, LIBKEILAND
# as /lib/libkeiland.so (by a rename) and PROBE as /bin/keiland-system; a compositor of its own (--testing --glass,
# HOME=/tmp/p006dev) runs while the probe dumps the state, asks for a scan and a volume and dumps again; the compositor
# is then told to stop (SIGTERM) and its own KWL lines are collected.  The guest is ended by QEMU's monitor.
# Prints "daemon-guest: PASS" when the KWL THREADS line told after the first tick names audiod, networkd, bluetoothd,
# phone and volumed, the dumps have audiod and networkd reachable, the scan and the volume are answered error=0, the
# second dump has the volume asked, and the compositor ended within 10 s of SIGTERM.
#   usage: plan/ws309/tests/daemon-guest.sh OUT IMAGE WAYLAND LIBKEILAND PROBE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: daemon-guest.sh OUT IMAGE WAYLAND LIBKEILAND PROBE}
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
g wait > "$out/wait.log" 2>&1 || { echo "daemon-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	g put "$lib" /tmp/libkeiland.so > "$out/put-lib.log" 2>&1 || failed=1
	g put "$probe" /bin/keiland-system > "$out/put-probe.log" 2>&1 || failed=1
	G_TIMEOUT=180 g run 'chmod 755 /bin/wayland /bin/keiland-system; cp /tmp/libkeiland.so /lib/libkeiland.so.new && mv /lib/libkeiland.so.new /lib/libkeiland.so; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p006dev; mkdir -p $HOME; rm -f /tmp/wayland-0; /bin/wayland --testing --glass --timeout=90000 --width=1280 --height=800 > /tmp/daemon.log 2>&1 </dev/null & pid=$!; sleep 8; echo "== dump 1"; keiland-system --timeout-ms=4000 dump scan volume 40 40 0 2>&1; sleep 1; echo "== dump 2"; keiland-system --timeout-ms=2000 dump 2>&1; kill $pid; i=0; while kill -0 $pid 2>/dev/null && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done; if kill -0 $pid 2>/dev/null; then echo "== stopped alive=1"; else echo "== stopped alive=0 tenths=$((i*2))"; fi; echo "== compositor"; grep -E "KWL (READY|THREADS|VOLUME reachable|NETWORK|BT open|PHONE open|MEDIA|EXIT|FAILED)" /tmp/daemon.log' > "$out/guest.out" 2>&1
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
	print("daemon-guest: " + why)
threads = [line for line in text if line.startswith("KWL THREADS")]
if len(threads) < 2:
	fail("KWL THREADS told %d times" % len(threads))
else:
	for name in ("audiod", "networkd", "bluetoothd", "phone", "volumed"):
		if not re.search(r"names=.*\b%s\b" % name, threads[-1]):
			fail("no %s in %r" % (name, threads[-1]))
dumps = []
current = None
for line in text:
	if line.startswith("== dump"):
		current = []
		dumps.append(current)
	elif line.startswith("== "):
		current = None
	elif current is not None:
		current.append(line)
if len(dumps) != 2:
	fail("%d dumps" % len(dumps))
else:
	for number, dump in enumerate(dumps):
		if not any(re.match(r"KEILAND-SYSTEM audio reachable=1 ", line) for line in dump):
			fail("dump %d: audiod not reachable" % (number + 1))
		if not any(re.match(r"KEILAND-SYSTEM network reachable=1 ", line) for line in dump):
			fail("dump %d: networkd not reachable" % (number + 1))
	results = [line for line in dumps[0] if line.startswith("KEILAND-SYSTEM result")]
	if len(results) < 2 or any("error=0" not in line for line in results):
		fail("the scan's and the volume's results: %r" % results)
	if not any(re.match(r"KEILAND-SYSTEM audio reachable=1 .*left=40 right=40 muted=0", line) for line in dumps[1]):
		fail("the second dump has not the volume asked")
stopped = [line for line in text if line.startswith("== stopped")]
if not stopped or "alive=0" not in stopped[0]:
	fail("the compositor did not end after SIGTERM: %r" % stopped)
print("daemon-guest: " + ("PASS" if ok else "FAIL"))
PY
