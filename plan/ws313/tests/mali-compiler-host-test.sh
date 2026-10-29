#!/bin/sh
# Host test of the Mali Valhall shader compiler (src/drivers/gpu/mali/compiler/): the encoder's words against Mesa's
# assembler cases, every case of that file decoded by the test's own decoder (generated from Mesa's ISA.xml), and
# compute, vertex and fragment modules compiled, decoded and run on a one-lane Valhall model against the IR reference.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws313/tests/mali-compiler-host-test.sh [TRIALS]   (from anywhere; 200 trials by default)
# The modules are plan/ws313/tests/mali-compiler-shaders/*, plan/tools/nvrtx/sm75/* and the gpu-compute host shaders
# add, ids and atomic, and the compositor's quad.  Needs cc and python3; without glslc (GLSLC names another) the modules
# are skipped (and counted).  MESA names Mesa's source tree; by default mali-mesa-source.sh fetches the pinned Mesa 26.2.6 release once
# (checked by its size and SHA-256) and extracts it below build/.  Output goes in a new directory below build/pc;
# nothing is removed.  Nothing here is evidence about the GPU.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
trials=${1:-200}
mesa=${MESA:-$(sh "$repo/plan/ws313/tests/mali-mesa-source.sh")}
valhall=$mesa/src/panfrost/compiler/bifrost/valhall
mkdir -p "$repo/build/pc"
out=$(mktemp -d "$repo/build/pc/mali-compiler-host.XXXXXX")
status=0
if [ -f "$valhall/ISA.xml" ]; then
	python3 -I "$repo/plan/ws313/tests/mali-compiler-isa-table.py" "$valhall/ISA.xml" "$out/isa-table.inc" > /dev/null
	if cmp -s "$out/isa-table.inc" "$repo/tests/mali-compiler-isa-table.inc"; then
		echo "PASS table: tests/mali-compiler-isa-table.inc is what ISA.xml generates"
	else
		echo "FAIL table: tests/mali-compiler-isa-table.inc differs from what ISA.xml generates"
		status=1
	fi
	cases="--cases $valhall/test/assembler-cases.txt"
else
	echo "SKIP table and assembler cases: no Mesa tree at $mesa"
	cases=""
fi
sources_count=$(ls "$repo"/plan/tools/nvrtx/sm75/*.comp "$repo"/plan/ws313/tests/mali-compiler-shaders/* | wc -l)
sources_count=$((sources_count + 3 + 4))
glslc=${GLSLC:-glslc}
if ! command -v "$glslc" > /dev/null 2>&1; then
	echo "SKIP $sources_count modules: no glslc"
	glslc=:
	no_glslc=1
fi
for source in "$repo"/plan/tools/nvrtx/sm75/*.comp "$repo/plan/tools/gpu-compute/host/shaders/add.comp" \
	"$repo/plan/tools/gpu-compute/host/shaders/ids.comp" "$repo/plan/tools/gpu-compute/host/shaders/atomic.comp"; do
	"$glslc" --target-env=vulkan1.0 -fshader-stage=compute -o "$out/$(basename "$source" .comp).comp.spv" "$source"
done
for source in "$repo"/plan/ws313/tests/mali-compiler-shaders/*; do
	"$glslc" --target-env=vulkan1.0 -o "$out/mali-$(basename "$source").spv" "$source"
done
for source in "$repo"/plan/tools/nvrtx/sm75/texture.vert "$repo"/plan/tools/nvrtx/sm75/texture.frag \
	"$repo/userland/desktop/wayland/shaders/quad.vert" "$repo/userland/desktop/wayland/shaders/quad.frag"; do
	name=$(basename "$source")
	"$glslc" --target-env=vulkan1.0 -o "$out/$(dirname "$source" | xargs basename)-${name%.*}.${name##*.}.spv" "$source"
done
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement"
cc $flags -o "$out/mali-compiler-host-test" "$repo/tests/mali-compiler-host-test.c" "$repo/tests/mali-compiler-model.c" \
	"$repo/src/drivers/gpu/mali/compiler/va-compile.c" "$repo/src/drivers/gpu/mali/compiler/va-encode.c" \
	"$repo/src/drivers/gpu/mali/compiler/va-schedule.c" "$repo/src/drivers/gpu/compiler/spirv.c" \
	"$repo/src/drivers/gpu/compiler/optimize.c" -lm
# shellcheck disable=SC2086
"$out/mali-compiler-host-test" "$out" $cases --trials "$trials" $(ls "$out"/*.spv 2>/dev/null) > "$out/result.txt" 2>&1 || status=1
cat "$out/result.txt"
echo "modules: $(ls "$out"/*.spv 2>/dev/null | wc -l), passed: $(grep -c '^PASS .*\.spv$' "$out/result.txt" || true)${no_glslc:+, skipped: $sources_count (no glslc)}"
if [ "$status" -ne 0 ] || grep -q '^FAIL' "$out/result.txt"; then
	echo "FAIL mali-compiler-host-test ($out)"
	exit 1
fi
echo "PASS mali-compiler-host-test ($out)"
