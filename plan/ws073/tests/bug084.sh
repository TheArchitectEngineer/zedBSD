#!/bin/sh
# BUG-084: the first image build after a C library header is renamed.  Builds
# the rootfs of the guest configuration (it has clang, whose package lists
# the sysroot's headers), renames include/libc/fmtmsg.h to fmtmsg-renamed.h
# (with its one #include), builds the rootfs again with -j, and puts the
# header back.  A tree that lists the headers from the sysroot on disk fails
# the second build with "cp: cannot stat .../sysroot/usr/include/fmtmsg.h".
#
# The package builds are not what the bug is about and take an hour, so the
# staged packages are taken as they are (make -o): stage them first, or copy
# build/packages/<name>/stage and the license files from a tree that has them.
#
#   sh plan/ws073/tests/bug084.sh [BUILD [JOBS]]
#
# Prints "PASS bug084" when the second build succeeds and the rootfs has the
# renamed header and not the old one.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
build=${1:-build/ws073-b084}
jobs=${2:-32}
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
config=plan/ws035/tests/config-amd64-guest.mk

# The staged packages the rootfs reads, kept as they are.
old=
for input in $(make --no-print-directory ZEDBSD_CONFIG=$config BUILD="$build" \
    --eval='bug084-inputs: ; @echo $(ZEDBSD_PACKAGE_INPUTS)' bug084-inputs); do
	case $input in
	"$root"/build/packages/*) old="$old -o $input" ;;
	esac
done

restore() {
	if test -f include/libc/fmtmsg-renamed.h; then
		mv include/libc/fmtmsg-renamed.h include/libc/fmtmsg.h
		sed -i 's/<fmtmsg-renamed\.h>/<fmtmsg.h>/' src/libc/fmtmsg.c
	fi
}
trap restore EXIT HUP INT TERM

echo "bug084: first build"
make -j"$jobs" ZEDBSD_CONFIG=$config BUILD="$build" $old rootfs \
	> "$build.first.log" 2>&1 || { echo "FAIL bug084: first build (see $build.first.log)"; exit 1; }

echo "bug084: rename include/libc/fmtmsg.h, second build"
mv include/libc/fmtmsg.h include/libc/fmtmsg-renamed.h
sed -i 's/<fmtmsg\.h>/<fmtmsg-renamed.h>/' src/libc/fmtmsg.c
if ! make -j"$jobs" ZEDBSD_CONFIG=$config BUILD="$build" $old rootfs \
    > "$build.second.log" 2>&1; then
	grep -m 3 'cannot stat' "$build.second.log"
	echo "FAIL bug084: second build (see $build.second.log)"
	exit 1
fi
if test -e "$build/rootfs/usr/include/fmtmsg.h" ||
    ! test -f "$build/rootfs/usr/include/fmtmsg-renamed.h"; then
	echo "FAIL bug084: the rootfs has the old header or lacks the new one"
	exit 1
fi
echo "PASS bug084"
