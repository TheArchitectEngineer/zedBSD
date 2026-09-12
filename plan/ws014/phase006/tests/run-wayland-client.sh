#!/bin/sh
# zedBSD; Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Runs the independent peer against the actual client sources and sanitizer build.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
cd "$repo"
output=${1:-build/q309-wayland-client}
mkdir -p "$output"
sources='userland/base/libwayland/client.c userland/base/libwayland/proxy.c userland/base/libwayland/wire.c userland/base/libwayland/event.c userland/base/libwayland/protocol.c userland/base/libwayland/utility.c'
cc -std=c99 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-cast-function-type \
 -Ilibc/include/wayland -idirafter libc/include -pthread $sources \
 plan/ws014/phase006/tests/wayland-client.c -o "$output/wayland-client"
timeout 30 "$output/wayland-client"
cc -std=c99 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-cast-function-type \
 -Ilibc/include/wayland -idirafter libc/include -pthread \
 -fsanitize=address,undefined -fno-omit-frame-pointer -g $sources \
 plan/ws014/phase006/tests/wayland-client.c -o "$output/wayland-client-asan"
ASAN_OPTIONS=detect_leaks=1 timeout 30 "$output/wayland-client-asan"
