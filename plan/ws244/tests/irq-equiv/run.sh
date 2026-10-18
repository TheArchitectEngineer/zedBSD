#!/bin/sh
# The interrupt equivalence test: builds the harness against two versions of
# irq.c and irq.h and compares their traces.
# Usage: run.sh <old irq.c> <old irq.h> <new irq.c> <new irq.h> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
i915="$root/src/drivers/gpu/i915"
out=$5
build() {
	mkdir -p "$out/$1/intel"
	cp "$2" "$out/$1/irq.c"
	cp "$3" "$out/$1/irq.h"
	cp "$i915/device-info.h" "$i915/mmio.h" "$i915/sync.h" "$out/$1/"
	cp "$i915/intel/gt-regs.h" "$i915/intel/bits.h" "$out/$1/intel/"
	cp "$here/stub/i915.h" "$out/$1/"
	cc -std=c11 -Wall -Wextra -Werror $4 -I"$here/stub" -I"$out/$1" -o "$out/$1/t" "$out/$1/irq.c" "$here/harness.c"
	"$out/$1/t" > "$out/$1.trace"
}
build old "$1" "$2" -DIRQ_EQUIV_OLD
build new "$3" "$4" ""
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "irq-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "irq-equiv: FAIL"
	exit 1
fi
