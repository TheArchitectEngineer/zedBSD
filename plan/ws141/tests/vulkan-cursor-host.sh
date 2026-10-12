#!/bin/sh
# ws141-p016 (B) B-u: host test of libvulkan's cursor-plane presentation (wsi-swapchain.c, wsi-display.c,
# content.c) with fake core calls, platform and display ioctl; ordinary and sanitized, with the cursor log
# off and on (VULKAN_ZEDBSD_CURSOR_LOG=1, u12).
#   sh plan/ws141/tests/vulkan-cursor-host.sh [output-directory]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${1:-$repo/build/ws141-vulkan-cursor-host}
mkdir -p "$out/headers"
if test ! -e "$out/headers/vulkan"; then
	ln -s "$repo/include/libc/vulkan" "$out/headers/vulkan"
fi
for mode in ordinary sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-omit-frame-pointer'
	fi
	# shellcheck disable=SC2086
	cc -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror $extra \
	    -I"$repo" -I"$out/headers" -I"$repo/include" -I"$repo/userland/desktop/include" \
	    "$repo/plan/ws141/tests/vulkan-cursor-host.c" -lpthread -o "$out/$mode"
	env -u VULKAN_ZEDBSD_CURSOR_LOG ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	    timeout 60 "$out/$mode" 2> "$out/$mode-off.log"
	test "$(grep -c '^VK CURSOR ' "$out/$mode-off.log" || true)" = 0
	VULKAN_ZEDBSD_CURSOR_LOG=1 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	    timeout 60 "$out/$mode" 2> "$out/$mode-on.log"
	lines=$(grep -c '^VK CURSOR ' "$out/$mode-on.log" || true)
	fast=$(grep -c '^VK CURSOR .* path=fast ' "$out/$mode-on.log" || true)
	other=$(grep -v '^VK CURSOR ticket=[0-9]* path=[a-z]* result=-\{0,1\}[0-9]* outcome=[a-z]* image=[0-9]* x=-\{0,1\}[0-9]* y=-\{0,1\}[0-9]* depth=[0-9]* call_ns=[0-9]* mutex=[0-9]* queue=[0-9]* publish=[0-9]* fence=[0-9]* lock=[0-9]* set=[0-9]* done=[0-9]* retries=[0-9]*$' "$out/$mode-on.log" | grep -c . || true)
	echo "$mode: cursor log lines=$lines fast=$fast unexpected=$other"
	test "$lines" -gt 20
	test "$fast" -gt 5
	test "$other" = 0
done
echo "vulkan-cursor-host.sh: PASS"
