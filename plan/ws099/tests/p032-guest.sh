#!/bin/sh
# ws099-p032: Alt+click on the system bar's network icon opens the network's details.  On the desktop test guest
# (plan/ws079/tests/config-amd64-pen.mk, started with plan/ws079/tests/pen-guest.sh start IMAGE: QEMU's user network
# on the USB adapter, a wired connection); the compositor under test (BUILD/bin/wayland) is copied in, 1280x800.
#  1. Alt held (QMP key alt down), a left click on the icon (ZWL NETWORK icon ...), Alt released: "ZWL NETWORK info
#     open" and no menu ("ZWL NETWORK open" absent); the rows: title Ethernet, Interface, Status Connected, an IPv4
#     address that `ifconfig -a` in the guest shows too, Received and Sent (details.png for the eye).
#  2. Two seconds later the rows were read again, with rates ("Received" with "/s)").
#  3. Esc closes them ("ZWL NETWORK info close via=key").
#  4. A plain click on the icon still opens the menu ("ZWL NETWORK open"); Esc closes it.
#  5. The compositor is still up, with no ERROR in its log.
#
#   plan/ws079/tests/pen-guest.sh start IMAGE
#   plan/ws099/tests/p032-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: p032-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws099-p032}
mkdir -p "$out"
qmp="$GUEST_RUNTIME/qmp.sock"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
# A picture for the eye, read from the Venus head through QEMU's VNC (QMP screendump shows the text console instead).
shot() { timeout 60 python3 plan/ws035/tests/zdesktop-check.py "$out/$1.png" --runtime "$GUEST_RUNTIME" >> "$out/qmp.txt" 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$qmp" "$@"; }
key() { send input-send-event "{\"events\":[{\"type\":\"key\",\"data\":{\"down\":$2,\"key\":{\"type\":\"qcode\",\"data\":\"$1\"}}}]}"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[w]lshm" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
: > "$out/qmp.txt"

# The compositor under test.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 3; echo started' >/dev/null

# The icon's place.
set -- $(guest "grep 'ZWL NETWORK icon ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
ix=${1:-0}; iy=${2:-0}; iw=${3:-0}; ih=${4:-0}
cx=$((ix + iw / 2)); cy=$((iy + ih / 2))
echo "icon: $ix,$iy ${iw}x$ih, click at $cx,$cy"
[ "$iw" -gt 0 ] || fail icon-found

# 1. Alt+click.
pointer move "$cx" "$cy" sleep 300 >/dev/null
key alt true
sleep 0.2
pointer down sleep 60 up sleep 300 >/dev/null
key alt false
sleep 1.5
guest 'cat /tmp/zdesktop.log' > "$out/open.log"
if grep -q 'ZWL NETWORK info open' "$out/open.log"; then pass info-open; else fail info-open; fi
if grep -q 'ZWL NETWORK open$' "$out/open.log"; then fail no-menu; else pass no-menu; fi
if grep -q 'ZWL NETWORK info row label= value=Ethernet' "$out/open.log"; then pass title-ethernet; else fail title-ethernet; fi
if grep -q 'ZWL NETWORK info row label=Status value=Connected' "$out/open.log"; then pass status; else fail status; fi
if grep -Eq 'ZWL NETWORK info row label=Interface value=[a-z]+[0-9]+' "$out/open.log"; then pass interface; else fail interface; fi
address=$(sed -n 's/.*ZWL NETWORK info row label=IPv4 address value=\([0-9.]*\).*/\1/p' "$out/open.log" | tail -1)
guest 'ifconfig -a' > "$out/ifconfig.txt"
if [ -n "$address" ] && grep -q "$address" "$out/ifconfig.txt"; then pass "address $address as ifconfig"; else fail "address ($address) as ifconfig"; fi
if grep -q 'ZWL NETWORK info row label=Received value=' "$out/open.log" && grep -q 'ZWL NETWORK info row label=Sent value=' "$out/open.log"; then pass bytes; else fail bytes; fi
pointer move 700 500 sleep 300 >/dev/null
shot details

# 2. Read again with rates.
sleep 2
guest 'cat /tmp/zdesktop.log' > "$out/again.log"
if grep -q 'ZWL NETWORK info row label=Received value=.*/s)' "$out/again.log"; then pass rates; else fail rates; fi

# 3. Esc closes them.
key esc true
key esc false
sleep 0.5
if guest "grep -c 'ZWL NETWORK info close via=key' /tmp/zdesktop.log" | tail -1 | grep -qx 1; then pass esc-closes; else fail esc-closes; fi

# 4. A plain click opens the menu.
pointer move "$cx" "$cy" sleep 300 down sleep 60 up sleep 800 >/dev/null
if guest "grep -c 'ZWL NETWORK open\$' /tmp/zdesktop.log" | tail -1 | grep -qx 1; then pass plain-click-menu; else fail plain-click-menu; fi
key esc true
key esc false
sleep 0.5

# 5. Still up, no ERROR.
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
if guest 'ps -A -o args' | grep -qE '[w]ayland( |$)'; then pass alive; else fail alive; fi
if grep -q ERROR "$out/zdesktop.log"; then fail no-error; else pass no-error; fi

echo "p032-guest: status $status (outputs in $out; details.png for the eye)"
exit $status
