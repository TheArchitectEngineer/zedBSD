#!/bin/sh
# The execlists equivalence test: builds the harness against two versions of
# submit.c and submit.h and compares their traces.
# Usage: run.sh <old submit.c> <old submit.h> <new submit.c> <new submit.h> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
i915="$repo/src/drivers/gpu/i915"
out=$5
headers="-DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/include/libc"
build() {
	mkdir -p "$out/$1"
	cp "$2" "$out/$1/submit.c"
	cp "$3" "$out/$1/submit.h"
	cc -std=gnu11 -Wall -Wextra -Werror $4 -I"$out/$1" -I"$i915" $headers -o "$out/$1/t" "$out/$1/submit.c" "$here/harness.c"
	"$out/$1/t" > "$out/$1.trace"
}
# The old version read the context's descriptor by its old field names.
build old "$1" "$2" "-DSUBMIT_EQUIV_OLD -Dlrc_desc=desc -Dlrc_reg_state=regs"
build new "$3" "$4" ""
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "submit-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "submit-equiv: FAIL"
	exit 1
fi
