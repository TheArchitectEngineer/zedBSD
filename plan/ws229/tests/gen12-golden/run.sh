#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# ws229-p002: the Gen12 golden.  Builds golden.c against the i915 sources of
# a base commit (default d1e743394, main before WS229) and against this
# tree, runs both, and compares their outputs: every register write of the
# Gen12 GT programming in order, the workaround lists, the context images,
# the request rings and the submission writes.  PASS when identical.
# Output goes to a new directory under build/tmp (nothing is removed here;
# Q1's plan/tools/q1-clean.sh removes old runs).
#
# Usage: run.sh [base-commit]
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$repo/plan/ws229/tests/gen12-golden
base=${1:-d1e743394}
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws229-gen12-golden"
out=$fresh_dir
CC=${CC:-cc}

mkdir -p "$out/base"
git -C "$repo" archive "$base" src/drivers/gpu/i915 include | tar -x -C "$out/base"

# Builds the golden against one tree ($1) into $2, with extra flags $3.
build() {
	tree=$1
	dest=$2
	extra=${3:-}
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
	    "$driver/submit.c" "$driver/reset.c"; do
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
build "$repo" "$out/build-tree"
build "$repo" "$out/build-gen9" -DGOLDEN_GEN9
"$out/build-base/golden" > "$out/base.txt"
"$out/build-tree/golden" > "$out/tree.txt"
"$out/build-gen9/golden" > "$out/gen9.txt"
echo "gen9 smoke: $(grep -c '^W ' "$out/gen9.txt") register writes, $(grep -c 'rc=0' "$out/gen9.txt") steps rc=0, $(grep 'rc=' "$out/gen9.txt" | grep -vc 'rc=0') steps failed ($out/gen9.txt)"
lines=$(wc -l < "$out/tree.txt")
if cmp -s "$out/base.txt" "$out/tree.txt"; then
	echo "gen12 golden PASS: $lines lines identical to $base ($out)"
	exit 0
fi
diff "$out/base.txt" "$out/tree.txt" | head -40
echo "gen12 golden FAIL: differs from $base ($out)"
exit 1
