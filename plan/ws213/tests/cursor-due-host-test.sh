#!/bin/sh
# Host test of the cursor thread (ws213-p004, the design's 9 (i) and 9 (m) for
# the cursor thread; the one present a refresh of ws213-p001 C0): the steps one
# at a time, then the real thread, once plainly and once under ThreadSanitizer.
# Usage: plan/ws213/tests/cursor-due-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws213-host}
mkdir -p "$out"
sources="plan/ws213/tests/cursor-due-host-test.c userland/desktop/wayland/cursor-plane.c \
	userland/desktop/wayland/cursor-thread.c userland/desktop/wayland/cursor-box.c \
	userland/desktop/wayland/plane.c"
flags="-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland"
# shellcheck disable=SC2086
cc $flags -O1 $sources -lpthread -o "$out/cursor-due-host-test"
timeout 120 "$out/cursor-due-host-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=thread $sources -lpthread -o "$out/cursor-due-host-test-tsan"
TSAN_OPTIONS="halt_on_error=1 exitcode=66" timeout 120 "$out/cursor-due-host-test-tsan"
# The cursor thread's objects are its own: its command buffers come from its pool, never the frames' (review-4 H-C).
if grep -n 'compose->pool' userland/desktop/wayland/cursor-plane.c userland/desktop/wayland/cursor-thread.c; then
	echo "cursor-due-host-test: FAIL (the cursor plane uses the frames' command pool)"
	exit 1
fi
echo "cursor-due-host-test: PASS (plain and ThreadSanitizer)"
