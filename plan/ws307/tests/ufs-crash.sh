#!/bin/sh
# Native UFS (mkfs -t ufs --profile=native, the batched journal) cut off in the middle of writing, on a 512-byte and
# a 4096-byte NVMe disk: what fsync made durable survives, the journal replays at the next mount, and the volume
# takes writes after it.  With OLD_MKFS (an mkfs that still writes the tail journal, e.g. an earlier build's
# bin/mkfs), a volume of that older form is also made and must still mount, take writes and read them back.
#
# For each sector size and each SECONDS: a fresh blank 2 GiB disk; boot IMAGE from USB, format and mount the disk,
# write a 256 KiB file of random bytes and fsync it (sync FILE), create names in one directory (plan/tools/ufs/crash-grow.sh) for
# SECONDS, cut the machine off; boot again, mount (the journal replays), check the file's checksum and the names
# (HOLDS = PREFIX: no hole), write and fsync a new file, unmount, mount again, read it back.  The 512-byte disk is
# also checked on the host (plan/tools/ufs/check-volume.py; the checker reads 1 KiB fragments only).
#
# The summary is "CRASH SECTOR SECONDS step ok|fail ..." lines and "ufs-crash: PASS|FAIL OUT".  Output goes to a new
# build/ws307-test/ufs-crash.*.
#   sh plan/ws307/tests/ufs-crash.sh IMAGE [SECONDS...]       (default 3 20)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname "$0")/../../.."
image=${1:?usage: ufs-crash.sh IMAGE [SECONDS...]}
shift
[ $# -gt 0 ] || set -- 3 20
mkdir -p build/ws307-test
out=$(mktemp -d build/ws307-test/ufs-crash.XXXXXX)
export GUEST_RUNTIME="$(pwd)/$out/guest"
g() { timeout 900 python3 plan/tools/guest/guest.py "$@"; }
status=0
say() { echo "$*" | tee -a "$out/summary.txt"; case "$*" in *" fail"*) status=1 ;; esac; }

# Boots the image with one disk and waits for SSH.
boot() {
	g stop > /dev/null 2>&1
	cp --sparse=always "$image" "$out/boot.img"
	g start "$out/boot.img" --disk usb --qemu-extra "-drive file=$1,if=none,id=c1,format=raw,cache=writeback -device nvme,serial=crash,drive=c1$2" > "$out/start.txt" 2>&1 || return 1
	g wait --timeout 420 > /dev/null 2>&1 || return 1
	g put plan/tools/ufs/crash-grow.sh /root/crash-grow.sh > /dev/null 2>&1
	return 0
}

# The guest's disk: the one NVMe disk.
find_disk='disk=$(diskpart --machine list | awk -F"\t" "\$1 == \"device\" && \$4 == 0 && \$2 ~ /^nvme/ { print \$2 \":\" \$3; exit }")'

for size in 512 4096; do
	extra=""
	[ $size = 4096 ] && extra=",logical_block_size=4096,physical_block_size=4096"
	for seconds in "$@"; do
		disk="$out/d$size-$seconds.img"
		truncate -s 2G "$disk"
		if ! boot "$disk" "$extra"; then
			say "CRASH $size $seconds boot fail"
			continue
		fi
		g run "$find_disk; echo \"FORMAT \$disk\" | mkfs -t ufs --profile=native /dev/\${disk%%:*} > /root/mkfs.txt 2>&1 &&
			mkdir -p /c && mount -t ufs /dev/\${disk%%:*} /c && dd if=/dev/urandom of=/c/kept bs=4096 count=64 2> /dev/null && sync /c/kept &&
			cksum < /c/kept > /root/kept.sum &&
			(nohup sh /root/crash-grow.sh /c run > /root/made.txt 2>&1 < /dev/null &)" > "$out/run1-$size-$seconds.txt" 2>&1
		kept=$(g run "cat /root/kept.sum" 2> /dev/null)
		[ -n "$kept" ] && say "CRASH $size $seconds write ok" || say "CRASH $size $seconds write fail"
		sleep "$seconds"
		made=$(g run "tail -1 /root/made.txt" 2> /dev/null)
		g stop > /dev/null 2>&1

		if ! boot "$disk" "$extra"; then
			say "CRASH $size $seconds reboot fail"
			continue
		fi
		g run "$find_disk; mkdir -p /c && mount -t ufs /dev/\${disk%%:*} /c && echo MOUNTED &&
			cksum < /c/kept && sh /root/crash-grow.sh /c check &&
			dd if=/dev/urandom of=/root/after bs=4096 count=16 2> /dev/null && cp /root/after /c/after && sync /c/after && umount /c &&
			mount -t ufs /dev/\${disk%%:*} /c && cmp /root/after /c/after && umount /c && echo REMOUNTED" > "$out/run2-$size-$seconds.txt" 2>&1
		g stop > /dev/null 2>&1
		r="$out/run2-$size-$seconds.txt"
		grep -q MOUNTED "$r" && say "CRASH $size $seconds replay-mount ok" || say "CRASH $size $seconds replay-mount fail"
		grep -qxF "$kept" "$r" && say "CRASH $size $seconds fsynced-kept ok" || say "CRASH $size $seconds fsynced-kept fail"
		holds=$(sed -n 's/^HOLDS //p' "$r")
		prefix=$(sed -n 's/^PREFIX //p' "$r")
		[ -n "$holds" ] && [ "$holds" = "$prefix" ] && say "CRASH $size $seconds names ok holds=$holds made=($made)" ||
			say "CRASH $size $seconds names fail holds=$holds prefix=$prefix made=($made)"
		grep -q REMOUNTED "$r" && say "CRASH $size $seconds write-after ok" ||
			say "CRASH $size $seconds write-after fail"
		if [ $size = 512 ]; then
			if timeout 600 python3 plan/tools/ufs/check-volume.py "$disk" > "$out/check-$size-$seconds.txt" 2>&1; then
				say "CRASH $size $seconds host-check ok"
			else
				say "CRASH $size $seconds host-check fail"
			fi
		fi
	done

	# A volume of the older form, with the tail journal, still mounts and takes writes.
	if [ -n "${OLD_MKFS:-}" ]; then
		disk="$out/old$size.img"
		truncate -s 2G "$disk"
		if boot "$disk" "$extra"; then
			g put "$OLD_MKFS" /root/old-mkfs > /dev/null 2>&1
			# The locator sits in the sector after the volume: 2 GiB less the tail, in whole fragments.
			frag=1024
			[ $size = 4096 ] && frag=4096
			end=$(( (2147483648 - 1181184) / 1024 * 1024 / frag * (frag / 512) ))
			g run "chmod 755 /root/old-mkfs; $find_disk; echo \"FORMAT \$disk\" | /root/old-mkfs -t ufs --profile=native /dev/\${disk%%:*} > /root/mkfs.txt 2>&1 &&
				dd if=/dev/\${disk%%:*} bs=$size count=1 skip=$(( end * 512 / size )) 2> /dev/null | head -c 4 && echo &&
				dd if=/dev/urandom of=/root/f bs=4096 count=16 2> /dev/null &&
				mkdir -p /c && mount -t ufs /dev/\${disk%%:*} /c && cp /root/f /c/f && sync && umount /c &&
				mount -t ufs /dev/\${disk%%:*} /c && cmp /root/f /c/f && umount /c && echo DONE" > "$out/old-$size.txt" 2>&1
			grep -q '^ZUJ2' "$out/old-$size.txt" && grep -q DONE "$out/old-$size.txt" &&
				say "CRASH $size tail-journal-volume ok" || say "CRASH $size tail-journal-volume fail"
		else
			say "CRASH $size tail-journal-volume boot fail"
		fi
		g stop > /dev/null 2>&1
	fi
done
[ $status -eq 0 ] && echo "ufs-crash: PASS $out" || echo "ufs-crash: FAIL $out"
exit $status
