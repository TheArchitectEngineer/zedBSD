#!/usr/bin/env python3
"""Host test of fsck (userland/base/fsck, BUG-312) on damaged UFS images.

Builds fsck for the host (ASan/UBSan) and zedimage-host, makes a small
volume with files, directories, a large file (single indirect) and a link,
then damages copies of it one way each: the superblock's totals, a group's
counts and maps, a link count, entries naming free inodes, files and
directories nothing names, a block owned twice, a block outside the data
area, a damaged directory record, an allocated inode with no type, an
unlinked file left allocated, a time's nanoseconds, a wrong "..".  For each:
fsck -n reports and changes nothing (exit 8), fsck -y repairs (exit 4),
fsck -n is then clean (exit 0), the host validator
(tools/build/check-ufs-image.py) accepts the volume, and the files that
should survive have their contents.

    python3 -I plan/tools/ufs/fsck-host-test.py [OUT]   (default build/tmp/fsck-host)
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import hashlib
import importlib.util
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("check_ufs_image", ROOT / "tools/build/check-ufs-image.py")
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)

FSCK_SOURCES = ["main.c", "volume.c", "check.c", "directory.c"]
CHECKS = 0
FAILURES = 0


def check(passed, what):
	"""Counts one check and reports a failure."""
	global CHECKS, FAILURES
	CHECKS += 1
	if not passed:
		FAILURES += 1
		print(f"FAIL: {what}")


def fresh_out(name):
	"""Makes a new run directory and points NAME at it, removing nothing (plan/tools/fresh-out.sh)."""
	name = pathlib.Path(name)
	name.parent.mkdir(parents=True, exist_ok=True)
	if name.is_dir() and not name.is_symlink():
		name.rename(f"{name}.old.{os.getpid()}")
	run = pathlib.Path(tempfile.mkdtemp(prefix=name.name + ".run.", dir=name.parent))
	if name.is_symlink():
		name.unlink()
	name.symlink_to(run.name)
	return run


def build(out):
	"""Builds fsck and zedimage-host for the host."""
	sources = [str(ROOT / "userland/base/fsck" / source) for source in FSCK_SOURCES]
	sources += [str(ROOT / "userland/base/mkfs/ufs-super.c"), str(ROOT / "userland/base/mkfs/ufs-endian.c")]
	subprocess.run(["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
			"-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{ROOT}", "-o", str(out / "fsck")] + sources,
		       check=True)
	subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", str(ROOT / "tools/build/zedimage-host.c"),
			"-o", str(out / "zedimage-host")], check=True)


def make_tree(tree):
	"""The files of the volume: small files, a directory, a large file, a short link."""
	(tree / "sub" / "deep").mkdir(parents=True)
	(tree / "a.txt").write_bytes(b"alpha\n")
	(tree / "sub" / "b.txt").write_bytes(b"bravo\n")
	(tree / "sub" / "deep" / "c.txt").write_bytes(b"charlie\n")
	(tree / "big.bin").write_bytes(bytes((index * 7) & 0xff for index in range(300 * 1024)))
	(tree / "short-link").symlink_to("a.txt")


def run_fsck(out, image, option):
	"""Runs fsck and returns its exit code and output."""
	result = subprocess.run([str(out / "fsck"), option, str(image)], capture_output=True, text=True)
	return result.returncode, result.stdout + result.stderr


class Volume:
	"""Writes into an image at the places the host validator names."""

	def __init__(self, path):
		self.path = path
		self.data = bytearray(path.read_bytes())
		self.fs = CHECKER.UFS(bytes(self.data), runtime=True)

	def save(self):
		self.path.write_bytes(self.data)

	def inode_offset(self, number):
		group, index = divmod(number, self.fs.ipg)
		return (self.fs.cgstart(group) + self.fs.iblk) * self.fs.fsize + index * 256

	def group_offset(self, group):
		return (self.fs.cgstart(group) + self.fs.cblk) * self.fs.fsize

	def put(self, offset, fmt, value):
		struct.pack_into(fmt, self.data, offset, value)

	def get(self, offset, fmt):
		return struct.unpack_from(fmt, self.data, offset)[0]

	def entry_offset(self, directory, name):
		"""The byte offset of a name's record in a directory."""
		fragment = self.fs.blocks(directory)[0]
		base = fragment * self.fs.fsize
		size = self.get(self.inode_offset(directory) + 16, "<Q")
		offset = 0
		while offset < size:
			ino, reclen, _, namlen = struct.unpack_from("<IHBB", self.data, base + offset)
			if ino and bytes(self.data[base + offset + 8:base + offset + 8 + namlen]).decode() == name:
				return base + offset
			offset += reclen
		raise KeyError(name)

	def set_free_bit(self, fragment, free):
		"""Sets a fragment's bit in its group's free map."""
		group = fragment // self.fs.fpg
		local = fragment - self.fs.cgstart(group)
		cg = self.group_offset(group)
		freeoff = self.get(cg + 96, "<I")
		byte = cg + freeoff + local // 8
		if free:
			self.data[byte] |= 1 << (local % 8)
		else:
			self.data[byte] &= ~(1 << (local % 8)) & 0xff

	def set_used_bit(self, number, used):
		"""Sets an inode's bit in its group's inode map."""
		group, index = divmod(number, self.fs.ipg)
		cg = self.group_offset(group)
		usedoff = self.get(cg + 92, "<I")
		byte = cg + usedoff + index // 8
		if used:
			self.data[byte] |= 1 << (index % 8)
		else:
			self.data[byte] &= ~(1 << (index % 8)) & 0xff

	def free_inode(self):
		"""A free inode number of group 0."""
		for number in range(10, self.fs.ipg):
			if self.get(self.inode_offset(number), "<H") == 0:
				return number
		raise RuntimeError("no free inode")


def lost_found_entries(fs):
	"""The names in /lost+found, or none."""
	names = [entry for entry in fs.entries(CHECKER.ROOT) if entry[0] == "lost+found"]
	if not names:
		return {}
	return {name: number for name, number, _ in fs.entries(names[0][1]) if name not in (".", "..")}


def damage_cases(base):
	"""Each case: a name, a function that damages a Volume, and a check of the repaired volume."""
	probe = Volume(base)
	root = CHECKER.ROOT
	a = probe.fs.lookup("/a.txt")
	b = probe.fs.lookup("/sub/b.txt")
	sub = probe.fs.lookup("/sub")
	deep = probe.fs.lookup("/sub/deep")
	big = probe.fs.lookup("/big.bin")
	a_block = probe.fs.blocks(a)[0]
	free_inode = probe.free_inode()

	def content(fs, path, expected):
		return fs.read_file(fs.lookup(path)) == expected

	def totals(volume):
		volume.put(65536 + 1016, "<Q", volume.get(65536 + 1016, "<Q") + 5)
		volume.data[65536 + 209] = 0

	def group_counts(volume):
		cg = volume.group_offset(1)
		volume.put(cg + 28, "<I", volume.get(cg + 28, "<I") + 3)

	def used_block_free(volume):
		volume.set_free_bit(a_block, True)

	def free_block_used(volume):
		volume.set_free_bit(volume.fs.cgstart(2) + volume.fs.dblk + 40 * 8, False)

	def link_count(volume):
		volume.put(volume.inode_offset(a) + 2, "<H", 5)

	def entry_to_free(volume):
		volume.put(volume.entry_offset(sub, "b.txt"), "<I", free_inode)

	def unnamed_file(volume):
		volume.put(volume.entry_offset(root, "a.txt"), "<I", 0)

	def unnamed_directory(volume):
		volume.put(volume.entry_offset(root, "sub"), "<I", 0)

	def duplicate_block(volume):
		volume.put(volume.inode_offset(big) + 112, "<Q", a_block)

	def bad_block(volume):
		volume.put(volume.inode_offset(big) + 112 + 8, "<Q", 1)

	def damaged_record(volume):
		offset = volume.entry_offset(sub, "b.txt")
		volume.put(offset + 4, "<H", 3)

	def typeless_inode(volume):
		volume.set_used_bit(free_inode, True)

	def unlinked_file(volume):
		volume.put(volume.entry_offset(root, "a.txt"), "<I", 0)
		volume.put(volume.inode_offset(a) + 2, "<H", 0)

	def nanoseconds(volume):
		volume.put(volume.inode_offset(a) + 68, "<I", 2000000000)

	def wrong_dotdot(volume):
		offset = volume.entry_offset(deep, "..")
		volume.put(offset, "<I", root)

	return [
		("superblock totals and clean flag", totals, lambda fs: content(fs, "/a.txt", b"alpha\n")),
		("group counts", group_counts, lambda fs: True),
		("used block marked free", used_block_free, lambda fs: content(fs, "/a.txt", b"alpha\n")),
		("free block marked used", free_block_used, lambda fs: True),
		("link count", link_count, lambda fs: CHECKER.UFS.u16(fs.inode(a), 2) == 1),
		("entry naming a free inode", entry_to_free,
		 lambda fs: f"#{b}" in lost_found_entries(fs) and fs.read_file(b) == b"bravo\n"),
		("file nothing names", unnamed_file,
		 lambda fs: f"#{a}" in lost_found_entries(fs) and fs.read_file(a) == b"alpha\n"),
		("directory nothing names", unnamed_directory,
		 lambda fs: f"#{sub}" in lost_found_entries(fs) and content(fs, f"/lost+found/#{sub}/deep/c.txt", b"charlie\n")),
		("block owned twice", duplicate_block,
		 lambda fs: content(fs, "/a.txt", b"alpha\n") and "big.bin" not in [entry[0] for entry in fs.entries(root)]),
		("block outside the data area", bad_block, lambda fs: "big.bin" not in [entry[0] for entry in fs.entries(root)]),
		("damaged directory record", damaged_record, lambda fs: f"#{b}" in lost_found_entries(fs)),
		("allocated inode with no type", typeless_inode, lambda fs: True),
		("unlinked file left allocated", unlinked_file, lambda fs: "a.txt" not in [entry[0] for entry in fs.entries(root)]),
		("nanoseconds", nanoseconds, lambda fs: CHECKER.UFS.u32(fs.inode(a), 68) == 0),
		("wrong ..", wrong_dotdot, lambda fs: [entry[1] for entry in fs.entries(deep) if entry[0] == ".."] == [sub]),
	]


def main():
	out = fresh_out(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/tmp/fsck-host")
	build(out)
	tree = out / "tree"
	make_tree(tree)
	base = out / "base.img"
	subprocess.run([str(out / "zedimage-host"), "ufs", "24M", str(tree), str(base), "--inodes=768"], check=True)

	# The undamaged volume is clean.
	code, text = run_fsck(out, base, "-n")
	check(code == 0, f"clean volume: fsck -n exit {code}\n{text}")
	code, text = run_fsck(out, base, "-y")
	check(code == 0, f"clean volume: fsck -y exit {code}\n{text}")

	# Each damage.
	for name, damage, survived in damage_cases(base):
		image = out / (name.replace(" ", "-").replace(".", "dot") + ".img")
		shutil.copyfile(base, image)
		volume = Volume(image)
		damage(volume)
		volume.save()
		before = hashlib.sha256(image.read_bytes()).hexdigest()
		code, text = run_fsck(out, image, "-n")
		check(code == 8, f"{name}: fsck -n exit {code}, expected 8\n{text}")
		check(hashlib.sha256(image.read_bytes()).hexdigest() == before, f"{name}: fsck -n changed the image")
		code, text = run_fsck(out, image, "-y")
		check(code == 4, f"{name}: fsck -y exit {code}, expected 4\n{text}")
		code, again = run_fsck(out, image, "-n")
		check(code == 0, f"{name}: fsck -n after the repair exit {code}\n{again}")
		try:
			fs = CHECKER.UFS(image.read_bytes(), runtime=True)
			fs.validate()
			check(survived(fs), f"{name}: the expected files are not as they should be\n{text}")
		except Exception as error:  # the validator's verdict is the test's
			check(False, f"{name}: the validator refuses the repaired volume: {error}\n{text}")

	print(f"fsck_host_test: {CHECKS} checks, {FAILURES} failures")
	return 1 if FAILURES else 0


if __name__ == "__main__":
	sys.exit(main())
