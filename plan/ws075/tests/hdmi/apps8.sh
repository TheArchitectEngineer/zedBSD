#!/bin/sh
# ws075-p009/p018: opens the eight applications of App Home one after another on the running H4 run
# (plan/ws075/tests/hdmi-h4-hw.sh start ...), on the 1920x1080 panel, and takes a shot after each (a-NAME).
# The desktop must show no App Home when it starts: the first click on the Kei button opens it.
#   plan/ws075/tests/hdmi/apps8.sh [WAIT_MS]     (default 10000 after each start)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cd "$(dirname -- "$0")/../../../.."
wait=${1:-10000}
for a in "files 600 386" "notes 743 386" "terminal 887 386" "pdf 1031 386" "browser 1175 386" "mview 1319 386" \
	"gears 600 538" "xterm 743 538"; do
	set -- $a
	timeout 90 plan/ws075/tests/hdmi-h4-hw.sh ctl pointer move 22 16 sleep 100 down up sleep 1500 move $2 $3 sleep 150 down up sleep "$wait" > /dev/null
	timeout 90 plan/ws075/tests/hdmi-h4-hw.sh ctl shot "a-$1" | tail -1
done
