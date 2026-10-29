#!/bin/sh
# Host test of the nvrtx GSP parts that touch no hardware.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/tools/nvrtx/nvrtx-gsp-host-test.sh [FIRMWARE-DIR]   (from the repository root)
# FIRMWARE-DIR is a verified nvidia-gsp-firmware cache of the Turing, Ampere
# and Ada set (default: this tree's build cache, else the main checkout's,
# read only); without it the checks of the real files are skipped, and the
# last line counts them (SKIPPED 0 means every file was checked).  The binary goes in a new directory
# below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
cache=build/sources/firmware/nvidia-gsp/dc85ccedc9c973682fbcf4d628ca61174bcc3120-tu-ga-ad
firmware=${1:-$repo/$cache}
# A worktree reads the main checkout's cache (the parent of the common git directory).
main=$(cd "$(git -C "$repo" rev-parse --git-common-dir)/.." 2>/dev/null && pwd || true)
if [ $# -eq 0 ] && [ ! -d "$firmware" ] && [ -n "$main" ] && [ -d "$main/$cache" ]; then
	firmware=$main/$cache
fi
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-gsp.XXXXXX")
src=$repo/src/drivers/gpu/nvrtx
cc -std=c11 -Wall -Wextra -Werror -I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/gsp-test" \
	"$repo/plan/tools/nvrtx/nvrtx-gsp-host-test.c" \
	"$src/wire.c" "$src/image.c" "$src/layout.c" "$src/radix3.c" "$src/queue.c" "$src/vbios.c" "$src/records.c" "$src/mmu.c" "$src/objects.c" "$src/display/disp-records.c" "$src/heap.c" "$src/work-records.c" "$src/display/disp-first.c" "$src/identify.c"
if [ -d "$firmware" ]; then
	"$out/gsp-test" "$firmware"
else
	"$out/gsp-test"
fi
