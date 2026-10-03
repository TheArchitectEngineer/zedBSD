#!/bin/sh
# ws073-p051 (BUG-135): cuts the machine off in the window the two-step commit opens between one commit's seal
# and the writing of its homes, and checks what the journal's replay leaves.  Needs a kernel built with DWARF and
# frame pointers (ZEDBSD_KERNEL_LTO_CFLAGS="-g -fno-omit-frame-pointer") in IMAGE, named as VMUNIX for gdb.
#
#   sh plan/ws073/tests/p051-window.sh IMAGE VMUNIX OUTDIR [ARM] [SEALS]
#
# Names are created in one directory of a fresh v3-journal volume (plan/tools/ufs/crash-grow.sh) while, through the
# gdbstub, the test waits for the function ARM to run (default j3_home_direct: a range of the closed transaction
# whose line the running transaction pinned again, the case the window is about), then for SEALS more returns of
# j3_commit_seal (default 1: the next transaction's commit record is durable), and kills the machine right there,
# before that transaction's homes are written.  For a kernel without j3_home_direct, ARM=j3_finish_commit SEALS=2
# cuts off after the seal of the transaction following any commit.  Then the volume and the root are mounted again
# (both replay), the names are counted (HOLDS must equal PREFIX), and both file systems are checked on the host.
# Prints PASS or FAIL.  env: GUEST_RUNTIME (default build/p9-window-run), GROW_SECONDS (default 3, the names made
# before the gdb is attached).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
vmunix=$2
out=$3
arm=${4:-j3_home_direct}
seals=${5:-1}
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/p9-window-run}
guest=plan/tools/guest/guest.py
mkdir -p "$out"
volume=$out/volume.img
empty=$(mktemp -d)
drive="-drive if=none,id=jour,file=$root/$volume,format=raw -device nvme,serial=ufswindow,drive=jour"
fail=0

# Makes the fresh volume (the batched journal is made at its first mount).
rm -f "$volume"
build/zedimage-host ufs $((256 * 1024 * 1024)) "$empty" "$root/$volume" --inodes=8192 || exit 1
rmdir "$empty"

# Boots the guest and starts creating names on the volume.
python3 $guest stop > /dev/null 2>&1
python3 $guest start --disk nvme --qemu-extra "$drive" "$image" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH"; exit 1; }
python3 $guest put plan/tools/ufs/crash-grow.sh /root/crash-grow.sh > /dev/null
python3 $guest run 'mkdir -p /jour && mount -t ufs /dev/nvme1n1 /jour &&
	(nohup sh /root/crash-grow.sh /jour run > /root/made.txt 2>&1 < /dev/null &)' || exit 1
sleep "${GROW_SECONDS:-3}"

# Writes the gdb script: arm on ARM, then SEALS returns of j3_commit_seal, then kill the machine.
port=$(python3 -c "import json; print(json.load(open('$GUEST_RUNTIME/session.json'))['debug_port'])")
cat > "$out/kill.gdb" <<EOF
set pagination off
target remote 127.0.0.1:$port
break $arm
continue
delete
break j3_commit_seal
EOF
n=0
while [ "$n" -lt "$seals" ]; do
	printf 'continue\n' >> "$out/kill.gdb"
	n=$((n + 1))
done
cat >> "$out/kill.gdb" <<EOF
delete
finish
info registers rip
kill
EOF
timeout 120 gdb -q -batch -x "$out/kill.gdb" "$vmunix" > "$out/gdb.txt" 2>&1
grep -q "Run till exit" "$out/gdb.txt" || { echo "FAIL the window was not reached (gdb.txt)"; python3 $guest stop > /dev/null 2>&1; exit 1; }
sleep 1
python3 $guest stop > /dev/null 2>&1
cp --reflink=auto "$GUEST_RUNTIME/disk.img" "$out/crashed-root.img"

# Boots the crashed root with the volume: both replay their journals.  Counts the names.
python3 $guest start --disk nvme --qemu-extra "$drive" "$out/crashed-root.img" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH after the cut"; exit 1; }
python3 $guest put plan/tools/ufs/crash-grow.sh /root/crash-grow.sh > /dev/null
python3 $guest run 'mkdir -p /jour && mount -t ufs /dev/nvme1n1 /jour &&
	sh /root/crash-grow.sh /jour check; umount /jour; sync' > "$out/check.txt" 2>&1
python3 $guest stop > /dev/null
cat "$out/check.txt"
holds=$(sed -n 's/^HOLDS \([0-9]*\).*/\1/p' "$out/check.txt" | head -1)
prefix=$(sed -n 's/^PREFIX \([0-9]*\).*/\1/p' "$out/check.txt" | head -1)
if [ -z "$holds" ] || [ "$holds" != "$prefix" ]; then
	echo "FAIL names: HOLDS=$holds PREFIX=$prefix"
	fail=1
fi

# Checks both file systems on the host (the root is the second GPT partition, 1 MiB + 64 MiB in).
python3 plan/tools/ufs/check-volume.py "$volume" | tail -1 | tee -a "$out/check.txt" | grep -q "UFS OK" || { echo "FAIL volume check"; fail=1; }
dd if="$GUEST_RUNTIME/disk.img" of="$out/root.ufs" bs=1048576 skip=65 count=1024 status=none
python3 plan/tools/ufs/check-volume.py "$out/root.ufs" | tail -1 | tee -a "$out/check.txt" | grep -q "UFS OK" || { echo "FAIL root check"; fail=1; }
rm -f "$out/root.ufs"

# Reports.
if [ "$fail" = 0 ]; then
	echo "PASS window ($arm +$seals seals): names $holds, volume and root UFS OK"
else
	exit 1
fi
