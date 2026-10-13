#!/bin/sh
# BUG-296: builds Notes' touch screen (touch.c) with libkeiland's motion, scroll and gestures for the host -- plain, with
# ASan and with UBSan -- and runs host-notes-zoom-limit (the far pinch on the 5330's window against the i915
# executor's 16 MiB allocations, and the zoom held to the picture the device holds).
#   sh plan/ws079/tests/run-notes-zoom-limit.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname -- "$0")/../../.." && pwd)
cd "$root"
out=build/bug296-host
cc=${CC:-clang}
# Each run gets a new directory behind the fixed name (2026-10-06 user: deleting is Q1's step, so this script removes
# nothing; plan/tools/q1-clean.sh removes the old runs).
. plan/tools/fresh-out.sh
fresh_out "$out"

# Only <keiland/keiland.h> is taken from the desktop's headers: include/libc is zedBSD's C library.
mkdir -p "$out/include/keiland"
ln -sf "$root/userland/desktop/include/keiland/keiland.h" "$out/include/keiland/keiland.h"

for variant in plain asan ubsan; do
	extra=
	if [ "$variant" = asan ]; then
		extra="-fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		extra="-fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion $extra -I$out/include"
	for name in motion scroll gesture; do
		"$cc" $flags -c "userland/desktop/libkeiland/$name.c" -o "$out/keiland-$name-$variant.o"
	done
	for name in scroll scroll-bar canvas; do
		"$cc" $flags -c "userland/desktop/libkeiland/ui/$name.c" -o "$out/keiland-ui-$name-$variant.o"
	done
	"$cc" $flags -c userland/desktop/notes/touch.c -o "$out/touch-$variant.o"
	"$cc" $flags -Wno-conversion -c plan/ws079/tests/host-notes-zoom-limit.c -o "$out/host-$variant.o"
	"$cc" $extra "$out/host-$variant.o" "$out/touch-$variant.o" "$out/keiland-motion-$variant.o" "$out/keiland-scroll-$variant.o" \
		"$out/keiland-gesture-$variant.o" "$out/keiland-ui-scroll-$variant.o" "$out/keiland-ui-scroll-bar-$variant.o" \
		"$out/keiland-ui-canvas-$variant.o" -lm -o "$out/host-notes-zoom-limit-$variant"
	timeout 60 "$out/host-notes-zoom-limit-$variant" > "$out/$variant.log" 2>&1 || {
		grep -E '^(FAIL|host-notes-zoom-limit)|runtime error|ERROR' "$out/$variant.log"
		echo "run-notes-zoom-limit: $variant FAILED"
		exit 1
	}
	grep -E '^(FAIL|host-notes-zoom-limit|NOTES TOUCH picture limit)' "$out/$variant.log" | sed "s/^/$variant: /"
done
echo "run-notes-zoom-limit: PASS"
