#!/bin/sh
# Builds and runs the host test of the system's events (ws132-p002):
# src/kern/system-event.c compiled freestanding like the kernel, the test
# with the host C library and the sanitizers.
# Usage: plan/ws132/tests/run-host-system-event.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws132-host}
mkdir -p "$out"

cc=${CC:-clang}
san="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -D__ZEDBSD__ -DKERN_USER_ABI_LP64 $san -I "$root/include" -I "$root/src" \
	-c "$root/src/kern/system-event.c" -o "$out/system-event.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror $san -I "$root/include" \
	-c "$root/plan/ws132/tests/host-system-event.c" -o "$out/host-system-event.o"
$cc $san "$out/host-system-event.o" "$out/system-event.o" -o "$out/host-system-event"
"$out/host-system-event"
