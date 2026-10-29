#!/bin/sh
# ws309-p017: runs WS035 zdesktop tests (plan/ws035/tests/zdesktop-NAME.sh, as WS099's criteria.sh runs them for C9)
# on two images in turn, each test on a fresh guest with a runtime directory of its own (OUT/IMAGE-TAG-NAME-run), so
# that another chain's guest on build/ws035-sq-run is never stopped.  Used to tell a WS309 change from the host's load:
# OLD is an image of the tree before WS309, NEW one of WS309's tree, run back to back under the same load.
# OUT/summary.txt: "TAG NAME PASS|FAIL seconds=N", tag old or new.
#   usage: plan/ws309/tests/zdesktop-compare.sh OUT OLD_IMAGE NEW_IMAGE NAME...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: zdesktop-compare.sh OUT OLD_IMAGE NEW_IMAGE NAME...}
old=${2:?}
new=${3:?}
shift 3
top=$(pwd)
mkdir -p "$out"
: > "$out/summary.txt"
for name in "$@"; do
	for tag in old new; do
		image=$old
		[ $tag = new ] && image=$new
		GUEST_RUNTIME=$top/$out/$tag-$name-run
		export GUEST_RUNTIME
		began=$(date +%s)
		timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/$tag-$name.start.log" 2>&1
		sleep 40
		timeout 200 python3 -I plan/tools/guest/guest.py wait --timeout 180 >> "$out/$tag-$name.start.log" 2>&1
		env -u VENUS_SIZE timeout 1800 sh "plan/ws035/tests/zdesktop-$name.sh" "$out/$tag-$name" > "$out/$tag-$name.log" 2>&1
		code=$?
		timeout 30 python3 -I plan/tools/guest/guest.py run 'cat /tmp/zdesktop.log 2>/dev/null' > "$out/$tag-$name.zdesktop.log" 2>&1
		timeout 60 python3 -I plan/tools/guest/guest.py stop > /dev/null 2>&1
		verdict=FAIL
		[ $code -eq 0 ] && verdict=PASS
		echo "$tag $name $verdict seconds=$(($(date +%s) - began)) $(uptime | sed 's/.*load average: //')" | tee -a "$out/summary.txt"
	done
done
