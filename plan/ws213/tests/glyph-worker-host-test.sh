#!/bin/sh
# Host test of the glyph worker (ws213-p005, the design's 9 (j) and 9 (m) for
# the worker): order, results and errors, then the look's cache played with
# random assignments, once plainly and once under ThreadSanitizer.
# Usage: plan/ws213/tests/glyph-worker-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws213-host}
mkdir -p "$out"
sources="plan/ws213/tests/glyph-worker-host-test.c userland/desktop/wayland/glyph-worker.c $(ls userland/desktop/libtruetype/*.c)"
flags="-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I. -Iinclude -Iuserland/desktop/include -Iuserland/desktop/wayland"
# shellcheck disable=SC2086
cc $flags -O1 $sources -lpthread -lm -o "$out/glyph-worker-host-test"
timeout 120 "$out/glyph-worker-host-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=thread $sources -lpthread -lm -o "$out/glyph-worker-host-test-tsan"
TSAN_OPTIONS="halt_on_error=1 exitcode=66" timeout 120 "$out/glyph-worker-host-test-tsan"
echo "glyph-worker-host-test: PASS (plain and ThreadSanitizer)"
