#!/bin/sh
# Host test of the glass's shader after ws211-p011 (shaders/panel.frag, glass.c's per-shape values):
# the real SPIR-V, interpreted, against the glass as drawn before p011.
# Usage: plan/ws211/tests/glass-shader-host-test.sh [build-dir]  (from the tree root)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:-build/ws211-host}
mkdir -p "$out"
cc -std=c11 -D_GNU_SOURCE -O1 -Wall -Wextra -Werror -I. -Iinclude -Isrc \
	plan/ws211/tests/glass-shader-host-test.c src/drivers/gpu/compiler/spirv.c \
	-lm -o "$out/glass-shader-host-test"
timeout 120 "$out/glass-shader-host-test"
