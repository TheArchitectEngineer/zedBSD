#!/bin/sh
# ws309: on QEMU's Venus guest, a FIFO client (mview, 900 frames) must not lose its surface when keiland-shot
# captures the screen meanwhile (the compositor kept sending its frame callbacks).  The guest is started from IMAGE
# in OUT/runtime (nothing of an earlier guest is touched), each WAYLAND binary given is put in as /bin/wayland in
# turn, a compositor of its own is started (--testing), mview runs and keiland-shot captures at 6 s, then the
# compositor's pid and mview's exit status and last lines are collected.  The guest is ended by QEMU's monitor.
# Prints one line a binary and "shot-fifo: PASS" or "shot-fifo: FAIL".  With SHOT_STALL=S, keiland-shot is stopped
# (SIGSTOP) 0.3 s after it starts and continued S seconds later, as a reader slower than the display: the
# compositor must go on serving its clients meanwhile (the capture itself may then fail; only mview is judged).
#   usage: [SHOT_STALL=S] plan/ws309/tests/shot-fifo.sh OUT IMAGE WAYLAND...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: shot-fifo.sh OUT IMAGE WAYLAND...}
image=${2:?usage: shot-fifo.sh OUT IMAGE WAYLAND...}
shift 2
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-300}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
stall=${SHOT_STALL:-0}
if [ "$stall" = 0 ]; then
	shoot='timeout 60 keiland-shot /tmp/shot-fifo.png > /tmp/shot-fifo.out 2>&1 </dev/null; echo shot=$?'
else
	shoot="keiland-shot /tmp/shot-fifo.png > /tmp/shot-fifo.out 2>&1 </dev/null & k=\$!; sleep 0.3; kill -STOP \$k; sleep $stall; kill -CONT \$k; wait \$k; echo shot=\$? stalled=$stall"
fi

timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
g wait > "$out/wait.log" 2>&1 || { echo "shot-fifo: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[m]view" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[m]view" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
n=0
for wl in "$@"; do
	[ $failed = 0 ] || break
	n=$((n+1))
	name=w$n
	echo "== $name $wl $(sha256sum "$wl" | cut -c1-16) $(date +%H:%M:%S)"
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/$name-put.log" 2>&1 || { echo "shot-fifo: $name put failed"; failed=1; break; }
	g run 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=600 --width=1280 --height=800 > /tmp/shot-fifo.log 2>&1 </dev/null & for w in $(seq 1 30); do grep -q KWL.READY /tmp/shot-fifo.log 2>/dev/null && break; sleep 0.5; done; sleep 1' > /dev/null 2>&1
	G_TIMEOUT=200 g run 'export XDG_RUNTIME_DIR=/tmp; before=$(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); (timeout 120 /bin/mview --token=shot --frames=900 --content=none --present=fifo --copies=1 > /tmp/mview-shot.log 2>&1 </dev/null; echo status=$? > /tmp/mview-shot.status) & sleep 6; '"$shoot"'; wait; after=$(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); echo before=$before after=$after; cat /tmp/mview-shot.status; grep -v "^MVIEW HITCH " /tmp/mview-shot.log | tail -3; grep -E "KWL SHOT" /tmp/shot-fifo.log | tail -3' > "$out/$name.out" 2>&1
	g get /tmp/shot-fifo.png "$out/$name.png" > /dev/null 2>&1
	shot_ok=1
	[ "$stall" = 0 ] && { grep -q '^shot=0' "$out/$name.out" || shot_ok=0; }
	if grep -q '^status=0' "$out/$name.out" && [ $shot_ok = 1 ]; then
		echo "shot-fifo: $name ok"
	else
		echo "shot-fifo: $name FAIL ($(grep -E '^(status|shot)=' "$out/$name.out" | tr '\n' ' '))"
		failed=1
	fi
done
g run "$stop_all" > /dev/null 2>&1
# The guest ends by its monitor.
python3 -I - "$GUEST_RUNTIME/qmp.sock" <<'EOF' > "$out/quit.log" 2>&1
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.settimeout(10)
s.connect(sys.argv[1])
s.recv(4096)
s.sendall(b'{"execute":"qmp_capabilities"}\n')
s.recv(4096)
s.sendall(b'{"execute":"quit"}\n')
print(s.recv(4096))
EOF
if [ $failed = 0 ]; then echo "shot-fifo: PASS"; else echo "shot-fifo: FAIL"; fi
