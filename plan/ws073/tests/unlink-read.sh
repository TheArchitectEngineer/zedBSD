#!/bin/sh
# BUG-075 check, run in the guest as root: a file that was read and then
# removed gives its space back at once, without an unmount; a removed file
# still open keeps its data for the opener until it is closed.
#
#   sh unlink-read.sh DIR NAME [DEVICE]
#
# DIR is a directory on the filesystem to check; with DEVICE, a FAT volume
# is first mounted there (and left mounted, so a later power-off finds it
# as a sync left it).  Two 1 MiB files are written and synced; one is read
# (cat), then both are removed and synced; df's used column must drop back
# to where it started.  Then a file is opened, read and removed: its data
# stays readable through the descriptor, and the space returns when the
# descriptor closes.  Prints one line per check and exits with the number
# of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
dir=${1:?directory}
name=${2:?name}
device=${3:-}
failures=0

# used: df's used kilobytes of the filesystem holding DIR.
used() {
	df -k "$dir" | awk 'NR == 2 { print $3 }'
}

# expect WHAT ACTUAL WANTED: passes when the two numbers are equal.
expect() {
	if [ "$2" = "$3" ]; then
		echo "PASS $name: $1 ($2)"
	else
		echo "FAIL $name: $1 (used $2, expected $3)"
		failures=$((failures + 1))
	fi
}

# A FAT volume is mounted first.
if [ -n "$device" ]; then
	mkdir -p "$dir"
	if ! mount -t fat "$device" "$dir"; then
		echo "FAIL $name: mount $device"
		exit 1
	fi
fi
mkdir -p "$dir/bug075"
sync
before=$(used)

# Written and synced, one of them read, both removed.
dd if=/dev/zero of="$dir/bug075/read" bs=1024 count=1024 2>/dev/null
dd if=/dev/zero of="$dir/bug075/unread" bs=1024 count=1024 2>/dev/null
sync
cat "$dir/bug075/read" >/dev/null
rm "$dir/bug075/read" "$dir/bug075/unread"
sync
expect "read and unread files removed, space back" "$(used)" "$before"

# A removed file still open keeps its data for the opener.
dd if=/dev/zero of="$dir/bug075/open" bs=1024 count=1024 2>/dev/null
sync
exec 3<"$dir/bug075/open"
cat "$dir/bug075/open" >/dev/null
rm "$dir/bug075/open"
sync
bytes=$(cat <&3 | wc -c | tr -d ' ')
expect "open removed file still readable" "$bytes" 1048576
exec 3<&-
sync
expect "space back after the close" "$(used)" "$before"
rmdir "$dir/bug075"
sync
exit $failures
