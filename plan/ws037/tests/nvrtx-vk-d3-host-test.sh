#!/bin/sh
# Host test of the nvrtx Vulkan executor's objects (ws037-p007 stage D3): the commands as libvulkan
# writes them (its wire writer and its codec's encoders) through the real dispatcher; the node is faked.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws037/tests/nvrtx-vk-d3-host-test.sh   (from the repository root)
# The binary goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-vk-d3.XXXXXX")
mkdir -p "$out/client-headers"
ln -s "$repo/include/libc/vulkan" "$out/client-headers/vulkan"
src=$repo/src/drivers/gpu/nvrtx
# The compositor's quad shaders (glslc).
for source in "$repo/userland/desktop/wayland/shaders/quad.vert" "$repo/userland/desktop/wayland/shaders/quad.frag"; do
	name=$(basename "$source" | tr . -)
	glslc --target-env=vulkan1.0 -o "$out/$name.spv" "$source"
done
# The two codecs share a file name: libvulkan's goes through a copy of its own name.
cp "$repo/userland/desktop/libvulkan/codec.c" "$out/client-codec.c"
cc -std=gnu11 -D_POSIX_C_SOURCE=200809L -DKERN_USER_ABI_LP64 -include time.h -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Wl,--gc-sections \
	-I"$out/client-headers" -I"$repo/include" -I"$repo/src" -I"$repo" -I"$repo/userland/desktop/libvulkan" \
	-o "$out/vk-d3-test" \
	"$repo/plan/ws037/tests/nvrtx-vk-d3-host-test.c" \
	"$src/vk-object.c" "$src/vk-session.c" "$src/vk-stream.c" "$src/vk-dispatch.c" "$src/vk-device.c" \
	"$src/vk-reply.c" "$src/vk-memory.c" "$src/vk-resource.c" "$src/vk-layout.c" "$src/vk-target.c" "$src/vk-pipeline.c" \
	"$src/vk-command.c" "$src/vk-record.c" "$src/vk-execute.c" "$src/vk-queue.c" "$src/exec-push.c" "$src/draw-push.c" "$src/wire.c" \
	"$src/compiler/sm75.c" "$repo/src/drivers/gpu/compiler/spirv.c" \
	"$repo/src/drivers/gpu/compiler/optimize.c" \
	"$repo/src/drivers/gpu/i915/render/codec.c" "$repo/userland/desktop/libvulkan/wire.c" "$out/client-codec.c" -lm
"$out/vk-d3-test" "$out"
