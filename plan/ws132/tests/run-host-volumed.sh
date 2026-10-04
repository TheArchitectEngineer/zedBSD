#!/bin/sh
# ws132-p004: host test of volumed's names, lines and permission rule (userland/base/volumed/names.c), with ASan/UBSan.
#   sh plan/ws132/tests/run-host-volumed.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    plan/ws132/tests/host-volumed.c userland/base/volumed/names.c -o "$work/host-volumed"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/host-volumed"
