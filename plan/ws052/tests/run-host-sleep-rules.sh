#!/bin/sh
# ws052-p012: builds and runs the host test of the compositor's sleep rules (host-sleep-rules.c with
# userland/desktop/wayland/sleep-rules.c) as gnu89, plainly and under ASan/UBSan, in a new directory under build/tmp
# (nothing is removed here; Q1's plan/tools/q1-clean.sh removes old runs).
#   sh plan/ws052/tests/run-host-sleep-rules.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws052-sleep-rules"
work=$fresh_dir
for mode in plain sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
	fi
	${CC:-cc} -std=gnu89 -Wdeclaration-after-statement -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g $extra \
		-I"$repo" -I"$repo/include" \
		"$repo/plan/ws052/tests/host-sleep-rules.c" "$repo/userland/desktop/wayland/sleep-rules.c" \
		-o "$work/$mode"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 "$work/$mode"
done
echo "WS052 p012 sleep rules host test PASS (plain, ASan/UBSan)"
