#!/bin/sh
# ws113-p004b: builds and runs the host test of the arithmetic and the file of several displays (host-displays.c with
# userland/desktop/wayland/displays.c) as gnu89, plainly and under ASan/UBSan, in a new directory under build/tmp (nothing
# is removed here; Q1's plan/tools/q1-clean.sh removes old runs).
#   sh plan/ws113/tests/host-displays.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws113-displays"
work=$fresh_dir
for mode in plain sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
	fi
	${CC:-cc} -std=gnu89 -Wdeclaration-after-statement -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g $extra \
		-I"$repo/userland/desktop/wayland" \
		"$repo/plan/ws113/tests/host-displays.c" "$repo/userland/desktop/wayland/displays.c" -lm \
		-o "$work/$mode"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 "$work/$mode"
done
echo "WS113 p004b displays host test PASS (plain, ASan/UBSan)"
