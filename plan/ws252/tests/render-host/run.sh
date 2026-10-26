#!/bin/sh
# The render path's 3D and compute state for Gen12, Gen9 and Ice Lake (Gen11), on the host.
#
# 1. state-test.c builds a depth-stencil draw, an indexed draw with a geometry kernel, a fill and a copy rectangle, a
#    dispatch with shared memory and an indirect dispatch, the surface states, samplers and interface descriptors
#    they point at, and the hardware tests' draw fixture, for graphics versions 12, 9 and 11.
# 2. The same test is built with -DTEST_BASELINE against the tree of BASE (main before WS252's Ice Lake forms) and
#    its version 12 and 9 files must be the new tree's byte for byte: the Gen12 and Gen9 state does not change.
# 3. gen11-check.py decodes every version 12 file with Mesa's gen120.xml and every version 11 file with gen110.xml
#    (every dword in an instruction, no bit outside a field) and compares the two field by field.
#
#   plan/ws252/tests/render-host/run.sh     (GENXML: a Mesa 25.0.7 tree's src/intel/genxml, default
#                                            build/mesa/mesa-25.0.7/src/intel/genxml; BASE: the commit to compare
#                                            Gen12 and Gen9 with, default below)
# The run's files are under build/ws252-render-host (plan/tools/fresh-out.sh; Q1 removes old runs).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$repo/plan/ws252/tests/render-host
genxml=${GENXML:-$repo/build/mesa/mesa-25.0.7/src/intel/genxml}
base=${BASE:-684a5243f}
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/ws252-render-host"
work=$fresh_dir
status=0

if [ ! -f "$genxml/gen110.xml" ]; then
	echo "FAIL: no gen110.xml under $genxml (set GENXML)"
	exit 1
fi

# The executor's sources as the ws101 host test links them; draw.c, blit.c and compute.c are in the test itself.
link_list() {
	tree=$1
	list=""
	for part in codec object dispatch transport instance vulkan fence objects reply memory image descriptor pipeline \
	    pipeline-prepare render-pass sync state batch math forget video video-mfx video-h264-tables command; do
		# The executor moved from render/X.c to vk/vk-X.c (vulkan.c to vk.c); BASE has the old names.
		if [ -d "$tree/src/drivers/gpu/i915/vk" ]; then
			case $part in
			vulkan) list="$list $tree/src/drivers/gpu/i915/vk/vk.c" ;;
			*) list="$list $tree/src/drivers/gpu/i915/vk/vk-$part.c" ;;
			esac
		else
			list="$list $tree/src/drivers/gpu/i915/render/$part.c"
		fi
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

# The base tree, with this test and the stubs it reads copied into it.
mkdir -p "$work/base/plan/ws252/tests/render-host" "$work/base-out"
git -C "$repo" archive "$base" src/drivers/gpu include plan/ws031/tests | tar -x -C "$work/base" || exit 1
cp "$here/state-test.c" "$work/base/plan/ws252/tests/render-host/"
cc $flags -DTEST_BASELINE -I"$work/base/include" -I"$work/base" -idirafter "$work/base/include/libc" \
	-o "$work/state-test-base" "$work/base/plan/ws252/tests/render-host/state-test.c" $(link_list "$work/base") -lm || exit 1
"$work/state-test-base" "$work/base-out" || exit 1
same=0
for file in "$work"/base-out/12-*.bin "$work"/base-out/9-*.bin; do
	name=$(basename "$file")
	if cmp -s "$file" "$work/new/$name"; then
		same=$((same + 1))
	else
		echo "FAIL $name: the bytes differ from $base's"
		status=1
	fi
done
echo "Gen12 and Gen9: $same files byte for byte as $base's"

# The decode of the Gen12 and Ice Lake files and their comparison.
python3 -I "$here/gen11-check.py" "$genxml" "$work/new" || status=1

if [ $status -ne 0 ]; then
	echo "render host test FAIL"
else
	echo "render host test PASS"
fi
exit $status
