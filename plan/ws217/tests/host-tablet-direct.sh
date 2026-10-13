#!/bin/sh
# ws217-p003 (T1-592 P3-3): builds and runs the host test of a pen on the display (host-tablet-direct.c with
# userland/desktop/wayland/tablet.c, cursor-box.c, plane.c and rotate.c, the seat faked) as gnu89, plainly and under
# ASan/UBSan, in a new directory under build/tmp (nothing is removed here; Q1's plan/tools/q1-clean.sh removes old runs).
#   sh plan/ws217/tests/host-tablet-direct.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws217-tablet-direct"
work=$fresh_dir
wayland=$repo/userland/desktop/wayland
for mode in plain sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
	fi
	${CC:-cc} -std=gnu89 -Wdeclaration-after-statement -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g $extra \
		-I"$repo" -I"$repo/include" -I"$wayland" \
		"$repo/plan/ws217/tests/host-tablet-direct.c" "$wayland/tablet.c" "$wayland/cursor-box.c" \
		"$wayland/plane.c" "$wayland/rotate.c" -lpthread -lm -o "$work/$mode"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$work/$mode" >"$work/$mode.out"
	tail -n 1 "$work/$mode.out"
done
echo "WS217 p003 tablet direct host test PASS (plain, ASan/UBSan)"
