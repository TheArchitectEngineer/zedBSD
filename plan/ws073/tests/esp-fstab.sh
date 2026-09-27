#!/bin/sh
# ws073-p014 check, run in the guest as root on an amd64 image: the kernel
# no longer shows /boot/esp by itself, an fstab line (or a mount command)
# for the ESP mounts it at /boot/esp over the kernel's own hold, writes
# reach the ESP, a second mount elsewhere is another view of the same
# filesystem, and a read-only request over the writable hold is refused.
# MOUNT names the mount under test (default: the installed one); ESP names
# the partition (default /dev/nvme0n1p1).  With KEEP=yes the fstab line is
# left in place for a reboot.  Prints one line per check and exits with the
# number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
MOUNT=${MOUNT:-mount}
ESP=${ESP:-/dev/nvme0n1p1}
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

check "the kernel shows nothing at /boot/esp" sh -c "! $MOUNT | grep -q ' on /boot/esp '"
mkdir -p /boot/esp

# An fstab line, mounted by mount -a as init does at boot.
cp /etc/fstab /tmp/fstab.orig
echo "$ESP /boot/esp msdosfs rw 0 0" >> /etc/fstab
check "mount -a mounts the fstab line" "$MOUNT" -a
check "the ESP is at /boot/esp" sh -c "$MOUNT | grep -q '^$ESP on /boot/esp type fat'"
check "the ESP's files are there" test -f /boot/esp/EFI/BOOT/BOOTX64.EFI
check "write a directory and a file" sh -c 'mkdir -p /boot/esp/zedbsd && cp /etc/passwd /boot/esp/zedbsd/passwd && sync'
check "read them back" cmp /etc/passwd /boot/esp/zedbsd/passwd
check "a second mount elsewhere shows the same filesystem" sh -c "mkdir -p /mnt-esp && $MOUNT -t fat $ESP /mnt-esp && cmp /etc/passwd /mnt-esp/zedbsd/passwd"
check "unmount the second view" umount /mnt-esp
check "umount /boot/esp" umount /boot/esp
check "the command line spelling mounts it too" "$MOUNT" -t msdosfs "$ESP" /boot/esp
check "the files are still there" cmp /etc/passwd /boot/esp/zedbsd/passwd
check "remove the test files" sh -c 'rm -r /boot/esp/zedbsd && sync'
check "umount again" umount /boot/esp
check "a read-only request over the writable hold is refused" sh -c "! $MOUNT -r -t fat $ESP /boot/esp"
check "the kernel still holds the partition" sh -c "! $MOUNT | grep -q ' on /boot/esp '"

# Leaves the fstab line for a reboot, or puts fstab back.
if [ "${KEEP:-no}" = yes ]; then
	check "mount -a again for the reboot" "$MOUNT" -a
	check "write a marker for after the reboot" sh -c 'echo reboot > /boot/esp/REBOOT.TXT && sync'
else
	cp /tmp/fstab.orig /etc/fstab
fi

echo "failures $failures"
exit "$failures"
