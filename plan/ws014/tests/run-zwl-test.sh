#!/bin/sh
# Exercise real compositor protocol, ownership and Unix stream transport.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${1:-"$root/build/q309-zwl"}
mkdir -p "$work"
# The zedBSD uapi evdev header spells its ioctl numbers with the zedBSD libc
# KERN_IOC encoder; the host libc does not have it, so the same encoder is
# supplied here.  The host tests never issue those ioctls.
ioc_out=-DKERN_IOC_OUT=0x40000000UL
ioc='-DKERN_IOC(dir,group,nr,size)=((unsigned long)(dir)|(((unsigned long)(size)&0x1fffUL)<<16)|((unsigned long)(group)<<8)|(unsigned long)(nr))'
for mode in ordinary sanitize; do
    extra=
    if test "$mode" = sanitize; then
        extra='-fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'
    fi
    cc -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
        -I"$root/include" -I"$root/userland/base/zwl" $extra \
        "$ioc_out" "$ioc" \
        "$root/plan/ws014/tests/zwl-protocol.c" \
        "$root/userland/base/zwl/wire.c" "$root/userland/base/zwl/objects.c" \
        "$root/userland/base/zwl/protocol.c" "$root/userland/base/zwl/display.c" \
        "$root/userland/base/zwl/seat.c" "$root/userland/base/zwl/input.c" \
        -o "$work/$mode" >"$work/$mode-build.log" 2>&1
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        timeout 20 "$work/$mode" >"$work/$mode.log" 2>&1
    cat "$work/$mode.log"
done
