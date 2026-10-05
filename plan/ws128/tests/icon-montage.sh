#!/bin/sh
# ws128-p012: builds icon-dump (userland/desktop/wayland/icons.c unchanged) on the host, draws every icon at 448 pixels
# and makes the montage of the applications' icons (icon-montage.py).
# Usage: plan/ws128/tests/icon-montage.sh [OUT.png] [WORK_DIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws128-p012/montage.png}
work=${2:-$root/build/ws128-p012/icons}
mkdir -p "$work" "$(dirname "$out")"
cc=${CC:-cc}
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root/userland/desktop/wayland" "$root/userland/desktop/wayland/icons.c" \
	"$root/plan/ws128/tests/icon-dump.c" -lm -o "$work/icon-dump"
"$work/icon-dump" 448 "$work"
python3 "$root/plan/ws128/tests/icon-montage.py" "$work" "$out"
echo "icon-montage: $out"
