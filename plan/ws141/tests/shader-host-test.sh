#!/bin/sh
# Every shader of the tree compiled natively and run on the timing model of one QPU lane (qpu-machine.c),
# compared bit for bit with the source program; prints the table of native program sizes (WS141 p013).
# Usage: sh plan/ws141/tests/shader-host-test.sh [output-directory]
set -eu
out=${1:-build/ws141-shader-host}
mesa=plan/ws141/temp/mesa/src
# Reuses only the fixed independently hashed oracle object setup, not any production encoding expectation.
sh plan/ws141/tests/qpu-format-host-test.sh "$out"
cc -std=c99 -O1 -Wall -Wextra -Werror -Iinclude -Isrc -I. -I"$out/mesa-host" -I"$mesa" \
    plan/ws141/tests/shader-host-test.c plan/ws141/tests/qpu-machine.c src/drivers/gpu/compiler/spirv.c src/drivers/gpu/compiler/optimize.c \
    src/drivers/gpu/bcm2711/shader.c src/drivers/gpu/bcm2711/shader-analyze.c \
    src/drivers/gpu/bcm2711/shader-lower.c src/drivers/gpu/bcm2711/shader-output.c \
    src/drivers/gpu/bcm2711/shader-schedule.c src/drivers/gpu/bcm2711/qpu.c \
    "$out/mesa-pack.o" "$out/mesa-instr.o" \
    -lm -o "$out/shader-host-test"
timeout 600 "$out/shader-host-test"
