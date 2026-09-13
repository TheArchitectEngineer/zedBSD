#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d /tmp/zedbsd-vkdemo-recording.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c89 -pedantic -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -D_POSIX_C_SOURCE=200809L -I "$repo/include" \
    "$repo/plan/ws014/tests/vkdemo-recording.c" -Wl,--gc-sections -o "$work/recording"
"$work/recording"
