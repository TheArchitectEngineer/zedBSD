#!/bin/sh
# ws073-p009 check, run in the guest as root: the kernel's own boot
# filesystems are shown at /boot (a BOOT partition) and /boot/esp (an EFI
# system partition), a change made there reaches the FAT the kernel holds,
# a second mount of the partition is still refused, and unmounting the view
# hides it.  ESP_DEVICE names the ESP (default /dev/nvme0n1p1); BOOT=yes
# expects a BOOT partition at /boot as well; FILE_WRITE=yes also writes a
# file there (needs BUG-070 fixed).  Prints one line per check and
# exits with the number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ESP_DEVICE=${ESP_DEVICE:-/dev/nvme0n1p1}
BOOT=${BOOT:-no}
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

check "/boot is a directory" test -d /boot
check "/boot/esp holds the ESP's zedbsd.cfg" test -f /boot/esp/zedbsd.cfg
check "mount lists the ESP at /boot/esp" sh -c "mount | grep -q '^$ESP_DEVICE on /boot/esp type fat'"
if [ "$BOOT" = yes ]; then
	check "mount lists a FAT at /boot" sh -c "mount | grep -q ' on /boot type fat'"
else
	check "no filesystem is shown at /boot itself" sh -c "! mount | grep -q ' on /boot type'"
fi

# A change made through /boot/esp is on the FAT the kernel holds.
check "make a directory on the ESP" sh -c 'mkdir /boot/esp/ws073 && sync'
check "see it" test -d /boot/esp/ws073
check "remove it" sh -c 'rmdir /boot/esp/ws073 && sync'
if [ "${FILE_WRITE:-no}" = yes ]; then
	check "write a file on the ESP" sh -c 'echo publish > /boot/esp/ws073.txt && sync'
	check "read it back" sh -c 'grep -q publish /boot/esp/ws073.txt'
	check "remove the file" sh -c 'rm /boot/esp/ws073.txt && sync'
fi

# The partition itself still cannot be mounted a second time (BUG-065).
mkdir -p /mnt-esp
check "second mount of the ESP is refused" sh -c "! mount -t auto $ESP_DEVICE /mnt-esp"

# Unmounting the view hides it; the kernel keeps its own mount.
check "umount /boot/esp" umount /boot/esp
check "the ESP is hidden again" sh -c '! test -f /boot/esp/zedbsd.cfg'
check "the partition is still held by the kernel" sh -c "! mount -t auto $ESP_DEVICE /mnt-esp"

echo "failures $failures"
exit "$failures"
