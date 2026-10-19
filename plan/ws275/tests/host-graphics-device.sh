#!/bin/sh
# Host test of the generic /dev/graphics front end (WS275 p003).
# Usage: plan/ws275/tests/host-graphics-device.sh [OUT_DIR]   (default build/ws275-p003/host)
set -eu
top=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$top/build/ws275-p003/host}
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -I"$top/include" \
  -o "$out/host-graphics-device" \
  "$top/src/drivers/generic/graphics-device.c" "$top/plan/ws275/tests/host-graphics-device.c"
"$out/host-graphics-device"
"$out/host-graphics-device" none
