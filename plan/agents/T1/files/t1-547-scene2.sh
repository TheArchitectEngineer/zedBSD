#!/bin/sh
# T1-547/548 part 2: drag the Files window by its titlebar, then close it (its close button); the session log is kept.
# usage: t1-547-scene2.sh LABEL OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
A="plan/tools/aat/aat --qemu"
label=$1; out=$2
$A drag 500 78 700 200 --steps 25; sleep 2
$A move 100 600; sleep 1.5
$A shot "$out/$label-5-dragged.png"
$A drag 700 200 500 78 --steps 25; sleep 2
$A move 100 600; sleep 1.5
$A shot "$out/$label-6-dragged2.png"
$A windows
$A click 1174 78; sleep 3
$A move 900 700; sleep 1.5
$A shot "$out/$label-7-closed.png"
$A windows
$A run 'cat /run/user/1000/session.log' > "$out/$label-session.log"
echo "log lines: $(wc -l < "$out/$label-session.log")"
grep -c -E "KWL VULKAN_ERROR|KWL FAILED" "$out/$label-session.log"
