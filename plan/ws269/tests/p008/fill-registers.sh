#!/bin/sh
# The host simulator's registers of the compiled fill after each prefix,
# to compare with the board's compute-check dump lines.
#   plan/ws269/tests/p008/fill-registers.sh OUTDIR ADDRESS
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p008
compiler=$root/plan/ws269/tests/p010-compiler
out=$1
mkdir -p "$out"
sources="$root/src/drivers/gpu/powervr/shader-spirv.c $root/src/drivers/gpu/powervr/shader-spirv-interface.c $root/src/drivers/gpu/powervr/shader-spirv-control.c $root/src/drivers/gpu/powervr/shader-spirv-instruction.c $root/src/drivers/gpu/powervr/shader-backend.c $root/src/drivers/gpu/powervr/usc-encode.c"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" -I"$root" -I"$compiler" \
	$sources "$compiler/ir-run.c" "$compiler/machine-run.c" "$here/fill-registers.c" -lm -o "$out/fill-registers"
"$out/fill-registers" "$2"
