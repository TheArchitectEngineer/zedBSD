#!/bin/sh
# ws101-p002: the i915 compiler's compute shaders on the host.
#
# 1. Compiles the GLSL of shaders/ with glslc (Vulkan 1.0 SPIR-V).
# 2. compute-dump.c parses and compiles each module as a compute shader: the scoreboard must be sound, every
#    atomic's reply bit must agree with its reply length, the kernel must end at the thread spawner; the modules of
#    shaders/refuse/ must be refused.
# 3. Mesa's brw_disasm must accept every instruction of each kernel and brw_asm must assemble the listing back into
#    the same bytes.
# 4. compute-lower.c runs each module's IR on a small interpreter, dispatch by dispatch, and compares the buffers with
#    what C computes on its own.
#
#   plan/ws101/tests/host/run.sh        (BRW_TOOLS: a Mesa 25.0.7 build's src/intel/compiler, default below)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$repo/plan/ws101/tests/host
tools=${BRW_TOOLS:-/home/awe/p014-c/mesa/build-asm/src/intel/compiler}
work=$(mktemp -d "${TMPDIR:-/tmp}/ws101-host.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
status=0

# The tools: the dumper and the interpreter, built from the compiler's sources.
cc -std=gnu99 -O0 -Wall -Wextra -Wno-unused-function -I"$repo" -I"$repo/include" -DHAL_ARCH_AMD64 \
	-o "$work/dump" "$here/compute-dump.c" -lm || exit 1
cc -std=gnu99 -O0 -Wall -Wextra -Wno-unused-function -I"$repo" -I"$repo/include" -DHAL_ARCH_AMD64 \
	-o "$work/lower" "$here/compute-lower.c" -lm || exit 1

# The modules.
mkdir -p "$work/refuse"
for source in "$here"/shaders/*.comp "$here"/shaders/refuse/*.comp; do
	relative=${source#"$here"/shaders/}
	glslc --target-env=vulkan1.0 -fshader-stage=compute -o "$work/${relative%.comp}.spv" "$source" || {
		echo "$relative: FAIL glslc"; status=1; }
done

# Every module compiles, and its kernel passes Mesa's disassembler and assembler.
for spv in "$work"/*.spv; do
	name=$(basename "$spv" .spv)
	"$work/dump" "$spv" "$work/$name.bin" > "$work/$name.dump" 2>&1 || {
		cat "$work/$name.dump"; echo "$name: FAIL"; status=1; continue; }
	grep -q 'descriptors agree' "$work/$name.dump" || { echo "$name: FAIL descriptors"; status=1; continue; }
	"$tools/brw_disasm" --gen=adl --input-path="$work/$name.bin" > "$work/$name.asm" 2>&1
	if grep -q 'ERROR\|illegal' "$work/$name.asm"; then
		echo "$name: FAIL the disassembler rejects an instruction"; grep -B1 'ERROR\|illegal' "$work/$name.asm" | head
		status=1; continue
	fi
	"$tools/brw_asm" --gen=adl -o "$work/$name.re" "$work/$name.asm" > /dev/null 2>&1
	cmp -s "$work/$name.bin" "$work/$name.re" || { echo "$name: FAIL re-assembled bytes differ"; status=1; continue; }
	echo "$name: $(grep -c '' "$work/$name.asm") listing lines; disassembled and re-assembled to the same bytes"
	grep 'scoreboard\|sends,' "$work/$name.dump" | sed "s|^[^:]*:|$name:|"
done

# The modules that must be refused are.
for spv in "$work"/refuse/*.spv; do
	"$work/dump" -refuse "$spv" | sed "s|^.*/refuse/|refuse/|" || status=1
done

# The IR computes what C computes.
"$work/lower" "$work" || status=1

[ $status = 0 ] && echo "ws101-p002 host test PASS"
exit $status
