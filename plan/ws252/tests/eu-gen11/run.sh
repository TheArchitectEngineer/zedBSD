#!/bin/sh
# The i915 compiler's Ice Lake (Gen11) kernels on the host, judged by Mesa 25.0.7's brw_disasm / brw_asm (--gen=icl).
#
# 1. The encoder test (gentool-eu.c -gen11): every emitter once.
# 2. Every vertex, fragment and geometry module the Gen9 test of ws229 compiles, and ws229's render-target modules,
#    compiled for Ice Lake (eudump.c -gen11); the modules refused on purpose must stay refused.
# 3. Every compute module of the ws101 host test and of the vkcs scenario (compute-dump.c -gen11).
# Each kernel must disassemble without an error and assemble back into the same bytes.  Then the message forms Ice
# Lake takes from Gen12 (the cleared sampler header, the barrier ID mask, the end of thread without the URB bit) and
# the ones it keeps from Gen9 (the render-target header, the fence replies read back) must be in the kernels using
# them.
#
#   plan/ws252/tests/eu-gen11/run.sh      (BRW_TOOLS: a Mesa 25.0.7 build's src/intel/compiler, default
#                                          build/mesa-tools/build-asm/src/intel/compiler: plan/tools/gpu-compute/host/mesa-tools.sh)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$repo/plan/ws252/tests/eu-gen11
tools=${BRW_TOOLS:-$repo/build/mesa-tools/build-asm/src/intel/compiler}
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws252-eu-gen11"
work=$fresh_dir
status=0

if [ ! -x "$tools/brw_disasm" ] || [ ! -x "$tools/brw_asm" ]; then
	echo "FAIL: no brw_disasm/brw_asm under $tools (plan/tools/gpu-compute/host/mesa-tools.sh builds them, or set BRW_TOOLS)"
	exit 1
fi

# judge NAME BINARY: Mesa's disassembler accepts every instruction and its listing assembles back into the same bytes.
judge() {
	"$tools/brw_disasm" --gen=icl --input-path="$2" > "$work/$1.asm" 2>&1
	if grep -q 'ERROR\|illegal' "$work/$1.asm"; then
		echo "$1: FAIL the disassembler rejects an instruction:"; grep -B1 'ERROR\|illegal' "$work/$1.asm" | head
		status=1; return
	fi
	if ! "$tools/brw_asm" --gen=icl -o "$work/$1.re" "$work/$1.asm" > "$work/$1.asm-log" 2>&1; then
		echo "$1: FAIL the listing does not assemble:"; head -5 "$work/$1.asm-log"
		status=1; return
	fi
	if ! cmp -s "$2" "$work/$1.re"; then
		echo "$1: FAIL re-assembled bytes differ"; status=1; return
	fi
	echo "$1: $(($(wc -c < "$2") / 16)) instructions; disassembled and re-assembled to the same bytes"
}

base="-std=gnu11 -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -I$repo/include -I$repo -idirafter $repo/include/libc"
cc $base -O1 "$here/gentool-eu.c" -o "$work/eu" || exit 1
cc $base -O1 "$here/eudump.c" -o "$work/eudump" -lm || exit 1
cc -std=gnu99 -O0 -Wall -Wextra -Wno-unused-function -I"$repo" -I"$repo/include" -DHAL_ARCH_AMD64 \
	-o "$work/cdump" "$here/compute-dump.c" -lm || exit 1

# 1. The encoder.
if "$work/eu" -gen11 "$work/encoder.bin"; then
	judge encoder "$work/encoder.bin"
else
	echo "encoder: FAIL the Ice Lake encoder poisoned its buffer"; status=1
fi

# 2. The vertex, fragment and geometry modules.
"$work/eudump" -gen11 vertex "$repo/userland/tests/vkdemo/shaders/cuboid.vert.spv" "$work/vkdemo-vertex.bin" 2> /dev/null &&
	judge vkdemo-vertex "$work/vkdemo-vertex.bin" || { echo "vkdemo-vertex: FAIL refused"; status=1; }
"$work/eudump" -gen11 fragment "$repo/userland/tests/vkdemo/shaders/cuboid.frag.spv" "$work/vkdemo-fragment.bin" 2> /dev/null &&
	judge vkdemo-fragment "$work/vkdemo-fragment.bin" || { echo "vkdemo-fragment: FAIL refused"; status=1; }
for spv in "$repo"/userland/tests/mview/shaders/*.spv "$repo"/src/drivers/gpu/i915/tests/render/compiler-shaders/*.spv \
    "$repo"/src/drivers/gpu/i915/tests/render/generality-shaders/*.spv "$repo"/src/drivers/gpu/i915/tests/render/feature-shaders/*.spv; do
	name=$(basename "$spv" .spv)
	refused=0
	case $name in
	dual-alone.frag|refuse-*) refused=1 ;;
	esac
	case $name in
	*.comp) continue ;;
	*.geom) stage=geometry ;;
	*.vert) stage=vertex ;;
	*) stage=fragment ;;
	esac
	if "$work/eudump" -gen11 $stage "$spv" "$work/$name.bin" 2> "$work/$name.log"; then
		if [ $refused -eq 1 ]; then
			echo "$name: FAIL compiled for Ice Lake, but must be refused"; status=1
		else
			judge "$name" "$work/$name.bin"
		fi
	elif [ $refused -eq 1 ]; then
		echo "$name: refused as expected"
	else
		echo "$name: FAIL refused for Ice Lake: $(cat "$work/$name.log")"; status=1
	fi
done
for source in "$repo"/plan/tools/i915-gen9/shaders/*.frag; do
	name=$(basename "$source")
	glslc --target-env=vulkan1.0 -o "$work/$name.spv" "$source" || { echo "$name: FAIL glslc"; status=1; continue; }
	if "$work/eudump" -gen11 fragment "$work/$name.spv" "$work/$name.bin" 2> "$work/$name.log"; then
		judge "$name" "$work/$name.bin"
	else
		echo "$name: FAIL refused for Ice Lake: $(cat "$work/$name.log")"; status=1
	fi
done

# 3. The compute modules.
mkdir -p "$work/cs"
for source in "$repo"/plan/tools/gpu-compute/host/shaders/*.comp "$repo"/src/drivers/gpu/i915/tests/render/compute-shaders/*.comp; do
	name=$(basename "$source" .comp)
	glslc --target-env=vulkan1.0 -fshader-stage=compute -o "$work/cs/$name.spv" "$source" || { echo "$name: FAIL glslc"; status=1; continue; }
	if "$work/cdump" -gen11 "$work/cs/$name.spv" "$work/cs/$name.bin" > "$work/cs/$name.log" 2>&1; then
		judge "compute-$name" "$work/cs/$name.bin"
	else
		echo "compute-$name: FAIL $(cat "$work/cs/$name.log")"; status=1
	fi
done

# 4. Intent: the forms of Gen12 and of Gen9 Ice Lake takes, in the kernels that use them.
# expect NAME LISTING PATTERN WHAT: the listing has a line matching PATTERN.
expect() {
	if grep -q -- "$3" "$work/$2"; then
		echo "$1: $4"
	else
		echo "$1: FAIL no $4 in $2"; status=1
	fi
}
# refute NAME LISTING PATTERN WHAT: the listing has no line matching PATTERN.
refute() {
	if grep -q -- "$3" "$work/$2"; then
		echo "$1: FAIL $4 in $2"; status=1
	else
		echo "$1: no $4"
	fi
}
expect barrier compute-shared.asm 'and(1).*g0.2<0,1,0>UD.*0x7f000000UD' "barrier ID masked with 0x7f000000"
expect barrier compute-shared.asm '^wait(1)' "WAIT on n0 after the gateway message"
refute fence compute-shared.asm 'DC mfence, bti 0,' "data cache fence for a shared-memory barrier (Gen9's one fence)"
expect fence compute-shared.asm 'DC mfence, bti 254,' "shared memory fence at entry 254"
expect fence compute-shared.asm '^mov(1) *null<1>UW *g[0-9]*<0,1,0>UW' "MOV of a fence's reply to null"
refute terminate compute-shared.asm 'send(8) .*0x82000010' "\"do not dereference URB\" end of thread"
refute sampler texops.frag.asm 'mov(8) *g[0-9]*<1>UD *g0<8,8,1>UD *{ align1 WE_all 1Q }' "sampler header copied from r0"
expect targets mrt.frag.asm 'mov(1) *g124.2<1>UD *0x00000003UD' "last write's header naming target 3"
expect dual dual.frag.asm 'mlen 6 ex_mlen 4' "header and first colour as one run, second colour as the other"

if [ $status -ne 0 ]; then
	echo "Ice Lake EU host test FAIL"
else
	echo "Ice Lake EU host test PASS"
fi
exit $status
