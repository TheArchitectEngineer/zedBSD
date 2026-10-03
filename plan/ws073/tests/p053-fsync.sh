#!/bin/sh
# ws073-p053 (BUG-163): cuts the machine off right after an fsync() of a file on a volume whose start is not aligned
# to a 4 KiB line of the buffer cache, while directory changes keep pinning the lines its blocks share, and checks
# what the volume keeps.  Needs a kernel built with DWARF (ZEDBSD_KERNEL_LTO_CFLAGS="-g -fno-omit-frame-pointer") in
# IMAGE, named as VMUNIX for gdb, and the probe built for the guest (plan/ws073/tests/syncprobe-build.sh).
#
#   sh plan/ws073/tests/p053-fsync.sh IMAGE VMUNIX SYNCPROBE OUTDIR [START]
#
# The volume is a fresh UFS (v3 journal at its first mount) in an MBR partition that starts at sector START (default
# 63: a line spans the end of one 8 KiB block and the start of the next; 2048 is aligned).  syncprobe makes 16
# one-block files, each followed by a directory, then writes the files in place and fsyncs them in turn, while a
# loop creates and removes names in the directories (their blocks are journaled and pinned).  After RUN_SECONDS
# (default 6) the test reads vfs.io.stats (the unjournaled blocks written beneath the cache at a sync,
# IO_BUF_UNJOURNALED_WRITE, when the kernel has it), then through the gdbstub waits for a sync of /jour to return
# (where fsync() returns) and kills the machine there.  The guest boots the crashed root with the volume, mounts it
# (the journal replays), and syncprobe checks that every file holds one whole generation; the volume is checked on
# the host too.  Prints the check lines and "RESULT start=S torn=T unjournaled_writes=C bytes=B"; exit 0 when the
# machine was cut where asked.  env: GUEST_RUNTIME (default build/p053-run), RUN_SECONDS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
vmunix=$2
probe=$3
out=$4
start=${5:-63}
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/p053-run}
guest=plan/tools/guest/guest.py
mkdir -p "$out"
volume=$out/volume.ufs
disk=$out/disk.img
empty=$(mktemp -d)
drive="-drive if=none,id=jour,file=$root/$disk,format=raw -device nvme,serial=ufsfsync,drive=jour"

# The index of IO_BUF_UNJOURNALED_WRITE in vfs.io.stats (the enum's order), empty when the kernel has none.
event=$(sed -n '/^enum io_stat_event/,/IO_STAT_COUNT/p' include/uapi/io-stats.h | grep -E '^[[:space:]]+IO_' |
	grep -n 'IO_BUF_UNJOURNALED_WRITE' | cut -d: -f1)
[ -n "$event" ] && event=$((event - 1))

# Makes the volume and puts it in an MBR partition at sector START.
rm -f "$volume" "$disk"
build/zedimage-host ufs $((128 * 1024 * 1024)) "$empty" "$root/$volume" --inodes=4096 || exit 1
rmdir "$empty"
python3 - "$volume" "$disk" "$start" <<'EOF' || exit 1
import struct, sys
volume, disk, start = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = open(volume, "rb").read()
sectors = len(data) // 512
mbr = bytearray(512)
mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\0\0\0", 0xa5, b"\0\0\0", start, sectors)
mbr[510:512] = b"\x55\xaa"
with open(disk, "wb") as f:
    f.write(mbr)
    f.write(bytes((start - 1) * 512))
    f.write(data)
    f.truncate((start + sectors + 2048) * 512)
EOF

# Boots the guest, makes the files and starts the writes and the names.
python3 $guest stop > /dev/null 2>&1
python3 $guest start --disk nvme --qemu-extra "$drive" "$image" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH"; exit 1; }
python3 $guest put "$probe" /root/syncprobe > /dev/null
python3 $guest run 'chmod 755 /root/syncprobe && mkdir -p /jour && mount -t ufs /dev/nvme1n1p1 /jour &&
	/root/syncprobe setup /jour 16 &&
	(nohup sh -c "i=0; while :; do for k in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do : > /jour/d\$k/n\$i; rm -f /jour/d\$k/n\$((i - 1)); done; i=\$((i + 1)); done" > /dev/null 2>&1 < /dev/null &) &&
	(nohup /root/syncprobe write /jour 16 600 > /root/write.txt 2>&1 < /dev/null &)' > "$out/setup.txt" 2>&1 || { cat "$out/setup.txt"; exit 1; }
sleep "${RUN_SECONDS:-6}"
calls=-
bytes=-
if [ -n "$event" ]; then
	python3 $guest run "sysctl vfs.io.stats | grep 'event=$event '" > "$out/iostat.txt" 2>&1
	calls=$(sed -n 's/.* calls=\([0-9]*\) .*/\1/p' "$out/iostat.txt")
	bytes=$(sed -n 's/.* bytes=\([0-9]*\).*/\1/p' "$out/iostat.txt")
fi

# Waits for a sync of /jour to return, and kills the machine there.
port=$(python3 -c "import json; print(json.load(open('$GUEST_RUNTIME/session.json'))['debug_port'])")
cat > "$out/cut.gdb" <<EOF
set pagination off
target remote 127.0.0.1:$port
break ufs_sync if \$_streq(mountp->m_path, "/jour")
continue
print mountp->m_path
delete
finish
info registers rip
kill
EOF
timeout 300 gdb -q -batch -x "$out/cut.gdb" "$vmunix" > "$out/gdb.txt" 2>&1
grep -q '^\$1 = "/jour"' "$out/gdb.txt" && grep -q 'Inferior 1 .* killed' "$out/gdb.txt" ||
	{ echo "FAIL the sync of /jour was not reached (gdb.txt)"; python3 $guest stop > /dev/null 2>&1; exit 1; }
sleep 1
python3 $guest stop > /dev/null 2>&1
cp --reflink=auto "$GUEST_RUNTIME/disk.img" "$out/crashed-root.img"

# Boots the crashed root with the volume: the volume's journal replays at the mount.  Checks the files.
python3 $guest start --disk nvme --qemu-extra "$drive" "$out/crashed-root.img" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH after the cut"; exit 1; }
python3 $guest put "$probe" /root/syncprobe > /dev/null
python3 $guest run 'chmod 755 /root/syncprobe; mkdir -p /jour && mount -t ufs /dev/nvme1n1p1 /jour &&
	/root/syncprobe check /jour 16; umount /jour; sync' > "$out/check.txt" 2>&1
python3 $guest stop > /dev/null
grep -E 'SYNCPROBE file=[0-9]+ (gen|torn|missing|short)|SYNCPROBE torn=' "$out/check.txt"

# Checks the volume on the host.
dd if="$disk" of="$out/volume-after.ufs" bs=512 skip="$start" count=$(($(stat -c %s "$volume") / 512)) status=none
python3 plan/tools/ufs/check-volume.py "$out/volume-after.ufs" | tail -1 | tee -a "$out/check.txt"
rm -f "$out/volume-after.ufs" "$out/crashed-root.img"
torn=$(sed -n 's/^SYNCPROBE torn=\([0-9]*\).*/\1/p' "$out/check.txt")
echo "RESULT start=$start torn=${torn:-?} unjournaled_writes=${calls:-?} bytes=${bytes:-?}"
