#!/bin/sh
# ws113-p005 (the displays on the system extension: kl_system_displays_v1 and libkeiland's kl_system_displays_*) on
# the Venus guest with two heads through QEMU's D-Bus display.  The image is plan/ws113/tests/config-amd64-p005.mk's,
# started with
#   VENUS_DISPLAY=dbus VENUS_OUTPUTS=2 plan/tools/files/files-guest.sh start IMAGE
# The judgement is by the probe's lines (keiland-system, a client of libkeiland's kl_system_*):
#  1. The probe opens with the displays offered (capabilities has 0x8000) and the snapshot has two displays, display 0
#     the anchor and shown (flags 0x6), display 1 shown (flags 0x4), mode extended.
#  2. display-mode mirror is answered 0 and the next snapshot says mirror; display-place puts display 1 left
#     (extended, -1024 0, answered 0, the snapshot has x=-1024); an overlapping place is answered EINVAL.
#  3. A light for display 0 is answered ENOTSUP (a virtual adapter has no backlight device).
#  4. Head 1 unplugged: the snapshot has one display; plugged again: two.  No KWL FAILED, the compositor alive.
# PASS: every "ok" line and the last line displays-p005: PASS.
#   plan/ws113/tests/displays-p005.sh [OUTDIR]     (default build/ws113-p005-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws113-p005-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
head_set() { sh plan/tools/guest/venus-head.sh "$1" "$2"; }
probe() { guest "XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/tmp/p005-home /bin/keiland-system --timeout-ms=5000 $1"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[d]isplay-events" | awk "{print \$1}"); do kill $p; done; sleep 1'
start_compositor='export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p005-home; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
nohup /bin/wayland --testing --timeout=300 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started'
status=0

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

# The guest's ssh answers first.
waited=0
while [ "$waited" -lt 90 ]; do
	answer=$(guest 'echo guest-ready')
	case "$answer" in *guest-ready*) break ;; esac
	sleep 2
	waited=$((waited + 2))
done
guest "$stop_all" >/dev/null
guest 'rm -rf /tmp/p005-home; mkdir -p /tmp/p005-home' >/dev/null
head_set 0 1280x800
head_set 1 1024x768
sleep 3
guest "$start_compositor" >/dev/null
wait_line /tmp/zdesktop.log 'KWL OUTPUT head open name=Venus virtual display 1 ' 60

# 1. The snapshot.
text=$(probe displays)
printf '%s\n' "$text" > "$out/probe-1.txt"
expect_text "$text" 'KEILAND-SYSTEM open capabilities=0x[0-9a-f]*[89a-f][0-9a-f]{3}$' "the displays are offered"
expect_text "$text" 'KEILAND-SYSTEM displays mode=extended count=2' "two displays, extended"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 0 .* width=1280 height=800 .* flags=0x7 ' "display 0 is the anchor (its own display: no name says which is built in)"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* x=1280 y=0 width=1024 height=768 .* flags=0x4 ' "display 1 is shown right of it"

# 2. The mirror, a place, an overlap.
text=$(probe 'display-mode mirror displays')
printf '%s\n' "$text" > "$out/probe-2.txt"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "the mirror is applied"
expect_text "$text" 'KEILAND-SYSTEM displays mode=mirror' "the snapshot says mirror"
text=$(probe "display-place 'Venus virtual display 1' -1024 0 displays")
printf '%s\n' "$text" > "$out/probe-3.txt"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=0' "the place is applied"
expect_text "$text" 'KEILAND-SYSTEM display key=Venus virtual display 1 .* x=-1024 y=0 ' "the snapshot has the place"
text=$(probe "display-place 'Venus virtual display 1' 100 0")
printf '%s\n' "$text" > "$out/probe-4.txt"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=EINVAL' "an overlapping place is refused"

# 3. No light on a virtual display.
text=$(probe "brightness 'Venus virtual display 0' 50")
printf '%s\n' "$text" > "$out/probe-5.txt"
expect_text "$text" 'KEILAND-SYSTEM result request=[0-9]+ error=ENOTSUP' "a virtual display has no light"

# 4. Unplugged and plugged.
head_set 1 off
sleep 3
text=$(probe displays)
printf '%s\n' "$text" > "$out/probe-6.txt"
expect_text "$text" 'KEILAND-SYSTEM displays mode=extended count=1' "one display after the unplugging"
head_set 1 1024x768
sleep 3
text=$(probe displays)
printf '%s\n' "$text" > "$out/probe-7.txt"
expect_text "$text" 'KEILAND-SYSTEM displays mode=extended count=2' "two again after the plugging"
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
if grep -q 'KWL FAILED' "$out/zdesktop.log"; then echo "FAIL: the compositor failed"; status=1; else echo "ok: no KWL FAILED"; fi
alive=$(guest 'ps -A -o args | grep -cE "^/bin/wayland( |$)"' | tail -1)
if [ "${alive:-0}" -gt 0 ] 2>/dev/null; then echo "ok: the compositor runs on"; else echo "FAIL: the compositor is gone"; status=1; fi
guest "$stop_all" >/dev/null

[ $status = 0 ] && echo "displays-p005: PASS" || echo "displays-p005: FAIL"
exit $status
