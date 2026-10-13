#!/bin/sh
# Host test of the AHCI FIS, command header, PRDT and IDENTIFY arithmetic (ws231-p001).
# The output goes to a fresh build/tmp/ws231-ahci-protocol run directory (removed by Q1's q1-clean.sh).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws231-ahci-protocol"
out=$repo/build/tmp/ws231-ahci-protocol

compiler=${HOSTCC:-cc}
fixture=$repo/plan/ws231/tests/ahci-protocol-test.c

$compiler -std=c89 -pedantic -O2 -Wall -Wextra -Werror -Wno-long-long \
	-I"$repo/include" "$fixture" -o "$out/ahci-protocol-test"
"$out/ahci-protocol-test"

if $compiler -std=c11 -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-omit-frame-pointer \
	-I"$repo/include" "$fixture" -o "$out/ahci-protocol-test-sanitized" 2>/dev/null; then
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$out/ahci-protocol-test-sanitized"
	echo 'ahci-protocol sanitizer: PASS'
else
	echo 'ahci-protocol sanitizer: SKIP (compiler unavailable)'
fi
