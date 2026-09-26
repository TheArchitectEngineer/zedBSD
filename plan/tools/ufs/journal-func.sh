#!/bin/sh
# ws063: functional test of the journal defaults in a guest: the hidden
# journal name on the root, `nojournal`/`writethru` in the list of mounts, a
# journal made at the first mount of a volume made without one, none for a
# volume made with --journal-size=0, directory growth over the journal, and
# mkfs --journal-size.  The host checks both volumes and the records.
#   sh plan/tools/ufs/journal-func.sh IMAGE   (an SSH guest image, plan/tools/guest/build-ssh-image.sh)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
export GUEST_RUNTIME=${GUEST_RUNTIME:-build/ufs-func-run}
guest=plan/tools/guest/guest.py
work=${WORK:-build/ufs-func}
rm -rf "$work"
mkdir -p "$work/empty"
build/zedimage-host ufs $((256 * 1024 * 1024)) "$work/empty" "$root/$work/a.img" --inodes=16384 || exit 1
build/zedimage-host ufs $((256 * 1024 * 1024)) "$work/empty" "$root/$work/b.img" --journal-size=0 || exit 1
drives="-drive if=none,id=va,file=$root/$work/a.img,format=raw -device nvme,serial=ufsfunca,drive=va"
drives="$drives -drive if=none,id=vb,file=$root/$work/b.img,format=raw -device nvme,serial=ufsfuncb,drive=vb"

python3 $guest stop > /dev/null 2>&1
python3 $guest start --disk nvme --qemu-extra "$drives" "$image" > /dev/null || exit 1
python3 $guest wait > /dev/null || { echo "FAIL no SSH"; exit 1; }
python3 $guest put plan/tools/ufs/dir-grow.sh /root/dir-grow.sh
python3 $guest put plan/tools/ufs/journal-guest.sh /root/journal-guest.sh
python3 $guest run 'sh /root/journal-guest.sh'
python3 $guest get /root/mkfs.img "$work/mkfs.img"
python3 $guest stop > /dev/null

# The host's view: both volumes check, A has a locator, B has none, and the
# mkfs volume records 8 MiB.
python3 plan/tools/ufs/check-volume.py "$work/a.img" | tail -1
python3 plan/tools/ufs/check-volume.py "$work/b.img" | tail -1
python3 plan/tools/ufs/check-volume.py "$work/mkfs.img" | tail -1
python3 - "$work/a.img" "$work/b.img" "$work/mkfs.img" <<'EOF'
import struct
import sys

def words(path):
	with open(path, "rb") as stream:
		stream.seek(65536 + 1220)
		locator = stream.read(28)
		stream.seek(65536 + 1248)
		request = stream.read(16)
	return locator, request

for name, path in zip(("A", "B", "mkfs"), sys.argv[1:]):
	locator, request = words(path)
	has_locator = locator[:4] == b"ZJ3L"
	mib = None
	if request[:4] == b"ZJ3R":
		mib = struct.unpack("<I", request[8:12])[0]
	print(f"HOST {name}: locator={'yes' if has_locator else 'no'} request_mib={mib}")
EOF
