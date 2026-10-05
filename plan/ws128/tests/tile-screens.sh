#!/bin/sh
# ws128-p012: builds tile-dump (userland/desktop/wayland/icons.c unchanged) on the host, draws every application's tile
# at the sizes the compositor keeps (20, 28, 48, 72) and makes the host pictures of App Home and the bar (tile-screens.py).
# Usage: plan/ws128/tests/tile-screens.sh [OUT_DIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws128-p012/tiles}
mkdir -p "$out/dump"
cc=${CC:-cc}
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root/userland/desktop/wayland" "$root/userland/desktop/wayland/icons.c" \
	"$root/plan/ws128/tests/tile-dump.c" -lm -o "$out/tile-dump"
for size in 20 28 48 72; do
	"$out/tile-dump" "$size" "$out/dump"
done
python3 "$root/plan/ws128/tests/tile-screens.py" "$out/dump" "$out/home.png" "$out/bar.png"
echo "tile-screens: $out/home.png $out/bar.png"
