#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Exercise the actual text observer lifetime against concurrent host writers.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d /tmp/zedbsd-text-notification.XXXXXX)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
for mode in ordinary sanitize; do
    extra=
    if test "$mode" = sanitize; then
        extra='-fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'
    fi
    timeout 30 cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
        -pthread $extra -I"$root/include" \
        "$root/plan/ws014/tests/text-notification.c" "$root/src/kern/text-display.c" \
        -o "$work/$mode"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 "$work/$mode"
done
