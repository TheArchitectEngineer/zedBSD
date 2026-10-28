#!/bin/sh
# Host test of the sun4u console's leaving of the screen: text-end-sun4u-host-test.c (which
# includes the sun4u console and firmware framebuffer) and text-end-sun4u-stubs.c with the real
# src/kern/text-display.c, the generic /dev/graphics front end and the 8x16 font; ordinary, with
# the address and undefined-behaviour sanitizers, and without /dev/graphics
# (CONFIG_DRIVER_GRAPHICS_DEVICE 0); each scene in its own process.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws290/tests/text-end-sun4u-host-test.sh   (from anywhere)
# Output goes to a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/text-end-sun4u-host.XXXXXX")
status=0
scenes='1 2 3 4 5 6 7 8 9 10'
for mode in ordinary address nodevice; do
	extra=
	device="$repo/src/drivers/generic/graphics-device.c"
	case "$mode" in
	address)
		extra='-fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer'
		;;
	nodevice)
		extra='-DCONFIG_DRIVER_GRAPHICS_DEVICE=0'
		device=
		;;
	esac
	# shellcheck disable=SC2086
	timeout 180 cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement $extra \
		-DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE \
		-I"$repo" -I"$repo/include" -I"$repo/src" -I"$repo/include/libc" \
		"$repo/plan/ws290/tests/text-end-sun4u-host-test.c" "$repo/plan/ws290/tests/text-end-sun4u-stubs.c" \
		"$repo/src/kern/text-display.c" "$repo/src/drivers/platform/pcat/graphics/vgafont.c" \
		$device -o "$out/$mode"
	for scene in $scenes; do
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 timeout 60 "$out/$mode" "$scene" || status=1
	done
done
if test "$status" -eq 0; then
	echo "PASS text-end-sun4u-host-test ($out)"
else
	echo "FAIL text-end-sun4u-host-test ($out)"
fi
exit "$status"
