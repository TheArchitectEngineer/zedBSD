#!/bin/sh
# Host test of the sun4u console's cursor (WS275 p004, T1-745).
# Usage: plan/ws275/tests/host-console-cursor.sh [OUT_DIR]   (default build/ws275-p004/host)
set -eu
top=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$top/build/ws275-p004/host}
mkdir -p "$out"
cc -std=gnu11 -DKERN_UAPI_NATIVE -Wall -Wextra -Werror -I"$top" -I"$top/include" -I"$top/src" \
  -o "$out/host-console-cursor" \
  "$top/plan/ws275/tests/host-console-cursor.c" "$top/src/drivers/platform/pcat/graphics/vgafont.c"
"$out/host-console-cursor"
