#!/bin/sh
# ws063: the guest half of journal-func.sh.  Checks the journal file's
# name on the root, the mount options on two work volumes (A: journal by
# default, B: made with --journal-size=0), grows directories on A, and makes
# a volume with mkfs --journal-size.  Prints OK/FAIL lines.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u

check() {
	# check NAME COMMAND...: OK when the command succeeds.
	name=$1
	shift
	if "$@" > /dev/null 2>&1; then
		echo "OK $name"
	else
		echo "FAIL $name"
	fi
}

refused() {
	# refused NAME COMMAND...: OK when the command fails.
	name=$1
	shift
	if "$@" > /dev/null 2>&1; then
		echo "FAIL $name (allowed)"
	else
		echo "OK $name (refused)"
	fi
}

# The root's journal file is the volume's own.
mount | grep ' on / ' | sed 's/^/ROOT /'
check root-rw sh -c "mount | grep ' on / ' | grep -q '(rw)'"
refused listed sh -c "ls -a / | grep -q ufs-journal"
refused touch touch /.ufs-journal
refused rm rm -f /.ufs-journal
refused cat cat /.ufs-journal
refused mkdir mkdir /.ufs-journal
refused symlink ln -s /etc/motd /.ufs-journal
echo x > /root/journal-move
refused rename mv /root/journal-move /.ufs-journal
rm -f /root/journal-move

# Volume A: the journal is made at the first mount and is on by default.
mkdir -p /va /vb
check mount-a mount -t ufs /dev/nvme1n1 /va
mount | grep ' on /va ' | sed 's/^/A /'
check a-rw sh -c "mount | grep ' on /va ' | grep -q '(rw)'"
refused a-listed sh -c "ls -a /va | grep -q ufs-journal"
LONG=300 SHORT=600 MOVE=100 GONE=300 sh /root/dir-grow.sh /va make | tail -1
check umount-a umount /va
check remount-a mount -t ufs /dev/nvme1n1 /va
LONG=300 SHORT=600 MOVE=100 GONE=300 sh /root/dir-grow.sh /va verify | tail -1
check umount-a2 umount /va

# The options show in the list of mounts.
check mount-a-nojournal mount -t ufs -o nojournal /dev/nvme1n1 /va
check a-nojournal sh -c "mount | grep ' on /va ' | grep -q '(rw,nojournal)'"
echo nojournal > /va/nojournal.txt
check umount-a3 umount /va
check mount-a-writethru mount -t ufs -o writethru /dev/nvme1n1 /va
check a-writethru sh -c "mount | grep ' on /va ' | grep -q '(rw,writethru)'"
check a-kept grep -q nojournal /va/nojournal.txt
check umount-a4 umount /va

# Volume B: made without a journal, and asked for none.
check mount-b mount -t ufs /dev/nvme2n1 /vb
echo b > /vb/b.txt
check umount-b umount /vb

# mkfs records the journal size it is given.  (Known: on a write-cached
# mount mkfs writes the format but exits EBUSY; the host checks the record.)
rm -f /root/mkfs.img
check dd dd if=/dev/zero of=/root/mkfs.img bs=1048576 count=64
sync
echo "MKFS $(mkfs -t ufs --journal-size=8 /root/mkfs.img 2>&1)"
refused mkfs-big mkfs -t ufs --journal-size=1025 /root/mkfs.img
sync
echo DONE
