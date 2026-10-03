#!/bin/sh
# BUG-071 regression, run in the guest as root: ordinary tools create files
# on a FAT volume whatever mode they ask for, the files show the mode the
# mount presents, and their contents survive an unmount.  FAT_DEVICE names
# an empty FAT volume that nothing has mounted (default /dev/nvme1n1).
# Prints one line per check and exits with the number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
FAT_DEVICE=${FAT_DEVICE:-/dev/nvme1n1}
dir=/mnt-fat
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

mkdir -p "$dir"
check "mount the FAT volume" mount -t auto "$FAT_DEVICE" "$dir"

# Creation with the shell's default mode, touch, cp and an explicit mode.
check "shell redirection creates a file" sh -c "echo hello > $dir/a.txt"
check "touch creates a file" touch "$dir/b.txt"
check "cp creates a copy" cp /etc/passwd "$dir/passwd"
check "a mode-0600 creation" sh -c "umask 077; echo secret > $dir/c.txt"
check "a directory" mkdir "$dir/sub"
check "a file in the directory" sh -c "echo inner > $dir/sub/d.txt"

# The files show what the mount presents.
check "a file shows 0755 root:wheel" sh -c "ls -l $dir/a.txt | grep -q '^-rwxr-xr-x .* root wheel'"
check "the 0600 file shows the same" sh -c "ls -l $dir/c.txt | grep -q '^-rwxr-xr-x'"

# A larger file, then the contents after an unmount and a new mount.
check "write 1 MiB" dd if=/dev/urandom of=/tmp/fat-big bs=1024 count=1024
check "copy it to FAT" cp /tmp/fat-big "$dir/big"
check "sync" sync
check "unmount" umount "$dir"
check "mount again" mount -t auto "$FAT_DEVICE" "$dir"
check "the text survived" sh -c "grep -q hello $dir/a.txt"
check "the copy is identical" cmp /etc/passwd "$dir/passwd"
check "the large file is identical" cmp /tmp/fat-big "$dir/big"
check "the inner file survived" sh -c "grep -q inner $dir/sub/d.txt"

# Rename and removal.
check "rename" mv "$dir/b.txt" "$dir/renamed.txt"
check "remove the files" rm -r "$dir/a.txt" "$dir/renamed.txt" "$dir/c.txt" "$dir/passwd" "$dir/big" "$dir/sub"
check "the volume is empty" sh -c "test -z \"\$(ls -A $dir)\""
check "final unmount" umount "$dir"
rm -f /tmp/fat-big

echo "failures $failures"
exit "$failures"
