#!/bin/sh
# BUG-107: many Wayland clients on the zdesktop compositor in the Venus guest.  Boots IMAGE with
# plan/ws035/tests/zdesktop-guest.sh (runtime build/ws073-venus, the renderer of the main tree), starts the
# compositor and CLIENTS wl_shm windows (wlshm) plus 4 EGL windows (wltest on Venus), counts the kernel's sockets
# (tests/resources.c at /tmp/resources), checks every client drew frames and failed nothing, and takes the screen.
#   sh plan/ws073/tests/socket-desktop.sh IMAGE [CLIENTS] [OUTDIR]
# The clients start 0.3 s apart (STAGGER=0 starts them at once): the compositor's listen backlog is clamped to 16 and
# a unix connect to a full backlog fails at once with EAGAIN (a separate limit, see ws073-p034).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
clients=${2:-40}
out=${3:-build/ws073-p034/desktop}
mkdir -p "$out"
export GUEST_RUNTIME=$PWD/build/ws073-venus
export VENUS_RENDERER=${VENUS_RENDERER:-$PWD/build/ws035-sq-venus/install}
guest() { timeout 300 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" </dev/null | tail -1
timeout 400 python3 plan/tools/guest/guest.py wait </dev/null | tail -1
timeout 120 python3 plan/tools/guest/guest.py put build/ws073-p030/resources-guest /tmp/resources </dev/null
guest 'chmod 755 /tmp/resources; /tmp/resources' | tee "$out/resources-before.txt"
guest "ps -A -o pid,comm | awk '\$2 == \"/bin/wayland\" || \$2 == \"/bin/wlshm\" || \$2 == \"/bin/wltest\" {print \$1}' | while read p; do kill \$p; done; sleep 1
export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/wayland --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null &
w=0; while ! grep -q 'ZWL READY' /tmp/zdesktop.log && [ \$w -lt 60 ]; do sleep 1; w=\$((w+1)); done
i=1; while [ \$i -le $clients ]; do /bin/wlshm --size=120x90 --color=ff\$(printf %02x%02x%02x \$((i*5%256)) \$((i*37%256)) \$((i*91%256))) --frames=100000 --delay-ms=200 --token=s\$i > /tmp/s\$i.log 2>&1 </dev/null & sleep \${STAGGER:-0.3}; i=\$((i+1)); done
i=1; while [ \$i -le 4 ]; do /bin/wltest --windowed --size=200x150 --color=dfe9f7 --frames=3600 --delay-ms=200 --token=e\$i > /tmp/e\$i.log 2>&1 </dev/null & sleep \${STAGGER:-1}; i=\$((i+1)); done
sleep 20; echo started" | tail -1
guest '/tmp/resources' | tee "$out/resources-during.txt"
python3 plan/ws035/tests/zdesktop-shot.py "$out/desktop.png" --runtime "$GUEST_RUNTIME" 2>&1 | tail -1
guest "ok=0; bad=0; i=1; while [ \$i -le $clients ]; do if grep -q 'WLSHM FRAME' /tmp/s\$i.log && ! grep -q FAILED /tmp/s\$i.log; then ok=\$((ok+1)); else bad=\$((bad+1)); echo \"s\$i: \$(tail -2 /tmp/s\$i.log | tr '\n' ' ')\"; fi; i=\$((i+1)); done
e=0; i=1; while [ \$i -le 4 ]; do if grep -q 'WLTEST FRAME' /tmp/e\$i.log; then e=\$((e+1)); else echo \"e\$i: \$(tail -2 /tmp/e\$i.log | tr '\n' ' ')\"; fi; i=\$((i+1)); done
echo \"wlshm drawing: \$ok of $clients, failed \$bad; wltest drawing: \$e of 4\"
grep -ciE 'too many|ENFILE|error' /tmp/zdesktop.log; dmesg | grep -c 'killed by signal'" | tee "$out/clients.txt"
grep -q "wlshm drawing: $clients of $clients, failed 0; wltest drawing: 4 of 4" "$out/clients.txt" && echo "SOCKET-DESKTOP:PASS" || echo "SOCKET-DESKTOP:FAIL"
