#!/bin/sh
# Host test of the display list's replay (ws213-p003, p001 9 (a)): the scenes'
# commands are compared with the golden file taken from the direct recording.
# Usage: plan/ws213/tests/list-replay-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws213-host}
mkdir -p "$out"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland \
	plan/ws213/tests/list-replay-host-test.c userland/desktop/wayland/batch.c \
	userland/desktop/wayland/compose-bind.c userland/desktop/wayland/scene.c \
	-o "$out/list-replay-host-test"
"$out/list-replay-host-test" "$out/list-replay.txt"
if ! cmp -s "$out/list-replay.txt" plan/ws213/tests/list-replay.golden; then
	diff -u plan/ws213/tests/list-replay.golden "$out/list-replay.txt" | head -40
	echo "list-replay-host-test: FAIL"
	exit 1
fi
echo "list-replay-host-test: PASS ($(wc -l < "$out/list-replay.txt") lines)"
