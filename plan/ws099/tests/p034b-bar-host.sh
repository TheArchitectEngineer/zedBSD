#!/bin/sh
# ws099-p034b: the host montage of the light bar in the title bar's colours and the docked buttons at the right end: builds tile-dump,
# icon-dump (icons.c unchanged) and p034-mark-dump (mark.c unchanged) on the host, then p034b-bar-host.py.
# Usage: plan/ws099/tests/p034b-bar-host.sh [OUT.png] [WORK_DIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws099-p034b/bar-host.png}
work=${2:-$root/build/ws099-p034b/bar-host}
mkdir -p "$work/tiles" "$work/icons" "$(dirname "$out")"
cc=${CC:-cc}
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root/userland/desktop/wayland" "$root/userland/desktop/wayland/icons.c" \
	"$root/plan/ws128/tests/tile-dump.c" -lm -o "$work/tile-dump"
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root/userland/desktop/wayland" "$root/userland/desktop/wayland/icons.c" \
	"$root/plan/ws128/tests/icon-dump.c" -lm -o "$work/icon-dump"
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root" "$root/userland/desktop/artwork/mark.c" \
	"$root/plan/ws099/tests/p034-mark-dump.c" -lm -o "$work/mark-dump"
"$work/tile-dump" 20 "$work/tiles"
"$work/tile-dump" 26 "$work/tiles"
"$work/icon-dump" 20 "$work/icons"
"$work/mark-dump" 104 "$work/icons"
python3 "$root/plan/ws099/tests/p034b-bar-host.py" "$work/tiles" "$work/icons" "$out"
