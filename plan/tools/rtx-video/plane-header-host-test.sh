#!/bin/sh
# Host test of the texture headers of a video picture's planes (R8 and R8G8, block linear in blocks two GOBs high)
# against the NV12 layout the decoder writes.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/tools/rtx-video/plane-header-host-test.sh   (from anywhere)
# The binary goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/ws243-plane.XXXXXX")
src=$repo/src/drivers/gpu/nvrtx
cc -std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-fsanitize=address,undefined -fno-sanitize=alignment -ffunction-sections -fdata-sections -Wl,--gc-sections \
	-I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/plane-header-host-test" \
	"$repo/plan/tools/rtx-video/plane-header-host-test.c" \
	"$src/vk/exec-push.c" "$src/vk/vk-video-h264.c" "$src/nvdec-records.c" "$src/objects.c" "$src/wire.c"
"$out/plane-header-host-test"
