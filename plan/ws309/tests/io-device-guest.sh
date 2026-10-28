#!/bin/sh
# ws309: on QEMU's Venus guest, the input devices are opened by the compositor's io-device thread: the guest is
# started from IMAGE in OUT/runtime, WAYLAND is put in as /bin/wayland, a compositor of its own (--testing) runs
# for 20 s while aat-input (the test-only input injection) is stopped and started again (a device that goes and
# comes), and its own output's KWL THREADS, KWL INPUT and KWL IO lines are collected.  The guest is ended by QEMU's
# monitor.  Prints "io-device-guest: PASS" when io-device is in the register, a device attached after READY, the
# injected devices closed and attached again, and no "KWL IO ... errno" line was written.
#   usage: plan/ws309/tests/io-device-guest.sh OUT IMAGE WAYLAND
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: io-device-guest.sh OUT IMAGE WAYLAND}
image=${2:?}
wl=${3:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-300}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
g wait > "$out/wait.log" 2>&1 || { echo "io-device-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	G_TIMEOUT=120 g run 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=25000 --width=1280 --height=800 > /tmp/io-device.log 2>&1 </dev/null & sleep 4; aat-input start; sleep 4; aat-input stop; sleep 3; aat-input start; sleep 5; grep -E "KWL (READY|THREADS|INPUT|IO|EVENT input)" /tmp/io-device.log' > "$out/guest.out" 2>&1
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
ok=1
grep -q 'KWL THREADS .*io-device' "$out/guest.out" || { echo "io-device-guest: no io-device in KWL THREADS"; ok=0; }
grep -q 'KWL INPUT' "$out/guest.out" || { echo "io-device-guest: no device attached"; ok=0; }
grep -q 'INPUT_CLOSED' "$out/guest.out" || { echo "io-device-guest: no device closed after aat-input stop"; ok=0; }
grep -q 'KWL IO .*errno' "$out/guest.out" && { echo "io-device-guest: an io-device failure"; ok=0; }
if [ $failed = 0 ] && [ $ok = 1 ]; then echo "io-device-guest: PASS"; else echo "io-device-guest: FAIL"; fi
