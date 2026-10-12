#!/bin/sh
# Host test of the compositor's binds on change (ws211-p003 段 1).
# Usage: plan/ws211/tests/bind-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws211-host}
mkdir -p "$out"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland \
	plan/ws211/tests/bind-host-test.c userland/desktop/wayland/compose-bind.c \
	-o "$out/bind-host-test"
"$out/bind-host-test"
