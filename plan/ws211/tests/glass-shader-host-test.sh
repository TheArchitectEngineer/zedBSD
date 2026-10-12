#!/bin/sh
# Host test of the glass's shader after ws211-p011 (shaders/panel.frag, glass.c's per-shape values):
# the real SPIR-V, interpreted, against the glass as drawn before p011; and since WS141 p013 the kernel's
# native program of each glass shader, run on the lane model, against the interpretation bit for bit.
# Usage: plan/ws211/tests/glass-shader-host-test.sh [build-dir]  (from the tree root)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:-build/ws211-host}
mesa=plan/ws141/temp/mesa/src
mkdir -p "$out"
# The native programs run on the WS141 lane model, read with the pinned MIT Mesa decoder (built by the format test).
sh plan/ws141/tests/qpu-format-host-test.sh "$out" > /dev/null
cc -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -I. -Iinclude -Isrc -I"$out/mesa-host" -I"$mesa" \
	plan/ws211/tests/glass-shader-host-test.c plan/ws141/tests/qpu-machine.c src/drivers/gpu/compiler/spirv.c \
	src/drivers/gpu/bcm2711/shader.c src/drivers/gpu/bcm2711/shader-analyze.c \
	src/drivers/gpu/bcm2711/shader-lower.c src/drivers/gpu/bcm2711/shader-output.c \
	src/drivers/gpu/bcm2711/shader-schedule.c src/drivers/gpu/bcm2711/qpu.c \
	"$out/mesa-pack.o" "$out/mesa-instr.o" \
	-lm -o "$out/glass-shader-host-test"
timeout 600 "$out/glass-shader-host-test"
