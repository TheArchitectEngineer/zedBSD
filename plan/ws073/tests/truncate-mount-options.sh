#!/bin/sh
# BUG-063 regression, run in the guest as root: truncate creates a missing
# file unless -c is given, and mount -o takes rw, defaults and comma lists.
# TRUNCATE and MOUNT name the programs under test (default: the installed
# ones).  Prints one line per check and exits with the number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
TRUNCATE=${TRUNCATE:-truncate}
MOUNT=${MOUNT:-mount}
dir=/tmp/bug063.$$
failures=0

# check NAME COMMAND...: passes when COMMAND succeeds.
check() {
	name=$1
	shift
	if "$@" >/dev/null 2>&1; then
		echo "PASS $name"
	else
		echo "FAIL $name"
		failures=$((failures + 1))
	fi
}

# size FILE: prints the size of FILE in bytes.
size() {
	wc -c < "$1" | tr -d ' '
}

mkdir -p "$dir"

# truncate: creation, -c, both size spellings, growing and shrinking.
check "truncate creates a missing file" "$TRUNCATE" -s 100 "$dir/a"
check "created file has the size" test "$(size "$dir/a")" = 100
check "-c leaves a missing file alone" "$TRUNCATE" -c -s 7 "$dir/b"
check "-c created nothing" test ! -e "$dir/b"
check "-c sets an existing file" "$TRUNCATE" -c -s 7 "$dir/a"
check "-c changed the size" test "$(size "$dir/a")" = 7
check "-sSIZE and several files" "$TRUNCATE" -s4096 "$dir/a" "$dir/c"
check "both files have the size" test "$(size "$dir/a")$(size "$dir/c")" = 40964096
check "shrink to zero" "$TRUNCATE" -s 0 "$dir/c"
check "shrunk file is empty" test "$(size "$dir/c")" = 0
check "a directory is refused" sh -c "! $TRUNCATE -s 1 $dir"
check "a missing size is refused" sh -c "! $TRUNCATE $dir/a"
check "a file in a missing directory is refused" sh -c "! $TRUNCATE -s 1 $dir/no/such"

# mount: -o rw, defaults, comma lists; ro still read-only; bad option refused.
mkdir -p "$dir/m"
check "mount -o rw" "$MOUNT" -t tmpfs -o rw tmpfs "$dir/m"
check "rw mount is writable" sh -c "echo x > $dir/m/f"
check "umount rw" umount "$dir/m"
check "mount -o defaults,nosuid" "$MOUNT" -t tmpfs -o defaults,nosuid tmpfs "$dir/m"
check "list shows nosuid" sh -c "$MOUNT | grep '$dir/m' | grep -q nosuid"
check "umount nosuid" umount "$dir/m"
check "mount -o ro,nosuid" "$MOUNT" -t tmpfs -o ro,nosuid tmpfs "$dir/m"
check "ro mount refuses a write" sh -c "! sh -c 'echo x > $dir/m/f' 2>/dev/null"
check "umount ro" umount "$dir/m"
check "-r then -o rw is writable" "$MOUNT" -t tmpfs -r -o rw tmpfs "$dir/m"
check "rw after -r writes" sh -c "echo x > $dir/m/f"
check "umount -r rw" umount "$dir/m"
check "unknown option is refused" sh -c "! $MOUNT -t tmpfs -o rw,bogus tmpfs $dir/m"
check "refused mount left nothing" sh -c "! $MOUNT | grep -q '$dir/m'"

rm -rf "$dir"
echo "failures $failures"
exit "$failures"
