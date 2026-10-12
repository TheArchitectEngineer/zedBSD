#!/bin/sh
# T1-547/548: scenes on a running AAT guest (GUEST_RUNTIME set): App Home open/close x3, a window drag, the window's end.
# usage: t1-547-scene.sh LABEL OUTDIR   (PNG OUTDIR/LABEL-*.png, log OUTDIR/LABEL-session.log)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
A="plan/tools/aat/aat --qemu"
label=$1; out=$2
$A start >/dev/null 2>&1
$A mark scene >/dev/null
$A move 900 700; sleep 2
$A shot "$out/$label-1-desktop.png"
for i in 1 2 3; do
	$A key super; sleep 2.5
	[ $i = 1 ] && $A shot "$out/$label-2-home.png"
	$A key super; sleep 2
	[ $i = 1 ] && $A shot "$out/$label-3-homeclosed.png"
done
$A key super; sleep 2; $A type files; sleep 1; $A key enter; sleep 6
$A move 900 700; sleep 2
$A shot "$out/$label-4-window.png"
$A windows > "$out/$label-windows.txt"
cat "$out/$label-windows.txt"
