#!/bin/sh
# ws090-p019: builds the terminal's touch screen (touch.c, with its touch pad inputs) with libkeiland's motion,
# scroller and gestures for the host and runs host-pad-terminal.
#   plan/ws090/tests/host-pad.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws090-pad-host}
cc=${CC:-cc}
mkdir -p "$out/include"
ln -sf "$root/userland/desktop/keiland/keiland.h" "$out/include/keiland.h"
ln -sf "$root/userland/desktop/keiland/keiland-ui.h" "$out/include/keiland-ui.h"
flags="-std=gnu11 -O2 -g -Wall -Wextra -Werror -I$out/include"
for name in motion scroll gesture; do
	"$cc" $flags -c "$root/userland/desktop/libkeiland/$name.c" -o "$out/keiland-$name.o"
done
"$cc" $flags -c "$root/userland/desktop/terminal/touch.c" -o "$out/terminal-touch.o"
"$cc" $flags -c "$root/plan/ws090/tests/host-pad-terminal.c" -o "$out/host-pad-terminal.o"
"$cc" "$out/host-pad-terminal.o" "$out/terminal-touch.o" "$out/keiland-motion.o" "$out/keiland-scroll.o" "$out/keiland-gesture.o" -lm \
	-o "$out/host-pad-terminal"
"$out/host-pad-terminal" > "$out/host-pad-terminal.log" 2>&1 || { grep -E '^(FAIL|host-pad)' "$out/host-pad-terminal.log"; exit 1; }
grep -E '^(FAIL|host-pad)' "$out/host-pad-terminal.log"
grep -E 'KINETIC|caught' "$out/host-pad-terminal.log" | head -5
