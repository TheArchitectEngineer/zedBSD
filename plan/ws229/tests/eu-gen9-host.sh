#!/bin/sh
# ws229-p003: the i915 compiler's Gen9 kernels on the host, judged by Mesa 25.0.7's brw_disasm / brw_asm (--gen=skl).
#
# 1. The encoder test (plan/ws031/tests/i915-vk-gentool-eu.c -gen9): every emitter once.
# 2. Every vertex, fragment and geometry module the Gen12 gentool test compiles (plan/ws031/tests/run-vk-gentool-test.sh),
#    compiled for Gen9 (i915-vk-eudump.c -gen9); the modules refused on purpose must stay refused.
# 3. Every compute module of the ws101 host test and of the vkcs scenario (compute-dump.c -gen9).
# Each kernel must disassemble without an error and assemble back into the same bytes.  Gen12 is judged by the existing
# tests (run-vk-gentool-test.sh, plan/ws101/tests/host/run.sh, plan/ws075/tests/guard/run.sh), which this one leaves alone.
#
#   plan/ws229/tests/eu-gen9-host.sh       (BRW_TOOLS: a Mesa 25.0.7 build's src/intel/compiler, default below)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tools=${BRW_TOOLS:-$repo/build/mesa-tools/build-asm/src/intel/compiler}
# A new directory for each run under build/tmp; nothing is removed here (Q1's plan/tools/q1-clean.sh removes old runs).
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws229-eu-gen9"
work=$fresh_dir
status=0

if [ ! -x "$tools/brw_disasm" ] || [ ! -x "$tools/brw_asm" ]; then
	echo "FAIL: no brw_disasm/brw_asm under $tools (plan/ws101/tests/host/mesa-tools.sh builds them, or set BRW_TOOLS)"
	exit 1
fi

# judge NAME BINARY: Mesa's disassembler accepts every instruction and its listing assembles back into the same bytes.
judge() {
	"$tools/brw_disasm" --gen=skl --input-path="$2" > "$work/$1.asm" 2>&1
	if grep -q 'ERROR\|illegal' "$work/$1.asm"; then
		echo "$1: FAIL the disassembler rejects an instruction:"; grep -B1 'ERROR\|illegal' "$work/$1.asm" | head
		status=1; return
	fi
	if ! "$tools/brw_asm" --gen=skl -o "$work/$1.re" "$work/$1.asm" > "$work/$1.asm-log" 2>&1; then
		echo "$1: FAIL the listing does not assemble:"; head -5 "$work/$1.asm-log"
		status=1; return
	fi
	if ! cmp -s "$2" "$work/$1.re"; then
		echo "$1: FAIL re-assembled bytes differ"; status=1; return
	fi
	echo "$1: $(($(wc -c < "$2") / 16)) instructions; disassembled and re-assembled to the same bytes"
}

base="-std=gnu11 -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -I$repo/include -I$repo -idirafter $repo/include/libc"
cc $base -O1 "$repo/plan/ws031/tests/i915-vk-gentool-eu.c" -o "$work/eu" || exit 1
cc $base -O1 "$repo/plan/ws031/tests/i915-vk-eudump.c" -o "$work/eudump" -lm || exit 1
cc -std=gnu99 -O0 -Wall -Wextra -Wno-unused-function -I"$repo" -I"$repo/include" -DHAL_ARCH_AMD64 \
	-o "$work/cdump" "$repo/plan/ws101/tests/host/compute-dump.c" -lm || exit 1

# 1. The encoder.
if "$work/eu" -gen9 "$work/encoder.bin"; then
	judge encoder "$work/encoder.bin"
else
	echo "encoder: FAIL the Gen9 encoder poisoned its buffer"; status=1
fi

# 2. The vertex, fragment and geometry modules.
"$work/eudump" -gen9 vertex "$repo/userland/tests/vkdemo/shaders/cuboid.vert.spv" "$work/vkdemo-vertex.bin" 2> /dev/null &&
	judge vkdemo-vertex "$work/vkdemo-vertex.bin" || { echo "vkdemo-vertex: FAIL refused"; status=1; }
"$work/eudump" -gen9 fragment "$repo/userland/tests/vkdemo/shaders/cuboid.frag.spv" "$work/vkdemo-fragment.bin" 2> /dev/null &&
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
	if "$work/eudump" -gen9 $stage "$spv" "$work/$name.bin" 2> "$work/$name.log"; then
		if [ $refused -eq 1 ]; then
			echo "$name: FAIL compiled for Gen9, but must be refused"; status=1
		else
			judge "$name" "$work/$name.bin"
		fi
	elif [ $refused -eq 1 ]; then
		echo "$name: refused as expected"
	else
		echo "$name: FAIL refused for Gen9: $(cat "$work/$name.log")"; status=1
	fi
done

# 2b. ws229's own fragment modules for the Gen9-only render-target header (several targets, a target other than 0 with
#     discard), judged for Gen9 and, as a check that the Gen12 path still takes them, for Gen12 too (--gen=adl).
for source in "$repo"/plan/ws229/tests/shaders/*.frag; do
	name=$(basename "$source")
	glslc --target-env=vulkan1.0 -o "$work/$name.spv" "$source" || { echo "$name: FAIL glslc"; status=1; continue; }
	if "$work/eudump" -gen9 fragment "$work/$name.spv" "$work/$name.bin" 2> "$work/$name.log"; then
		judge "$name" "$work/$name.bin"
	else
		echo "$name: FAIL refused for Gen9: $(cat "$work/$name.log")"; status=1
	fi
	if "$work/eudump" fragment "$work/$name.spv" "$work/$name.gen12.bin" 2> "$work/$name.gen12.log"; then
		"$tools/brw_disasm" --gen=adl --input-path="$work/$name.gen12.bin" > "$work/$name.gen12.asm" 2>&1
		"$tools/brw_asm" --gen=adl -o "$work/$name.gen12.re" "$work/$name.gen12.asm" > /dev/null 2>&1
		if grep -q 'ERROR\|illegal' "$work/$name.gen12.asm" || ! cmp -s "$work/$name.gen12.bin" "$work/$name.gen12.re"; then
			echo "$name (Gen12): FAIL the round trip"; status=1
		else
			echo "$name (Gen12): disassembled and re-assembled to the same bytes"
		fi
	else
		echo "$name (Gen12): FAIL refused: $(cat "$work/$name.gen12.log")"; status=1
	fi
done

# 3. The compute modules.
mkdir -p "$work/cs"
for source in "$repo"/plan/ws101/tests/host/shaders/*.comp "$repo"/src/drivers/gpu/i915/tests/render/compute-shaders/*.comp; do
	name=$(basename "$source" .comp)
	glslc --target-env=vulkan1.0 -fshader-stage=compute -o "$work/cs/$name.spv" "$source" || { echo "$name: FAIL glslc"; status=1; continue; }
	if "$work/cdump" -gen9 "$work/cs/$name.spv" "$work/cs/$name.bin" > "$work/cs/$name.log" 2>&1; then
		judge "compute-$name" "$work/cs/$name.bin"
	else
		echo "compute-$name: FAIL $(cat "$work/cs/$name.log")"; status=1
	fi
done

# 4. Intent, not only a round trip (design review-1 M6): the round trip passes an opcode or a descriptor that is wrong
#    but valid.  The encoder's bytes must equal Mesa's assembly of the reviewed listing eu-gen9-expect.asm, and the
#    Gen9-only message forms of the code generator must be in the kernels that use them.
"$tools/brw_asm" --gen=skl -o "$work/expect.bin" "$repo/plan/ws229/tests/eu-gen9-expect.asm" > "$work/expect.log" 2>&1
if [ -s "$work/expect.bin" ] && cmp -s "$work/expect.bin" "$work/encoder.bin"; then
	echo "encoder: the bytes equal Mesa's assembly of the reviewed Gen9 listing"
else
	echo "encoder: FAIL the bytes differ from Mesa's assembly of eu-gen9-expect.asm"; status=1
fi
# expect NAME LISTING PATTERN WHAT: the listing has a line matching PATTERN.
expect() {
	if grep -q -- "$3" "$work/$2"; then
		echo "$1: $4"
	else
		echo "$1: FAIL no $4 in $2"; status=1
	fi
}
expect barrier compute-shared.asm 'and(1).*g0.2<0,1,0>UD.*0x8f000000UD' "barrier ID masked with 0x8f000000"
expect barrier compute-shared.asm '^wait(1)' "WAIT on n0 after the gateway message"
expect fence compute-shared.asm 'DC mfence, bti 0,' "one data cache fence at entry 0"
expect fence compute-shared.asm '^mov(1) *null<1>UW *g[0-9]*<0,1,0>UW' "MOV of the fence's reply to null"
expect terminate compute-shared.asm 'send(8) .*0x82000010' "thread spawner message with \"do not dereference URB\""
expect sampler texops.frag.asm 'mov(8) *g[0-9]*<1>UD *g0<8,8,1>UD *{ align1 WE_all 1Q }' "sampler header copied from r0"
expect targets mrt.frag.asm 'mov(1) *g124.2<1>UD *0x00000003UD' "last write's header naming target 3"
expect targets mrt-kill.frag.asm 'mov(1) *g124.15<1>UW *f1<0,1,0>UW' "live pixels in header word 15"
expect dual dual.frag.asm 'mlen 6 ex_mlen 4' "header and first colour as one run, second colour as the other"

if [ $status -ne 0 ]; then
	echo "ws229 Gen9 EU host test FAIL"
else
	echo "ws229 Gen9 EU host test PASS"
fi
exit $status
