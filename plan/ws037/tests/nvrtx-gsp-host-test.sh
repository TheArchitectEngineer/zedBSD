#!/bin/sh
# Host test of the nvrtx GSP parts that touch no hardware (ws037-p004 4a).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws037/tests/nvrtx-gsp-host-test.sh [FIRMWARE-DIR]   (from the repository root)
# FIRMWARE-DIR is a verified nvidia-gsp-firmware cache (default: the build's
# cache of the pinned revision, when it is there); without it the checks of
# the real files' containers are skipped.  The binary goes in a new directory
# below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
firmware=${1:-$repo/build/sources/firmware/nvidia-gsp/dc85ccedc9c973682fbcf4d628ca61174bcc3120}
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-gsp.XXXXXX")
src=$repo/src/drivers/gpu/nvrtx
cc -std=c11 -Wall -Wextra -Werror -I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/gsp-test" \
	"$repo/plan/ws037/tests/nvrtx-gsp-host-test.c" \
	"$src/wire.c" "$src/image.c" "$src/layout.c" "$src/radix3.c" "$src/queue.c" "$src/vbios.c"
if [ -d "$firmware" ]; then
	"$out/gsp-test" "$firmware"
else
	"$out/gsp-test"
fi
