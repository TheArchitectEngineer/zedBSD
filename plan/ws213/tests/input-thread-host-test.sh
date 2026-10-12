#!/bin/sh
# Host test of the input thread and the cursor's box (ws213-p004, the design's
# 9 (k), (k1) to (k4), and 9 (m) for the input thread): the passes one at a
# time, then the real thread against a writer, once plainly and once under
# ThreadSanitizer (which must report nothing).
# Usage: plan/ws213/tests/input-thread-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws213-host}
mkdir -p "$out"
sources="plan/ws213/tests/input-thread-host-test.c userland/desktop/wayland/input-thread.c \
	userland/desktop/wayland/cursor-box.c userland/desktop/wayland/plane.c \
	userland/desktop/wayland/pointer-accel.c userland/desktop/wayland/touchpad.c"
flags="-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland"
# shellcheck disable=SC2086
cc $flags -O1 $sources -lpthread -lm -o "$out/input-thread-host-test"
timeout 120 "$out/input-thread-host-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=thread $sources -lpthread -lm -o "$out/input-thread-host-test-tsan"
TSAN_OPTIONS="halt_on_error=1 exitcode=66" timeout 120 "$out/input-thread-host-test-tsan"
echo "input-thread-host-test: PASS (plain and ThreadSanitizer)"
