#!/bin/sh
# Host test of the nvrtx Vulkan executor's parts that touch no hardware (ws037-p007 stage D2):
# the object registry, the session and the transport, fed by libvulkan's own wire writer.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws037/tests/nvrtx-vk-host-test.sh   (from the repository root)
# The binary goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-vk.XXXXXX")
mkdir -p "$out/client-headers"
ln -s "$repo/include/libc/vulkan" "$out/client-headers/vulkan"
src=$repo/src/drivers/gpu/nvrtx
cc -std=gnu11 -D_POSIX_C_SOURCE=200809L -DKERN_USER_ABI_LP64 -include time.h -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Wl,--gc-sections \
	-I"$out/client-headers" -I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/vk-test" \
	"$repo/plan/ws037/tests/nvrtx-vk-host-test.c" \
	"$src/vk/vk-object.c" "$src/vk/vk-session.c" "$src/vk/vk-stream.c" "$src/vk/vk-dispatch.c" "$src/vk/vk-device.c" \
	"$src/vk/vk-reply.c" "$src/vk/vk-memory.c" "$src/vk/vk-resource.c" "$src/vk/vk-layout.c" "$src/vk/vk-target.c" "$src/vk/vk-pipeline.c" \
	"$src/vk/vk-command.c" "$src/vk/vk-record.c" "$src/vk/vk-execute.c" "$src/vk/vk-queue.c" "$src/vk/exec-push.c" "$src/draw-push.c" "$src/wire.c" \
	"$src/compiler/sm75"/*.c "$repo/src/drivers/gpu/compiler/spirv.c" \
	"$repo/src/drivers/gpu/compiler/optimize.c" \
	"$repo/src/drivers/gpu/i915/vk/vk-codec.c" "$repo/userland/desktop/libvulkan/wire.c" -lm
"$out/vk-test"
