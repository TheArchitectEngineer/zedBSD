#!/bin/sh
# ws075-p009/p018: opens the eight applications of App Home one after another on the running H4 run
# (plan/ws075/tests/hdmi-h4-hw.sh start ...), on the 1920x1080 panel, and takes a shot after each (a-NAME).
# The desktop must show no App Home when it starts: the first click on the Kei button opens it.
# APPS=p018 is App Home as it was on 2026-09-29 before main added Settings and Image Viewer (eight tiles and Lock
# Screen, Log Out); the default is the later App Home with ten applications (ws075-p021): Files, Notes, Settings,
# Terminal, PDF Viewer, Image Viewer, Browser, Model viewer, Gears, X terminal.
#   plan/ws075/tests/hdmi/apps8.sh [WAIT_MS]     (default 10000 after each start)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cd "$(dirname -- "$0")/../../../.."
wait=${1:-10000}
if [ "${APPS:-}" = p018 ]; then
	tiles="files:600:386 notes:743:386 terminal:887:386 pdf:1031:386 browser:1175:386 mview:1319:386 gears:600:538 xterm:743:538"
else
	tiles="files:600:386 notes:743:386 settings:887:386 terminal:1031:386 pdf:1175:386 images:1319:386 browser:600:538 mview:743:538 gears:887:538 xterm:1031:538"
fi
for tile in $tiles; do
	a=$(echo "$tile" | tr : ' ')
	set -- $a
	timeout 90 plan/ws075/tests/hdmi-h4-hw.sh ctl pointer move 22 16 sleep 100 down up sleep 1500 move $2 $3 sleep 150 down up sleep "$wait" > /dev/null
	timeout 90 plan/ws075/tests/hdmi-h4-hw.sh ctl shot "a-$1" | tail -1
done
