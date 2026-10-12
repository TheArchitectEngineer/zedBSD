#!/bin/sh
# ws207-p002: the Pi 4 headphone jack's arithmetic (rpi4-audio-ring.c) on the host: duty words, channel order,
# the DMA's control-block ring and the fragment/position of a source address.  Builds in a new folder of the
# build (old folders are Q1's to remove), normally and with ASan/UBSan.  The last line is "rpi4-audio-ring: all PASS".
# usage: sh plan/ws207/tests/run-rpi4-audio-ring-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws207-ring.XXXXXX")
flags="-std=c11 -Wall -Wextra -Werror -I$repo/src -I$repo/include"
sources="$repo/plan/ws207/tests/rpi4-audio-ring-test.c $repo/src/drivers/platform/rpi4/rpi4-audio-ring.c"
# shellcheck disable=SC2086
cc $flags -O2 $sources -o "$work/ring-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $sources -o "$work/ring-sanitized"
UBSAN_OPTIONS=halt_on_error=1 "$work/ring-sanitized"
"$work/ring-test"
