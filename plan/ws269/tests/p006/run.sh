#!/bin/sh
# Host check of ws269-p006: the PowerVR core's page tables (4 KiB pages,
# never a snoop), a context's address space and the firmware heap's page
# map.
#   plan/ws269/tests/p006/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p006
out=$1
mkdir -p "$out"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/gpu-mmu.c" \
	"$root/src/drivers/gpu/powervr/mips-table.c" \
	"$here/gpu-mmu-host-test.c" -o "$out/gpu-mmu-host-test"
"$out/gpu-mmu-host-test"
