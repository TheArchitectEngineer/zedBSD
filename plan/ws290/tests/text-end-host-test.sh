#!/bin/sh
# Host test of the console's leaving of the display on the PC/AT (kern_text_end()):
# text-end-host-test.c with the real src/kern/text-display.c and text.c (the text layer
# alone), and text-end-graphics-host-test.c with the real text-display.c, text.c,
# pcat-graphics.c, backend.c, font.c, vgafont.c and splash.c over text-end-stubs.c
# (/dev/graphics and the backend) and text-end-host-os.c (the host's pages and threads, built
# with the host's headers alone), once more built with KERN_TEST_TEXT_END_SECONDS for
# the console's own end; ordinary and with the sanitizers, each scene in its own process.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws290/tests/text-end-host-test.sh   (from anywhere)
# Output goes to a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests=$repo/plan/ws290/tests
graphics=$repo/src/drivers/platform/pcat/graphics
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/text-end-host.XXXXXX")
status=0
flags='-std=gnu11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement -DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE'
scenes='draw end quiet graphics unregistered unmap-fail unmap-fail-apart window-busy window-map-busy vga vga-apart moved moved-fail order-bars-first order-null-first concurrent'
for mode in ordinary sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer'
	fi

	# The text layer alone.
	# shellcheck disable=SC2086
	timeout 120 cc $flags $extra -I"$repo/include" -I"$repo/src" -I"$repo/include/libc" \
		"$tests/text-end-host-test.c" "$repo/src/kern/text-display.c" \
		"$graphics/text.c" -o "$out/text-$mode"
	for scene in 1 2 3 4 5 6; do
		UBSAN_OPTIONS=halt_on_error=1 timeout 30 "$out/text-$mode" "$scene" || status=1
	done

	# The host's services, with the host's headers only.
	# shellcheck disable=SC2086
	timeout 120 cc $flags $extra -c "$tests/text-end-host-os.c" -o "$out/host-os-$mode.o"

	# The text layer with /dev/graphics and the backend, without and with the console's own end.
	for trigger in plain trigger; do
		define=
		if test "$trigger" = trigger; then
			define='-DKERN_TEST_TEXT_END_SECONDS=3'
		fi
		# shellcheck disable=SC2086
		timeout 180 cc $flags $extra $define -pthread -Wl,--wrap=kern_text_resume \
			-I"$repo/include" -I"$repo/src" -I"$repo/include/libc" \
			"$tests/text-end-graphics-host-test.c" "$tests/text-end-stubs.c" \
			"$tests/text-end-text-peek.c" "$repo/src/kern/text-display.c" \
			"$graphics/pcat-graphics.c" "$graphics/backend.c" "$graphics/font.c" \
			"$graphics/vgafont.c" "$graphics/splash.c" "$out/host-os-$mode.o" \
			-o "$out/graphics-$trigger-$mode"
	done
	for scene in $scenes; do
		UBSAN_OPTIONS=halt_on_error=1 timeout 30 "$out/graphics-plain-$mode" "$scene" || status=1
	done
	for scene in trigger trigger-none; do
		UBSAN_OPTIONS=halt_on_error=1 timeout 30 "$out/graphics-trigger-$mode" "$scene" || status=1
	done
done
if test "$status" -eq 0; then
	echo "PASS text-end-host-test ($out)"
else
	echo "FAIL text-end-host-test ($out)"
fi
exit "$status"
