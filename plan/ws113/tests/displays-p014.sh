#!/bin/sh
# ws113-p014 (a display turned off in the extended mode: kl_system_displays_set_shown) on the Venus guest with two heads
# through QEMU's D-Bus display.  The image is plan/ws113/tests/config-amd64-p006.mk's (keiland-system), started with
#   VENUS_DISPLAY=dbus VENUS_OUTPUTS=2 plan/tools/files/files-guest.sh start IMAGE
# Head 0 is 1280x800 (the anchor), head 1 1024x768 right of it.  The judgement is by keiland-system's lines (the
# displays' snapshot: flags 0x2 anchor, 0x4 shown, 0x20 off) and the compositor's.
#  1. Display 1 turned off: answered 0, its head closes, the snapshot has it off and not shown; on again: its head opens.
#  2. Display 0 (the anchor) turned off: the desktop goes to display 1 (KWL DISPLAYS anchor off, KWL OUTPUT switch),
#     the snapshot has display 1 the anchor and display 0 off; display 1 turned off then, the last one on, is refused
#     EINVAL; display 0 on again: shown again (display 1 stays the anchor).
#  3. Display 1 off kept in displays.conf (off=...): a new compositor starts without its head.
#  4. The mirror shows it still (off and shown); turning a display off in the mirror is refused EINVAL; extended again:
#     off again.  Then on.
#  5. No KWL FAILED, the compositor alive.
# PASS: every "ok" line and the last line displays-p014: PASS.
#   plan/ws113/tests/displays-p014.sh [OUTDIR]     (default build/ws113-p014-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws113-p014-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null | tr -d '\r'; }
head_set() { sh plan/tools/guest/venus-head.sh "$1" "$2"; }
probe() { guest "XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/tmp/p014-home /bin/keiland-system --timeout-ms=5000 $1"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[d]isplay-events" | awk "{print \$1}"); do kill $p; done; sleep 1'
start_compositor='export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p014-home; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
nohup /bin/wayland --testing --timeout=600 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started'
one="'Venus virtual display 0'"
two="'Venus virtual display 1'"
status=0
step=0

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

# Asks keiland-system, keeping its lines in OUTDIR/probe-N.txt; sets text.
ask() {
	step=$((step + 1))
	text=$(probe "$1")
	printf '%s\n' "$text" > "$out/probe-$step.txt"
}

# The guest, the heads and the compositor.
waited=0
while [ "$waited" -lt 90 ]; do
	answer=$(guest 'echo guest-ready')
	case "$answer" in *guest-ready*) break ;; esac
	sleep 2
	waited=$((waited + 2))
done
guest "$stop_all" >/dev/null
guest 'rm -rf /tmp/p014-home; mkdir -p /tmp/p014-home' >/dev/null
head_set 0 1280x800
head_set 1 1024x768
sleep 3
guest "$start_compositor" >/dev/null
wait_line /tmp/zdesktop.log 'KWL OUTPUT head open name=Venus virtual display 1 ' 60

# 1. Display 1 off and on.
ask "display-shown $two off displays"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "display 1 turned off"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* flags=0x20 ' "the snapshot has display 1 off, not shown"
wait_line /tmp/zdesktop.log 'KWL OUTPUT head closed name=Venus virtual display 1' 10 && echo "ok: its head closed" || { echo "FAIL: its head closed"; status=1; }
ask "display-shown $two on displays"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "display 1 turned on"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* flags=0x4 ' "the snapshot has display 1 shown"

# 2. The anchor off: the desktop moves; the last one on refused; the first on again.
ask "display-shown $one off displays"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "display 0 (the anchor) turned off"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* flags=0x6 ' "display 1 is the anchor now"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 0 .* flags=0x20 ' "display 0 is off"
wait_line /tmp/zdesktop.log 'KWL DISPLAYS anchor off name=Venus virtual display 0 to=Venus virtual display 1' 10 && echo "ok: the anchor moved" ||
    { echo "FAIL: the anchor moved"; status=1; }
ask "display-shown $two off"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=EINVAL' "the last display on stays on"
ask "display-shown $one on displays"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "display 0 turned on"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 0 .* flags=0x4 ' "display 0 shown again"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* flags=0x6 ' "display 1 stays the anchor"

# 3. Kept in displays.conf: a new compositor starts without display 1's head.
ask "display-shown $two off"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "display 1 off before the restart"
guest 'cat /tmp/p014-home/.config/keiland/displays.conf' > "$out/displays.conf"
if grep -q '^off=Venus virtual display 1$' "$out/displays.conf"; then echo "ok: displays.conf keeps it off"; else echo "FAIL: displays.conf keeps it off"; status=1; fi
guest "$stop_all" >/dev/null
guest "$start_compositor" >/dev/null
wait_line /tmp/zdesktop.log 'KWL READY' 60
sleep 3
ask "displays"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* flags=0x20 ' "the new compositor keeps display 1 off"
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop-restart.log"
if grep -q 'KWL OUTPUT head open name=Venus virtual display 1 ' "$out/zdesktop-restart.log"; then echo "FAIL: display 1 has a head"; status=1; else echo "ok: no head for display 1"; fi

# 4. The mirror shows it still; no display is turned off in the mirror.
ask "display-mode mirror displays"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "the mirror applied"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* flags=0x24 ' "the mirror shows display 1, off"
ask "display-shown $one off"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=EINVAL' "no display turned off in the mirror"
ask "display-mode extended displays"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* flags=0x20 ' "extended again: display 1 off"
ask "display-shown $two on"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "display 1 on at the end"

# 5. Nothing failed.
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
if grep -q 'KWL FAILED' "$out/zdesktop.log"; then echo "FAIL: the compositor failed"; status=1; else echo "ok: no KWL FAILED"; fi
alive=$(guest 'ps -A -o args | grep -cE "^/bin/wayland( |$)"' | tail -1)
if [ "${alive:-0}" -gt 0 ] 2>/dev/null; then echo "ok: the compositor runs on"; else echo "FAIL: the compositor is gone"; status=1; fi
guest "$stop_all" >/dev/null

[ $status = 0 ] && echo "displays-p014: PASS" || echo "displays-p014: FAIL"
exit $status
