#!/bin/sh
# ws236-p010: the V3D executor's settling list (render-worker.c) with fixture executions and stubbed kernel services.
# New folder of build/tmp per run (Q1 removes old ones).  Last line "ws236 p010 executor settling list: PASS".
# usage: sh plan/ws236/tests/run-render-settle-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws236-settle.XXXXXX")
cc -std=c99 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -include time.h -Wall -Wextra -Werror -O1 -g \
	-fsanitize=address,undefined -ffunction-sections -fdata-sections \
	-I"$repo/plan/ws141/tests/host" -I"$repo/include" -I"$repo/src" -I"$repo" \
	"$repo/plan/ws236/tests/render-settle-host-test.c" "$repo/src/drivers/gpu/bcm2711/render-worker.c" \
	-Wl,--gc-sections -o "$work/render-settle-test"
UBSAN_OPTIONS=halt_on_error=1 "$work/render-settle-test"
