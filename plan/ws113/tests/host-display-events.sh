#!/bin/sh
# ws113-p003: builds and runs the host test of libvulkan's display events and power
# (host-display-events.c with wsi-display-control.c), as gnu89 (the Khronos headers use line comments),
# plainly and under ASan/UBSan, in a new directory under build/tmp (nothing is removed here; Q1's
# plan/tools/q1-clean.sh removes old runs).
#   sh plan/ws113/tests/host-display-events.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws113-display-events"
work=$fresh_dir
mkdir "$work/include"
ln -s "$repo/include/libc/vulkan" "$work/include/vulkan"
library="$repo/userland/desktop/libvulkan"
for mode in plain sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
	fi
	${CC:-cc} -std=gnu89 -Wdeclaration-after-statement -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g $extra \
		-I"$work/include" -I"$repo/include" -I"$library" \
		"$repo/plan/ws113/tests/host-display-events.c" "$library/wsi-display-control.c" \
		-pthread -o "$work/$mode"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 "$work/$mode"
done
echo "WS113 p003 display events host test PASS (plain, ASan/UBSan)"
