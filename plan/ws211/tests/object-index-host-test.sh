#!/bin/sh
# Host test of the bcm2711 Vulkan session's identity index (ws211-p010, vulkan-object.c),
# then the WS141 driver host test against the changed driver.
# Usage: sh plan/ws211/tests/object-index-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws211-host}
mkdir -p "$out"
cc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror -Iinclude -Isrc -I. \
	plan/ws211/tests/object-index-host-test.c src/drivers/gpu/bcm2711/vulkan-object.c \
	-o "$out/object-index-host-test"
"$out/object-index-host-test"

# The WS141 driver test, with the two symbols its link lacks (ws211-tests/ws141-link-stubs.c).
. plan/ws141/tests/vulkan-sources.sh
mkdir -p "$out/client-headers"
if [ ! -e "$out/client-headers/vulkan" ]; then
	ln -s "$(pwd)/include/libc/vulkan" "$out/client-headers/vulkan"
fi
cc -std=c99 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -include time.h -Wall -Wextra -Werror \
	-ffunction-sections -fdata-sections -Iplan/ws141/tests/host -I"$out/client-headers" -Iinclude -Isrc -I. \
	plan/ws141/tests/vulkan-device-host-test.c plan/ws141/tests/vulkan-client-pipeline-host.c plan/ws141/tests/display-lock-host.c \
	plan/ws211/tests/ws141-link-stubs.c \
	src/drivers/gpu/bcm2711/v3d-memory.c src/drivers/gpu/bcm2711/mmu.c \
	userland/desktop/libvulkan/wire.c userland/desktop/libvulkan/codec.c userland/desktop/libvulkan/objects.c \
	$ws141_vulkan_sources -Wl,--gc-sections -o "$out/vulkan-device-host-test"
"$out/vulkan-device-host-test"
