#!/bin/sh
# Host check of ws269-p010a: the stage lines and hw.gpu.powervr.stages text
# (stage.c), and gpu-selftest's run of its stages against stand-in programs.
#   plan/ws269/tests/p010/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p010
out=$1
mkdir -p "$out"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/stage.c" "$here/stage-host-test.c" -o "$out/stage-host-test"
"$out/stage-host-test"
sh "$here/selftest-host.sh" "$out"
