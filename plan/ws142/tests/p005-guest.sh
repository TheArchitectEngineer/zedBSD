#!/bin/sh
# ws142-p005: the application switcher on the pen test guest (plan/ws079/tests/config-amd64-pen.mk;
# plan/ws079/tests/pen-guest.sh start IMAGE).  The compositor under test (BUILD/bin/wayland) is copied in, 1280x800.
# Three applications by wltest --app-id: apps.a, apps.b (two windows), apps.c, opened in that order (apps.c on top:
# the latest use is c, b, a).  Keys through QMP, the touch pad through touchinject's "pad" (the injector's pad).
#  1. Alt held, Tab: "ZWL SWITCH open via=keys index=1 app=apps.b placement=bar count=3" and apps.b's previews under
#     its icon ("ZWL APPS preview app=apps.b windows=2 via=switch"; switch-bar.png); Tab: apps.a; Shift+Tab: apps.b;
#     Alt let go: "ZWL SWITCH commit app=apps.b ... via=alt" and its window raised ("ZWL APPS raise ... via=switch").
#  2. Alt+Tab then Esc: "ZWL SWITCH cancel via=escape", nothing brought.
#  3. A quick Alt+Tab goes back to the application used before: apps.c ("commit app=apps.c").
#  4. The pad: a tap of three fingers opens it ("open via=pad"), two fingers 15 mm to the right step on ("step ...
#     via=pad"), a tap brings the selection ("commit ... via=pad").
#  5. A docked window (double click on the top window's title): Alt+Tab shows in the middle ("placement=center",
#     "ZWL SWITCH center"; switch-center.png); Alt let go brings it.
#  6. A fullscreen window: Alt+Tab opens nothing (D6).
#  7. The compositor stays up, with no ERROR in its log.
#   plan/ws142/tests/p005-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: p005-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws142-p005}
mkdir -p "$out"
qmp="$GUEST_RUNTIME/qmp.sock"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
# A picture for the eye, read from the Venus head through QEMU's VNC (QMP screendump shows the text console instead).
shot() { timeout 60 python3 plan/ws035/tests/zdesktop-check.py "$out/$1.png" --runtime "$GUEST_RUNTIME" >> "$out/qmp.txt" 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$qmp" "$@" >/dev/null; }
key() { send input-send-event "{\"events\":[{\"type\":\"key\",\"data\":{\"down\":$2,\"key\":{\"type\":\"qcode\",\"data\":\"$1\"}}}]}"; }
tap() { key "$1" true; sleep 0.12; key "$1" false; sleep 0.6; }
count() { guest "grep -c -- '$1' /tmp/zdesktop.log" | tail -1; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect_count() { n=$(count "$2"); if [ "${n:-0}" -eq "$3" ] 2>/dev/null; then pass "$1"; else fail "$1 ($2: ${n:-?}, expected $3)"; fi; }
open_app() { guest "$env /bin/wltest --windowed --size=$3 --color=$2 --app-id=$1 --frames=3600 --delay-ms=250 > /tmp/$1-$$.log 2>&1 </dev/null & sleep 3; echo started" >/dev/null; }
pad() { name=$1; shift; script="pad 1336 760 5 scan\nwait 2600"; for line in "$@"; do script="$script\n$line"; done
	guest "printf '$script\nhold 800\n' | /bin/touchinject; echo replay=\$?" > "$out/$name.txt"
	grep -q '^replay=0$' "$out/$name.txt" || fail "$name replay"; sleep 0.5; }
: > "$out/qmp.txt"

# The compositor under test, and the applications.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 2; echo started' >/dev/null
open_app apps.a f4d0d0 380x260
open_app apps.b d0f4d0 400x280
open_app apps.b c0e4c0 360x240
open_app apps.c d0d0f4 420x300
pointer move 640 760 sleep 300

# 1. Alt+Tab, Tab, Shift+Tab, Alt let go.
key alt true
tap tab
expect_count keys-open 'ZWL SWITCH open via=keys index=1 app=apps.b placement=bar count=3' 1
expect_count keys-bar-preview 'ZWL APPS preview app=apps.b windows=2 via=switch' 1
shot switch-bar
tap tab
expect_count keys-tab 'ZWL SWITCH step index=2 app=apps.a via=tab' 1
key shift true
tap tab
key shift false
expect_count keys-shift-tab 'ZWL SWITCH step index=1 app=apps.b via=tab' 1
key alt false
sleep 0.8
expect_count keys-commit 'ZWL SWITCH commit app=apps.b surface=[0-9]* via=alt' 1
expect_count keys-raise 'ZWL APPS raise surface=[0-9]* via=switch' 1

# 2. Alt+Tab, Esc.
key alt true
tap tab
tap esc
key alt false
sleep 0.8
expect_count escape-cancels 'ZWL SWITCH cancel via=escape' 1
expect_count escape-brings-nothing 'ZWL APPS raise surface=[0-9]* via=switch' 1

# 3. A quick Alt+Tab: back to apps.c.
key alt true
tap tab
key alt false
sleep 0.8
expect_count quick-back 'ZWL SWITCH commit app=apps.c surface=[0-9]* via=alt' 1

# 4. The pad: a tap of three fingers, two fingers 15 mm right, a tap.
pad switch-pad "down 0 400 500; down 1 550 480; down 2 700 500" "wait 40" "up 0; up 1; up 2" "wait 500" \
	"down 0 500 400; down 1 700 400" "wait 30" "swipe 180 0 12 16" "up 0; up 1" "wait 500" \
	"down 0 600 400" "wait 40" "up 0" "wait 600"
expect_count pad-open 'ZWL SWITCH open via=pad' 1
n=$(count 'ZWL SWITCH step index=[0-9]* app=[^ ]* via=pad'); [ "${n:-0}" -ge 1 ] 2>/dev/null && pass pad-step || fail "pad-step (${n:-?})"
expect_count pad-commit 'ZWL SWITCH commit app=[^ ]* surface=[0-9]* via=pad' 1

# 5. A docked window: the switcher in the middle.
# The window the pad brought is on top; surface numbers are each client's own, so it is found by its client (T1-134).
top=$(guest "grep 'ZWL SWITCH commit app=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) .*/\1/p')
topc=$(guest "grep 'ZWL SWITCH commit app=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\).*/\1/p')
set -- $(guest "grep 'ZWL MAP client=${topc:-0} ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
tx=${1:-300}; ty=${2:-300}
pointer move $((tx + 150)) $((ty - 30)) sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 1500
n=$(count "ZWL GLASS dock surface=${top:-0} "); [ "${n:-0}" -ge 1 ] 2>/dev/null && pass docked || fail docked
pointer move 640 760 sleep 300
key alt true
tap tab
expect_count center-open 'ZWL SWITCH open via=keys index=1 app=[^ ]* placement=center' 1
n=$(count 'ZWL SWITCH center app='); [ "${n:-0}" -ge 1 ] 2>/dev/null && pass center-shown || fail center-shown
shot switch-center
key alt false
sleep 0.8
n=$(count 'ZWL SWITCH commit app=[^ ]* surface=[0-9]* via=alt'); [ "${n:-0}" -ge 3 ] 2>/dev/null && pass center-commit || fail "center-commit (${n:-?})"

# 6. A fullscreen window: no switcher.
guest "$env /bin/wltest --color=203040 --frames=3600 --delay-ms=250 > /tmp/f.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
before=$(count 'ZWL SWITCH open')
key alt true
tap tab
key alt false
sleep 0.8
expect_count fullscreen-no-switcher 'ZWL SWITCH open' "${before:-0}"

# 7. Up, without errors.
running=$(guest 'ps -A -o args | grep -cE "[w]ayland( |$)"' | tail -1)
[ "$running" = "1" ] && pass alive || fail alive
guest 'grep -E "ZWL SWITCH|ZWL APPS|ZWL GESTURE|ZWL MAP|ZWL GLASS dock|ZWL GLASS bar|ERROR" /tmp/zdesktop.log' > "$out/log.txt"
grep -q ERROR "$out/log.txt" && fail no-error || pass no-error
guest "$stop_all" >/dev/null

echo "p005-guest: status $status (outputs in $out; switch-bar.png and switch-center.png for the eye)"
exit $status
