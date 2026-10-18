#!/bin/sh
# The GT information equivalence test: builds the harness against two
# versions of device-info.c and compares their traces.
# Usage: run.sh <old device-info.c> <new device-info.c> <new empty out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
i915="$root/src/drivers/gpu/i915"
old=$1; new=$2; out=$3
for v in old new; do
	eval src=\$$v
	mkdir -p "$out/$v/intel"
	cp "$src" "$out/$v/device-info.c"
	cp "$i915/device-info.h" "$i915/mmio.h" "$out/$v/"
	cp "$i915/intel/engine-table.inc" "$i915/intel/engine-table-gen9.inc" "$out/$v/intel/"
	cc -std=c11 -Wall -Wextra -Werror -I"$here/stub" -I"$out/$v" -o "$out/$v/t" "$out/$v/device-info.c" "$here/harness.c"
	"$out/$v/t" > "$out/$v.trace"
done
if diff -u "$out/old.trace" "$out/new.trace"; then
	echo "device-info-equiv: PASS ($(wc -l < "$out/new.trace") lines)"
else
	echo "device-info-equiv: FAIL"
	exit 1
fi
