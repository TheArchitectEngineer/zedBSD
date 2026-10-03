#!/bin/sh
# ws073-p054 (BUG-164): cuts the machine off while a file that lost its last name is still open on a fresh UFS volume
# (the batched journal, v3, at its first mount), boots again, mounts the volume (the journal replays, and the mount
# reclaims what nothing names), unmounts it, and checks it on the host: no allocated inode without a name, no block
# owned by none.
#
#   sh plan/ws073/tests/p054-orphan.sh IMAGE OUTDIR
#
# The file holds 64 KiB; it is kept open by a sleep reading from it, its name is removed and the unlink is made
# durable (sync), then the machine is killed through the gdbstub.  Prints the volume check and "PASS" or "FAIL".
# env: GUEST_RUNTIME (default build/p054-run).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
out=$2
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/p054-run}
guest=plan/tools/guest/guest.py
mkdir -p "$out"
volume=$out/volume.img
empty=$(mktemp -d)
drive="-drive if=none,id=jour,file=$root/$volume,format=raw -device nvme,serial=ufsorphan,drive=jour"

# Makes the fresh volume.
rm -f "$volume"
build/zedimage-host ufs $((64 * 1024 * 1024)) "$empty" "$root/$volume" --inodes=2048 || exit 1
rmdir "$empty"

# Boots, makes the file, keeps it open, removes its name and makes the unlink durable.
python3 $guest stop > /dev/null 2>&1
python3 $guest start --disk nvme --qemu-extra "$drive" "$image" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH"; exit 1; }
python3 $guest run 'mkdir -p /jour && mount -t ufs /dev/nvme1n1 /jour &&
	dd if=/dev/zero of=/jour/kept bs=4096 count=16 2>/dev/null && mkdir /jour/named && echo named > /jour/named/file && sync &&
	(nohup sleep 600 < /jour/kept > /dev/null 2>&1 &) && sleep 1 && rm /jour/kept && sync && ls -l /jour' > "$out/before.txt" 2>&1
cat "$out/before.txt"

# Kills the machine (no unmount, no close).
port=$(python3 -c "import json; print(json.load(open('$GUEST_RUNTIME/session.json'))['debug_port'])")
printf 'target remote 127.0.0.1:%s\nkill\n' "$port" > "$out/kill.gdb"
timeout 60 gdb -q -batch -x "$out/kill.gdb" > "$out/gdb.txt" 2>&1
python3 $guest stop > /dev/null 2>&1

# The volume as the cut left it.
python3 plan/tools/ufs/check-volume.py "$volume" > "$out/check-cut.txt" 2>&1
echo "after the cut: $(tail -1 "$out/check-cut.txt")"

# Boots again, mounts the volume (replay and the reclaim of what nothing names) and unmounts it.
cp --reflink=auto "$GUEST_RUNTIME/disk.img" "$out/root-after-cut.img"
python3 $guest start --disk nvme --qemu-extra "$drive" "$out/root-after-cut.img" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH after the cut"; exit 1; }
python3 $guest run 'mkdir -p /jour && mount -t ufs /dev/nvme1n1 /jour && ls -l /jour /jour/named && cat /jour/named/file && umount /jour' > "$out/after.txt" 2>&1
python3 $guest stop > /dev/null 2>&1
rm -f "$out/root-after-cut.img"
cat "$out/after.txt"

# Checks the volume on the host.
python3 plan/tools/ufs/check-volume.py "$volume" > "$out/check.txt" 2>&1
tail -1 "$out/check.txt"
if tail -1 "$out/check.txt" | grep -q "UFS OK" && grep -q '^named$' "$out/after.txt"; then
	echo PASS
else
	echo FAIL
	exit 1
fi
