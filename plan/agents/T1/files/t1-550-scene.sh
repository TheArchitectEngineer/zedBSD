#!/bin/sh
# T1-550: per app: open from App Home, hover, scroll, resize (right edge), dock, hover/scroll, undock, close.
# usage (GUEST_RUNTIME set, aat guest started): t1-550-scene.sh LABEL OUTDIR APPNAME...   (PNG OUTDIR/LABEL-APP-N.png)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
A="plan/tools/aat/aat --qemu"
label=$1; out=$2; shift 2
for app in "$@"; do
	tag=$(echo "$app" | tr -d ' ')
	$A key super; sleep 2.5; $A type "$app"; sleep 1; $A key enter; sleep 6
	geom=$($A windows | awk '$3=="True" && $8==0 {x=$4;y=$5;w=$6;h=$7} END{print x,y,w,h}')
	set -- $geom
	x=$1; y=$2; w=$3; h=$4
	re=$((x+w)); ty=$((y-30)); cy=$((y+h/2)); cx=$((x+w/2))
	echo "$app geom $geom"
	for p in "$((x+w/4)) $((y+h/4))" "$((x+w/2)) $((y+h/2))" "$((x+3*w/4)) $((y+h/3))" "$((x+w/4)) $((y+3*h/4))"; do $A move $p; sleep 0.5; done
	$A wheel $cx $cy -4; sleep 1; $A wheel $cx $cy 2; sleep 1
	$A move $((x+w/2)) $((y+h/2)); sleep 1.5
	$A shot "$out/$label-$tag-1-hover.png"
	$A drag $re $cy $((re-230)) $cy --steps 15; sleep 2
	$A move $((x+w/3)) $((y+h/3)); sleep 1.5
	$A shot "$out/$label-$tag-2-resized.png"
	$A click $((re-230-60)) $ty; sleep 2.5
	$A move 600 400; sleep 0.5; $A wheel 600 400 -3; sleep 1; $A move 640 450; sleep 1.5
	$A shot "$out/$label-$tag-3-docked.png"
	$A click 1217 22; sleep 2.5
	$A move 700 500; sleep 1.5
	$A shot "$out/$label-$tag-4-undocked.png"
	$A windows | tail -2
	$A click $((re-230-26)) $ty; sleep 2
done
