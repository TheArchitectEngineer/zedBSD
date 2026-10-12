#!/bin/sh
# ws204-p003: compiles audio-loop's arithmetic (userland/tests/audio-loop/measure.c) with its host test, once
# normally and once with ASan/UBSan, in a new folder of the build (old folders are Q1's to remove), and runs it;
# audio-loop's program itself is compiled too, to keep it building on the host.
# The last line is "audio-loop measure: all PASS".
# usage: sh plan/ws204/tests/run-audio-loop-host-test.sh   (from anywhere)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws204-loop.XXXXXX")
flags="-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I$repo"
# shellcheck disable=SC2086
cc $flags -O2 "$repo/userland/tests/audio-loop/main.c" "$repo/userland/tests/audio-loop/measure.c" -lm -o "$work/audio-loop"
# shellcheck disable=SC2086
cc $flags -O2 "$repo/plan/ws204/tests/audio-loop-host-test.c" "$repo/userland/tests/audio-loop/measure.c" -lm -o "$work/ordinary"
"$work/ordinary"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$repo/plan/ws204/tests/audio-loop-host-test.c" \
    "$repo/userland/tests/audio-loop/measure.c" -lm -o "$work/sanitized"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/sanitized"
