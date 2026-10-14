#!/bin/sh
# ws236-p011: Video Player's shaders through the bcm2711 native shader compiler on the host.
# New folder of build/tmp per run (Q1 removes old ones).  Last line "ws236 p011 videoplayer shaders: PASS".
# usage: sh plan/ws236/tests/run-shader-compile-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws236-shader.XXXXXX")
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
	-I"$repo/include" -I"$repo/src" -I"$repo" \
	"$repo/plan/ws236/tests/shader-compile-host-test.c" \
	"$repo/src/drivers/gpu/compiler/spirv.c" "$repo/src/drivers/gpu/compiler/optimize.c" \
	"$repo/src/drivers/gpu/bcm2711/shader.c" "$repo/src/drivers/gpu/bcm2711/shader-analyze.c" \
	"$repo/src/drivers/gpu/bcm2711/shader-lower.c" "$repo/src/drivers/gpu/bcm2711/shader-output.c" \
	"$repo/src/drivers/gpu/bcm2711/shader-schedule.c" "$repo/src/drivers/gpu/bcm2711/qpu.c" \
	-lm -o "$work/shader-compile-test"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$work/shader-compile-test"
