#!/bin/sh
# The PPGTT equivalence test: builds the harness against two versions of
# ppgtt.c and ppgtt.h and compares their traces.
# Usage: run.sh <old ppgtt.c> <old ppgtt.h> <new ppgtt.c> <new ppgtt.h> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
i915="$root/src/drivers/gpu/i915"
out=$5
build() {
	mkdir -p "$out/$1/intel"
	cp "$2" "$out/$1/ppgtt.c"
	cp "$3" "$out/$1/ppgtt.h"
	cp "$i915/intel/gt-regs.h" "$i915/intel/bits.h" "$out/$1/intel/"
	cc -std=c11 -Wall -Wextra -Werror $4 -I"$here/stub" -I"$out/$1" -o "$out/$1/t" "$out/$1/ppgtt.c" "$here/harness.c"
	"$out/$1/t" > "$out/$1.trace"
}
build old "$1" "$2" -DPPGTT_EQUIV_OLD
build new "$3" "$4" ""
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "ppgtt-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "ppgtt-equiv: FAIL"
	exit 1
fi
