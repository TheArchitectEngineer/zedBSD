#!/bin/sh
# ws142-p003: the touch pad's gestures on the pen test guest (plan/ws079/tests/config-amd64-pen.mk built from this
# branch: the injector's touch pad, 1336x760 units at 12 a millimetre, driven by touchinject's "pad" scripts).  The
# compositor under test (BUILD/bin/wayland) is copied in, 1280x800, with one wltest window.  Each script waits 2.6 s
# first: the compositor looks for new evdev nodes every 2 s.
#  1. Two fingers up 30 mm from the pad's bottom edge, quickly: "ZWL GESTURE kind=bottom2 phase=begin" and "...end",
#     Wiseview opens following them ("ZWL WISEVIEW gesture via=pad", "ZWL WISEVIEW opening"; wiseview-pad.png); Esc
#     closes it.
#  2. The same, 8 mm and slowly: Wiseview goes back closed ("ZWL WISEVIEW cancel").
#  3. Two fingers left 40 mm from the right edge: the desktop to the right ("ZWL GLASS desktop=2 via=pad").
#  4. Two fingers right 40 mm from the left edge: back to the first ("ZWL GLASS desktop=1 via=pad"); again on the
#     first, where there is no neighbour on the left: it stays ("desktop=1 via=pad" once more).
#  5. Three fingers up 25 mm in the middle: "kind=up3", Wiseview opens; Esc closes it.
#  6. A tap of three fingers: "kind=tap3 phase=end" (the switcher comes with ws142-p005).
#  7. Two fingers in the middle moving up: a scroll, no gesture.
#  8. A fullscreen window (the bar hidden): the bottom edge's gesture is logged but opens nothing (D6).
#  9. The compositor stays up, with no ERROR in its log.
#   plan/ws079/tests/pen-guest.sh start IMAGE
#   plan/ws142/tests/p003-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: p003-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws142-p003}
mkdir -p "$out"
qmp="$GUEST_RUNTIME/qmp.sock"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
# A picture for the eye, read from the Venus head through QEMU's VNC (QMP screendump shows the text console instead).
shot() { timeout 60 python3 plan/ws035/tests/zdesktop-check.py "$out/$1.png" --runtime "$GUEST_RUNTIME" >> "$out/qmp.txt" 2>&1; }
key() { send input-send-event "{\"events\":[{\"type\":\"key\",\"data\":{\"down\":$2,\"key\":{\"type\":\"qcode\",\"data\":\"$1\"}}}]}"; }
tap() { key "$1" true; sleep 0.15; key "$1" false; sleep 1; }
count() { guest "grep -c '$1' /tmp/zdesktop.log" | tail -1; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect_count() { n=$(count "$2"); if [ "${n:-0}" -eq "$3" ] 2>/dev/null; then pass "$1"; else fail "$1 ($2: ${n:-?}, expected $3)"; fi; }
# A pad script after the declaration and the 2.6 s wait; its lines are joined by newlines.
pad() { name=$1; shift; script="pad 1336 760 5 scan\nwait 2600"; for line in "$@"; do script="$script\n$line"; done
	guest "printf '$script\nhold 800\n' | /bin/touchinject; echo replay=\$?" > "$out/$name.txt"
	grep -q '^replay=0$' "$out/$name.txt" || fail "$name replay"; sleep 0.5; }
: > "$out/qmp.txt"

# The compositor under test, and one wltest window.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 2; echo started' >/dev/null
guest "$env /bin/wltest --windowed --size=420x300 --color=f4f7fc --frames=3600 --delay-ms=250 > /tmp/w.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null

# 1. Two fingers up 30 mm from the bottom edge in 190 ms.
pad bottom2 "down 0 500 750; down 1 700 745" "wait 30" "swipe 0 -360 12 16" "up 0; up 1"
expect_count bottom2-begin 'ZWL GESTURE kind=bottom2 phase=begin' 1
expect_count bottom2-end 'ZWL GESTURE kind=bottom2 phase=end' 1
expect_count bottom2-follows 'ZWL WISEVIEW gesture via=pad' 1
expect_count bottom2-opens 'ZWL WISEVIEW opening from=' 1
sleep 1
shot wiseview-pad
tap esc
expect_count bottom2-esc-closes 'ZWL WISEVIEW close key' 1
sleep 1

# 2. 8 mm in 720 ms: back closed.
pad bottom2-short "down 0 500 750; down 1 700 745" "wait 30" "swipe 0 -96 12 60" "up 0; up 1"
expect_count short-begin 'ZWL GESTURE kind=bottom2 phase=begin' 2
expect_count short-cancel 'ZWL WISEVIEW cancel from=' 1
# A wrong opening is closed, so that the steps after it are not judged on an open Wiseview (T1-126).
opened=$(count 'ZWL WISEVIEW opening from=')
[ "${opened:-0}" -gt 1 ] 2>/dev/null && tap esc
sleep 1

# 3. Two fingers left 40 mm from the right edge: the second desktop.
pad right2 "down 0 1320 300; down 1 1310 450" "wait 30" "swipe -480 0 12 16" "up 0; up 1"
expect_count right2-begin 'ZWL GESTURE kind=right2 phase=begin' 1
expect_count right2-desktop 'ZWL GLASS desktop=2 via=pad' 1
sleep 1

# 4. Two fingers right 40 mm from the left edge: the first desktop; again there: it stays.
pad left2 "down 0 20 300; down 1 30 450" "wait 30" "swipe 480 0 12 16" "up 0; up 1"
expect_count left2-begin 'ZWL GESTURE kind=left2 phase=begin' 1
expect_count left2-desktop 'ZWL GLASS desktop=1 via=pad' 1
sleep 1
pad left2-end "down 0 20 300; down 1 30 450" "wait 30" "swipe 480 0 12 16" "up 0; up 1"
expect_count left2-no-neighbour 'ZWL GLASS desktop=1 via=pad' 2
sleep 1

# 5. Three fingers up 25 mm in the middle: Wiseview; Esc closes it.
pad up3 "down 0 400 500; down 1 550 480; down 2 700 500" "wait 30" "swipe 0 -300 12 16" "up 0; up 1; up 2"
expect_count up3-begin 'ZWL GESTURE kind=up3 phase=begin' 1
expect_count up3-opens 'ZWL WISEVIEW opening from=' 2
tap esc
expect_count up3-esc-closes 'ZWL WISEVIEW close key' 2
sleep 1

# 6. A tap of three fingers.
pad tap3 "down 0 400 500; down 1 550 480; down 2 700 500" "wait 40" "up 0; up 1; up 2"
expect_count tap3 'ZWL GESTURE kind=tap3 phase=end' 1

# 7. Two fingers in the middle moving up: no gesture.
before=$(count 'ZWL GESTURE ')
pad scroll "down 0 500 500; down 1 700 500" "wait 30" "swipe 0 -240 12 16" "up 0; up 1"
expect_count scroll-no-gesture 'ZWL GESTURE ' "${before:-0}"

# 8. A fullscreen window: the gesture opens nothing (D6).
guest "$env /bin/wltest --color=203040 --frames=3600 --delay-ms=250 > /tmp/f.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
expect_count fullscreen-bar-hidden 'ZWL GLASS bar hidden fullscreen=' 1
pad fullscreen "down 0 500 750; down 1 700 745" "wait 30" "swipe 0 -360 12 16" "up 0; up 1"
expect_count fullscreen-gesture-logged 'ZWL GESTURE kind=bottom2 phase=begin' 3
expect_count fullscreen-no-wiseview 'ZWL WISEVIEW gesture via=pad' 3

# 9. Up, without errors.
running=$(guest 'ps -A -o args | grep -cE "[w]ayland( |$)"' | tail -1)
[ "$running" = "1" ] && pass alive || fail alive
guest 'grep -E "ZWL GESTURE|ZWL WISEVIEW|ZWL GLASS desktop|ZWL GLASS bar|ZWL INPUT|ERROR" /tmp/zdesktop.log' > "$out/log.txt"
grep -q ERROR "$out/log.txt" && fail no-error || pass no-error
guest "$stop_all" >/dev/null

echo "p003-guest: status $status (outputs in $out; wiseview-pad.png for the eye)"
exit $status
