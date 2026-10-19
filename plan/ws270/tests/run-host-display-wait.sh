#!/bin/sh
# ws270-p006a: builds and runs the host test of sessiond's wait for the display (host-display-wait.c with
# userland/desktop/sessiond/fallback-rules.c) as gnu89, plainly and under ASan/UBSan, in a new directory under build/tmp
# (nothing is removed here; Q1's plan/tools/q1-clean.sh removes old runs).
#   sh plan/ws270/tests/run-host-display-wait.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws270-display-wait"
work=$fresh_dir
for mode in plain sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
	fi
	${CC:-cc} -std=gnu89 -Wdeclaration-after-statement -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g $extra \
		-I"$repo" \
		"$repo/plan/ws270/tests/host-display-wait.c" "$repo/userland/desktop/sessiond/fallback-rules.c" \
		-o "$work/$mode"
	UBSAN_OPTIONS=halt_on_error=1 timeout 20 "$work/$mode"
done
