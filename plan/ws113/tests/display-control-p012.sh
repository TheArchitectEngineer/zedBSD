#!/bin/sh
# ws113-p012: GPU_DISPLAY_REFRESH and GPU_DISPLAY_POWER on the Venus guest (image of plan/ws113/tests/config-amd64-p012.mk,
# started with plan/tools/files/files-guest.sh start IMAGE), no compositor: display-control --hold=5 on /dev/gpu0.
#  1. The display offers both (power=1 counter=1); the refresh boundaries of a second are 40 to 80 (Venus's virtual
#     clock at the output's refresh, virtual=1); the claim and the present of one solid green frame work.
#  2. Power off works, the query says powered_off=1, no boundary comes while it is off (refresh-off error=<zedBSD ETIMEDOUT>,
#     its number read from include/uapi/errno.h) and the screen is not the green frame (off.png).
#  3. Power on works, the boundaries come again (40 to 80 in a second) and the screen is the green frame again (on.png).
#  4. The release works (done error=0).  shown.png is the frame before the power changes.
# PASS: every "ok" line and the last line display-control-p012: PASS.
#   plan/ws113/tests/display-control-p012.sh [OUTDIR]     (default build/ws113-p012)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws113-p012}
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

# Fails the run unless the probe's output has a line matching a pattern.
expect_line() {
	if grep -qE "$1" "$out/control.txt"; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}

# The zedBSD errno of ETIMEDOUT, from the C library's header in the tree.
etimedout=$(sed -n 's/^#define ETIMEDOUT \([0-9]*\).*/\1/p' include/uapi/errno.h | head -1)

# No compositor, no greeter: the probe has the display.
guest 'service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; sleep 1' >/dev/null
guest 'nohup /bin/display-control --hold=5 > /tmp/control.txt 2>&1 </dev/null & echo started' >/dev/null
sleep 4
check "$out/shown.png" >/dev/null; shown=$(centre "$out/shown.png")
sleep 5
check "$out/off.png" >/dev/null; off=$(centre "$out/off.png")
sleep 6
check "$out/on.png" >/dev/null; on=$(centre "$out/on.png")
sleep 6
guest 'cat /tmp/control.txt' > "$out/control.txt"

# 1.
expect_line 'DISPLAY-CONTROL display id=[0-9]+ .* power=1 counter=1' "the display offers power and refresh"
expect_line 'DISPLAY-CONTROL refresh now boundaries=(4[0-9]|[5-7][0-9]|80) ms=1[0-9][0-9][0-9] virtual=1 ' "40 to 80 virtual boundaries a second"
expect_line 'DISPLAY-CONTROL claim lease=[1-9]' "the claim"
expect_line 'DISPLAY-CONTROL present error=0' "the present of the green frame"
[ "$shown" = 30c060 ] && echo "ok: the green frame is shown ($shown)" || { echo "FAIL: the shown frame is $shown"; status=1; }

# 2.
expect_line 'DISPLAY-CONTROL power state=off error=0' "power off"
expect_line 'DISPLAY-CONTROL query powered_off=1' "the query says powered off"
expect_line "DISPLAY-CONTROL refresh-off error=$etimedout\$" "no boundary while off (ETIMEDOUT=$etimedout)"
[ "$off" != 30c060 ] && echo "ok: the screen is not the frame while off ($off)" || { echo "FAIL: the frame stayed on the screen while off"; status=1; }

# 3. and 4.
expect_line 'DISPLAY-CONTROL power state=on error=0' "power on"
expect_line 'DISPLAY-CONTROL refresh on boundaries=(4[0-9]|[5-7][0-9]|80) ' "the boundaries come again"
[ "$on" = 30c060 ] && echo "ok: the green frame is back ($on)" || { echo "FAIL: the frame after power on is $on"; status=1; }
expect_line 'DISPLAY-CONTROL done error=0' "the release"

[ $status = 0 ] && echo "display-control-p012: PASS" || echo "display-control-p012: FAIL"
exit $status
