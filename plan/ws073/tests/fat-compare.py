#!/usr/bin/env python3
"""Checks a FAT partition that zedBSD wrote against the image it came from.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

ws073-p012 (BUG-072): after zedBSD adds, moves and appends files on a boot
FAT (the ESP of an amd64 image, the firmware FAT of a Raspberry Pi 4
image), the volume must still pass the host's fsck.fat -n, and the files the
firmware or the loader reads must be byte-for-byte what they were.

    fat-compare.py ORIGINAL_IMAGE WRITTEN_IMAGE START_SECTOR SECTORS FILE...

FILE names files inside the FAT (for example start4.elf or
EFI/BOOT/BOOTX64.EFI).  Exits with the number of failures.
"""
import subprocess
import sys
import tempfile
from pathlib import Path


def extract(image: str, start: int, sectors: int, output: Path) -> None:
	"""Copies one partition out of a disk image."""
	with open(image, "rb") as source, open(output, "wb") as target:
		source.seek(start * 512)
		remaining = sectors * 512
		while remaining > 0:
			block = source.read(min(remaining, 1 << 20))
			if not block:
				break
			target.write(block)
			remaining -= len(block)


def read_file(partition: Path, name: str) -> bytes:
	"""Reads one file out of a FAT partition with mtools."""
	return subprocess.run(["mtype", "-i", str(partition), "::/" + name],
			      check=True, capture_output=True).stdout


def main() -> int:
	"""Runs the checks and reports each one."""
	original, written = sys.argv[1], sys.argv[2]
	start, sectors = int(sys.argv[3], 0), int(sys.argv[4], 0)
	names = sys.argv[5:]
	failures = 0
	with tempfile.TemporaryDirectory() as scratch:
		before = Path(scratch) / "before.img"
		after = Path(scratch) / "after.img"
		extract(original, start, sectors, before)
		extract(written, start, sectors, after)
		fsck = subprocess.run(["/sbin/fsck.fat", "-n", str(after)],
				      capture_output=True, text=True)
		report = fsck.stdout + fsck.stderr
		print("".join("  fsck: " + line + "\n" for line in report.splitlines()), end="")
		clean = fsck.returncode == 0
		for word in ("invalid", "wrong", "uninitialized", "fixing", "correct"):
			if word in report.lower():
				clean = False
		print(("PASS" if clean else "FAIL") + " fsck.fat -n is clean")
		failures += 0 if clean else 1
		for name in names:
			same = read_file(before, name) == read_file(after, name)
			print(("PASS" if same else "FAIL") + " unchanged " + name)
			failures += 0 if same else 1
	print("failures %d" % failures)
	return failures


if __name__ == "__main__":
	sys.exit(main())
