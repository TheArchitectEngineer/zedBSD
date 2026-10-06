#!/bin/sh
# ws099-p035a: the host mock of App Home's stage in three strengths (p035-stage-mock.py): builds tile-dump
# (userland/desktop/wayland/icons.c unchanged) on the host, draws the tiles at 72 px, then the mock.
# Usage: plan/ws099/tests/p035-stage-mock.sh [OUT_DIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws099-p035a}
mkdir -p "$out/dump"
cc=${CC:-cc}
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root/userland/desktop/wayland" "$root/userland/desktop/wayland/icons.c" \
	"$root/plan/ws128/tests/tile-dump.c" -lm -o "$out/tile-dump"
"$out/tile-dump" 72 "$out/dump"
python3 "$root/plan/ws099/tests/p035-stage-mock.py" "$out/dump" "$out"
