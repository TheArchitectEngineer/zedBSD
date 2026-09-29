#!/bin/sh
# ws075-p025 (BUG-117): on the running H4 run with the ten applications open (hdmi/apps8.sh), opens the Model viewer
# from App Home and quits it with q, ROUNDS times (a new client's first frames each time, where BUG-117 was seen).
# After every round the flips of pipe A over 3 s tell whether the desktop still draws; the first round with none
# stops it.  One line per round.
#   plan/ws075/tests/hdmi/stress-117.sh [ROUNDS]     (default 20)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cd "$(dirname -- "$0")/../../../.."
rounds=${1:-20}
ctl() { timeout 120 plan/ws075/tests/hdmi-h4-hw.sh ctl "$@"; }
alive() {
	flips=$(ctl rate A 3 | sed -n 's/^rate: \([0-9]*\) flips.*/\1/p')
	echo "$1: ${flips:-?} flips in 3 s"
	[ "${flips:-0}" -gt 0 ]
}
round=0
while [ $round -lt "$rounds" ]; do
	ctl pointer move 22 16 sleep 100 down up sleep 1500 move 743 538 sleep 150 down up sleep 4000 > /dev/null
	ctl hmp sendkey q > /dev/null
	sleep 2
	alive "C $round" || exit 1
	round=$((round + 1))
done
echo "stress-117: no stop"
