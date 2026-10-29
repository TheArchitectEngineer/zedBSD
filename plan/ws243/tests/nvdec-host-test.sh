#!/bin/sh
# Host test of the nvrtx video decoder's parts that touch no hardware (WS243): the picture structure, the slice
# table, the decode push buffer, the work buffers' sizes and the NV12 layout, the scaling matrices, the DPB entries,
# the copy engine's plane copy, the resource manager's parameters and the device table's pick.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws243/tests/nvdec-host-test.sh   (from anywhere)
# The binary goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/ws243-nvdec.XXXXXX")
mkdir -p "$out/client-headers"
ln -s "$repo/include/libc/vulkan" "$out/client-headers/vulkan"
src=$repo/src/drivers/gpu/nvrtx
cc -std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-fsanitize=address,undefined -fno-sanitize=alignment \
	-I"$out/client-headers" -I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/nvdec-host-test" \
	"$repo/plan/ws243/tests/nvdec-host-test.c" \
	"$src/nvdec-records.c" "$src/vk/vk-video-h264.c" "$src/vk/exec-copy.c" "$src/objects.c" "$src/wire.c"
"$out/nvdec-host-test"
