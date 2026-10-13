#!/bin/sh
#
# Builds the proposed hal.h comment diffs of ws171-p002 from the specs.
#
# Usage: plan/ws171/phase002/gen.sh WORKDIR
#
# WORKDIR is an empty scratch directory (for example one made by mktemp -d
# under build/tmp).  The script copies include/hal/hal.h there, applies the
# specs of spec/ in name order, one after another, and writes into diff/:
#   NN-area.diff  the change of one spec, on top of the specs before it
#   all.diff      every spec at once, against include/hal/hal.h
# Each step is checked to change comments only (check.py).  The script
# never touches include/hal/hal.h itself.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$1

mkdir -p "$here/diff"
cp "$top/include/hal/hal.h" "$work/hal.0.h"
previous="$work/hal.0.h"

for spec in "$here"/spec/*.txt; do
	name=$(basename "$spec" .txt)
	next="$work/hal.$name.h"
	cp "$previous" "$next"
	python3 "$here/apply.py" "$next" "$spec"
	python3 "$here/check.py" "$previous" "$next"
	(cd "$work" && diff -u --label a/include/hal/hal.h --label b/include/hal/hal.h \
	    "$(basename "$previous")" "$(basename "$next")") > "$here/diff/$name.diff" || true
	previous=$next
done

(cd "$work" && diff -u --label a/include/hal/hal.h --label b/include/hal/hal.h \
    hal.0.h "$(basename "$previous")") > "$here/diff/all.diff" || true
python3 "$here/check.py" "$work/hal.0.h" "$previous"
