#!/bin/sh
# ws113-p007 (each window on one output in the extended mode, and moved between them) on the Venus guest with two heads
# through QEMU's D-Bus display.  The image is plan/ws113/tests/config-amd64-p006.mk's (Files, keiland-system), started with
#   VENUS_DISPLAY=dbus VENUS_OUTPUTS=2 plan/tools/files/files-guest.sh start IMAGE
# Head 0 is 1280x800 (the anchor), head 1 1024x768 right of it.  The judgement is by the compositor's lines: KWL RENDER
# output=N surfaces=... is each output's render list when it changes (D-ATOMIC: a window is in its own output's list
# alone), KWL WINDOW output ... why=... a window given to an output.  QEMU's tablet is absolute and maps onto the anchor,
# so the pointer's crossing is the host test's (plan/ws113/tests/host-plane.sh) and the hardware's (p008).
#  1. A Files window opens on the anchor: in output 0's list, not in output 1's.
#  2. Super+Shift+Right gives it to head 1 (why=key, output=1): in output 1's list, out of output 0's; Super+Shift+Left
#     gives it back.
#  3. On head 1, the head unplugged: it comes back to the anchor (why=retreat) and is in output 0's list.
#  4. Plugged again, the window on head 1, the mirror chosen (keiland-system display-mode mirror): it comes to the
#     anchor (why=mode); Super+Shift+Right in the mirror finds no display (KWL WINDOW output none).
#  5. No KWL FAILED; the compositor and Files run on.
# PASS: every "ok" line and the last line displays-p007: PASS.
#   plan/ws113/tests/displays-p007.sh [OUTDIR]     (default build/ws113-p007-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws113-p007-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null | tr -d '\r'; }
head_set() { sh plan/tools/guest/venus-head.sh "$1" "$2"; }
probe() { guest "XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/tmp/p007-home /bin/keiland-system --timeout-ms=5000 $1"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[d]isplay-events|[f]iles" | awk "{print \$1}"); do kill $p; done; sleep 1'
start_compositor='export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p007-home; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
nohup /bin/wayland --testing --timeout=600 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started'
start_files='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/tmp/p007-home
nohup /bin/files --timeout-s=500 --width=800 --height=500 > /tmp/f.log 2>&1 </dev/null & echo started'
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

# Counts the compositor's lines matching a pattern.
count_lines() {
	guest "grep -cE '$1' /tmp/zdesktop.log" | tail -1
}

# Waits until the compositor has COUNT lines matching a pattern (up to 10 s); fails the run otherwise.
expect_count() {
	waited=0
	found=0
	while [ "$waited" -lt 10 ]; do
		found=$(count_lines "$1")
		[ "${found:-0}" -ge "$2" ] 2>/dev/null && { echo "ok: $3"; return 0; }
		sleep 1
		waited=$((waited + 1))
	done
	echo "FAIL: $3 (found ${found:-0} of $2)"
	status=1
	return 1
}

# Tells whether output N's last render list has the window (yes or no).
listed() {
	line=$(guest "grep 'KWL RENDER output=$1 ' /tmp/zdesktop.log | tail -1")
	case ",$(printf '%s' "$line" | sed -n 's/.*surfaces=//p' | tr -d ' ')," in
	*",$surface,"*) echo yes ;;
	*) echo no ;;
	esac
}

# Checks where the window's render lists have it: on output ON and not on output OFF.
expect_on() {
	sleep 1
	on=$(listed "$1")
	off=$(listed "$2")
	if [ "$on" = yes ] && [ "$off" = no ]; then
		echo "ok: $3 (in output $1's list only)"
	else
		echo "FAIL: $3 (output $1: $on, output $2: $off)"
		status=1
	fi
}

# The guest, the heads and the compositor.
wait_guest
guest "$stop_all" >/dev/null
guest 'rm -rf /tmp/p007-home; mkdir -p /tmp/p007-home' >/dev/null
head_set 0 1280x800
head_set 1 1024x768
sleep 3
guest "$start_compositor" >/dev/null
wait_line /tmp/zdesktop.log 'KWL OUTPUT head open name=Venus virtual display 1 ' 60
wait_desktop

# 1. A Files window on the anchor.
guest "$start_files" >/dev/null
find_window
surface=$wsurface
echo "files: surface ${surface:-none} at $wx,$wy"
[ -n "$surface" ] || { echo "FAIL: no Files window"; status=1; }
expect_on 0 1 "the new window is the anchor's"

# 2. To head 1 and back by the keyboard.
moves=$(count_lines "KWL WINDOW output surface=$surface .*output=1 .*why=key")
keys '<super-shift-right>'
expect_count "KWL WINDOW output surface=$surface .*output=1 .*why=key" $((${moves:-0} + 1)) "Super+Shift+Right gives the window to head 1"
expect_on 1 0 "the window is head 1's"
keys '<super-shift-left>'
expect_count "KWL WINDOW output surface=$surface .*output=0 .*why=key" 1 "Super+Shift+Left gives it back"
expect_on 0 1 "the window is the anchor's again"

# 3. On head 1, the head unplugged.
keys '<super-shift-right>'
expect_on 1 0 "the window is on head 1 before the unplugging"
head_set 1 off
expect_count "KWL WINDOW output surface=$surface .*output=0 .*why=retreat" 1 "the unplugging brings the window to the anchor"
expect_on 0 1 "the window is the anchor's after the unplugging"

# 4. Plugged again; the mirror brings the window to the anchor and has no display beside.
head_set 1 1024x768
wait_line /tmp/zdesktop.log 'KWL OUTPUT head open name=Venus virtual display 1 .*' 30
sleep 2
keys '<super-shift-right>'
expect_on 1 0 "the window is on head 1 again"
text=$(probe 'display-mode mirror')
printf '%s\n' "$text" > "$out/probe-mirror.txt"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "the mirror is applied"
expect_count "KWL WINDOW output surface=$surface .*output=0 .*why=mode" 1 "the mirror brings the window to the anchor"
expect_on 0 1 "the window is the anchor's in the mirror"
nones=$(count_lines 'KWL WINDOW output none')
keys '<super-shift-right>'
expect_count 'KWL WINDOW output none' $((${nones:-0} + 1)) "the mirror has no display beside"
text=$(probe 'display-mode extended')
printf '%s\n' "$text" > "$out/probe-extended.txt"

# 5. Nothing failed.
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
guest 'cat /tmp/f.log' > "$out/files.log"
if grep -q 'KWL FAILED' "$out/zdesktop.log"; then echo "FAIL: the compositor failed"; status=1; else echo "ok: no KWL FAILED"; fi
alive=$(guest 'ps -A -o args | grep -cE "^/bin/wayland( |$)"' | tail -1)
if [ "${alive:-0}" -gt 0 ] 2>/dev/null; then echo "ok: the compositor runs on"; else echo "FAIL: the compositor is gone"; status=1; fi
alive=$(guest 'ps -A -o args | grep -cE "^/bin/files( |$)"' | tail -1)
if [ "${alive:-0}" -gt 0 ] 2>/dev/null; then echo "ok: Files runs on"; else echo "FAIL: Files is gone"; status=1; fi
guest "$stop_all" >/dev/null

[ $status = 0 ] && echo "displays-p007: PASS" || echo "displays-p007: FAIL"
exit $status
