#!/bin/sh
# Host test of the GHCI driver's arithmetic on words (src/drivers/gpu/ghci/ghci-words.h):
# fences and their wrap, the ring's room and padding, the extended capability list, the
# allocator of GPU addresses, the driver's packets, the native command check, the capset
# record and the job markers (plan/ws288/tests/ghci-host-test.c).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws288/tests/ghci-host-test.sh   (from anywhere)
# Needs cc.  The program goes in a new directory below build/tmp; nothing is removed.
# A few seconds.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws288/tests"
ghci="$repo/src/drivers/gpu/ghci"
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/ghci-host.XXXXXX")
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement"

cc $flags -o "$out/ghci-host-test" "$tests/ghci-host-test.c" "$ghci/check.c" "$ghci/range.c" \
	"$ghci/state.c" "$ghci/capset.c" "$ghci/jobs.c"

status=0
timeout 120 "$out/ghci-host-test" || status=1
if [ "$status" -ne 0 ]; then
	echo "FAIL ghci-host-test ($out)"
	exit 1
fi
echo "PASS ghci-host-test ($out)"
