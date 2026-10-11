#!/bin/sh
# ws197-p007a2: the SCO pieces of the Bluetooth HCI class's pure part (src/drivers/generic/bt-hci-proto.c) on the host,
# normally and with ASan/UBSan, in a new folder of the build (old folders are Q1's to remove).
# The last line is "bt-sco-proto: all PASS".
# usage: sh plan/ws197/tests/run-bt-sco-proto-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws197-sco.XXXXXX")
flags="-std=c11 -Wall -Wextra -Werror -I$repo/include -I$repo"
sources="$repo/plan/ws197/tests/bt-sco-proto-test.c $repo/src/drivers/generic/bt-hci-proto.c"
# shellcheck disable=SC2086
cc $flags -O2 $sources -o "$work/sco-test"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $sources -o "$work/sco-sanitized"
UBSAN_OPTIONS=halt_on_error=1 "$work/sco-sanitized"
"$work/sco-test"
