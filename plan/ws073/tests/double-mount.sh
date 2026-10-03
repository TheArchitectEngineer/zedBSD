#!/bin/sh
# BUG-065 regression, run in the guest: one block device may not be mounted
# twice when either mount can write.  Needs the root on /dev/nvme0n1p2 and an
# empty UFS volume on /dev/nvme1n1 (plan/ws073/tests/double-mount-host.sh
# makes and attaches one).  Prints one line per check and exits with the
# number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
failures=0

# expect NAME WANTED_STATUS COMMAND...: runs COMMAND and compares its status.
expect() {
	name=$1
	wanted=$2
	shift 2
	"$@" >/dev/null 2>&1
	got=$?
	if [ "$got" -eq "$wanted" ]; then
		echo "PASS $name"
	else
		echo "FAIL $name (status $got, wanted $wanted)"
		failures=$((failures + 1))
	fi
}

mkdir -p /mnt-a /mnt-b /mnt-c

# The root volume is mounted read-write already.
expect "root again rw is refused" 1 mount -t ufs /dev/nvme0n1p2 /mnt-a
expect "root again ro is refused" 1 mount -r -t ufs /dev/nvme0n1p2 /mnt-a

# A second volume: one rw mount, then no second of either kind.
expect "volume rw" 0 mount -t ufs /dev/nvme1n1 /mnt-a
expect "volume again rw is refused" 1 mount -t ufs /dev/nvme1n1 /mnt-b
expect "volume again ro is refused" 1 mount -r -t ufs /dev/nvme1n1 /mnt-b
expect "write through the first mount" 0 sh -c 'echo kept > /mnt-a/file'
expect "unmount" 0 umount /mnt-a

# Two read-only mounts may share it; a writable one may not join them.
expect "volume ro" 0 mount -r -t ufs /dev/nvme1n1 /mnt-a
expect "volume ro twice" 0 mount -r -t ufs /dev/nvme1n1 /mnt-b
expect "rw over two ro is refused" 1 mount -t ufs /dev/nvme1n1 /mnt-c
expect "both ro mounts read the file" 0 sh -c 'grep -q kept /mnt-a/file && grep -q kept /mnt-b/file'
expect "unmount ro 1" 0 umount /mnt-b
expect "unmount ro 2" 0 umount /mnt-a

# After the last unmount the volume mounts read-write again.
expect "remount rw" 0 mount -t ufs /dev/nvme1n1 /mnt-a
expect "file survived" 0 grep -q kept /mnt-a/file
expect "final unmount" 0 umount /mnt-a

# A refused mount leaves no mount behind.
count=$(mount | grep -c nvme)
expect "only the root is mounted from nvme" 0 test "$count" -eq 1

echo "failures=$failures"
exit "$failures"
