#!/bin/sh
# Host check of ws269-p008: the PowerVR Rogue shading cluster and data
# sequencer encoders, and the driver's own compute programs built with
# them, against bit strings worked out by hand from the Mesa 26.2.5
# field tables.
#   plan/ws269/tests/p008/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p008
out=$1
mkdir -p "$out"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/usc-encode.c" \
	"$root/src/drivers/gpu/powervr/pds-encode.c" \
	"$here/encode-host-test.c" -o "$out/encode-host-test"
"$out/encode-host-test"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/usc-encode.c" \
	"$root/src/drivers/gpu/powervr/pds-encode.c" \
	"$root/src/drivers/gpu/powervr/compute-program.c" \
	"$here/compute-host-test.c" -o "$out/compute-host-test"
"$out/compute-host-test"
