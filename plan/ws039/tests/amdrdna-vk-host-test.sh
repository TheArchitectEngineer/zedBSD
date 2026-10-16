#!/bin/sh
# Host test of the amdrdna Vulkan executor (ws039-p008): the commands as libvulkan writes them through the real
# dispatcher, the node faked; the indirect buffers decoded packet by packet, and every draw replayed on the
# GFX10 interpreter of the compiler's tests.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/amdrdna-vk-host-test.sh   (from anywhere)
# Needs cc and glslc.  Everything goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws039/tests"
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/amdrdna-vk.XXXXXX")
mkdir -p "$out/client-headers"
ln -s "$repo/include/libc/vulkan" "$out/client-headers/vulkan"
vk=$repo/src/drivers/gpu/amdrdna/vk
compiler=$repo/src/drivers/gpu/amdrdna/compiler
# The compositor's quad shaders and a compute module (glslc).
for source in "$repo/userland/desktop/wayland/shaders/quad.vert" "$repo/userland/desktop/wayland/shaders/quad.frag"; do
	name=$(basename "$source" | tr . -)
	glslc --target-env=vulkan1.0 -o "$out/$name.spv" "$source"
done
glslc --target-env=vulkan1.0 -fshader-stage=compute -o "$out/ids.spv" "$tests/shaders/ids.comp"
# The two codecs share a file name: libvulkan's goes through a copy of its own name.
cp "$repo/userland/desktop/libvulkan/codec.c" "$out/client-codec.c"
cc -std=gnu11 -O1 -g -D_POSIX_C_SOURCE=200809L -DKERN_USER_ABI_LP64 -include time.h -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize=alignment -ffunction-sections -fdata-sections -Wl,--gc-sections \
	-I"$out/client-headers" -I"$repo/include" -I"$repo/src" -I"$repo" -I"$repo/userland/desktop/libvulkan" \
	-o "$out/amdrdna-vk-host-test" \
	"$tests/amdrdna-vk-host-test.c" "$tests/gfx10-machine.c" "$tests/gfx10-texture.c" \
	"$vk/vk-object.c" "$vk/vk-session.c" "$vk/vk-stream.c" "$vk/vk-dispatch.c" "$vk/vk-device.c" \
	"$vk/vk-reply.c" "$vk/vk-memory.c" "$vk/vk-resource.c" "$vk/vk-layout.c" "$vk/vk-target.c" "$vk/vk-pipeline.c" \
	"$vk/vk-command.c" "$vk/vk-record.c" "$vk/vk-execute.c" "$vk/vk-queue.c" "$vk/vk-pm4.c" "$vk/vk-state.c" \
	"$vk/vk-descriptor.c" "$vk/vk-quad.c" "$vk/vk-float.c" \
	"$compiler/gfx10-compile.c" "$compiler/gfx10-wait.c" "$compiler/gfx10-encode.c" "$compiler/gfx10-program.c" \
	"$repo/src/drivers/gpu/compiler/spirv.c" "$repo/src/drivers/gpu/compiler/optimize.c" \
	"$repo/src/drivers/gpu/i915/render/codec.c" "$repo/userland/desktop/libvulkan/wire.c" "$out/client-codec.c" -lm
if "$out/amdrdna-vk-host-test" "$out"; then
	echo "PASS amdrdna-vk-host-test ($out)"
else
	echo "FAIL amdrdna-vk-host-test ($out)"
	exit 1
fi
