#!/bin/sh
# The GT reset equivalence test: builds the harness against the GT reset
# part (before the recovery part) of two versions of reset.c and compares
# their traces.  Usage: run.sh <old reset.c> <new reset.c> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
i915="$root/src/drivers/gpu/i915"
old=$1; new=$2; out=$3
for v in old new; do
	eval src=\$$v
	mkdir -p "$out/$v/intel"
	n=$(grep -n '^ \* Recovery of the GPU node\.$' "$src" | cut -d: -f1)
	head -n $((n - 3)) "$src" > "$out/$v/reset.c"
	cp "$i915/reset.h" "$i915/device-info.h" "$i915/mmio.h" "$i915/sync.h" "$out/$v/"
	cp "$i915/intel/gt-regs.h" "$i915/intel/bits.h" "$out/$v/intel/"
	for h in i915.h memory.h ppgtt.h request-queue.h session.h worker.h; do cp "$here/stub/$h" "$out/$v/"; done
	cc -std=c11 -Wall -Wextra -Wno-unused-function -Werror -I"$here/stub" -I"$out/$v" -o "$out/$v/t" "$out/$v/reset.c" "$here/harness.c"
	"$out/$v/t" > "$out/$v.trace"
done
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "reset-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "reset-equiv: FAIL"
	exit 1
fi
