#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The Gen12 GT golden.  Builds golden.c against the i915 sources of a base
# commit (default 684a5243f, main before WS252) and against this tree, runs
# both, and compares their outputs: every register write of the Gen12 GT
# programming in order, the workaround lists, the context images, the
# request rings and the submission writes.  The tree's run takes the Alder
# Lake-P row of the product table (0x46a8).  Then the tree is run once for
# each Gen12 product row (Tiger Lake, Rocket Lake, Alder Lake-S, Raptor
# Lake-S, Alder Lake-P) and check-linux.py compares their engines and the
# platform-only render rule with what Linux v6.8.12 says (LINUX: the Linux
# tree, default build/linux/linux-6.8.12; without it the check is skipped
# and said so).  PASS when the base comparison is identical and the check
# passes.  Output goes to a new directory under build/tmp (nothing is
# removed here; Q1's plan/tools/q1-clean.sh removes old runs).
#
# Usage: run.sh [base-commit]
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$repo/plan/ws252/tests/gen12-golden
base=${1:-684a5243f}
linux=${LINUX:-$repo/build/linux/linux-6.8.12}
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws252-gen12-golden"
out=$fresh_dir
CC=${CC:-cc}

mkdir -p "$out/base"
git -C "$repo" archive "$base" src/drivers/gpu/i915 include | tar -x -C "$out/base"

# Builds the golden against one tree ($1) into $2, with extra flags $3.
build() {
	tree=$1
	dest=$2
	extra=${3:-}
	products=
	if [ -f "$tree/src/drivers/gpu/i915/product.c" ]; then
		products=$tree/src/drivers/gpu/i915/product.c
	fi
	driver=$tree/src/drivers/gpu/i915
	contracts=$driver/tests/contracts
	mkdir -p "$dest"
	host="-DKERN_USER_ABI_LP64 -I$tree/include -I$tree/src -I$tree -idirafter $tree/include/libc -I$contracts"
	kernel="-DKERN_USER_ABI_LP64 -I$tree/include -I$tree/src -I$tree -I$tree/include/libc -DKERN_UAPI_NATIVE -I$contracts"
	flags="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections"
	objects=
	for source in "$here/golden.c" "$here/stubs.c" "$contracts/mock_mmio.c" "$contracts/host_kernel.c" "$contracts/host_thread.c" "$contracts/contract.c" \
	    "$driver/workarounds.c" "$driver/gt-power.c" "$driver/mmio.c" "$driver/trace.c" "$driver/workqueue.c" "$driver/power.c" \
	    "$driver/sync.c" "$driver/irq.c" "$driver/device-info.c" "$driver/engine.c" "$driver/context.c" "$driver/request.c" \
	    "$driver/submit.c" "$driver/reset.c" $products; do
		name=$(basename "$source" .c)
		case $name in
		workqueue|host_thread) headers=$kernel ;;
		*) headers=$host ;;
		esac
		"$CC" $flags $extra $headers -c "$source" -o "$dest/$name.o"
		objects="$objects $dest/$name.o"
	done
	"$CC" -Wl,--gc-sections $objects -o "$dest/golden"
}

build "$out/base" "$out/build-base"
build "$repo" "$out/build-tree" -DGOLDEN_PRODUCT=0x46a8
build "$repo" "$out/build-gen9" -DGOLDEN_GEN9
"$out/build-base/golden" > "$out/base.txt"
"$out/build-tree/golden" > "$out/tree.txt" 2> /dev/null
"$out/build-gen9/golden" > "$out/gen9.txt"
echo "gen9 smoke: $(grep -c '^W ' "$out/gen9.txt") register writes, $(grep -c 'rc=0' "$out/gen9.txt") steps rc=0, $(grep 'rc=' "$out/gen9.txt" | grep -vc 'rc=0') steps failed ($out/gen9.txt)"
status=0
lines=$(wc -l < "$out/tree.txt")
if cmp -s "$out/base.txt" "$out/tree.txt"; then
	echo "gen12 golden PASS: $lines lines identical to $base ($out)"
else
	diff "$out/base.txt" "$out/tree.txt" | head -40
	echo "gen12 golden FAIL: differs from $base ($out)"
	status=1
fi

# One run per Gen12 product row: Tiger Lake, Rocket Lake, Alder Lake-S, Raptor Lake-S, Alder Lake-P.
for product in tgl:0x9a49 rkl:0x4c8a adls:0x4680 rpls:0xa780 adlp:0x46a8; do
	row=${product%%:*}
	build "$repo" "$out/build-$row" -DGOLDEN_PRODUCT=${product#*:}
	"$out/build-$row/golden" > "$out/$row.txt" 2> "$out/$row.product"
done
if [ -d "$linux/drivers/gpu/drm/i915" ]; then
	if ! python3 -I "$here/check-linux.py" "$linux" "$out"; then
		status=1
	fi
else
	echo "linux check: SKIPPED (no Linux tree at $linux)"
fi
exit $status
