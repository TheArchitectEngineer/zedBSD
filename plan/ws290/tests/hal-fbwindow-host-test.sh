#!/bin/sh
# Host test of the amd64 HAL's early console framebuffer window (given to the
# kernel's console after kernel_putc, removed with a global TLB shootdown):
# hal-fbwindow-host-test.c with the real src/hal/amd64/space.c (included),
# framebuffer-map.c and bsp-pcat/cons.c (its output fixture), ordinary and with
# the sanitizers, each scene in its own process.  x86-64 Linux host only (the
# test points GS at a simulated per-CPU record with arch_prctl).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws290/tests/hal-fbwindow-host-test.sh   (from anywhere)
# Output goes to a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/hal-fbwindow-host.XXXXXX")
status=0
passed=0
total=0
for mode in ordinary sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer'
	fi
	# shellcheck disable=SC2086
	if ! timeout 300 cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement $extra \
		-DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DHAL_PCAT_DEBUGCON -DKERN_USER_ABI_LP64 \
		-DKERN_CONSOLE_OUTPUT_TEST \
		-I"$repo" -I"$repo/include" -I"$repo/src" \
		"$repo/plan/ws290/tests/hal-fbwindow-host-test.c" \
		"$repo/src/hal/amd64/framebuffer-map.c" \
		"$repo/src/hal/amd64/bsp-pcat/cons.c" \
		-o "$out/$mode" >"$out/$mode.build.log" 2>&1; then
		echo "FAIL build $mode (see $out/$mode.build.log)"
		status=1
		continue
	fi
	for scene in 1 2 3 4 5 6 7; do
		total=$((total + 1))
		if UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 timeout 60 "$out/$mode" "$scene" \
			>"$out/$mode.$scene.log" 2>&1; then
			passed=$((passed + 1))
			echo "$mode: $(tail -n 1 "$out/$mode.$scene.log")"
		else
			echo "$mode: FAIL scene $scene (see $out/$mode.$scene.log)"
			status=1
		fi
	done
done
if test "$status" -eq 0; then
	echo "PASS hal-fbwindow-host-test $passed/$total ($out)"
else
	echo "FAIL hal-fbwindow-host-test $passed/$total ($out)"
fi
exit "$status"
