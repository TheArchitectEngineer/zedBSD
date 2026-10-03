#!/bin/sh
# ws075-p016: logs out and in again COUNT times on a running hdmi-h4-hw.sh demonstration run (the session must be up
# at the start), from this host.  Each Log Out clicks App Home (top left) and its Log Out icon, waits for the
# greeter's lease and takes the screen (h4-ctl.py shot logoutN); each login presses Enter (root, empty password),
# waits for the session's lease and takes the screen (loginN).  The action times (epoch) go to OUTDIR/actions.log,
# for h4-blank.py and the kernel log.  The positions are those of the 1920x1280 HDMI output.
#
#   plan/ws075/tests/hdmi/h4-cycle.sh OUTDIR COUNT [FIRST]      (FIRST numbers the first cycle, default 1)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=$1
count=$2
first=${3:-1}
cd "$(dirname -- "$0")/../../../.."
host=${I915_HOST:-solaris10-man}
ctl=plan/ws075/tests/hdmi-h4-hw.sh

# Waits until the kernel log counts more claimed leases than $1; prints the new count.
wait_lease() {
	i=0
	while [ $i -lt 120 ]; do
		now=$(ssh "$host" 'grep -c "resident display: lease [0-9]* claimed" bigbang/h4/run.log')
		if [ "$now" -gt "$1" ]; then
			echo "$now"
			return 0
		fi
		sleep 1
		i=$((i + 1))
	done
	echo "$1"
	return 1
}

n=$first
last=$((first + count))
while [ $n -lt $last ]; do
	leases=$(ssh "$host" 'grep -c "resident display: lease [0-9]* claimed" bigbang/h4/run.log')
	echo "logout $n $(date +%s.%N)" >> "$out/actions.log"
	$ctl ctl pointer move 22 16 sleep 150 down sleep 80 up sleep 1500 move 1031 617 sleep 150 down sleep 80 up > /dev/null
	leases=$(wait_lease "$leases") || { echo "h4-cycle: no greeter lease after logout $n"; exit 1; }
	sleep 4
	$ctl ctl shot "logout$n" > /dev/null
	echo "login $n $(date +%s.%N)" >> "$out/actions.log"
	$ctl ctl keys '\\n' > /dev/null
	leases=$(wait_lease "$leases") || { echo "h4-cycle: no session lease after login $n"; exit 1; }
	sleep 4
	$ctl ctl shot "login$n" > /dev/null
	echo "h4-cycle: cycle $n done (leases $leases)"
	n=$((n + 1))
done
