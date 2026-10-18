#!/bin/sh
# The PCODE equivalence test: builds the harness against two versions of
# power.c and compares their traces.  Usage: run.sh <old power.c> <new power.c> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
old=$1; new=$2; out=$3
for v in old new; do
	eval src=\$$v
	mkdir -p "$out/$v"
	cp "$src" "$out/$v/power.c"
	cp "$root/src/drivers/gpu/i915/power.h" "$here/stub/mmio.h" "$here/stub/sync.h" "$out/$v/"
	cc -std=c11 -Wall -Wextra -Werror -I"$here/stub" -I"$out/$v" -o "$out/$v/t" "$out/$v/power.c" "$here/harness.c"
	"$out/$v/t" > "$out/$v.trace"
done
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "pcode-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "pcode-equiv: FAIL"
	exit 1
fi
