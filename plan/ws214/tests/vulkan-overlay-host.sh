#!/bin/sh
# ws214-p004: host test of libvulkan's overlay planes (wsi-swapchain.c, wsi-display.c, content.c) with the
# real display platform over a fake kernel; ordinary and sanitized, with the overlay log off and on
# (VULKAN_ZEDBSD_OVERLAY_LOG=1).
#   sh plan/ws214/tests/vulkan-overlay-host.sh [output-directory]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${1:-$repo/build/ws214-vulkan-overlay-host}
mkdir -p "$out/headers"
if test ! -e "$out/headers/vulkan"; then
	ln -s "$repo/include/libc/vulkan" "$out/headers/vulkan"
fi
pattern='^WSI OVERLAY present plane=[0-9]* serial=[0-9]* ticket=[0-9]* min_primary=[0-9]* path=[a-z]* why=[a-z]* result=-\{0,1\}[0-9]* sequence=[0-9]* adopted=[0-9]* call_ns=[0-9]*$'
for mode in ordinary sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-omit-frame-pointer'
	fi
	# shellcheck disable=SC2086
	cc -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror $extra \
	    -I"$repo" -I"$out/headers" -I"$repo/include" -I"$repo/userland/desktop/include" \
	    "$repo/plan/ws214/tests/vulkan-overlay-host.c" -lpthread -o "$out/$mode"
	env -u VULKAN_ZEDBSD_OVERLAY_LOG ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	    timeout 60 "$out/$mode" > "$out/$mode-off.out" 2> "$out/$mode-off.log"
	test "$(grep -c '^WSI OVERLAY ' "$out/$mode-off.log" || true)" = 0
	VULKAN_ZEDBSD_OVERLAY_LOG=1 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
	    timeout 60 "$out/$mode" > "$out/$mode-on.out" 2> "$out/$mode-on.log"
	grep -q 'vulkan-overlay-host: PASS' "$out/$mode-on.out"
	lines=$(grep -c '^WSI OVERLAY ' "$out/$mode-on.log" || true)
	other=$(grep -v "$pattern" "$out/$mode-on.log" | grep -c . || true)
	echo "$mode: overlay log lines=$lines unexpected=$other"
	test "$other" = 0
	# Every path appears; a fast commit names no failed condition; expiry, idle flush and ENOSPC are named.
	for path in held replaced fast lane commit flush; do
		grep -q " path=$path " "$out/$mode-on.log"
	done
	test "$(grep ' path=fast ' "$out/$mode-on.log" | grep -v ' why=nospace ' | grep -vc ' why=none ' || true)" = 0
	grep -q ' path=flush why=expired ' "$out/$mode-on.log"
	grep -q ' path=flush why=flush ' "$out/$mode-on.log"
	grep -q ' path=flush why=destroy ' "$out/$mode-on.log"
	grep -q ' path=commit why=nospace ' "$out/$mode-on.log"
	grep -q ' path=fast why=nospace ' "$out/$mode-on.log"
done
echo "vulkan-overlay-host.sh: PASS"
