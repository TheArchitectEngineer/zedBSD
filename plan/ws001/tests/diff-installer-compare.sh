#!/bin/sh
# ws001-p029: runs the diff invocation the installer (treecopy.noct) makes,
# diff -r -q --metadata, with two diff binaries and compares output and
# status, so that the rewritten diff keeps the installer's contract.  Each
# run gets freshly made trees, because reading a tree moves its access
# times, which --metadata compares.
#   sh plan/ws001/tests/diff-installer-compare.sh OLD_DIFF NEW_DIFF
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
old=$1
new=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0

# Makes trees a and b in a directory: equal, then with one difference.
make_trees() {
	rm -rf "$1"
	mkdir -p "$1/a/d" "$1/b/d"
	for tree in a b; do
		printf 'x\n' > "$1/$tree/f"
		printf 'y\n' > "$1/$tree/d/g"
		ln "$1/$tree/f" "$1/$tree/h"
		ln -s f "$1/$tree/l"
	done
	case $2 in
	content) printf 'z\n' > "$1/b/d/g" ;;
	mode) chmod 600 "$1/b/f" ;;
	time) touch -t 200101020304.05 "$1/b/d/g" ;;
	link) rm "$1/b/h"; printf 'x\n' > "$1/b/h" ;;
	only) printf 'n\n' > "$1/b/new" ;;
	esac
	# Every time is fixed, access times included.
	find "$1/a" "$1/b" ! -type l -exec touch -t 200001020304.05 {} +
	if [ "$2" = time ]; then
		touch -t 200101020304.05 "$1/b/d/g"
	fi
}

for variant in equal content mode time link only; do
	for side in old new; do
		eval tool=\$$side
		make_trees "$work/$side" "$variant"
		(cd "$work/$side" && "$tool" -r -q --metadata -- a b) > "$work/$side.out" 2>&1
		echo "status $?" >> "$work/$side.out"
	done
	if cmp -s "$work/old.out" "$work/new.out"; then
		echo "SAME $variant: $(tr '\n' ' ' < "$work/new.out")"
	else
		echo "DIFF $variant"
		diff "$work/old.out" "$work/new.out"
		status=1
	fi
done
exit $status
