#!/bin/sh
# ws113-p006 (the Settings Display page: Extend or Mirror, the arrangement's drag, Apply) on the Venus guest with two
# heads through QEMU's D-Bus display (M2).  The image is plan/ws113/tests/config-amd64-p006.mk's, started with
#   VENUS_DISPLAY=dbus VENUS_OUTPUTS=2 plan/tools/files/files-guest.sh start IMAGE
# Head 0 is 1280x800 (the anchor, where the pointer and Settings are), head 1 1024x768 right of it.
#  1. Settings opens on the Display page with the two displays: the Extend and Mirror controls (1, 2) and the two
#     cards (10, 11) are logged (ZSETTINGS CONTROL), display-head0.png and display-head1.png (QMP screendump of each head).
#  2. Five times: Mirror then Apply, answered 0 and the probe's snapshot says mirror; Extend then Apply, answered 0
#     and the snapshot says extended (mirror-N-headH.png, extend-N-headH.png).
#  3. The card of display 1 dragged left past display 0's card and released: the page logs its place x=-1024 y=0;
#     Apply is answered 0 and the probe's snapshot has display 1 at x=-1024 y=0 (swapped-headH.png).
#  4. No KWL FAILED, the compositor and Settings alive.
# PASS: every "ok" line and the last line displays-p006: PASS.
#   plan/ws113/tests/displays-p006.sh [OUTDIR]     (default build/ws113-p006-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws113-p006-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null | tr -d '\r'; }
head_set() { sh plan/tools/guest/venus-head.sh "$1" "$2"; }
probe() { guest "XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/tmp/p006-home /bin/keiland-system --timeout-ms=5000 $1"; }
# The pointer on head 0 (1280x800, qmp-pointer.py's default size; its options would be read as steps after the socket).
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
# A head's picture through QMP's screendump (the D-Bus display has no VNC socket for zdesktop-check.py).
head_shot() { python3 plan/ws113/tests/qmp-head-shot.py "$GUEST_RUNTIME/qmp.sock" "$1" "$2"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[d]isplay-events|[s]ettings" | awk "{print \$1}"); do kill $p; done; sleep 1'
start_compositor='export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p006-home; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
nohup /bin/wayland --testing --timeout=600 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started'
start_settings='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/tmp/p006-home
nohup /bin/settings --timeout-s=500 display > /tmp/s.log 2>&1 </dev/null & echo started'
status=0
. plan/ws089/tests/settings-wait.sh

# Fails the run unless a text has a line matching a pattern.
expect_text() {
	if printf '%s\n' "$1" | grep -qE "$2"; then echo "ok: $3"; else echo "FAIL: $3"; status=1; fi
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

# Waits until Settings' log has COUNT lines matching a pattern (up to 10 s); fails the run otherwise.
expect_count() {
	waited=0
	found=0
	while [ "$waited" -lt 10 ]; do
		found=$(guest "grep -cE '$1' /tmp/s.log" | tail -1)
		[ "${found:-0}" -ge "$2" ] 2>/dev/null && { echo "ok: $3"; return 0; }
		sleep 1
		waited=$((waited + 1))
	done
	echo "FAIL: $3 (found ${found:-0} of $2)"
	status=1
	return 1
}

# Finds a control's rectangle in window coordinates (the last one logged): sets cx0 cy0 cw ch (empty when none).
find_control() {
	set -- $(guest "grep 'ZSETTINGS CONTROL index=$1 ' /tmp/s.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
	cx0=${1:-}; cy0=${2:-}; cw=${3:-}; ch=${4:-}
}

# Clicks a control of the page by its index.
control() {
	find_control "$1"
	if [ -z "$cx0" ]; then
		echo "FAIL: control $1 not found"
		status=1
		return 1
	fi
	cx=$((wx + cx0 + cw / 2)); cy=$((wy + cy0 + ch / 2))
	pointer move $((cx - 2)) "$cy" sleep 150 move "$cx" "$cy" sleep 300 down sleep 60 up sleep 1200
}

# Pictures of both heads (NAME-head0.png, NAME-head1.png) with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 600
	head_shot 0 "$out/$1-head0.png" >/dev/null || echo "note: no picture of head 0"
	head_shot 1 "$out/$1-head1.png" >/dev/null || echo "note: no picture of head 1"
	echo "shot: $out/$1-head0.png $out/$1-head1.png"
}

# The guest's ssh answers first; the heads and the compositor.
wait_guest
guest "$stop_all" >/dev/null
guest 'rm -rf /tmp/p006-home; mkdir -p /tmp/p006-home' >/dev/null
head_set 0 1280x800
head_set 1 1024x768
sleep 3
guest "$start_compositor" >/dev/null
wait_line /tmp/zdesktop.log 'KWL OUTPUT head open name=Venus virtual display 1 ' 60
wait_desktop

# 1. Settings on the Display page.
guest "$start_settings" >/dev/null
find_window
echo "settings: window at $wx,$wy"
expect_count 'ZSETTINGS PAGE display' 1 "the Display page is shown"
expect_count 'ZSETTINGS CONTROL index=11 ' 1 "the second display's card is a control"
shot display
applied=0

# 2. Mirror and Extend, five times each.
round=1
while [ $round -le 5 ]; do
	control 2
	control 3
	applied=$((applied + 1))
	expect_count 'ZSETTINGS DISPLAY apply result errno=0' "$applied" "round $round: the mirror is applied"
	text=$(probe displays)
	printf '%s\n' "$text" > "$out/probe-mirror-$round.txt"
	expect_text "$text" 'KEILAND-SYSTEM displays mode=mirror' "round $round: the snapshot says mirror"
	shot "mirror-$round"
	control 1
	control 3
	applied=$((applied + 1))
	expect_count 'ZSETTINGS DISPLAY apply result errno=0' "$applied" "round $round: the extended mode is applied"
	text=$(probe displays)
	printf '%s\n' "$text" > "$out/probe-extend-$round.txt"
	expect_text "$text" 'KEILAND-SYSTEM displays mode=extended count=2' "round $round: the snapshot says extended"
	shot "extend-$round"
	round=$((round + 1))
done

# 3. Display 1's card dragged left of display 0's: grabbed at its middle, let go a little past the other's left edge.
find_control 10
left0=$cx0
find_control 11
if [ -z "$cx0" ] || [ -z "$left0" ]; then
	echo "FAIL: the cards are not found"
	status=1
else
	sx=$((wx + cx0 + cw / 2)); sy=$((wy + cy0 + ch / 2))
	ex=$((wx + left0 - cw / 2 - 4))
	pointer move "$sx" "$sy" sleep 200 down sleep 150 move $((sx - 20)) "$sy" sleep 120 move $(((sx + ex) / 2)) "$sy" sleep 120 \
	    move "$ex" "$sy" sleep 300 up sleep 1200
	expect_count 'ZSETTINGS DISPLAY place Venus virtual display 1 x=-1024 y=0' 1 "the card snaps left of display 0"
	control 3
	applied=$((applied + 1))
	expect_count 'ZSETTINGS DISPLAY apply result errno=0' "$applied" "the swapped places are applied"
	text=$(probe displays)
	printf '%s\n' "$text" > "$out/probe-swapped.txt"
	expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* x=-1024 y=0 ' "the snapshot has display 1 left of display 0"
	shot swapped
fi

# 4. Nothing failed.
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
guest 'cat /tmp/s.log' > "$out/settings.log"
if grep -q 'KWL FAILED' "$out/zdesktop.log"; then echo "FAIL: the compositor failed"; status=1; else echo "ok: no KWL FAILED"; fi
alive=$(guest 'ps -A -o args | grep -cE "^/bin/wayland( |$)"' | tail -1)
if [ "${alive:-0}" -gt 0 ] 2>/dev/null; then echo "ok: the compositor runs on"; else echo "FAIL: the compositor is gone"; status=1; fi
alive=$(guest 'ps -A -o args | grep -cE "^/bin/settings( |$)"' | tail -1)
if [ "${alive:-0}" -gt 0 ] 2>/dev/null; then echo "ok: Settings runs on"; else echo "FAIL: Settings is gone"; status=1; fi
guest "$stop_all" >/dev/null

[ $status = 0 ] && echo "displays-p006: PASS" || echo "displays-p006: FAIL"
exit $status
