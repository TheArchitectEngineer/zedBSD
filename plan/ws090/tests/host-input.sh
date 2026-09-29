#!/bin/sh
# ws090-p003: builds and runs the host test of libkeiui's scroll, input and text view touch (host-input.c)
# with libkeiland's scroller, gestures and touch motion, on Linux.
#   sh plan/ws090/tests/host-input.sh [OUTPUT]   (default build/ws090/host-input)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws090/host-input}
mkdir -p "$(dirname "$out")/inc"
cp include/libc/truetype.h include/libc/keiland.h include/libc/keiui.h "$(dirname "$out")/inc/"
U=userland/desktop
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -I"$(dirname "$out")/inc" -I$U/libkeiui \
	plan/ws090/tests/host-input.c \
	$U/libkeiui/canvas.c $U/libkeiui/theme.c $U/libkeiui/input.c $U/libkeiui/scroll.c $U/libkeiui/text-touch.c $U/libkeiui/ui.c \
	$U/libkeiland/gesture.c $U/libkeiland/motion.c $U/libkeiland/scroll.c -lm -o "$out"
"$out"
