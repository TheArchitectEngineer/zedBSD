#!/bin/sh
# ws081-p004: the compositor follows a finger with libkeiland's touch motion,
# on the pen test guest (plan/ws079/tests/config-amd64-pen.mk, the injector
# and touchinject with a Scan Time, ws081-p002).  The compositor and
# libkeiland under test are copied into the running guest; the output is
# 1280x800 and the touch screen is declared 0..1279 by 0..799, so a finger's
# numbers are the output's pixels.
#
#  1. A window (tablet-probe --pointer: wltest's Vulkan window does not open
#     on this guest now, errno ENOENT, with the image's own compositor too).
#     One finger on its title bar (30 px above the body) waits the 150 ms for
#     a second one, then drags it right and down by 300,150 in 30 frames of
#     33.3 ms with +-8 ms of jitter (a 30 Hz panel), on a screen with a Scan
#     Time; a still finger elsewhere first lets the screen's device trust the
#     Scan Time.
#  2. The compositor's --log-frames lines: every report's time is the Scan
#     Time on the host clock (stamp <= host, steps of the script's intervals),
#     the pointer follows the finger's motion and never steps back, and the
#     window ends as far as the finger went.
#  3. Judder, printed only: over the steady part of the drag, the rms distance
#     of the drawn points from a straight line through them, for the followed
#     points and for what drawing each report as it arrives would have shown
#     at the same moments.  QEMU's software Vulkan takes about 100 ms to
#     compose a frame, which stalls the event loop, so the moments are few
#     and uneven and the numbers say little; the smoothness is measured by the
#     library's host test (ws081-p003) and on the device (ws081-p007).
#
#   VENUS_RENDERER=/home/awe/zedBSD-rpi4/build/ws035-sq-venus/install \
#   GUEST_RUNTIME=$PWD/build/ws081-run plan/ws079/tests/pen-guest.sh start build/amd64/hdd-image.img
#   GUEST_RUNTIME=... plan/ws081/tests/p004-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
build=${1:?usage: p004-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws081-p004-guest}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]ablet-probe|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]ablet-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0

# The compositor and libkeiland under test.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/dynamic/libkeiland.so" /lib/libkeiland.so
guest 'chmod 755 /bin/wayland' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --timeout=300 --width=1280 --height=800 --glass --log-frames > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null

# 1. The window, where the compositor places it.
guest "$env /bin/tablet-probe --pointer --color=c8d8ec --token=b --timeout-s=120 > /tmp/b.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1:\2 \3 \4/p')
b=${1:-0:0}; sx=${2:-0}; sy=${3:-0}
tx=$((sx + 150)); ty=$((sy - 30))
pointer move 1200 780 sleep 300 >/dev/null
echo "window: $b at $sx,$sy, title bar at $tx,$ty"

# The drag: a 30 Hz panel with a Scan Time and jitter.
# A still finger on the desktop first (0.7 s of reports), so that the screen's device trusts the Scan Time before the drag.
printf 'size 1279 799 2 scan\nwait 2600\ndown 2 1000 760\nswipe 0 0 20 33.333\nup 2\nwait 500\ndown 1 %d %d\nwait 250\nswipe 300 150 30 33.333 8\nhold 200\nup 1\nhold 300\n' "$tx" "$ty" > "$out/drag.script"
put "$out/drag.script" /tmp/drag.script
guest '/bin/touchinject /tmp/drag.script; echo replay=$?' | grep -q '^replay=0$' || { echo "touchinject: FAILED"; status=1; }
sleep 1
guest "grep -E 'ZWL TOUCH (title|report|follow)|GLASS moved' /tmp/zdesktop.log" > "$out/drag-log.txt"
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null

# 2 and 3. The log, judged.
python3 - "$out/drag-log.txt" "${b#*:}" "$sx" "$sy" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
surface = sys.argv[2]
start_x = int(sys.argv[3])
start_y = int(sys.argv[4])
reports = []
follows = []
moved = None
drag = False
started = False
for line in lines:
    fields = dict(f.split("=", 1) for f in line.split() if "=" in f)
    # Only the drag: the lines from its title bar's first finger on.
    if "TOUCH title held" in line:
        started = True
    if not started:
        continue
    if "TOUCH title drag" in line:
        drag = True
    if "TOUCH report contact=0" in line:
        reports.append((int(fields["stamp_ms"]), int(fields["host_ms"]), int(fields["x"]), int(fields["y"]), int(fields["scan"])))
    if "TOUCH follow contact=0" in line:
        follows.append((int(fields["now_ms"]), int(fields["x"]), int(fields["y"])))
    if "GLASS moved" in line and fields.get("surface") == surface:
        moved = (int(fields["x"]), int(fields["y"]))
ok = True
def check(condition, text):
    global ok
    print(("ok: " if condition else "FAILED: ") + text)
    ok = ok and condition
check(drag, "the finger dragged the title bar (TOUCH title drag)")
check(len(reports) >= 30 and all(r[4] == 1 for r in reports), "%d reports, every one with a Scan Time" % len(reports))
check(all(r[0] <= r[1] for r in reports), "every report's time is not after its arrival")
steps = [b[0] - a[0] for a, b in zip(reports, reports[1:])]
moving = steps[2:-2]
check(len(moving) > 0 and all(24 <= s <= 43 for s in moving), "report steps follow the script's 33.3 +- 8 ms: %s" % moving[:10])
check(len(follows) > 0, "the pointer followed the motion at %d moments for %d reports" % (len(follows), len(reports)))
back = max([0] + [a[1] - b[1] for a, b in zip(follows, follows[1:])] + [a[2] - b[2] for a, b in zip(follows, follows[1:])])
check(back <= 1, "the followed point never steps back (at most %d px)" % back)
check(moved == (start_x + 300, start_y + 150), "the window ended at %s (it started at %d,%d)" % (moved, start_x, start_y))

# Judder over the steady part (the middle 60% of the followed moments).
def rms_from_line(points):
    n = len(points)
    mt = sum(p[0] for p in points) / n
    mx = sum(p[1] for p in points) / n
    var = sum((p[0] - mt) ** 2 for p in points)
    slope = sum((p[0] - mt) * (p[1] - mx) for p in points) / var
    return (sum((p[1] - (mx + slope * (p[0] - mt))) ** 2 for p in points) / n) ** 0.5
if len(follows) > 10:
    start = follows[0][0] + (follows[-1][0] - follows[0][0]) * 0.2
    end = follows[0][0] + (follows[-1][0] - follows[0][0]) * 0.8
    moments = [f for f in follows if start <= f[0] <= end]
    followed = [(f[0], f[1]) for f in moments]
    held = []
    for f in moments:
        arrived = [r for r in reports if r[1] <= f[0]]
        if arrived:
            held.append((f[0], arrived[-1][2]))
    a = rms_from_line(followed)
    b = rms_from_line(held)
    print("info: judder along x: followed %.2f px rms, drawn as the reports arrive %.2f px rms (%d moments)" % (a, b, len(moments)))
sys.exit(0 if ok else 1)
EOF
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }

[ $status -eq 0 ] && echo "p004: PASS" || echo "p004: FAIL"
exit $status
