#!/bin/sh
# ws073-p015 check, run in the guest as root on an amd64 image: the boot
# slots that bootN: files name are shown read-write at /boot/bootN, nothing
# else is shown under /boot, and a file in use (an overlay image, a swap
# file) stays readable but cannot be written, truncated, removed or renamed.
#
#   sh boot-slots.sh native   the native image (rootpart=, raw swap): nothing
#                             is shown at boot; the ESP is mounted at
#                             /boot/esp as an fstab line would, a swap file is
#                             made on it and added as boot0:swapfile, which
#                             shows boot0 at /boot/boot0, and the file is
#                             checked there and through /boot/esp
#   sh boot-slots.sh hybrid   the hybrid image (overlay-root, overlay-data and
#                             swap0 on boot0): boot0 is shown at /boot/boot0
#                             and rootfs.img, data.img and swapfile are checked
#
# ESP names the ESP partition (default /dev/nvme0n1p1); MOUNT names the mount
# under test (default: the installed one; it must know the msdosfs spelling).  Each step that could
# hang runs under timeout(1) and prints how long it took.  Prints one line
# per check and exits with the number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
MODE=${1:?native or hybrid}
ESP=${ESP:-/dev/nvme0n1p1}
MOUNT=${MOUNT:-mount}
LIMIT=${LIMIT:-120}
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

# refuse NAME COMMAND...: passes when COMMAND fails, printing its message.
refuse() {
	name=$1
	shift
	if message=$("$@" 2>&1); then
		echo "FAIL $name (succeeded)"
		failures=$((failures + 1))
	else
		echo "PASS $name: $(echo "$message" | tail -1)"
	fi
}

# timed NAME COMMAND...: a check under timeout(1) that prints its duration.
timed() {
	name=$1
	shift
	start=$(date +%s)
	if timeout "$LIMIT" "$@" >/dev/null 2>&1; then
		result=PASS
	else
		result=FAIL
		failures=$((failures + 1))
	fi
	echo "$result $name ($(($(date +%s) - start)) s)"
}

# shown N: whether /boot/bootN is a mount.
shown() {
	"$MOUNT" | grep -q " on /boot/boot$1 "
}

# stop: ends the run early when a later check would test nothing.
stop() {
	echo "FAIL $1; stopping"
	failures=$((failures + 1))
	echo "failures $failures"
	exit "$failures"
}

# in_use FILE: a file in use reads but cannot change.
in_use() {
	file=$1
	test -f "$file" || stop "$file is missing"
	size=$(stat -c %s "$file" 2>/dev/null)
	check "$file: read the first page" sh -c "test \$(dd if=$file bs=4096 count=1 2>/dev/null | wc -c) -eq 4096"
	refuse "$file: append" sh -c "echo x >> $file"
	refuse "$file: write in place" dd if=/dev/zero of="$file" bs=512 count=1 conv=notrunc
	refuse "$file: truncate" truncate -s 0 "$file"
	refuse "$file: remove" rm -f "$file"
	refuse "$file: rename" mv "$file" "$file.moved"
	echo other > "$file.other"
	refuse "$file: rename another file onto it" mv "$file.other" "$file"
	rm -f "$file.other" "$file.moved"
	check "$file: still there with its size" test "$(stat -c %s "$file" 2>/dev/null)" = "$size"
}

# Nothing but what a bootN: file names is shown.
check "/boot is a directory, not a mount" sh -c "test -d /boot && ! $MOUNT | grep -q ' on /boot '"
check "nothing is shown at /boot/esp" sh -c "! $MOUNT | grep -q ' on /boot/esp '"
check "boot1 to boot3 are not shown" sh -c "! $MOUNT | grep -q ' on /boot/boot[123] '"

case $MODE in
native)
	check "boot0 (the ESP, no bootN: file) is not shown" sh -c "! $MOUNT | grep -q ' on /boot/boot0 '"

	# The ESP as an fstab line mounts it, adopting the kernel's hold.
	mkdir -p /boot/esp
	"$MOUNT" -t msdosfs "$ESP" /boot/esp || stop "mount the ESP at /boot/esp"
	echo "PASS mount the ESP at /boot/esp"
	rm -f /boot/esp/swapfile

	# The steps that timed out in ws073-p014, one at a time.
	timed "make a 16 MiB file on the ESP" dd if=/dev/zero of=/boot/esp/swapfile bs=1048576 count=16
	timed "mkswap it" mkswap /boot/esp/swapfile
	timed "sync" sync
	timed "swapon boot0:swapfile" swapon boot0:swapfile
	shown 0 || stop "the swap file shows boot0 at /boot/boot0"
	echo "PASS the swap file shows boot0 at /boot/boot0"
	check "/boot/boot0 and /boot/esp are one filesystem" cmp /boot/boot0/zedbsd.cfg /boot/esp/zedbsd.cfg

	in_use /boot/boot0/swapfile
	refuse "the same file through /boot/esp: append" sh -c "echo x >> /boot/esp/swapfile"
	refuse "the same file through /boot/esp: remove" rm -f /boot/esp/swapfile

	check "another file on the slot is writable" sh -c 'echo y > /boot/boot0/other.txt && test "$(cat /boot/esp/other.txt)" = y && rm /boot/boot0/other.txt'
	timed "swapoff boot0:swapfile" swapoff boot0:swapfile
	check "after swapoff the file can be removed" rm /boot/boot0/swapfile
	check "/boot/boot0 stays shown" shown 0
	check "umount /boot/esp" umount /boot/esp
	;;
hybrid)
	check "boot0 (overlay images, swap file) is shown" shown 0
	check "boot0 is shown read-write" sh -c "$MOUNT | grep ' on /boot/boot0 ' | grep -q '(rw'"
	for file in rootfs.img data.img swapfile; do
		check "/boot/boot0/$file exists" test -f "/boot/boot0/$file"
		in_use "/boot/boot0/$file"
	done
	# Checked by its size, not read: a FAT file read and then removed keeps its
	# clusters until its inode goes (a separate bug), which the host fsck sees.
	check "a new file on the slot is writable" sh -c 'echo y > /boot/boot0/other.txt && test "$(stat -c %s /boot/boot0/other.txt)" = 2 && rm /boot/boot0/other.txt && sync'

	# The ESP is not a slot here, so an fstab line mounts it normally.
	mkdir -p /boot/esp
	check "mount the ESP at /boot/esp" "$MOUNT" -t msdosfs "$ESP" /boot/esp
	check "the ESP's files are there" test -f /boot/esp/EFI/BOOT/BOOTX64.EFI
	check "umount /boot/esp" umount /boot/esp
	;;
*)
	echo "unknown mode: $MODE" >&2
	exit 2
	;;
esac

echo "failures $failures"
exit "$failures"
