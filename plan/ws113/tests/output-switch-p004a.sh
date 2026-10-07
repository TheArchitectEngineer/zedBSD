#!/bin/sh
# ws113-p003 (two outputs, hotplug) and ws113-p004a (the output moved) on the Venus guest with two heads plugged and
# unplugged through QEMU's D-Bus display.  The image is plan/ws113/tests/config-amd64-p003.mk's (the files image with
# display-events), started with
#   VENUS_DISPLAY=dbus VENUS_OUTPUTS=2 plan/tools/files/files-guest.sh start IMAGE
# The judgement is by the guest's lines (the D-Bus display may give VNC no picture of GL scanouts):
#  1. head 1 plugged (1024x768): display-events lists 2 displays; the second's swapchain works (result=0, blue frames)
#     or is refused for the output limit (result=-3); the first-pixel fences signal.
#  2. display-events watching, head 1 unplugged and plugged: its hotplug fence signals (hotplug count=1, then count=2).
#  3. The compositor (/bin/wayland --testing --glass, at the native size) follows both displays (KWL OUTPUT displays
#     count=2); head 0 unplugged: the output is lost and moves to display 1 at 1024x768 (KWL OUTPUT switch ... width=1024
#     height=768, KWL OUTPUT resized width=1024 height=768); head 0 plugged again: the output stays on display 1; head 1
#     unplugged: the output moves back to display 0 at 1280x800.  The compositor runs on (no KWL FAILED, the process
#     alive).
# Both heads are given their size first: QEMU ignores a SetUIInfo equal to the last one a console was given, and a head
# never given one holds 0x0, so unplugging head 0 without it would change nothing (T1-357's first run).
# PASS: every "ok" line and the last line output-switch-p004a: PASS.
#   plan/ws113/tests/output-switch-p004a.sh [OUTDIR]     (default build/ws113-p004a-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws113-p004a-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
head_set() { sh plan/tools/guest/venus-head.sh "$1" "$2"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[d]isplay-events" | awk "{print \$1}"); do kill $p; done; sleep 1'
status=0

# Fails the run unless a file the test copied out has a line matching a pattern.
expect_line() {
	if grep -qE "$2" "$1"; then echo "ok: $3"; else echo "FAIL: $3"; status=1; fi
}

# Waits (up to SECONDS) until a guest file has a line matching a pattern, keeping a copy in OUTDIR.
wait_line() {
	waited=0
	while [ "$waited" -lt "$3" ]; do
		guest "cat $1" > "$out/$(basename "$1")"
		if grep -qE "$2" "$out/$(basename "$1")"; then return 0; fi
		sleep 1
		waited=$((waited + 1))
	done
	echo "note: no line /$2/ in $1 within $3 s"
	return 1
}

# The guest's ssh answers first.
waited=0
while [ "$waited" -lt 90 ]; do
	answer=$(guest 'echo guest-ready')
	case "$answer" in *guest-ready*) break ;; esac
	sleep 2
	waited=$((waited + 2))
done
sh plan/tools/guest/venus-head.sh list > "$out/consoles.txt" 2>&1
guest "$stop_all" >/dev/null

# 1. Two heads: display-events presents to both.  Head 0 is given its size too, so that its later unplugging is a change.
head_set 0 1280x800
head_set 1 1024x768
sleep 3
guest 'nohup /bin/display-events --watch=1 --frames=30 --hold=1 > /tmp/events2.txt 2>&1 </dev/null & echo started' >/dev/null
wait_line /tmp/events2.txt 'DISPLAY-EVENTS done' 60
expect_line "$out/events2.txt" 'DISPLAY-EVENTS displays count=2' "two displays are listed"
expect_line "$out/events2.txt" 'DISPLAY-EVENTS swapchain index=1 result=(0|-3)$' "the second display's swapchain works or is refused for the output limit"
expect_line "$out/events2.txt" 'DISPLAY-EVENTS first-pixel index=0 result=0 ' "the first display's refresh event signals"

# 2. The hotplug fence: head 1 unplugged, then plugged, while display-events watches.
guest 'nohup /bin/display-events --watch=20 --frames=1 --hold=1 > /tmp/events3.txt 2>&1 </dev/null & echo started' >/dev/null
wait_line /tmp/events3.txt 'DISPLAY-EVENTS hotplug watching' 60
head_set 1 off
wait_line /tmp/events3.txt 'DISPLAY-EVENTS hotplug count=1' 15
head_set 1 1024x768
wait_line /tmp/events3.txt 'DISPLAY-EVENTS hotplug count=2' 15
expect_line "$out/events3.txt" 'DISPLAY-EVENTS hotplug count=1 ' "unplugging head 1 signals the hotplug fence"
expect_line "$out/events3.txt" 'DISPLAY-EVENTS hotplug count=2 ' "plugging it again signals the new fence"
wait_line /tmp/events3.txt 'DISPLAY-EVENTS done' 30

# 3. The compositor follows the displays and moves its output.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
nohup /bin/wayland --testing --timeout=300 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started' >/dev/null
wait_line /tmp/zdesktop.log 'KWL OUTPUT displays count=2' 60
expect_line "$out/zdesktop.log" 'KWL OUTPUT displays count=2' "the compositor lists both displays"
head_set 0 off
wait_line /tmp/zdesktop.log 'KWL OUTPUT switch name=Venus virtual display 1 width=1024 height=768' 20
expect_line "$out/zdesktop.log" 'KWL OUTPUT lost' "the output's display is lost"
expect_line "$out/zdesktop.log" 'KWL OUTPUT switch name=Venus virtual display 1 width=1024 height=768' "the output moves to display 1"
expect_line "$out/zdesktop.log" 'KWL OUTPUT resized width=1024 height=768' "the desktop is fitted to 1024x768"
head_set 0 1280x800
sleep 3
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop-replug.log"
if grep -q 'KWL OUTPUT switch name=Venus virtual display 0' "$out/zdesktop-replug.log"; then
	echo "FAIL: plugging head 0 again leaves the output on display 1"
	status=1
else
	echo "ok: plugging head 0 again leaves the output on display 1"
fi
head_set 1 off
wait_line /tmp/zdesktop.log 'KWL OUTPUT switch name=Venus virtual display 0 width=1280 height=800' 20
expect_line "$out/zdesktop.log" 'KWL OUTPUT switch name=Venus virtual display 0 width=1280 height=800' "the output moves back to display 0"
expect_line "$out/zdesktop.log" 'KWL OUTPUT resized width=1280 height=800' "the desktop is fitted to 1280x800"
if grep -q 'KWL FAILED' "$out/zdesktop.log"; then echo "FAIL: the compositor failed"; status=1; else echo "ok: no KWL FAILED"; fi
alive=$(guest 'ps -A -o args | grep -cE "^/bin/wayland( |$)"' | tail -1)
if [ "${alive:-0}" -gt 0 ] 2>/dev/null; then echo "ok: the compositor runs on"; else echo "FAIL: the compositor is gone"; status=1; fi
head_set 1 1024x768 >/dev/null
guest "$stop_all" >/dev/null

[ $status = 0 ] && echo "output-switch-p004a: PASS" || echo "output-switch-p004a: FAIL"
exit $status
