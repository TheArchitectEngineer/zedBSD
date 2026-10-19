#!/bin/sh
# Host test of the sun4u firmware framebuffer's pixel bytes (WS275 p004).
# Usage: plan/ws275/tests/host-firmware-fb.sh [OUT_DIR]   (default build/ws275-p004/host)
set -eu
top=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$top/build/ws275-p004/host}
mkdir -p "$out"
cc -std=gnu11 -Wall -Wextra -Werror -I"$top" -I"$top/include" -I"$top/src" \
  -o "$out/host-firmware-fb" \
  "$top/plan/ws275/tests/host-firmware-fb.c" "$top/src/drivers/platform/pcat/graphics/vgafont.c"
"$out/host-firmware-fb"
