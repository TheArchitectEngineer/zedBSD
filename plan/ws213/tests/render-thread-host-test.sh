#!/bin/sh
# Host test of the render thread's pull (ws213-p004, the design's 9 (b), (c),
# (d), (e), (h) and 9 (m) for the render thread): the steps one at a time, then
# the real thread, once plainly and once under ThreadSanitizer.
# Usage: plan/ws213/tests/render-thread-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws213-host}
mkdir -p "$out"
sources="plan/ws213/tests/render-thread-host-test.c userland/desktop/wayland/render-thread.c"
flags="-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/wayland"
# shellcheck disable=SC2086
cc $flags -O1 $sources -lpthread -o "$out/render-thread-host-test"
timeout 120 "$out/render-thread-host-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=thread $sources -lpthread -o "$out/render-thread-host-test-tsan"
TSAN_OPTIONS="halt_on_error=1 exitcode=66" timeout 120 "$out/render-thread-host-test-tsan"
echo "render-thread-host-test: PASS (plain and ThreadSanitizer)"
