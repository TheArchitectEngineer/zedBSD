#!/bin/sh
# ws090-p003: builds and runs the host test of libkeiland's scroll, input and text view touch (host-input.c)
# with libkeiland's scroller, gestures and touch motion, on Linux.
#   sh plan/ws090/tests/host-input.sh [OUTPUT]   (default build/ws090/host-input)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws090/host-input}
mkdir -p "$(dirname "$out")/inc"
cp userland/desktop/keiland/truetype.h userland/desktop/keiland/keiland.h userland/desktop/keiland/keiland-ui.h userland/desktop/keiland/keiui.h "$(dirname "$out")/inc/"
U=userland/desktop
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -I"$(dirname "$out")/inc" -I$U/libkeiland/ui \
	plan/ws090/tests/host-input.c \
	$U/libkeiland/ui/canvas.c $U/libkeiland/ui/theme.c $U/libkeiland/ui/input.c $U/libkeiland/ui/scroll.c $U/libkeiland/ui/scroll-bar.c $U/libkeiland/ui/text-touch.c $U/libkeiland/ui/ui.c \
	$U/libkeiland/gesture.c $U/libkeiland/motion.c $U/libkeiland/scroll.c -lm -o "$out"
"$out"
