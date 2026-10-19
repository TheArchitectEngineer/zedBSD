#!/bin/sh
# Host check of ws269-p005: the PowerVR firmware file parser, the MIPS
# processor's page table and boot parameters, and the bounded EBUSY retry.
#   plan/ws269/tests/p005/run.sh OUTDIR [FIRMWARE]
# FIRMWARE, when given, is powervr/rogue_36.50.54.182_v1.fw (sha256
# 59127d3a...abb); it must parse to the values recorded in phase005.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p005
out=$1
mkdir -p "$out"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/firmware-image.c" \
	"$root/src/drivers/gpu/powervr/mips-table.c" \
	"$root/src/drivers/gpu/powervr/retry.c" \
	"$here/powervr-host-test.c" -o "$out/powervr-host-test"
if [ $# -gt 1 ]; then
	"$out/powervr-host-test" "$2"
else
	"$out/powervr-host-test"
fi
