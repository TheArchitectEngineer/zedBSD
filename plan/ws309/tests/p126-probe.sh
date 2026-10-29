#!/bin/sh
# ws309-p017: looks into WS099 C1's Log Out and Login (plan/ws035/tests/zdesktop-p126.sh) on one image: the guest is
# started from IMAGE in OUT/runtime, p126 runs CYCLES cycles, then the guest's logs are kept -- sessiond's, the
# greeter compositor's (/var/log/greeter.log), the session's -- and the times compared: when the greeter got its
# keyboard (its "KWL INPUT device=... kind=keyboard" and READY lines, with the uptime the log's lines carry) against
# when the password was typed (OUT/p126.log's "typed" mark, the guest's uptime read just before).
# Prints p126's result and "p126-probe: keyboard_ms=... typed_ms=..." when both are found.
#   usage: plan/ws309/tests/p126-probe.sh OUT IMAGE [CYCLES]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: p126-probe.sh OUT IMAGE [CYCLES]}
image=${2:?}
cycles=${3:-1}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-120}" python3 -I plan/tools/guest/guest.py "$@"; }
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
G_TIMEOUT=300 g wait > "$out/wait.log" 2>&1 || echo "p126-probe: the guest did not answer"
timeout 1500 sh plan/ws035/tests/zdesktop-p126.sh "$out/p126" "$cycles" > "$out/p126.log" 2>&1
echo "p126 exit=$?" >> "$out/p126.log"
g run 'cat /var/log/sessiond.log' > "$out/sessiond.log" 2>&1
g run 'cat /var/log/greeter.log' > "$out/greeter.log" 2>&1
g run 'cat /run/user/1000/session.log' > "$out/session.log" 2>&1
g run 'cat /proc/uptime 2>/dev/null; date' > "$out/uptime.txt" 2>&1
g run 'ls -l /dev/input; ps -A -o pid,args | grep -E "[w]ayland|[s]essiond"' > "$out/ps.txt" 2>&1
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
grep -E 'log:|frames:|exit=' "$out/p126.log"
grep -E 'KWL (READY|INPUT device|STARTUP step=input|INPUT_CLOSED|KEY|TEXT)|AUTH|GREETER' "$out/greeter.log" | head -40
