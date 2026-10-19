#!/bin/sh
# Host check of ws269-p007: the PowerVR driver's display node on a fake
# boot framebuffer (query, mode, claim, present with the red/blue swap and
# the clip, wait, release and close giving the screen back, the refused
# scanout import of a render buffer and its shared frame).
#   plan/ws269/tests/p007/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p007
out=$1
mkdir -p "$out"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/display.c" \
	"$here/display-host-test.c" -o "$out/display-host-test"
"$out/display-host-test"
