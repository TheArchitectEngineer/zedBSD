#!/bin/sh
# ws075-p021: one measured run of a demonstration passthrough image on the 5330 (plan/ws075/tests/hdmi-h4-hw.sh):
# waits for the lock and the desktop, then the desktop alone (h4-ctl.py rate A 10, latency A 10), the ten applications
# of App Home opened (hdmi/apps8.sh), the same two measures, the per-session engine time (hdmi/engine-gdb.sh, pointer
# over the empty desktop corner), two shots 2 s apart (animation), and gives the machine back.  Everything goes to
# OUTDIR/measure.txt as well as the output.
#   plan/ws075/tests/hdmi/measure-apps.sh IMAGE VMUNIX OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../../.."
image=$1
vmunix=$2
out=$3
rm -rf "$out"
mkdir -p "$out"
plan/ws075/tests/hdmi-h4-hw.sh start "$image" "$out" > "$out/start.log" 2>&1 || { cat "$out/start.log"; exit 1; }
ctl() { timeout 120 plan/ws075/tests/hdmi-h4-hw.sh ctl "$@"; }
{
	cat "$out/start.log"
	sleep 120
	echo "== desktop"
	ctl rate A 10 | tail -1
	ctl latency A 10 | tail -2
	echo "== ten applications"
	plan/ws075/tests/hdmi/apps8.sh 8000
	ctl rate A 10 | tail -1
	ctl latency A 10 | tail -2
	echo "== engine"
	RATE_XY="1700 1000" plan/ws075/tests/hdmi/engine-gdb.sh "$vmunix" 10
	ctl shot end1 | tail -1
	sleep 2
	ctl shot end2 | tail -1
} 2>&1 | tee "$out/measure.txt"
timeout 400 plan/ws075/tests/hdmi-h4-hw.sh stop "$out" | tail -1
