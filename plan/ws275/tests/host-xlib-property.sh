#!/bin/sh
# Host test of the libX11 property byte order (WS275 p005).
# Usage: plan/ws275/tests/host-xlib-property.sh [OUT_DIR]   (default build/ws275-p005/host)
# On a little-endian host the old memcpy gave the same bytes; the test
# checks the wire order and the round trip, not a big-endian CPU.
set -eu
top=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$top/build/ws275-p005/host}
mkdir -p "$out/inc"
ln -sfn "$top/include/libc/X11" "$out/inc/X11"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-unused-function \
  -I"$out/inc" -I"$top" -o "$out/host-xlib-property" \
  "$top/plan/ws275/tests/host-xlib-property.c"
"$out/host-xlib-property"
