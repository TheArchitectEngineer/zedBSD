#!/bin/sh
# ws099-p001, C7: the text on the frosted glass keeps a contrast of C7_MIN_CONTRAST (WCAG AA 4.5:1) on the default
# wallpaper and the 5 generated ones (/usr/share/keiland/wallpapers).  For each, zdesktop --glass at 1280x800 is
# started with it and a popup-probe window (centred, 400x300 at 440,293); the screenshot's system bar clock and the
# window's title are measured (plan/ws099/tests/c7-contrast.py: median glass against the darkest 1%).
#
#   plan/ws035/tests/zdesktop-guest.sh start build/ws099-criteria.img
#   plan/ws099/tests/c7-contrast.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
out=${1:-build/ws099-shots/c7}
C7_MIN_CONTRAST=${C7_MIN_CONTRAST:-4.5}
# The regions (x0 y0 x1 y1) of the text measured at 1280x800: the system bar's clock and the probe's title.
C7_CLOCK=${C7_CLOCK:-1130 4 1272 30}
C7_TITLE=${C7_TITLE:-480 250 600 278}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[p]opup-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[p]opup-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
pass=0
fail=0
worst=

pictures=$(guest 'ls /usr/share/keiland/wallpaper.ppm /usr/share/keiland/wallpapers/*.ppm 2>/dev/null' | grep '\.ppm$')
echo "wallpapers: $(echo $pictures | wc -w)"
for picture in $pictures; do
	name=$(basename "$picture" .ppm)
	guest "$stop_all" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --timeout=300 --width=1280 --height=800 --glass --wallpaper=$picture > /tmp/zdesktop.log 2>&1 </dev/null & i=0; while [ ! -S /tmp/wayland-0 ] && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 1
/bin/popup-probe --timeout-s=200 --token=c > /tmp/c.log 2>&1 </dev/null & sleep 3; echo started" >/dev/null
	pointer move 5 790 sleep 600
	check "$out/$name.png" >/dev/null
	lines=$(python3 plan/ws099/tests/c7-contrast.py "$out/$name.png" clock $C7_CLOCK title $C7_TITLE)
	echo "$lines" | sed "s/^C7 /C7 $name /"
	for ratio in $(echo "$lines" | sed -n 's/.*contrast=\([0-9.]*\).*/\1/p'); do
		if python3 -c "import sys; sys.exit(0 if $ratio >= $C7_MIN_CONTRAST else 1)"; then
			pass=$((pass + 1))
		else
			fail=$((fail + 1))
		fi
		worst=$(python3 -c "print(min([float(v) for v in '$worst $ratio'.split()]))")
	done
done
guest "$stop_all" >/dev/null
echo "C7 RESULT pass=$pass fail=$fail min_contrast=$worst limit=$C7_MIN_CONTRAST"
[ "$fail" -eq 0 ] && [ "$pass" -gt 0 ] && echo "C7: PASS" || echo "C7: FAIL"
[ "$fail" -eq 0 ] && [ "$pass" -gt 0 ]
