#!/bin/sh
# ws209-p003: audiod's adaptive rate converter (userland/base/audiod/asrc.c) on the host, normally and with ASan/UBSan,
# in a new folder of the build (old folders are Q1's to remove).  Prints the bands' levels and the drift's errors; the
# last line is "asrc: all PASS".
# usage: sh plan/ws209/tests/run-asrc-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws209-asrc.XXXXXX")
sources="$repo/plan/ws209/tests/asrc-test.c $repo/userland/base/audiod/asrc.c"
# shellcheck disable=SC2086
cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I"$repo" $sources -lm -o "$work/asrc-sanitized"
"$work/asrc-sanitized" >/dev/null
# shellcheck disable=SC2086
cc -std=gnu11 -Wall -Wextra -Werror -O2 -I"$repo" $sources -lm -o "$work/asrc-test"
"$work/asrc-test"
