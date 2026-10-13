#!/bin/sh
# ws229-p004: the render path's 3D and compute state for Gen12 and Gen9, on the host.
#
# 1. state-test.c builds a depth-stencil draw, an indexed draw with a geometry kernel, a fill and a copy rectangle, a
#    dispatch with shared memory and an indirect dispatch, and the surface states, samplers and interface descriptors
#    they point at, once for graphics version 12 and once for 9 (render/draw.c, blit.c, compute.c, state.c).
# 2. The same test is built with -DTEST_BASELINE against the tree of BASE (the commit before ws229-p004) and its
#    version 12 files must be the new tree's byte for byte: the Gen12 state does not change.
# 3. gen9-check.py decodes every version 12 file with Mesa's gen120.xml and every version 9 file with gen90.xml
#    (every dword in an instruction, no bit outside a field, no instruction the version lacks), and compares the two
#    versions field by field: a field both versions name must hold the same value unless it is a listed difference.
# 4. The start of a cleared instruction window is disassembled by Mesa's brw_disasm for Tiger Lake and for Skylake.
# 5. The draw fixture of the hardware tests (tests/fixtures/draw-fixture.c) goes through 1-3 too (ws229-p005); its
#    Gen9 pixel shader must be brw_asm --gen=skl's bytes for plan/ws229/tests/fixtures/draw-fixture-ps-gen9.asm, and
#    the room after it must disassemble as end-of-thread sends only.
#
#   plan/ws229/tests/host/run.sh      (GENXML: a Mesa 25.0.7 tree's src/intel/genxml; BRW_TOOLS: its build's
#                                      src/intel/compiler; BASE: the commit to compare Gen12 with, default below)
# The run's files are under build/ws229-p004/host (plan/tools/fresh-out.sh; Q1 removes old runs).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$repo/plan/ws229/tests/host
genxml=${GENXML:-$repo/build/mesa-tools/mesa-25.0.7/src/intel/genxml}
tools=${BRW_TOOLS:-$repo/build/mesa-tools/build-asm/src/intel/compiler}
base=${BASE:-03dee2287}
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/ws229-p004/host"
work=$fresh_dir
status=0

if [ ! -f "$genxml/gen90.xml" ]; then
	echo "FAIL: no gen90.xml under $genxml (plan/ws101/tests/host/mesa-tools.sh unpacks it, or set GENXML)"
	exit 1
fi

# The executor's sources as the ws101 host test links them; draw.c, blit.c and compute.c are in the test itself.
link_list() {
	tree=$1
	list=""
	for part in codec object dispatch transport instance vulkan fence objects reply memory image descriptor pipeline \
	    pipeline-prepare render-pass sync state batch math forget video video-mfx video-h264-tables command; do
		list="$list $tree/src/drivers/gpu/i915/render/$part.c"
	done
	list="$list $tree/src/drivers/gpu/compiler/spirv.c"
	list="$list $tree/src/drivers/gpu/i915/tests/fixtures/draw-fixture.c"
	for part in compile eu; do
		list="$list $tree/src/drivers/gpu/i915/compiler/$part.c"
	done
	echo "$list"
}
flags="-std=gnu11 -Wall -Wextra -Werror -Wdeclaration-after-statement -Wno-unused-function -DKERN_USER_ABI_LP64"

# The new tree.
mkdir -p "$work/new"
cc $flags -I"$repo/include" -I"$repo" -idirafter "$repo/include/libc" -o "$work/state-test" "$here/state-test.c" \
	$(link_list "$repo") -lm || exit 1
"$work/state-test" "$work/new" || exit 1

# The tree before ws229-p004, with this test copied into it.
mkdir -p "$work/base/plan/ws229/tests/host" "$work/base-out"
git -C "$repo" archive "$base" src/drivers/gpu include plan/ws031/tests | tar -x -C "$work/base" || exit 1
cp "$here/state-test.c" "$work/base/plan/ws229/tests/host/"
cc $flags -DTEST_BASELINE -I"$work/base/include" -I"$work/base" -idirafter "$work/base/include/libc" \
	-o "$work/state-test-base" "$work/base/plan/ws229/tests/host/state-test.c" $(link_list "$work/base") -lm || exit 1
"$work/state-test-base" "$work/base-out" || exit 1
same=0
for file in "$work"/base-out/12-*.bin; do
	name=$(basename "$file")
	if cmp -s "$file" "$work/new/$name"; then
		same=$((same + 1))
	else
		echo "FAIL $name: the Gen12 bytes differ from $base's"
		status=1
	fi
done
echo "Gen12: $same files byte for byte as $base's"

# The decode of both versions and their comparison.
python3 -I "$here/gen9-check.py" "$genxml" "$work/new" || status=1

# The cleared window's instructions, as Mesa disassembles them.
if [ -x "$tools/brw_disasm" ]; then
	"$tools/brw_disasm" --gen=tgl --input-path="$work/new/12-eot.bin" > "$work/eot12.asm" 2>&1
	"$tools/brw_disasm" --gen=skl --input-path="$work/new/9-eot.bin" > "$work/eot9.asm" 2>&1
	for asm in "$work/eot12.asm" "$work/eot9.asm"; do
		if grep -q 'ERROR\|illegal' "$asm" || ! grep -q 'EOT' "$asm"; then
			echo "FAIL $(basename "$asm"): not an end of thread"
			status=1
		fi
	done
	echo "cleared window: $(grep -c 'EOT' "$work/eot9.asm") Gen9 and $(grep -c 'EOT' "$work/eot12.asm") Gen12 end-of-thread sends"
else
	echo "NOTE: no brw_disasm under $tools; the cleared window's disassembly is NOT RUN"
fi

# The draw fixture's Gen9 pixel shader, against its assembly, and the room after it.
if [ -x "$tools/brw_asm" ] && [ -x "$tools/brw_disasm" ]; then
	"$tools/brw_asm" --gen=skl -o "$work/fixture-ps9.re" "$repo/plan/ws229/tests/fixtures/draw-fixture-ps-gen9.asm" \
		> "$work/fixture-ps9.asm-log" 2>&1 || status=1
	size=$(wc -c < "$work/fixture-ps9.re")
	head -c "$size" "$work/new/9-fixture-ps.bin" > "$work/fixture-ps9.bin"
	if [ "$size" -ne 192 ] || ! cmp -s "$work/fixture-ps9.re" "$work/fixture-ps9.bin"; then
		echo "FAIL fixture-ps: the Gen9 shader is not brw_asm's bytes ($size bytes assembled)"
		status=1
	fi
	tail -c +"$((size + 1))" "$work/new/9-fixture-ps.bin" > "$work/fixture-carpet9.bin"
	"$tools/brw_disasm" --gen=skl --input-path="$work/fixture-carpet9.bin" > "$work/fixture-carpet9.asm" 2>&1
	carpet=$(grep -c '^sendc' "$work/fixture-carpet9.asm")
	if grep -q 'ERROR\|illegal' "$work/fixture-carpet9.asm" || [ "$carpet" -ne 52 ] || \
	    [ "$(grep -c 'EOT' "$work/fixture-carpet9.asm")" -ne 52 ]; then
		echo "FAIL fixture-carpet: the room after the Gen9 shader is not 52 end-of-thread sends"
		status=1
	fi
	echo "draw fixture: the Gen9 shader is brw_asm's $size bytes, followed by $carpet Gen9 end-of-thread sends"
else
	echo "NOTE: no brw_asm/brw_disasm under $tools; the draw fixture's Gen9 shader check is NOT RUN"
fi

if [ $status -ne 0 ]; then
	echo "ws229 state host test FAIL"
else
	echo "ws229 state host test PASS"
fi
exit $status
