#!/bin/sh
# ws181-p007: the host sheet of the arrangement menu's glass and its opening (p007-host.py over p005-host.py and
# ws099-p034's host renderer).  Builds p034's dumps and p005-icon-dump (arrange.c's slots) on the host.
#   sh plan/ws181/tests/p007-host.sh [OUT.png] [WORK_DIR]   (default build/review/ws181-p007.png, build/ws181-p007-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/review/ws181-p007.png}
work=${2:-$root/build/ws181-p007-host}
mkdir -p "$work" "$(dirname "$out")"
sh "$root/plan/ws099/tests/p034-bar-host.sh" "$work/p034.png" "$work/p034" > /dev/null
cc=${CC:-cc}
$cc -std=gnu11 -O1 -Wall -Wextra -Werror -I "$root/userland/desktop/wayland" "$root/userland/desktop/wayland/arrange.c" \
	"$root/plan/ws181/tests/p005-icon-dump.c" -o "$work/icon-dump"
"$work/icon-dump" > "$work/slots.txt"
python3 "$root/plan/ws181/tests/p007-host.py" "$work/p034/tiles" "$work/p034/icons" "$work/slots.txt" "$out"
