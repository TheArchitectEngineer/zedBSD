#!/bin/sh
# ws236-p003: the MMAL message builders (rpi4-mmal-wire.c) against the layout measured from the userland's BSD-3-Clause
# headers on 32-bit ARM.  New folder of build/tmp per run (Q1 removes old ones).  Last line "mmal-wire: all PASS".
# usage: sh plan/ws236/tests/run-mmal-wire-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws236-mmal.XXXXXX")
cc -std=c11 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/include" -O1 -g -fsanitize=address,undefined \
	"$repo/plan/ws236/tests/mmal-wire-test.c" "$repo/src/drivers/platform/rpi4/rpi4-mmal-wire.c" -o "$work/mmal-test"
UBSAN_OPTIONS=halt_on_error=1 "$work/mmal-test"
