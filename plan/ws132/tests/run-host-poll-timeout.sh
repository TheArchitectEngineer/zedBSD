#!/bin/sh
# Builds and runs the host test of poll's deadline (T1-129; src/kern/poll.c compiled unchanged), plainly and with
# ASan and UBSan.
# Usage: sh plan/ws132/tests/run-host-poll-timeout.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws132-host-poll-timeout}
mkdir -p "$out"
for mode in plain sanitize; do
	extra=
	[ "$mode" = sanitize ] && extra='-fsanitize=address,undefined -fno-omit-frame-pointer'
	cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -I"$root/include" -I"$root/include/libc" \
		-DKERN_UAPI_NATIVE -I"$root/include/uapi" -ffunction-sections -fdata-sections $extra \
		"$root/plan/ws132/tests/host-poll-timeout.c" "$root/src/kern/poll.c" -Wl,--gc-sections \
		-o "$out/host-poll-timeout-$mode"
	UBSAN_OPTIONS=halt_on_error=1 timeout 20 "$out/host-poll-timeout-$mode"
done
