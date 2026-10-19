#!/bin/sh
# Host check of the DMA coherency procedure behind /dev/vf2-dmatest
# (ws256-p023) on a model of a write-back cache, its flush, an uncached
# alias and a bus master that does or does not snoop (no emulator, no
# board).
#   plan/ws256/tests/vf2-dmatest/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/vf2-dmatest
out=$1
mkdir -p "$out"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/platform/visionfive2/vf2-dmatest-core.c" \
	"$here/vf2-dmatest-host-test.c" -o "$out/vf2-dmatest-host-test"
"$out/vf2-dmatest-host-test"
