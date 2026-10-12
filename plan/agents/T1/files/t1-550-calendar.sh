#!/bin/sh
# T1-550: the Calendar window fills the screen: dock/undock/resize with fixed coordinates (window already open).
# usage: t1-550-calendar.sh LABEL OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
A="plan/tools/aat/aat --qemu"
label=$1; out=$2
$A click 1208 78; sleep 2.5
$A move 600 400; sleep 0.5; $A wheel 600 400 -3; sleep 1; $A move 640 450; sleep 1.5
$A shot "$out/$label-calendar-5-docked.png"
$A click 1217 22; sleep 2.5
$A move 700 500; sleep 1.5
$A shot "$out/$label-calendar-6-undocked.png"
$A windows | tail -2
$A drag 1268 450 1000 450 --steps 15; sleep 2
$A move 500 400; sleep 1.5
$A shot "$out/$label-calendar-7-resized.png"
$A windows | tail -2
