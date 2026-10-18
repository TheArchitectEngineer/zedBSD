#!/bin/sh
# The context image equivalence test: builds the harness against two
# versions of context.c and context.h and compares their traces.
# Usage: run.sh <old context.c> <old context.h> <new context.c> <new context.h> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
i915="$repo/src/drivers/gpu/i915"
out=$5
headers="-DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/include/libc"
build() {
	mkdir -p "$out/$1"
	cp "$2" "$out/$1/context.c"
	cp "$3" "$out/$1/context.h"
	cc -std=gnu11 -Wall -Wextra -Werror $4 -I"$out/$1" -I"$i915" $headers -o "$out/$1/t" "$out/$1/context.c" "$here/harness.c"
	"$out/$1/t" > "$out/$1.trace"
}
# The old version called the AUX emitter by its old name.
build old "$1" "$2" "-DCONTEXT_EQUIV_OLD -Ddrv_i915_gen12_emit_aux_table_inv=drv_i915_emit_aux_inv"
build new "$3" "$4" ""
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "context-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "context-equiv: FAIL"
	exit 1
fi
