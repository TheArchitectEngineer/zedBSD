#!/bin/sh
# Host test of the compositor's joined solid quads (ws211-p003 段 2).
# Usage: plan/ws211/tests/batch-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws211-host}
mkdir -p "$out"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland \
	plan/ws211/tests/batch-host-test.c userland/desktop/wayland/batch.c userland/desktop/wayland/compose-bind.c \
	-o "$out/batch-host-test"
"$out/batch-host-test"
