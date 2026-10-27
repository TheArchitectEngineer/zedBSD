#!/bin/sh
# Host test of the GHCI 1.0 driver (WS290): the words test (every command encoder,
# the records, the fence, ring, capability list and sizes; ghci-words-host-test.c)
# and the reference test of the layouts against amaterrace-gpu's include/ghci_cmd.h
# (ghci-ref-host-test.c + ghci-ref-layout.c). The reference header is written to a
# new directory for the run only; without amaterrace-gpu the reference test FAILS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws290/tests/ghci-host-test.sh   (from anywhere)
# Environment: GHCI_SPEC_REPO (default ~/amaterrace-gpu), GHCI_SPEC_REV (default cb937e6).
# Needs cc and git. Output goes to a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws290/tests"
ghci="$repo/src/drivers/gpu/ghci"
spec_repo=${GHCI_SPEC_REPO:-$HOME/amaterrace-gpu}
spec_rev=${GHCI_SPEC_REV:-cb937e6}
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/ghci290-host.XXXXXX")
export TMPDIR="$out"
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement"
status=0

cc $flags -o "$out/ghci-words-host-test" "$tests/ghci-words-host-test.c" "$ghci/cmd.c" "$ghci/caps.c" "$ghci/check.c"
timeout 120 "$out/ghci-words-host-test" || status=1

mkdir -p "$out/ref"
if git -C "$spec_repo" show "$spec_rev:include/ghci_cmd.h" > "$out/ref/ghci_cmd.h" 2>/dev/null; then
	cc -std=gnu11 -O1 -Wall -Wextra -Werror -I"$out/ref" -c -o "$out/ghci-ref-layout.o" "$tests/ghci-ref-layout.c"
	cc $flags -o "$out/ghci-ref-host-test" "$tests/ghci-ref-host-test.c" "$out/ghci-ref-layout.o"
	timeout 60 "$out/ghci-ref-host-test" || status=1
else
	echo "FAIL ghci-ref-host-test: no $spec_rev:include/ghci_cmd.h in $spec_repo"
	status=1
fi

if [ "$status" -ne 0 ]; then
	echo "FAIL ghci290-host-test ($out)"
	exit 1
fi
echo "PASS ghci290-host-test ($out)"
