#!/bin/sh
#
# Builds the sun4u handoff builder and its OpenFirmware client from a base
# commit and from the work tree, runs sparc-handoff-driver.c over both, and
# compares their output.
#
# usage: sparc-handoff-run.sh BASE_COMMIT OUT_DIR

set -eu

base=$1
out=$2
top=$(cd "$(dirname "$0")/../../.." && pwd)
cc=${HOSTCC:-cc}

mkdir -p "$out/old" "$out/new"
git -C "$top" archive "$base" bootloader include | tar -x -C "$out/old"
cp -R "$top/bootloader" "$top/include" "$out/new/"

for tree in old new; do
	$cc -std=c11 -O1 -w -I"$out/$tree" -I"$out/$tree/include" \
		"$top/plan/ws262/tests/sparc-handoff-driver.c" \
		"$out/$tree/bootloader/sparcv9/handoff.c" \
		"$out/$tree/bootloader/sparcv9/ofw.c" \
		-o "$out/$tree/driver"
	"$out/$tree/driver" > "$out/$tree.txt"
done

wc -l "$out/old.txt" "$out/new.txt"
if cmp -s "$out/old.txt" "$out/new.txt"; then
	echo "PASS: identical output"
else
	echo "FAIL: outputs differ"
	diff "$out/old.txt" "$out/new.txt" | head -20
	exit 1
fi
