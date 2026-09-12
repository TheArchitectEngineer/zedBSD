#!/bin/sh
# Exercise real compositor protocol, ownership and Unix stream transport.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${1:-"$root/build/q309-zwl"}
mkdir -p "$work"
for mode in ordinary sanitize; do
    extra=
    if test "$mode" = sanitize; then
        extra='-fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'
    fi
    cc -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
        -I"$root/include" -I"$root/userland/base/zwl" $extra \
        "$root/plan/ws014/tests/zwl-protocol.c" \
        "$root/userland/base/zwl/wire.c" "$root/userland/base/zwl/objects.c" \
        "$root/userland/base/zwl/protocol.c" "$root/userland/base/zwl/display.c" \
        -o "$work/$mode" >"$work/$mode-build.log" 2>&1
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        timeout 20 "$work/$mode" >"$work/$mode.log" 2>&1
    cat "$work/$mode.log"
done
