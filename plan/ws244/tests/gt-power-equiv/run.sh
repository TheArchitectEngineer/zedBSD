#!/bin/sh
# The GT power equivalence test: builds the harness against two versions of
# gt-power.c and gt-power.h and compares their traces.
# Usage: run.sh <old gt-power.c> <old gt-power.h> <new gt-power.c> <new gt-power.h> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
i915="$repo/src/drivers/gpu/i915"
out=$5
headers="-DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/include/libc"
build() {
	mkdir -p "$out/$1"
	cp "$2" "$out/$1/gt-power.c"
	cp "$3" "$out/$1/gt-power.h"
	cc -std=gnu11 -Wall -Wextra -Werror $4 -I"$out/$1" -I"$i915" $headers -o "$out/$1/t" "$out/$1/gt-power.c" "$here/harness.c"
	"$out/$1/t" > "$out/$1.trace"
}
build old "$1" "$2" "-DGT_POWER_EQUIV_OLD"
build new "$3" "$4" ""
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "gt-power-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "gt-power-equiv: FAIL"
	exit 1
fi
