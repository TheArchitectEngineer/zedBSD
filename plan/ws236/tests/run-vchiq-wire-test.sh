#!/bin/sh
# ws236-p002: the VCHIQ layout, message framing, slot recycling and page lists (rpi4-vchiq-wire.c) on the host,
# against a simulated master.  Builds in a new folder of build/tmp (old folders are Q1's to remove), normally and
# with ASan/UBSan.  The last line is "vchiq-wire: all PASS".
# usage: sh plan/ws236/tests/run-vchiq-wire-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws236-vchiq.XXXXXX")
flags="-std=c11 -Wall -Wextra -Werror -I$repo/src -I$repo/include"
sources="$repo/plan/ws236/tests/vchiq-wire-test.c $repo/src/drivers/platform/rpi4/rpi4-vchiq-wire.c"
# shellcheck disable=SC2086
cc $flags -O2 $sources -o "$work/vchiq-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $sources -o "$work/vchiq-sanitized"
UBSAN_OPTIONS=halt_on_error=1 "$work/vchiq-sanitized"
"$work/vchiq-test"
