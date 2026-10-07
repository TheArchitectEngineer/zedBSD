#!/bin/sh
# ws142-p010: the session's layout mode (p008, BUG-217), the window in the middle over the blurred scene (p008b) and
# the touch pad's gestures (p009, BUG-215, 216, 224, 228) on the pen test guest (plan/ws079/tests/config-amd64-pen.mk;
# plan/ws079/tests/pen-guest.sh start IMAGE).  The compositor and wltest under test (BUILD/bin/wayland and
# BUILD/bin/wltest, for --fixed) are copied in, 1280x800.  Keys through QMP, the touch pad through touchinject's "pad"
# (1336x760 units, 12 a millimetre).  Two applications by wltest --app-id: apps.a, then apps.b (on top).
#  1. Docked, then a switch: a double click on apps.b's title bar docks it ("KWL LAYOUT mode=docked
#     reason=double-click"); Alt held, Tab, Tab, Alt let go brings apps.a, which docks too ("KWL LAYOUT switch surface=A
#     action=dock mode=docked via=switch"); apps.b is not drawn (layout-docked.png shows apps.a alone).
#  2. Two fingers from the pad's top edge down (TOP2) open App Home (ws181-p008: "KWL GESTURE kind=top2 phase=begin",
#     "KWL HOME open via=pad"), which Esc closes; then a double click on apps.a's title in the bar floats it again
#     ("KWL GLASS undock surface=A via=double-click", "KWL LAYOUT mode=windowed reason=double-click"), and apps.b,
#     docked behind it, floats at once too (WS181, the 2026-10-07 UAT: "KWL LAYOUT float-quiet ... client=B",
#     "KWL LAYOUT leave via=double-click"; layout-windowed.png shows both as windows).
#  3. Windowed, a switch to apps.b: it is a window already, so it only comes forward ("KWL LAYOUT switch surface=B
#     action=keep mode=windowed via=switch").
#  4. Docked again (apps.b's title double-clicked), then apps.b ends by itself: the docked mode ends and apps.a stays a
#     window (WS181: "KWL LAYOUT leave via=closed", no "KWL LAYOUT front ... action=dock").
#  5. apps.a docked (its title double-clicked), a window of one size opens (wltest --fixed 420x300, apps.f): it opens
#     docked ("KWL GLASS open-docked client=F"), drawn at its size in the middle over the blurred, darkened scene
#     (fixed-docked.png).
#  6. Wiseview by two fingers up from the bottom edge, then two fingers across the middle 25 mm to the right: one step
#     only ("KWL SWIPE right via=pad", one "KWL WISEVIEW select step=+1 via=swipe"); two fingers down 15 mm choose
#     ("KWL WISEVIEW select surface=[0-9]* via=swipe"); wiseview-swipe.png before the choice, without the title text.
#  7. The switcher by a tap of three fingers, two fingers 40 mm to the right: one step only ("KWL SWITCH step ... via=pad"
#     once); two fingers down: it brings the selection ("KWL SWITCH commit ... via=pad-swipe").
#  8. A fullscreen window (wltest without --windowed, apps.s): two fingers up from the bottom edge dock it
#     ("KWL GLASS fullscreen-leave surface=S via=bottom2", "WINDOW unfullscreen surface=S ... docked=1"), Wiseview does
#     not open.
#  9. The compositor stays up, with no ERROR in its log.
#   plan/ws142/tests/p010-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: p010-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws142-p010}
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
last() { guest "grep -- '$1' /tmp/zdesktop.log | tail -1"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect_count() { n=$(count "$2"); if [ "${n:-0}" -eq "$3" ] 2>/dev/null; then pass "$1"; else fail "$1 ($2: ${n:-?}, expected $3)"; fi; }
expect_some() { n=$(count "$2"); if [ "${n:-0}" -ge 1 ] 2>/dev/null; then pass "$1"; else fail "$1 ($2: none)"; fi; }
# The kernel keeps a process's argv[0] only as its command (src/kern/exec.c), so ps cannot tell two wltests apart by
# their arguments (T1-224, T1-224b: apps.b was never killed): each one's pid is kept in /tmp/NAME.pid when it starts.
open_app() { guest "$env /bin/wltest --app-id=$1 $4 --size=$3 --color=$2 --frames=3600 --delay-ms=250 > /tmp/$1.log 2>&1 </dev/null & echo \$! > /tmp/$1.pid; sleep 3; echo started" >/dev/null; }
pad() { name=$1; shift; script="pad 1336 760 5 scan\nwait 2600"; for line in "$@"; do script="$script\n$line"; done
	guest "printf '$script\nhold 800\n' | /bin/touchinject; echo replay=\$?" > "$out/$name.txt"
	grep -q '^replay=0$' "$out/$name.txt" || fail "$name replay"; sleep 0.5; }
# The client number of the latest window of an application (its MAP line after its wltest started).
client_of() { guest "grep -n 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.*client=\([0-9]*\) .*/\1/p'; }
# A double click on the docked window's title in the system bar (its x from the latest dock line).
bar_double_click() {
	set -- $(guest "grep 'KWL GLASS dock surface=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* title=\([0-9]*\).*/\1/p')
	pointer move $((${1:-200} + 60)) 30 sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 1500
}
# A double click on the title bar of the window mapped with a client number.
title_double_click() {
	set -- $(guest "grep 'KWL MAP client=$1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
	pointer move $((${1:-300} + 120)) $((${2:-300} - 30)) sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 1500
}
: > "$out/qmp.txt"

# The compositor and wltest under test, and the applications.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/bin/wltest" /bin/wltest
guest 'chmod 755 /bin/wayland /bin/wltest; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q KWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 2; echo started' >/dev/null
open_app apps.a f4d0d0 380x260 --windowed
a=$(client_of)
open_app apps.b d0f4d0 400x280 --windowed
b=$(client_of)
echo "apps.a client ${a:-?}, apps.b client ${b:-?}"
pointer move 640 760 sleep 300

# 1. apps.b docked by a double click, then Alt+Tab to apps.a: it docks too.
title_double_click "${b:-0}"
expect_some docked-mode 'KWL LAYOUT mode=docked reason=double-click'
pointer move 640 760 sleep 300
key alt true
tap tab
tap tab
key alt false
sleep 1.5
expect_some switch-docks "KWL LAYOUT switch surface=[0-9]* action=dock mode=docked via=switch client=${a:-0}\$"
shot layout-docked

# 2. Two fingers from the top edge down: App Home (ws181-p008), closed by Esc; apps.a's title in the bar double-clicked:
# it floats again, the mode windowed, and apps.b behind it floats too.
pad top2 "down 0 500 10; down 1 700 20" "wait 30" "swipe 0 240 12 16" "up 0; up 1" "wait 600"
expect_some top2-gesture 'KWL GESTURE kind=top2 phase=begin'
expect_some top2-home 'KWL HOME open via=pad'
tap esc
expect_some top2-home-esc 'KWL HOME close via=escape'
bar_double_click
expect_some bar-undock "KWL GLASS undock surface=[0-9]* via=double-click x=[-0-9]* y=[-0-9]* client=${a:-0}\$"
expect_some bar-windowed 'KWL LAYOUT mode=windowed reason=double-click'
expect_some bar-behind-floats "KWL LAYOUT float-quiet surface=[0-9]* x=[-0-9]* y=[-0-9]* w=[0-9]* h=[0-9]* client=${b:-0}\$"
expect_some bar-leave 'KWL LAYOUT leave via=double-click front=[0-9]* quiet=1'
sleep 1
shot layout-windowed

# 3. Windowed: Alt held, Tab, Tab (from apps.a to apps.b), Alt let go: apps.b, a window already, comes forward.
key alt true
tap tab
tap tab
key alt false
sleep 1.5
expect_some switch-keeps "KWL LAYOUT switch surface=[0-9]* action=keep mode=windowed via=switch client=${b:-0}\$"

# 4. apps.b docked again, then it ends by itself: the docked mode ends, apps.a stays a window (WS181).
title_double_click "${b:-0}"
n=$(count 'KWL LAYOUT mode=docked'); [ "${n:-0}" -ge 2 ] 2>/dev/null && pass docked-again || fail "docked-again (${n:-?})"
guest 'p=$(cat /tmp/apps.b.pid); ps -A -o pid,args | grep "^ *$p "; kill $p; sleep 2; echo killed pid=$p' > "$out/kill.txt"
expect_some apps-b-gone "KWL CLIENT gone client=${b:-0} "
expect_some closed-leaves 'KWL LAYOUT leave via=closed front=0'
expect_count front-not-docked "KWL LAYOUT front surface=[0-9]* action=dock client=${a:-0}\$" 0

# 5. apps.a docked again, so that the window of one size opens docked.
title_double_click "${a:-0}"
n=$(count 'KWL LAYOUT mode=docked'); [ "${n:-0}" -ge 3 ] 2>/dev/null && pass docked-third || fail "docked-third (${n:-?})"

# A window of one size opens docked, in the middle over the blurred scene.
open_app apps.f f4f0c0 420x300 "--windowed --fixed"
f=$(client_of)
expect_some fixed-open-docked "KWL GLASS open-docked client=${f:-0} "
sleep 1
shot fixed-docked

# 6. Wiseview from the bottom edge, one step for a long swipe across, a swipe down chooses.
pad wiseview-open "down 0 500 750; down 1 700 745" "wait 30" "swipe 0 -240 10 8" "up 0; up 1" "wait 1200"
expect_some wiseview-opens 'KWL WISEVIEW opening from='
pad wiseview-across "down 0 500 400; down 1 700 400" "wait 30" "swipe 300 0 15 16" "up 0; up 1" "wait 500"
expect_count wiseview-one-step 'KWL WISEVIEW select step=+1 via=swipe' 1
shot wiseview-swipe
pad wiseview-down "down 0 500 300; down 1 700 300" "wait 30" "swipe 0 180 10 16" "up 0; up 1" "wait 1200"
expect_some wiseview-chooses 'KWL WISEVIEW select surface=[0-9]* via=swipe'

# 7. The switcher: a tap of three fingers, 40 mm across is one step, a swipe down brings.
before=$(count 'KWL SWITCH step index=[0-9]* app=[^ ]* via=pad')
pad switch-across "down 0 400 500; down 1 550 480; down 2 700 500" "wait 40" "up 0; up 1; up 2" "wait 500" \
	"down 0 500 400; down 1 700 400" "wait 30" "swipe 480 0 20 16" "up 0; up 1" "wait 500" \
	"down 0 500 300; down 1 700 300" "wait 30" "swipe 0 180 10 16" "up 0; up 1" "wait 800"
expect_some switch-opens 'KWL SWITCH open via=pad'
expect_count switch-one-step 'KWL SWITCH step index=[0-9]* app=[^ ]* via=pad' $(( ${before:-0} + 1 ))
expect_some switch-down-brings 'KWL SWITCH commit app=[^ ]* surface=[0-9]* via=pad-swipe'

# 8. A fullscreen window: two fingers up from the bottom edge dock it, Wiseview does not open.
open_app apps.s 203040 640x400 ""
s=$(client_of)
sleep 1
opened=$(count 'KWL WISEVIEW opening from=')
pad fullscreen-bottom2 "down 0 500 750; down 1 700 745" "wait 30" "swipe 0 -240 10 8" "up 0; up 1" "wait 1200"
expect_some fullscreen-leaves "KWL GLASS fullscreen-leave surface=[0-9]* via=bottom2 error=0 client=${s:-0}\$"
expect_some fullscreen-docked "KWL WINDOW unfullscreen surface=[0-9]* x=8 y=52 placed=[01] docked=1 client=${s:-0}\$"
expect_count fullscreen-no-wiseview 'KWL WISEVIEW opening from=' "${opened:-0}"

# 9. Up, without errors.
running=$(guest 'ps -A -o args | grep -cE "[w]ayland( |$)"' | tail -1)
[ "$running" = "1" ] && pass alive || fail alive
guest 'grep -E "KWL LAYOUT|KWL SWIPE|KWL WISEVIEW|KWL SWITCH|KWL HOME (open|close|pad)|KWL GESTURE kind=(top2|bottom2|tap3)|KWL GLASS (dock|undock|open-docked|fullscreen-leave)|KWL WINDOW unfullscreen|KWL MAP|ERROR" /tmp/zdesktop.log' > "$out/log.txt"
grep -q ERROR "$out/log.txt" && fail no-error || pass no-error
guest "$stop_all" >/dev/null

echo "p010-guest: status $status (outputs in $out; layout-docked, layout-windowed, fixed-docked, wiseview-swipe .png for the eye)"
exit $status
