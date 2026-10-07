#!/bin/sh
# ws142-p004: the applications' icons in the system bar and their previews, on the pen test guest
# (plan/ws079/tests/config-amd64-pen.mk; plan/ws079/tests/pen-guest.sh start IMAGE).  The compositor under test
# (BUILD/bin/wayland) is copied in, 1280x800.  Three applications by wltest --app-id: apps.a (one window), apps.b
# (two windows) and apps.c (one window), opened in that order.  The mouse is QMP's tablet.
#  1. The bar: "KWL APPS bar count=3 hidden=0 desktop=2 apps=apps.a,apps.b,apps.c" (the opening order), an icon each.
#  2. The pointer resting on apps.b's icon: after 400 ms "KWL APPS preview app=apps.b windows=2 via=hover"
#     (preview-hover.png); away from it: "KWL APPS preview close via=leave".
#  3. A click on apps.a's icon (one window): "KWL APPS raise surface=<its window> via=bar".
#  4. A click on apps.b's icon: its previews at once ("via=click"); a click on the first preview brings that window
#     ("KWL APPS raise surface=<it> via=preview").
#  5. Two clicks on apps.b's icon: shown, then hidden ("preview close via=click"); shown again, Esc hides
#     ("preview close via=escape").
#  6. apps.c's icon dragged onto apps.a's place: "KWL APPS reorder app=apps.c place=0 desktop=2", the bar
#     "apps=apps.c,apps.a,apps.b".
#  7. The right desktop (Ctrl+Alt+Right; the session starts on the middle one, ws181-p006) with a window of apps.d: "desktop=3 apps=apps.d"; back on the middle one (Ctrl+Alt+Left)
#     the dragged order is still there ("desktop=2 apps=apps.c,apps.a,apps.b" again).
#  8. A docked window (double click on the top window's title): the docked title has the bar, no icon shows its
#     previews (docked.png).
#  9. The compositor stays up, with no ERROR in its log.
#   plan/ws142/tests/p004-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: p004-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws142-p004}
mkdir -p "$out"
qmp="$GUEST_RUNTIME/qmp.sock"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
# A picture for the eye, read from the Venus head through QEMU's VNC (QMP screendump shows the text console instead).
shot() { timeout 60 python3 plan/ws035/tests/zdesktop-check.py "$out/$1.png" --runtime "$GUEST_RUNTIME" >> "$out/qmp.txt" 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$qmp" "$@" >/dev/null; }
key() { send input-send-event "{\"events\":[{\"type\":\"key\",\"data\":{\"down\":$2,\"key\":{\"type\":\"qcode\",\"data\":\"$1\"}}}]}"; }
tap() { key "$1" true; sleep 0.15; key "$1" false; sleep 1; }
combo() { key ctrl true; key alt true; key "$1" true; sleep 0.15; key "$1" false; key alt false; key ctrl false; sleep 1; }
count() { guest "grep -c -- '$1' /tmp/zdesktop.log" | tail -1; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect_count() { n=$(count "$2"); if [ "${n:-0}" -eq "$3" ] 2>/dev/null; then pass "$1"; else fail "$1 ($2: ${n:-?}, expected $3)"; fi; }
expect_some() { n=$(count "$2"); if [ "${n:-0}" -ge 1 ] 2>/dev/null; then pass "$1"; else fail "$1 ($2 missing)"; fi; }
# The middle of an application's icon, from its last "KWL APPS icon" line.
icon_x() { guest "grep 'KWL APPS icon app=$1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) .*/\1/p' | tail -1; }
# A window of an application: wltest with that ID and colour, then its surface from the newest map.
open_app() { guest "$env /bin/wltest --windowed --size=$3 --color=$2 --app-id=$1 --frames=3600 --delay-ms=250 > /tmp/$1-$$.log 2>&1 </dev/null & sleep 3; echo started" >/dev/null; }
# The newest window's client (surface numbers are each client's own, so a window is found by its client).
last_client() { guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.*KWL MAP client=\([0-9]*\) .*/\1/p'; }
: > "$out/qmp.txt"

# The compositor under test, and the applications.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q KWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 2; echo started' >/dev/null
open_app apps.a f4d0d0 380x260
client_a=$(last_client)
open_app apps.b d0f4d0 400x280
open_app apps.b c0e4c0 360x240
open_app apps.c d0d0f4 420x300
pointer move 640 700 sleep 300

# 1. The bar in the opening order.
expect_some bar-opening-order 'KWL APPS bar count=3 hidden=0 desktop=2 apps=apps.a,apps.b,apps.c'
ax=$(icon_x apps.a); bx=$(icon_x apps.b); cx=$(icon_x apps.c)
echo "icons: apps.a=${ax:-?} apps.b=${bx:-?} apps.c=${cx:-?}"
[ -n "$ax" ] && [ -n "$bx" ] && [ -n "$cx" ] && [ "$ax" -lt "$bx" ] && [ "$bx" -lt "$cx" ] && pass icons-left-to-right || fail icons-left-to-right
ax=${ax:-100}; bx=${bx:-136}; cx=${cx:-172}

# 2. Resting on apps.b's icon, then away.
pointer move $((bx + 18)) 22 sleep 900
expect_count hover-preview 'KWL APPS preview app=apps.b windows=2 via=hover' 1
shot preview-hover
pointer move 640 700 sleep 800
expect_count hover-leaves 'KWL APPS preview close via=leave' 1

# 3. A click on apps.a's icon brings its window.
pointer move $((ax + 18)) 22 sleep 100 down sleep 60 up sleep 500
expect_count click-single-raises "KWL APPS raise surface=[0-9]* via=bar at_ms=[0-9]* client=${client_a:-0}\$" 1

# 4. A click on apps.b's icon: its previews at once; the first preview brings its window.
pointer move $((bx + 18)) 22 sleep 100 down sleep 60 up sleep 300
expect_count click-preview 'KWL APPS preview app=apps.b windows=2 via=click' 1
set -- $(guest "grep 'KWL APPS preview window surface=' /tmp/zdesktop.log | tail -2 | head -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\) client=\([0-9]*\).*/\1 \2 \3 \4 \5 \6/p')
ps=${1:-0}; px=${2:-0}; py=${3:-0}; pw=${4:-0}; ph=${5:-0}; pc=${6:-0}
pointer move $((px + pw / 2)) $((py + ph / 2)) sleep 200 down sleep 60 up sleep 500
expect_count preview-raises "KWL APPS raise surface=$ps via=preview at_ms=[0-9]* client=$pc\$" 1

# 5. Shown and hidden by clicks; shown, hidden by Esc.
pointer move $((bx + 18)) 22 sleep 100 down sleep 60 up sleep 400 down sleep 60 up sleep 400
expect_count second-click-hides 'KWL APPS preview close via=click' 1
pointer move $((bx + 18)) 22 sleep 100 down sleep 60 up sleep 400
tap esc
expect_count escape-hides 'KWL APPS preview close via=escape' 1
pointer move 640 700 sleep 600

# 6. apps.c's icon dragged onto apps.a's place.
pointer move $((cx + 18)) 22 sleep 100 down sleep 60 move $((cx + 6)) 22 sleep 60 move $((bx + 10)) 22 sleep 60 move $((ax + 10)) 22 sleep 60 move $((ax + 6)) 22 sleep 100 up sleep 500
expect_count drag-reorders 'KWL APPS reorder app=apps.c place=0 desktop=2' 1
expect_some drag-bar 'KWL APPS bar count=3 hidden=0 desktop=2 apps=apps.c,apps.a,apps.b'
pointer move 640 700 sleep 300

# 7. The right desktop with apps.d; back on the middle one, the order kept.
combo right
open_app apps.d f4f4c0 380x260
expect_some desktop2-bar 'KWL APPS bar count=1 hidden=0 desktop=3 apps=apps.d'
combo left
sleep 1
n=$(count 'KWL APPS bar count=3 hidden=0 desktop=2 apps=apps.c,apps.a,apps.b')
[ "${n:-0}" -ge 2 ] 2>/dev/null && pass desktop1-order-kept || fail "desktop1-order-kept (${n:-?})"

# 8. A docked window: the window brought by the preview (on top since; found by its client, T1-134) docked by a double click on its title.
set -- $(guest "grep 'KWL MAP client=$pc ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
tx=${1:-300}; ty=${2:-300}
pointer move $((tx + 150)) $((ty - 30)) sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 1500
expect_some docked "KWL GLASS dock surface=$ps "
before=$(count 'KWL APPS preview app=')
pointer move $((bx + 18)) 22 sleep 900
shot docked
expect_count docked-no-preview 'KWL APPS preview app=' "${before:-0}"

# 9. Up, without errors.
running=$(guest 'ps -A -o args | grep -cE "[w]ayland( |$)"' | tail -1)
[ "$running" = "1" ] && pass alive || fail alive
guest 'grep -E "KWL APPS|KWL MAP|KWL GLASS dock|KWL GLASS desktop|ERROR" /tmp/zdesktop.log' > "$out/log.txt"
grep -q ERROR "$out/log.txt" && fail no-error || pass no-error
guest "$stop_all" >/dev/null

echo "p004-guest: status $status (outputs in $out; preview-hover.png and docked.png for the eye)"
exit $status
