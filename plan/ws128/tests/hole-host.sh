#!/bin/sh
# BUG-237: builds tile-dump (userland/desktop/wayland/icons.c unchanged) on the host, draws the tiles at 20, 28, 48 and 72
# and checks what their cut-out pictures show over App Home, the light bar and Alt+Tab (hole-host.py).
# Usage: plan/ws128/tests/hole-host.sh [OUT_DIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws128-p012/hole}
mkdir -p "$out/dump"
cc=${CC:-cc}
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root/userland/desktop/wayland" "$root/userland/desktop/wayland/icons.c" \
	"$root/plan/ws128/tests/tile-dump.c" -lm -o "$out/tile-dump"
for size in 20 28 48 72; do
	"$out/tile-dump" "$size" "$out/dump"
done
python3 "$root/plan/ws128/tests/hole-host.py" "$out/dump" "$out/bug237.png"
