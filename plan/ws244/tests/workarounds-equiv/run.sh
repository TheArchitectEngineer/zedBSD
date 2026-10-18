#!/bin/sh
# The workaround equivalence test: builds the harness against two versions
# of workarounds.c and workarounds.h and compares their traces.
# Usage: run.sh <old workarounds.c> <old workarounds.h> <new workarounds.c> <new workarounds.h> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
i915="$repo/src/drivers/gpu/i915"
out=$5
headers="-DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/include/libc"
# The version under test is included first, before the tree's headers pull in theirs.
build() {
	mkdir -p "$out/$1"
	cp "$2" "$out/$1/workarounds.c"
	cp "$3" "$out/$1/workarounds.h"
	cc -std=gnu11 -Wall -Wextra -Werror $4 -I"$out/$1" -I"$i915" $headers -include "$out/$1/workarounds.h" -o "$out/$1/t" "$out/$1/workarounds.c" "$here/harness.c"
	"$out/$1/t" > "$out/$1.trace"
}
build old "$1" "$2" "-DWA_EQUIV_OLD"
build new "$3" "$4" ""
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "workarounds-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "workarounds-equiv: FAIL"
	exit 1
fi
