#!/bin/sh
# Host check of ws269 p010 (the compiler): the SPIR-V reader, the back end,
# the machine simulator against the intermediate form's interpreter, and
# the group bytes against fields built by hand from Mesa's tables.
#   plan/ws269/tests/p010-compiler/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws269/tests/p010-compiler
out=$1
mkdir -p "$out"
for shader in "$here"/shaders/*.comp; do
	name=$(basename "$shader" .comp)
	glslc --target-env=vulkan1.0 -O -o "$out/$name-o.spv" "$shader"
	glslc --target-env=vulkan1.0 -o "$out/$name-0.spv" "$shader"
done
for shader in "$here"/shaders/*.vert; do
	name=$(basename "$shader" .vert)
	glslc --target-env=vulkan1.0 -O -o "$out/$name-vert-o.spv" "$shader"
	glslc --target-env=vulkan1.0 -o "$out/$name-vert-0.spv" "$shader"
done
for name in fill offset; do
	glslc --target-env=vulkan1.0 -o "$out/p009-$name.spv" "$root/plan/ws269/tests/p009/shaders/$name.comp"
done
sources="$root/src/drivers/gpu/powervr/shader-spirv.c $root/src/drivers/gpu/powervr/shader-spirv-interface.c $root/src/drivers/gpu/powervr/shader-spirv-control.c $root/src/drivers/gpu/powervr/shader-spirv-instruction.c $root/src/drivers/gpu/powervr/shader-backend.c $root/src/drivers/gpu/powervr/usc-encode.c"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" -I"$root" -I"$here" \
	$sources "$here/ir-run.c" "$here/machine-run.c" "$here/compile-host-test.c" -lm -o "$out/compile-host-test"
"$out/compile-host-test" "$out"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" -I"$here" \
	$sources "$here/group-host-test.c" -o "$out/group-host-test"
"$out/group-host-test"
