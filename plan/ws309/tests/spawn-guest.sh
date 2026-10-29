#!/bin/sh
# ws309-p014: the compositor starts its programs on the file I/O thread with posix_spawn.  The guest is started from
# IMAGE; WAYLAND is put in as /bin/wayland, and spawn-closefrom (built by spawn-closefrom/run.sh into SPAWN) as
# /tmp/spawn-closefrom, and LIBC (this tree's build/amd64/dynamic/libc.so, which has posix_spawn_file_actions_addclosefrom_np)
# as /lib/libc.so (put beside it and renamed over it).  A compositor of its own runs with the desktop program "/tmp/spawn-closefrom --closed
# /tmp/desk-fds.txt; exec sleep 20", so the program the compositor starts writes whether it got any of the
# compositor's descriptors 3..20.
# Prints "spawn-guest: PASS" when the compositor logged "KWL DESKTOP start pid=N" (a number), the program wrote
# "closed-ok", "KWL THREADS" names io-file, and no KWL FAILED line came.
#   usage: plan/ws309/tests/spawn-guest.sh OUT IMAGE WAYLAND SPAWN LIBC
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: spawn-guest.sh OUT IMAGE WAYLAND SPAWN LIBC}
image=${2:?}
wl=${3:?}
spawn=${4:?}
libc=${5:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
G_TIMEOUT=300 g wait > "$out/wait.log" 2>&1 || { echo "spawn-guest: the guest did not answer"; failed=1; }
sleep 20
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$libc" /lib/libc.so.p014 > "$out/put.log" 2>&1 || failed=1
	g run 'mv /lib/libc.so.p014 /lib/libc.so' >> "$out/put.log" 2>&1 || failed=1
	g put "$wl" /bin/wayland >> "$out/put.log" 2>&1 || failed=1
	g put "$spawn" /tmp/spawn-closefrom >> "$out/put.log" 2>&1 || failed=1
	G_TIMEOUT=120 g run 'chmod 755 /bin/wayland /tmp/spawn-closefrom; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p014dev; mkdir -p $HOME; rm -f /tmp/wayland-0 /tmp/desk-fds.txt; /bin/wayland --testing --timeout=60 "--desktop-client=/tmp/spawn-closefrom --closed /tmp/desk-fds.txt; exec sleep 20" > /tmp/spawn.log 2>&1 </dev/null & sleep 12; cat /tmp/desk-fds.txt; echo done' > "$out/guest.out" 2>&1
	g run 'cat /tmp/spawn.log' > "$out/spawn.log" 2>&1
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
if [ -s "$GUEST_RUNTIME/dbus.pid" ]; then
	kill "$(cat "$GUEST_RUNTIME/dbus.pid")" 2>/dev/null
fi
python3 -I - "$out" "$failed" <<'PY'
import re, sys
out = sys.argv[1]
ok = sys.argv[2] == "0"
def read(name):
	try:
		return open(out + "/" + name, errors="replace").read()
	except OSError:
		return ""
log = read("spawn.log")
guest = read("guest.out")
for line in log.splitlines():
	if line.startswith("KWL DESKTOP") or line.startswith("KWL THREADS"):
		print("spawn-guest: " + line)
if not ok:
	print("spawn-guest: the guest or the compositor did not start")
if not re.search(r"KWL DESKTOP start pid=[0-9]+ ", log):
	print("spawn-guest: no KWL DESKTOP start pid=N line"); ok = False
if "closed-ok" not in guest:
	print("spawn-guest: the program got descriptors: " + guest.strip().replace("\n", " | ")); ok = False
if not re.search(r"KWL THREADS .*io-file", log):
	print("spawn-guest: no io-file thread"); ok = False
if "KWL FAILED" in log:
	print("spawn-guest: KWL FAILED"); ok = False
print("spawn-guest: " + ("PASS" if ok else "FAIL"))
PY
