#!/bin/sh
# ws113-p003: VK_EXT_display_control through libvulkan on the Venus guest (image of plan/ws113/tests/config-amd64-p003.mk,
# started with VENUS_OUTPUTS=2 plan/tools/files/files-guest.sh start IMAGE when the launcher takes VENUS_OUTPUTS, else with one
# output), no compositor: display-events --watch=5 --frames=60 --hold=4 (the pictures are taken
# when the probe's lines say each step was reached).
#  1. Both extensions are offered (surface-counter=1 display-control=1) and the displays are enumerated (count >= 1).
#  2. The first display gets a swapchain (result=0) and 60 red frames are presented (result=0); first.png is red.
#     With two outputs, the second display gets a swapchain and blue frames, or the swapchain is refused for the
#     limit of outputs shown at once (result=-3, VK_ERROR_INITIALIZATION_FAILED), which is not a failure.
#  3. The first-pixel event fence of each presented display signals within a second (result=0).
#  4. Power off and on of the first display work (result=0); off.png is not red, the frame after power on (on.png) is red.
#  5. A signaled event fence is not signaled again after a reset (reset-spent signaled=0 after-reset=1).
#  6. The hotplug fence does not signal while nothing is plugged (no "hotplug count=" line in the 5 s watch).
#  7. The probe ends (done error=0).
# PASS: every "ok" line and the last line display-events-p003: PASS.
#   plan/ws113/tests/display-events-p003.sh [OUTDIR]     (default build/ws113-p003-guest)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws113-p003-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
status=0

# The centre pixel of a picture, as RRGGBB.
centre() {
	python3 -c "
from PIL import Image
image = Image.open('$1').convert('RGB')
r, g, b = image.getpixel((image.width // 2, image.height // 2))
print('%02x%02x%02x' % (r, g, b))"
}

# Whether an RRGGBB colour is mostly red.
red() {
	python3 -c "
c = '$1'
r, g, b = int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16)
raise SystemExit(0 if r > 160 and g < 80 and b < 80 else 1)"
}

# Fails the run unless the probe's output has a line matching a pattern.
expect_line() {
	if grep -qE "$1" "$out/events.txt"; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}

# Waits (up to SECONDS) until the probe's output has a line matching a pattern; the output so far is in events.txt.
wait_line() {
	waited=0
	while [ "$waited" -lt "$2" ]; do
		guest 'cat /tmp/events.txt' > "$out/events.txt"
		if grep -qE "$1" "$out/events.txt"; then return 0; fi
		sleep 1
		waited=$((waited + 1))
	done
	echo "note: no line /$1/ within $2 s"
	return 1
}

# No compositor, no greeter: the probe has the displays.
guest 'service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; sleep 1' >/dev/null
guest 'nohup /bin/display-events --watch=5 --frames=60 --hold=4 > /tmp/events.txt 2>&1 </dev/null & echo started' >/dev/null

# The pictures follow the probe's own lines (T1-355: a frame takes 50 to 100 ms on the Venus guest, so fixed waits missed them):
# the frames presented, a second into the 4 s off, and a second after power on.
wait_line 'DISPLAY-EVENTS (present index=0|done)' 60
check "$out/first.png" >/dev/null; first=$(centre "$out/first.png")
wait_line 'DISPLAY-EVENTS (power index=0 state=off|done)' 30
sleep 1
check "$out/off.png" >/dev/null; off=$(centre "$out/off.png")
wait_line 'DISPLAY-EVENTS (power index=0 state=on|done)' 30
sleep 1
check "$out/on.png" >/dev/null; on=$(centre "$out/on.png")
wait_line 'DISPLAY-EVENTS done' 30

# 1.
expect_line 'DISPLAY-EVENTS extensions surface-counter=1 display-control=1' "both extensions are offered"
expect_line 'DISPLAY-EVENTS displays count=[1-9]' "the displays are enumerated"

# 2. and 3.
expect_line 'DISPLAY-EVENTS swapchain index=0 result=0' "the first display's swapchain"
expect_line 'DISPLAY-EVENTS present index=0 frames=60 result=0' "60 frames to the first display"
expect_line 'DISPLAY-EVENTS first-pixel index=0 result=0 ' "the first display's refresh event signals"
if red "$first"; then echo "ok: the first display is red ($first)"; else echo "FAIL: the first display is $first"; status=1; fi
if grep -qE 'DISPLAY-EVENTS swapchain index=1 result=0' "$out/events.txt"; then
	expect_line 'DISPLAY-EVENTS present index=1 frames=60 result=0' "60 frames to the second display"
	expect_line 'DISPLAY-EVENTS first-pixel index=1 result=0 ' "the second display's refresh event signals"
elif grep -qE 'DISPLAY-EVENTS swapchain index=1 result=-3' "$out/events.txt"; then
	echo "ok: the second display's swapchain is refused for the output limit (VK_ERROR_INITIALIZATION_FAILED)"
else
	echo "note: no second display's swapchain line (one output)"
fi

# 4.
expect_line 'DISPLAY-EVENTS power index=0 state=off result=0' "power off"
expect_line 'DISPLAY-EVENTS power index=0 state=on result=0' "power on"
if red "$off"; then echo "FAIL: the frame stayed on the screen while off"; status=1; else echo "ok: the screen is not the frame while off ($off)"; fi
if red "$on"; then echo "ok: the frame is back after power on ($on)"; else echo "FAIL: the frame after power on is $on"; status=1; fi

# 5. to 7.
expect_line 'DISPLAY-EVENTS reset-spent signaled=0 after-reset=1' "a reset event fence is not signaled again"
expect_line 'DISPLAY-EVENTS hotplug watching seconds=5' "the hotplug fence is registered"
if grep -qE 'DISPLAY-EVENTS hotplug count=' "$out/events.txt"; then echo "FAIL: the hotplug fence signaled with nothing plugged"; status=1; else echo "ok: no hotplug signal with nothing plugged"; fi
expect_line 'DISPLAY-EVENTS done error=0' "the probe ends"

[ $status = 0 ] && echo "display-events-p003: PASS" || echo "display-events-p003: FAIL"
exit $status
