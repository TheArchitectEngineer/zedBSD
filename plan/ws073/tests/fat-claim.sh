#!/bin/sh
# BUG-073 check, run in the guest as root: on a FAT volume that holds an
# active swap file, the files beside it stay usable (create, write, read,
# mkdir, rename, remove, sync), while the swap file itself reads but cannot
# be written, truncated, removed or renamed; swapoff works and the swap
# file can then be removed.  The volume is mounted at a directory of its
# own and the swap file is added by its absolute path.
#
#   sh fat-claim.sh DEVICE NAME [MIB]
#
# DEVICE is the FAT partition (e.g. /dev/nvme1n1p1), NAME a word for the
# mount point /mnt-NAME, MIB the swap file's size (default 8).  Prints one
# line per check and exits with the number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
DEVICE=${1:?device}
NAME=${2:?name}
MIB=${3:-8}
LIMIT=${LIMIT:-180}
DIR=/mnt-$NAME
failures=0

# check NAME COMMAND...: passes when COMMAND succeeds (within LIMIT seconds).
check() {
	label=$1
	shift
	if timeout "$LIMIT" "$@" >/dev/null 2>&1; then
		echo "PASS $NAME: $label"
	else
		echo "FAIL $NAME: $label"
		failures=$((failures + 1))
	fi
}

# refuse NAME COMMAND...: passes when COMMAND fails.
refuse() {
	label=$1
	shift
	if message=$(timeout "$LIMIT" "$@" 2>&1); then
		echo "FAIL $NAME: $label (succeeded)"
		failures=$((failures + 1))
	else
		echo "PASS $NAME: $label: $(echo "$message" | tail -1)"
	fi
}

mkdir -p "$DIR"
if ! mount -t fat "$DEVICE" "$DIR"; then
	echo "FAIL $NAME: mount $DEVICE"
	echo "failures 1"
	exit 1
fi
echo "PASS $NAME: mount $DEVICE at $DIR"

# The swap file, active by its path.
check "make a $MIB MiB file" dd if=/dev/zero of="$DIR/swapfile" bs=1048576 count="$MIB"
check "mkswap" mkswap "$DIR/swapfile"
check "sync" sync
check "swapon $DIR/swapfile" swapon "$DIR/swapfile"

# The files beside it: the buffer cache's lines hold both (BUG-073).
check "create and write a file beside it" sh -c "echo beside > $DIR/beside.txt"
check "read it back" sh -c "test \"\$(cat $DIR/beside.txt)\" = beside"
check "write 1 MiB more beside it" dd if=/dev/urandom of="$DIR/big.bin" bs=65536 count=16
check "sync with them" sync
check "mkdir and a file in it" sh -c "mkdir $DIR/sub && echo in > $DIR/sub/in.txt"
check "rename a file beside it" mv "$DIR/beside.txt" "$DIR/sub/beside.txt"
check "list the volume" ls -la "$DIR" "$DIR/sub"
# Named one by one: rm -r on FAT skips entries (a separate bug).
check "remove them" sh -c "rm $DIR/sub/in.txt $DIR/sub/beside.txt $DIR/big.bin && rmdir $DIR/sub && sync"

# The swap file itself.
size=$(stat -c %s "$DIR/swapfile")
check "the swap file reads" sh -c "test \$(dd if=$DIR/swapfile bs=4096 count=1 2>/dev/null | wc -c) -eq 4096"
refuse "append to the swap file" sh -c "echo x >> $DIR/swapfile"
refuse "write the swap file in place" dd if=/dev/zero of="$DIR/swapfile" bs=512 count=1 conv=notrunc
refuse "truncate the swap file" truncate -s 0 "$DIR/swapfile"
refuse "remove the swap file" rm -f "$DIR/swapfile"
refuse "rename the swap file" mv "$DIR/swapfile" "$DIR/moved"
check "the swap file keeps its size" test "$(stat -c %s "$DIR/swapfile")" = "$size"

# Undoing it.
check "swapoff $DIR/swapfile" swapoff "$DIR/swapfile"
check "remove the swap file after swapoff" rm "$DIR/swapfile"
check "sync at the end" sync
check "umount" umount "$DIR"

echo "failures $failures"
exit "$failures"
