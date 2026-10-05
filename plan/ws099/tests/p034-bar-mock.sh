#!/bin/sh
# ws099-p034: the system bar's mock without QEMU: builds icon-dump and p034-mark-dump on the host (the compositor's own
# icons.c and the Kei mark's mark.c, unchanged), then draws the mock sheet and the whole screen (p034-bar-mock.py).
# Usage: plan/ws099/tests/p034-bar-mock.sh [OUT.png] [WORK_DIR]     (OUT-screen.png is written beside OUT.png)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws099-p034/mock.png}
work=${2:-$root/build/ws099-p034/icons}
mkdir -p "$work" "$(dirname "$out")"
cc=${CC:-cc}
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root/userland/desktop/wayland" "$root/userland/desktop/wayland/icons.c" \
	"$root/plan/ws128/tests/icon-dump.c" -lm -o "$work/icon-dump"
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root" "$root/userland/desktop/artwork/mark.c" \
	"$root/plan/ws099/tests/p034-mark-dump.c" -lm -o "$work/mark-dump"
"$work/icon-dump" 448 "$work"
"$work/mark-dump" 104 "$work"
python3 "$root/plan/ws099/tests/p034-bar-mock.py" "$work" "$out"
echo "p034-bar-mock: $out"
