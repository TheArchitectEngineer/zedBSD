#!/bin/sh
# BUG-312: cuts the machine off again and again while the root (UFS, batched journal) is being written, and checks
# after each cut that the next boot replays the journal and mounts the root READ-WRITE.  What each boot's kernel
# said about the volume (the "ufs:" and "vfs:" lines: a dropped torn commit, rebuilt totals, a read-only root) is
# kept, and at the end the root partition is checked on the host.
#   sh plan/tools/ufs/root-powercut.sh IMAGE ROUNDS [OUT]   (an SSH guest image with the native layout;
#                                                             OUT defaults to build/tmp/ufs-root-powercut)
# Each round: boot what the last cut left, check SSH and the root's mount, start writing (small files created and
# removed, a large file rewritten, renames), wait 3 to 15 s, cut off (QEMU killed).  A round whose boot has no SSH or
# a read-only root is a FAIL, and the run goes on with the next round from the same disk.
# The cut is QEMU's death: writes QEMU took are in the host's page cache, so this is a guest crash, not a device
# losing its cache (BUG-307's limit).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
rounds=$2
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
. plan/tools/fresh-out.sh
fresh_out "${3:-build/tmp/ufs-root-powercut}"
work=$fresh_dir
export GUEST_RUNTIME="$work/rt"
guest=plan/tools/guest/guest.py
failures=0

# The disk every round works on: a copy of the image.
cp --reflink=auto "$image" "$work/disk.img" || exit 1

round=1
while [ "$round" -le "$rounds" ]; do
	python3 $guest stop > /dev/null 2>&1
	python3 $guest start --disk nvme "$work/disk.img" > /dev/null || exit 1
	if ! python3 $guest wait > /dev/null; then
		echo "round $round: FAIL no SSH after the cut"
		failures=$((failures + 1))
		round=$((round + 1))
		continue
	fi

	# The root's mount and what the kernel said about it.
	python3 $guest run 'mount | grep " on / "; dmesg | grep -e "ufs:" -e "vfs:" -e "init: the root" || true' > "$work/round-$round.txt" 2>&1
	if grep -q '(ro' "$work/round-$round.txt"; then
		echo "round $round: FAIL the root is read-only"
		failures=$((failures + 1))
	else
		echo "round $round: root read-write"
	fi
	grep -e 'ufs:' -e 'vfs:' -e 'init:' "$work/round-$round.txt" | sed "s/^/round $round: /"

	# Writes until cut off.
	python3 $guest run 'mkdir -p /root/cut && (nohup sh -c "i=0; while :; do echo \$i > /root/cut/f\$i; mv /root/cut/f\$i /root/cut/g\$i; rm -f /root/cut/g\$((i - 40)); dd if=/dev/zero of=/root/cut/big bs=64k count=64 2>/dev/null; i=\$((i + 1)); done" > /dev/null 2>&1 < /dev/null &)' > /dev/null 2>&1
	sleep $((3 + $(od -An -N1 -tu1 /dev/urandom) % 13))
	python3 $guest stop > /dev/null 2>&1
	round=$((round + 1))
done

# A last boot, a clean stop, and the host's check of the root partition (ROOT_SKIP_MIB and ROOT_MIB: by default
# 1 MiB + 64 MiB in and 1024 MiB long, as root-crash.sh; another layout gives its own).
python3 $guest start --disk nvme "$work/disk.img" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH at the end"; exit 1; }
python3 $guest run 'mount | grep " on / "; sync; poweroff' > /dev/null 2>&1
sleep 30
python3 $guest stop > /dev/null 2>&1
dd if="$work/disk.img" of="$work/root.ufs" bs=1048576 skip="${ROOT_SKIP_MIB:-65}" count="${ROOT_MIB:-1024}" status=none
python3 plan/tools/ufs/check-volume.py "$work/root.ufs" | tail -1
echo "root-powercut: $rounds rounds, $failures failures (logs in $work)"
[ "$failures" -eq 0 ]
