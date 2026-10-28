#!/bin/sh
# Host test of the console's leaving of the display on the PC/AT (kern_text_end() and the
# text layer's end): text-end-host-test.c with the real src/kern/text-display.c and
# src/drivers/platform/pcat/graphics/text.c, ordinary and with the sanitizers, each scene
# in its own process.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws290/tests/text-end-host-test.sh   (from anywhere)
# Output goes to a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/text-end-host.XXXXXX")
status=0
for mode in ordinary sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer'
	fi
	# shellcheck disable=SC2086
	timeout 120 cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement $extra \
		-DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE -I"$repo/include" -I"$repo/src" -I"$repo/include/libc" \
		"$repo/plan/ws290/tests/text-end-host-test.c" "$repo/src/kern/text-display.c" \
		"$repo/src/drivers/platform/pcat/graphics/text.c" -o "$out/$mode"
	for scene in 1 2 3 4 5 6; do
		UBSAN_OPTIONS=halt_on_error=1 timeout 30 "$out/$mode" "$scene" || status=1
	done
done
if test "$status" -eq 0; then
	echo "PASS text-end-host-test ($out)"
else
	echo "FAIL text-end-host-test ($out)"
fi
exit "$status"
