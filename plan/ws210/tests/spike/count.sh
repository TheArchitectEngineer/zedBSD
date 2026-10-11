#!/bin/sh
# WS210 p002 spike: counts the bcm2711 native code of the candidate GPU canvas shaders on the host.
# Usage: sh plan/ws210/tests/spike/count.sh [output-directory]   (run from the tree's top)
set -eu
out=${1:-build/ws210-spike}
here=plan/ws210/tests/spike
mkdir -p "$out"
cc -std=c99 -O1 -Wall -Wextra -Werror -Iinclude -Isrc -I. \
    "$here/count.c" src/drivers/gpu/compiler/spirv.c \
    src/drivers/gpu/bcm2711/shader.c src/drivers/gpu/bcm2711/shader-analyze.c \
    src/drivers/gpu/bcm2711/shader-lower.c src/drivers/gpu/bcm2711/shader-output.c \
    src/drivers/gpu/bcm2711/qpu.c -lm -o "$out/count"
status=0
for name in fill round round-solid shadow ring capsule atlas image replace triangle; do
	for stage in vert frag; do
		glslc --target-env=vulkan1.0 -O -I "$here" -o "$out/$name.$stage.spv" "$here/$name.$stage"
		spirv-val "$out/$name.$stage.spv"
	done
	"$out/count" "$name" "$out/$name.vert.spv" "$out/$name.frag.spv" || status=1
done
exit $status
