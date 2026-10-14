#!/usr/bin/env python3
"""Damages a disk image's UFS root so that the kernel cannot mount it read-write but can mount it read-only (BUG-312).

An unused inode (the first free one of any group) is marked allocated, as a regular file with no link (an orphan) and
a time whose nanoseconds overflow, and the volume is marked not clean.  At the next boot the kernel's read-write mount scans
for orphaned inodes (the volume is not clean), cannot load that inode and refuses the mount; the read-only mount does
not scan, so the kernel mounts the root READ-ONLY (K4) and init runs fsck -y on it (I1).  fsck clears or reconnects the
inode and rebuilds the group's counts, and init reboots; the next boot mounts the root read-write.

    python3 -I plan/tools/ufs/damage-root.py IMAGE [ROOT_SKIP_MIB [ROOT_MIB]]   (defaults 321 and 1024, the native layout since WS238)
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import importlib.util
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("check_ufs_image", ROOT / "tools/build/check-ufs-image.py")
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


def main():
	image = pathlib.Path(sys.argv[1])
	skip = int(sys.argv[2]) if len(sys.argv) > 2 else 321
	size = int(sys.argv[3]) if len(sys.argv) > 3 else 1024
	start = skip << 20
	with image.open("r+b") as disk:
		disk.seek(start)
		volume = bytearray(disk.read(size << 20))
		fs = CHECKER.UFS(bytes(volume), runtime=True)

		# An unused inode, marked allocated: the first group with one free (group 0 can be full).
		def free_inode():
			for group in range(fs.ncg):
				cg = (fs.cgstart(group) + fs.cblk) * fs.fsize
				usedoff = struct.unpack_from("<I", volume, cg + 92)[0]
				table = (fs.cgstart(group) + fs.iblk) * fs.fsize
				for n in range(3 if group == 0 else 0, fs.ipg):
					if (not volume[cg + usedoff + n // 8] & (1 << (n % 8))
					    and struct.unpack_from("<H", volume, table + n * 256)[0] == 0):
						return group, n, cg + usedoff, table
			raise SystemExit(f"{image}: no free inode")
		group, index, used, table = free_inode()
		number = group * fs.ipg + index
		volume[used + index // 8] |= 1 << (index % 8)

		# A regular file with no link (an orphan, which the kernel's scan reads only when the link count is 0) and an
		# overflowing time.
		inode = table + index * 256
		volume[inode:inode + 256] = bytes(256)
		struct.pack_into("<H", volume, inode + 0, 0o100644)
		struct.pack_into("<H", volume, inode + 2, 0)
		struct.pack_into("<I", volume, inode + 68, 2000000000)

		# The volume not clean, so that the read-write mount scans for orphans.
		volume[65536 + 209] = 0

		disk.seek(start)
		disk.write(volume)
	print(f"{image}: inode {number} of the root at {skip} MiB damaged; the volume is marked not clean")


if __name__ == "__main__":
	main()
