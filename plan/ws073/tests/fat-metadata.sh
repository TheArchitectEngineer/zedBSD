#!/bin/sh
# BUG-072 check, in two halves.
#
#   guest: sh fat-metadata.sh guest [DEVICE]   (default /dev/nvme1n1)
#     Mounts an empty FAT volume, makes top-level and nested directories,
#     files of several sizes, moves a directory between parents, removes a
#     file, syncs and unmounts, leaving the volume for the host to check.
#   host:  sh fat-metadata.sh host IMAGE
#     Runs fsck.fat -n on the image (no messages allowed) and lists the
#     dates mtools shows (none may be 1980-00-00).
#
# Exits with the number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
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

case "$1" in
guest)
	device=${2:-/dev/nvme1n1}
	dir=/mnt-fat
	mkdir -p "$dir"
	check "mount" mount -t auto "$device" "$dir"
	check "top-level directories" mkdir "$dir/top" "$dir/other"
	check "a nested directory" mkdir "$dir/top/nested"
	check "a small file" sh -c "echo small > $dir/top/small.txt"
	check "a file in the nested directory" sh -c "echo deep > $dir/top/nested/deep.txt"
	check "a 300 KiB file" dd if=/dev/urandom of="$dir/other/big.bin" bs=1024 count=300
	check "a file removed again" sh -c "echo gone > $dir/gone.txt && rm $dir/gone.txt"
	check "move a directory into another" mv "$dir/top/nested" "$dir/other/nested"
	check "move a directory to the root" mv "$dir/other/nested" "$dir/nested"
	check "sync" sync
	check "unmount" umount "$dir"
	;;
host)
	image=$2
	fsck=$(command -v fsck.fat || echo /sbin/fsck.fat)
	output=$("$fsck" -n "$image" 2>&1)
	echo "$output" | sed 's/^/  fsck: /'
	check "fsck.fat -n exits 0" "$fsck" -n "$image"
	check "fsck.fat reports nothing to fix" sh -c "! echo \"\$1\" | grep -Eqi 'invalid|wrong|uninitialized|fixing|correct'" sh "$output"
	listing=$(mdir -i "$image" -/ :: 2>&1)
	echo "$listing" | sed 's/^/  mdir: /'
	check "no entry dated 1980-00-00" sh -c "! echo \"\$1\" | grep -q '1980-00-00'" sh "$listing"
	check "entries carry this year's date" sh -c "echo \"\$1\" | grep -q \"\$(date +%Y)-\"" sh "$listing"
	;;
*)
	echo "usage: fat-metadata.sh guest [DEVICE] | host IMAGE" >&2
	exit 2
	;;
esac

echo "failures $failures"
exit "$failures"
