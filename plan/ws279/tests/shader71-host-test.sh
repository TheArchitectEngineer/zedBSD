#!/bin/sh
# Every shader of the tree compiled by the V3D 7.1 compiler (the shared files and the 7.1 copies) and run on
# the 7.1 timing model of one QPU lane (qpu71-machine.c), compared bit for bit with the source program; prints
# the table of native program sizes (ws279-p004).
# Usage: sh plan/ws279/tests/shader71-host-test.sh [output-directory]
set -eu
out=${1:-build/ws279-shader71-host}
mesa=plan/ws141/temp/mesa/src
# The 7.1 format test builds the fixed independently hashed Mesa oracle objects and checks the encoder first.
sh plan/ws279/tests/qpu71-format-host-test.sh "$out"
cc -std=c99 -O1 -Wall -Wextra -Werror -Iinclude -Isrc -I. -I"$out/mesa-host" -I"$mesa" \
    plan/ws279/tests/shader71-host-test.c plan/ws279/tests/qpu71-machine.c \
    src/drivers/gpu/compiler/spirv.c src/drivers/gpu/compiler/optimize.c \
    src/drivers/gpu/bcm2711/compiler/shader.c src/drivers/gpu/bcm2711/compiler/shader-analyze.c \
    src/drivers/gpu/bcm2711/compiler/shader71-lower.c src/drivers/gpu/bcm2711/compiler/shader71-output.c \
    src/drivers/gpu/bcm2711/compiler/shader71-schedule.c src/drivers/gpu/bcm2711/compiler/qpu71.c \
    "$out/mesa-pack.o" "$out/mesa-instr.o" \
    -lm -o "$out/shader71-host-test"
timeout 600 "$out/shader71-host-test"
