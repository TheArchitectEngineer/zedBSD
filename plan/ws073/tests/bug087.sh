#!/bin/sh
# BUG-087: the clang resource headers of the first image of a fresh build.
# Builds the rootfs of the guest configuration while the clang stage's
# resource directory (lib/clang/23/include) is absent when make parses and
# is put back by a prerequisite of the rootfs, the way the stage appears in
# the same make on a fresh checkout.  A tree that lists the headers while
# parsing makes a rootfs without them.
#
# The package builds take an hour and are not what the bug is about, so the
# staged packages are taken as they are (make -o), as in bug084.sh: stage
# them first, or copy build/packages/<name>/stage and the license files from
# a tree that has them.
#
#   sh plan/ws073/tests/bug087.sh [BUILD [JOBS]]
#
# Prints "PASS bug087" when the rootfs has every resource header of the stage.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
build=${1:-build/ws073-b087}
jobs=${2:-32}
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
config=plan/ws035/tests/config-amd64-guest.mk
resource=$root/build/packages/clang/stage/usr/lib/clang/23/include
hidden=$resource.bug087

# The staged packages the rootfs reads, kept as they are.
old=
for input in $(make --no-print-directory ZEDBSD_CONFIG=$config BUILD="$build" \
    --eval='bug087-inputs: ; @echo $(ZEDBSD_PACKAGE_INPUTS)' bug087-inputs); do
	case $input in
	"$root"/build/packages/*) old="$old -o $input" ;;
	esac
done

restore() {
	if test -d "$hidden"; then
		mv "$hidden" "$resource"
	fi
}
trap restore EXIT HUP INT TERM

# Hides the resource headers from the parse and restores them in the make.
# The rule is in a second -f makefile, not --eval: sub-makes (the cmake builds)
# inherit --eval through MAKEFLAGS and take its first rule as their goal.
expected=$(find "$resource" -maxdepth 1 -type f -name '*.h' | wc -l)
mkdir -p "$build"
printf '%s\n' "$build/rootfs/.stamp: bug087-restore" \
    "bug087-restore: ; test ! -d '$hidden' || mv '$hidden' '$resource'" \
    > "$build/bug087.mk"
mv "$resource" "$hidden"
rm -rf "$build/rootfs"
echo "bug087: build with the resource headers appearing during the make"
if ! make -j"$jobs" -f Makefile -f "$build/bug087.mk" ZEDBSD_CONFIG=$config \
    BUILD="$build" $old "$build/rootfs/.stamp" > "$build.log" 2>&1; then
	echo "FAIL bug087: build (see $build.log)"
	exit 1
fi

# Compares the rootfs with the stage.
installed=$(find "$build/rootfs/usr/lib/clang/23/include" -maxdepth 1 \
    -type f -name '*.h' 2>/dev/null | wc -l)
echo "bug087: stage $expected headers, rootfs $installed"
if test "$installed" -ne "$expected"; then
	echo "FAIL bug087"
	exit 1
fi
echo "PASS bug087"
