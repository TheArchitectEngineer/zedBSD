#!/bin/sh
# Compares the Mali compiler's listings of two runs of mali-compiler-desktop-test.sh (or mali-compiler-host-test.sh)
# byte for byte: every listing of the base run must be in the new run with the same bytes.  Listings only the new run
# has (new test modules) are counted, not compared.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws313/tests/mali-listing-compare.sh BASE_DIR NEW_DIR
# The directories are the ones the tests print on their last line.  One line per differing or missing listing, then
# a last line SAME (N listings) or DIFFERENT (N).  Nothing is written or removed.
set -u
base=${1:?usage: mali-listing-compare.sh BASE_DIR NEW_DIR}
new=${2:?usage: mali-listing-compare.sh BASE_DIR NEW_DIR}
count=0
bad=0
extra=0
for file in "$base"/*.txt; do
	name=$(basename "$file")
	[ "$name" = result.txt ] && continue
	count=$((count + 1))
	if [ ! -f "$new/$name" ]; then
		echo "MISSING $name"
		bad=$((bad + 1))
	elif ! cmp -s "$file" "$new/$name"; then
		echo "DIFFERS $name"
		bad=$((bad + 1))
	fi
done
for file in "$new"/*.txt; do
	name=$(basename "$file")
	[ "$name" = result.txt ] && continue
	[ -f "$base/$name" ] || extra=$((extra + 1))
done
if [ "$bad" = 0 ]; then
	echo "SAME ($count listings, $extra only in the new run)"
else
	echo "DIFFERENT ($bad of $count)"
fi
[ "$bad" = 0 ]
