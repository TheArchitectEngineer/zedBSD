#!/bin/sh
# ws035-p092: where new windows go, and the title bars over dark windows, on the Venus guest (the browser
# image, plan/ws074/tests/build-browser-image.sh).  It runs the demo sheet (plan/ws035/tests/zdesktop-p090.sh with
# the demo image's App Home list: Files, Terminal, Browser, Model viewer, Gears, X terminal, one after another)
# and then checks the places zdesktop logged (KWL MAP): no window's corner within 32 pixels of an earlier one's,
# and every corner inside the space under the system bar.  The sheet's pictures (99-all.png: the inactive
# title bars over the dark windows, the letters from the application IDs) are judged by eye.
#
#   plan/tools/files/files-guest.sh start     (the guest must be up)
#   plan/ws035/tests/zdesktop-p092.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws035-p092}
status=0
sh plan/ws035/tests/zdesktop-p090.sh "$out" demo || status=1

# The places, in the order the windows came (the client and the corner).
grep 'KWL MAP ' "$out/zdesktop.log" | sed -n 's/.*client=\([0-9]*\) surface=[0-9]* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p' > "$out/places.txt"
echo "places (client x y):"
cat "$out/places.txt"
awk '
	{ x[NR] = $2; y[NR] = $3 }
	$2 < 12 || $3 < 98 { print "outside the space: client " $1 " at " $2 "," $3; bad = 1 }
	END {
		for (i = 2; i <= NR; i++)
			for (j = 1; j < i; j++) {
				dx = x[i] - x[j]; dy = y[i] - y[j]
				if (dx > -32 && dx < 32 && dy > -32 && dy < 32) { print "near: window " i " and " j; bad = 1 }
			}
		exit bad
	}' "$out/places.txt" && echo "places: apart and inside ok" || { echo "places: MISSING"; status=1; }
[ $status = 0 ] && echo "zdesktop-p092: PASS (and judge the sheet)" || echo "zdesktop-p092: FAIL"
exit $status
