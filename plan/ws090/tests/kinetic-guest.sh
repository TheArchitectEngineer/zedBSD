#!/bin/sh
# ws090-p017 (BUG-211, BUG-218): a touch pad's two-finger scroll that flies on, on the pen test guest with Settings
# (plan/ws090/tests/config-amd64-kinetic.mk built from the commit under test; started with
# plan/ws079/tests/pen-guest.sh start IMAGE).  zdesktop --glass 1280x800 and Settings at 900x420, so that its list
# of pages scrolls.  The pointer (QMP) is put on the list; touchinject's pad (the Latitude 5330's size, 12 units a
# millimetre) has two fingers move 20 mm up in about 100 ms and lift.  Judged from the logs over SSH and a picture.
#  1. The compositor sends the fingers' scrolling a unit at a time with the finger as its source, and its end:
#     zdesktop's log has "ZWL AXIS stop".
#  2. Settings scrolls the list at once and lets it fly on after the lift: its log has
#     "ZSETTINGS KINETIC start pane=list" and, within 2 s, "ZSETTINGS KINETIC stop pane=list".
#  3. Both stay up, without ERROR or FAILED lines (kinetic.png: the list scrolled down).
#   plan/ws090/tests/kinetic-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
out=${1:-build/ws090-kinetic}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }

# The compositor and Settings.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
guest "$env /bin/settings --width=900 --height=420 --timeout-s=120 > /tmp/settings.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\2 \3 \4/p')
wx=${2:-0}; wy=${3:-0}
echo "settings window at $wx,$wy"

# The pointer on the list of pages; two fingers 20 mm up in about 100 ms, then lifted.
pointer move $((wx + 100)) $((wy + 200)) sleep 300 >/dev/null
guest 'printf "pad 1336 760 5 scan\nwait 2600\ndown 0 600 500; down 1 760 500\nwait 20\nswipe 0 -240 8 12\nup 0; up 1\nhold 2000\n" | /bin/touchinject; echo replay=$?' > "$out/pad.txt"
grep -q '^replay=0$' "$out/pad.txt" && pass replay || fail replay

# 1 and 2. The logs.
guest 'grep -E "ZWL INPUT|ZWL AXIS|ERROR" /tmp/zdesktop.log' > "$out/zdesktop.txt"
guest 'cat /tmp/settings.log' > "$out/settings.txt"
grep -q 'ZWL AXIS stop' "$out/zdesktop.txt" && pass axis-stop || fail axis-stop
grep -q 'KINETIC start pane=list' "$out/settings.txt" && pass kinetic-start || fail kinetic-start
grep -q 'KINETIC stop pane=list' "$out/settings.txt" && pass kinetic-stop || fail kinetic-stop

# 3. Both up, no errors, and the picture.
check "$out/kinetic.png" >/dev/null
running=$(guest 'ps -A -o args | grep -cE "[w]ayland( |$)|[s]ettings"' | tail -1)
[ "$running" = "2" ] && pass running || fail running
grep -q ERROR "$out/zdesktop.txt" && fail zdesktop-errors || pass zdesktop-errors
grep -q FAILED "$out/settings.txt" && fail settings-failed || pass settings-failed
guest "$stop_all" >/dev/null

[ $status -eq 0 ] && echo "kinetic-guest: PASS" || echo "kinetic-guest: FAIL"
exit $status
