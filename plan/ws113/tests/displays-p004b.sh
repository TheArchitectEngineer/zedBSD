#!/bin/sh
# ws113-p004b (several displays shown at once: extended and mirror, places, displays.conf, heads plugged and unplugged)
# on the Venus guest with two heads through QEMU's D-Bus display.  The image is plan/ws113/tests/config-amd64-p004b.mk's
# (the files image with display-events and the test capture), started with
#   VENUS_DISPLAY=dbus VENUS_OUTPUTS=2 plan/tools/files/files-guest.sh start IMAGE
# The judgement is by the guest's lines and the capture channel's answers (keiland-shot --request; the D-Bus display
# may give VNC no picture of GL scanouts):
#  1. Without displays.conf (D-BOOT2) the compositor shows display 0 and opens display 1 as a head right of it
#     (KWL OUTPUT head open name=Venus virtual display 1 width=1024 height=768 ... x=1280 y=0); DISPLAYS says
#     mode=extended and the head's place.
#  2. PLACE display 1 left of display 0 (-1024 0) is applied (OK saved=0, DISPLAYS x=-1024); a place that overlaps
#     (100 0) is refused (ERROR errno=22) and nothing changes; displays.conf keeps the place.
#  3. Head 1 unplugged: its head closes; plugged again: it opens at the place kept (x=-1024).
#  4. MODE mirror is applied (KWL DISPLAYS applied mode=mirror, DISPLAYS mode=mirror) and kept in displays.conf;
#     the compositor started again reads it (KWL DISPLAYS config mode=1) and opens the head in the mirror mode.
#  5. No KWL FAILED, no head lost but by the unplugging, the compositor alive.
# PASS: every "ok" line and the last line displays-p004b: PASS.
#   plan/ws113/tests/displays-p004b.sh [OUTDIR]     (default build/ws113-p004b-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws113-p004b-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
head_set() { sh plan/tools/guest/venus-head.sh "$1" "$2"; }
ask() { guest "XDG_RUNTIME_DIR=/tmp HOME=/tmp/p004b-home /bin/keiland-shot --request '$1'"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[d]isplay-events" | awk "{print \$1}"); do kill $p; done; sleep 1'
start_compositor='export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p004b-home; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
nohup /bin/wayland --testing --timeout=300 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started'
status=0

# Fails the run unless a file the test copied out has a line matching a pattern.
expect_line() {
	if grep -qE "$2" "$1"; then echo "ok: $3"; else echo "FAIL: $3"; status=1; fi
}

# Fails the run unless a text has a line matching a pattern.
expect_text() {
	if printf '%s\n' "$1" | grep -qE "$2"; then echo "ok: $3"; else echo "FAIL: $3 (got: $1)"; status=1; fi
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
guest "$stop_all" >/dev/null
guest 'rm -rf /tmp/p004b-home; mkdir -p /tmp/p004b-home' >/dev/null
head_set 0 1280x800
head_set 1 1024x768
sleep 3

# 1. No displays.conf: both displays extended, display 1 right of display 0.
guest "$start_compositor" >/dev/null
wait_line /tmp/zdesktop.log 'KWL OUTPUT head open name=Venus virtual display 1 ' 60
expect_line "$out/zdesktop.log" 'KWL OUTPUT head open name=Venus virtual display 1 width=1024 height=768 refresh_mhz=[0-9]+ x=1280 y=0 ' "display 1 opens as a head right of display 0"
listing=$(ask DISPLAYS)
printf '%s\n' "$listing" > "$out/displays-1.txt"
expect_text "$listing" '^mode=extended$' "the mode is extended without displays.conf"
expect_text "$listing" '^head name=Venus virtual display 1 width=1024 height=768 x=1280 y=0$' "DISPLAYS names the head's place"

# 2. A place applied, one refused.
answer=$(ask 'PLACE Venus virtual display 1 -1024 0')
expect_text "$answer" '^OK saved=0$' "the place left of display 0 is applied and saved"
answer=$(ask 'PLACE Venus virtual display 1 100 0')
expect_text "$answer" '^ERROR errno=22$' "an overlapping place is refused"
listing=$(ask DISPLAYS)
printf '%s\n' "$listing" > "$out/displays-2.txt"
expect_text "$listing" '^head name=Venus virtual display 1 width=1024 height=768 x=-1024 y=0$' "the refused place changed nothing"
guest 'cat /tmp/p004b-home/.config/keiland/displays.conf' > "$out/displays-2.conf"
expect_line "$out/displays-2.conf" '^place=Venus virtual display 1 -1024 0$' "displays.conf keeps the place"

# 3. Head 1 unplugged and plugged again: closed, then opened at the place kept.
head_set 1 off
wait_line /tmp/zdesktop.log 'KWL OUTPUT head closed name=Venus virtual display 1' 20
expect_line "$out/zdesktop.log" 'KWL OUTPUT head closed name=Venus virtual display 1' "unplugging display 1 closes its head"
head_set 1 1024x768
wait_line /tmp/zdesktop.log 'KWL OUTPUT head open name=Venus virtual display 1 .* x=-1024 y=0 ' 20
expect_line "$out/zdesktop.log" 'KWL OUTPUT head open name=Venus virtual display 1 .* x=-1024 y=0 ' "plugged again, it opens at the place kept"

# 4. The mirror applied, kept, and read again by a new compositor.
answer=$(ask 'MODE mirror')
expect_text "$answer" '^OK saved=0$' "the mirror is applied and saved"
listing=$(ask DISPLAYS)
printf '%s\n' "$listing" > "$out/displays-4.txt"
expect_text "$listing" '^mode=mirror$' "DISPLAYS says mirror"
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop-1.log"
expect_line "$out/zdesktop-1.log" 'KWL DISPLAYS applied mode=mirror' "the compositor applied the mirror"
if grep -q 'KWL FAILED' "$out/zdesktop-1.log"; then echo "FAIL: the compositor failed"; status=1; else echo "ok: no KWL FAILED"; fi
if grep -q 'KWL OUTPUT head lost' "$out/zdesktop-1.log" && ! grep -q 'KWL OUTPUT head closed' "$out/zdesktop-1.log"; then
	echo "FAIL: a head lost its display without an unplugging"; status=1
else
	echo "ok: no head lost but by the unplugging"
fi
alive=$(guest 'ps -A -o args | grep -cE "^/bin/wayland( |$)"' | tail -1)
if [ "${alive:-0}" -gt 0 ] 2>/dev/null; then echo "ok: the compositor runs on"; else echo "FAIL: the compositor is gone"; status=1; fi
guest "$stop_all" >/dev/null
guest "$start_compositor" >/dev/null
wait_line /tmp/zdesktop.log 'KWL OUTPUT head open name=Venus virtual display 1 ' 60
expect_line "$out/zdesktop.log" 'KWL DISPLAYS config mode=1 ' "a new compositor reads the mirror from displays.conf"
listing=$(ask DISPLAYS)
printf '%s\n' "$listing" > "$out/displays-5.txt"
expect_text "$listing" '^mode=mirror$' "and shows both displays in the mirror mode"
guest "$stop_all" >/dev/null

[ $status = 0 ] && echo "displays-p004b: PASS" || echo "displays-p004b: FAIL"
exit $status
