#!/bin/sh
# Host test of a window commit's damage (ws211-p008, damage.c), its rectangles (p009) and the cut around opaque shapes (p011).
# Usage: plan/ws211/tests/damage-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws211-host}
mkdir -p "$out"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland \
	plan/ws211/tests/damage-host-test.c userland/desktop/wayland/damage.c \
	-lm -o "$out/damage-host-test"
"$out/damage-host-test"
