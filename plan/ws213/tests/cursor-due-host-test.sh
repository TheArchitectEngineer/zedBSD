#!/bin/sh
# Host test of the cursor plane's one present a refresh (ws213-p001 C0).
# Usage: plan/ws213/tests/cursor-due-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws213-host}
mkdir -p "$out"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland \
	plan/ws213/tests/cursor-due-host-test.c userland/desktop/wayland/cursor-plane.c \
	-o "$out/cursor-due-host-test"
"$out/cursor-due-host-test"
