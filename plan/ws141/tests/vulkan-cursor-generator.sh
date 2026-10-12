#!/bin/sh
# ws141-p016 (B) B-u (u14): regenerates libvulkan's recording wrappers with the shared NoctLang interpreter and
# checks that the committed commands-generated.inc is the generator's output, with the seven swapchain-content
# hooks (the generator itself refuses to write without all seven).  Takes about three minutes.
#   sh plan/ws141/tests/vulkan-cursor-generator.sh [output-directory]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${1:-$repo/build/ws141-vulkan-cursor-generator}
mkdir -p "$out"
timeout 900 "$repo/build/NoctLang/build-static/noct" "$repo/userland/desktop/libvulkan/tools/maintain-commands.noct" \
    "$repo/include/libc/vulkan/vulkan_core.h" "$out/commands-generated.inc"
cmp "$out/commands-generated.inc" "$repo/userland/desktop/libvulkan/commands-generated.inc"
test "$(grep -c 'vulkan_content_note_image(command, ' "$out/commands-generated.inc")" = 6
test "$(grep -c 'vulkan_content_note_secondaries(command, commandBufferCount, pCommandBuffers);' "$out/commands-generated.inc")" = 1
echo "vulkan-cursor-generator.sh: PASS"
