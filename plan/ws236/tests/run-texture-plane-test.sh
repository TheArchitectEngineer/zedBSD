#!/bin/sh
# ws236-p011: the UIF layout of R8, RG8 and RGBA8 texels and their texture records (native-texture.c).
# New folder of build/tmp per run (Q1 removes old ones).  Last line "ws236 p011 texture planes: PASS".
# usage: sh plan/ws236/tests/run-texture-plane-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws236-texture.XXXXXX")
cc -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
	-I"$repo/include" -I"$repo/src" -I"$repo" \
	"$repo/plan/ws236/tests/texture-plane-host-test.c" "$repo/src/drivers/gpu/bcm2711/native-texture.c" \
	-o "$work/texture-plane-test"
UBSAN_OPTIONS=halt_on_error=1 "$work/texture-plane-test"
