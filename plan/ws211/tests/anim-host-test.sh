#!/bin/sh
# Host test of the dock animation's pictures (ws211-p007, layer-cache.c).
# Usage: plan/ws211/tests/anim-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws211-host}
mkdir -p "$out"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland \
	plan/ws211/tests/anim-host-test.c userland/desktop/wayland/layer-cache.c \
	-o "$out/anim-host-test"
"$out/anim-host-test"
