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

# The WS141 driver host test against the changed driver.
sh plan/ws141/tests/vulkan-device-host-test.sh "$out/ws141-vulkan-device"
