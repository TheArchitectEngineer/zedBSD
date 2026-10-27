#!/usr/bin/env python3
"""Grows a zedBSD UFS volume to fill the partition (or file) it was written to.

ws299 (2026-10-27, user: "5330 の NVMe の root は、書く時に p5 いっぱいに広げる").
5330-nvme-write.sh copies an image's 1 GiB root UFS onto the 5330's 15 GiB p5 and
then runs this on the 5330's Debian as root:

    python3 -I ufs-grow.py DEVICE            grow DEVICE's volume to DEVICE's size
    python3 -I ufs-grow.py --check DEVICE    say what would change, write nothing
    python3 -I ufs-grow.py --size BYTES FILE grow a volume image file (host test)

The volume keeps its cylinder-group geometry (fragments and inodes per group are
fixed by the superblock).  Growing extends the last group to a whole group and
adds new, empty groups behind it, each with its superblock copy, its header and
a zeroed inode table, the way tools/build/zedimage-host.c lays a group out; the
superblock's group count, size, data size and free totals are rewritten, then
the primary and every backup superblock.  Nothing outside the new size is read
or written.  Only a clean volume without a summary area and without the
journal-snapshot tail behind it (the formats the image build makes) is grown.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import argparse
import os
import struct
import sys

SUPER_OFFSET = 65536
SUPER_SIZE = 8192
UFS_MAGIC = 0x19540119
CG_MAGIC = 0x090255

# Superblock fields (src/drivers/fs/ufs.c UFS_FS_*).
FS_SBLKNO = 8
FS_CBLKNO = 12
FS_IBLKNO = 16
FS_DBLKNO = 20
FS_NCG = 44
FS_BSIZE = 48
FS_FSIZE = 52
FS_FRAG = 56
FS_CSSIZE = 156
FS_CGSIZE = 160
FS_IPG = 184
FS_FPG = 188
FS_CLEAN = 209
FS_SBLOCKLOC = 1000
FS_CSTOTAL_NDIR = 1008
FS_CSTOTAL_NBFREE = 1016
FS_CSTOTAL_NIFREE = 1024
FS_CSTOTAL_NFFREE = 1032
FS_SIZE = 1080
FS_DSIZE = 1088
FS_MAGIC = 1372

# Cylinder group header fields (src/drivers/fs/ufs.c UFS_CG_*).
CG_MAGIC_AT = 4
CG_CGX = 12
CG_NDBLK = 20
CG_NDIR = 24
CG_NBFREE = 28
CG_NIFREE = 32
CG_NFFREE = 36
CG_IUSEDOFF = 92
CG_FREEOFF = 96
CG_NEXTFREEOFF = 100
CG_NIBLK = 116
CG_INITEDIBLK = 120

# The used-inode map and the free-fragment map the image build places.
INODE_MAP_AT = 168
FREE_MAP_AT = 200


def fail(message):
    print('ufs-grow: ' + message, file=sys.stderr)
    sys.exit(1)


def get32(buffer, offset):
    return struct.unpack_from('<I', buffer, offset)[0]


def get64(buffer, offset):
    return struct.unpack_from('<Q', buffer, offset)[0]


def put32(buffer, offset, value):
    struct.pack_into('<I', buffer, offset, value)


def put64(buffer, offset, value):
    struct.pack_into('<Q', buffer, offset, value)


def read_at(fd, offset, length):
    data = os.pread(fd, length, offset)
    if len(data) != length:
        fail('short read at byte %d' % offset)
    return bytearray(data)


def write_at(fd, offset, data):
    written = os.pwrite(fd, bytes(data), offset)
    if written != len(data):
        fail('short write at byte %d' % offset)


class Geometry:
    """The fixed shape of the volume, as its primary superblock gives it."""

    def __init__(self, super_block):
        self.sblk = get32(super_block, FS_SBLKNO)
        self.cblk = get32(super_block, FS_CBLKNO)
        self.iblk = get32(super_block, FS_IBLKNO)
        self.dblk = get32(super_block, FS_DBLKNO)
        self.ncg = get32(super_block, FS_NCG)
        self.bsize = get32(super_block, FS_BSIZE)
        self.fsize = get32(super_block, FS_FSIZE)
        self.frag = get32(super_block, FS_FRAG)
        self.cgsize = get32(super_block, FS_CGSIZE)
        self.ipg = get32(super_block, FS_IPG)
        self.fpg = get32(super_block, FS_FPG)
        self.fragments = get64(super_block, FS_SIZE)

    def group_bytes(self, cg, field):
        """The byte offset of a group's area that starts at fragment FIELD."""
        return (cg * self.fpg + field) * self.fsize

    def ndblk(self, cg, fragments):
        """How many fragments group CG holds in a volume of FRAGMENTS."""
        return min(self.fpg, fragments - cg * self.fpg)


def refuse_unknown_volume(geometry, super_block, fd, medium_bytes):
    """Refuses any volume this tool does not know how to grow."""
    if get32(super_block, FS_MAGIC) != UFS_MAGIC:
        fail('no UFS superblock at byte %d' % SUPER_OFFSET)
    if get64(super_block, FS_SBLOCKLOC) != SUPER_OFFSET:
        fail('the superblock does not say it lives at byte %d' % SUPER_OFFSET)
    if geometry.fsize != 1024 or geometry.bsize != 8192 or geometry.frag != 8:
        fail('not the 1 KiB fragment, 8 KiB block format the image build writes')
    if not (geometry.sblk < geometry.cblk < geometry.iblk < geometry.dblk):
        fail('the group areas are not in the order superblock, header, inodes, data')
    if geometry.ncg == 0 or geometry.fpg == 0 or geometry.ipg == 0:
        fail('the superblock names no groups, fragments or inodes per group')
    if geometry.dblk % geometry.frag != 0:
        fail('the data area of a group does not start on a block')
    if geometry.cgsize > geometry.bsize:
        fail('a group header larger than a block')
    if FREE_MAP_AT + (geometry.fpg + 7) // 8 > geometry.cgsize:
        fail('the free map does not fit the group header')
    if get32(super_block, FS_CSSIZE) != 0:
        fail('the volume has a summary area, which growing would have to move')
    if super_block[FS_CLEAN] != 1:
        fail('the volume is not clean; check it first')
    if geometry.fragments * geometry.fsize > medium_bytes:
        fail('the volume is larger than the medium')

    # The journal-snapshot profile keeps its records right behind the volume.
    end = geometry.fragments * geometry.fsize
    # A file that ends with the volume has nothing behind it (os.pread gives b'').
    tail = os.pread(fd, 4, end)
    if tail in (b'ZUJ2', b'ZSL1'):
        fail('the volume has the journal-snapshot tail behind it')


def mark_free(free_map, start, end):
    """Sets the free bits of fragments START to END - 1, whole bytes at a time."""
    fragment = start
    while fragment < end and fragment % 8 != 0:
        free_map[fragment // 8] |= 1 << (fragment % 8)
        fragment += 1
    whole_bytes = (end - fragment) // 8
    free_map[fragment // 8:fragment // 8 + whole_bytes] = b'\xff' * whole_bytes
    fragment += whole_bytes * 8
    while fragment < end:
        free_map[fragment // 8] |= 1 << (fragment % 8)
        fragment += 1


def count_free(free_map, ndblk, frag):
    """Counts a group's whole free blocks and its loose free fragments."""
    free_fragments = 0
    for fragment in range(ndblk):
        if free_map[fragment // 8] & (1 << (fragment % 8)):
            free_fragments += 1

    whole_blocks = 0
    for block in range(0, ndblk - frag + 1, frag):
        whole = True
        for part in range(frag):
            fragment = block + part
            if not free_map[fragment // 8] & (1 << (fragment % 8)):
                whole = False
                break
        if whole:
            whole_blocks += 1

    return whole_blocks, free_fragments - whole_blocks * frag


def read_group(fd, geometry, cg, ndblk):
    """Reads group CG's header and refuses one that is not that group's."""
    header = read_at(fd, geometry.group_bytes(cg, geometry.cblk), geometry.bsize)
    if get32(header, CG_MAGIC_AT) != CG_MAGIC:
        fail('group %d has no header magic' % cg)
    if get32(header, CG_CGX) != cg:
        fail('group %d names itself %d' % (cg, get32(header, CG_CGX)))
    if get32(header, CG_NDBLK) != ndblk:
        fail('group %d holds %d fragments, the geometry says %d'
             % (cg, get32(header, CG_NDBLK), ndblk))
    if get32(header, CG_IUSEDOFF) != INODE_MAP_AT or get32(header, CG_FREEOFF) != FREE_MAP_AT:
        fail('group %d keeps its maps elsewhere than the image build does' % cg)
    return header


def extend_last_group(header, geometry, old_ndblk, new_ndblk):
    """Marks the fragments the last group gains free and recounts it."""
    free_map = memoryview(header)[FREE_MAP_AT:]
    mark_free(free_map, old_ndblk, new_ndblk)
    whole, loose = count_free(free_map, new_ndblk, geometry.frag)
    put32(header, CG_NDBLK, new_ndblk)
    put32(header, CG_NBFREE, whole)
    put32(header, CG_NFFREE, loose)


def new_group_header(geometry, cg, ndblk):
    """Builds the header of an empty group: no inodes used, the data area free."""
    header = bytearray(geometry.bsize)
    mark_free(memoryview(header)[FREE_MAP_AT:], geometry.dblk, ndblk)

    # The data area starts on a block, so it is whole blocks and a loose tail.
    whole = (ndblk - geometry.dblk) // geometry.frag
    loose = (ndblk - geometry.dblk) - whole * geometry.frag

    put32(header, CG_MAGIC_AT, CG_MAGIC)
    put32(header, CG_CGX, cg)
    put32(header, CG_NDBLK, ndblk)
    put32(header, CG_NDIR, 0)
    put32(header, CG_NBFREE, whole)
    put32(header, CG_NIFREE, geometry.ipg)
    put32(header, CG_NFFREE, loose)
    put32(header, CG_IUSEDOFF, INODE_MAP_AT)
    put32(header, CG_FREEOFF, FREE_MAP_AT)
    put32(header, CG_NEXTFREEOFF, geometry.cgsize)
    put32(header, CG_NIBLK, geometry.ipg)
    put32(header, CG_INITEDIBLK, geometry.ipg)
    return header


def plan_size(geometry, medium_bytes):
    """Chooses the grown fragment and group counts for a medium of that size."""
    fragments = medium_bytes // geometry.fsize
    ncg = (fragments + geometry.fpg - 1) // geometry.fpg

    # A last group without room for at least one data block is left off.
    if fragments - (ncg - 1) * geometry.fpg < geometry.dblk + geometry.frag:
        ncg -= 1
        fragments = ncg * geometry.fpg

    if ncg * geometry.ipg > 0xffffffff:
        fail('the grown volume would hold more inodes than a 32-bit number')
    return fragments, ncg


def grow(path, check_only, file_size):
    flags = os.O_RDONLY if check_only else os.O_RDWR
    fd = os.open(path, flags)
    try:
        # A file image is extended to the asked size first; a device has its own.
        if file_size is not None and not check_only:
            if os.lseek(fd, 0, os.SEEK_END) < file_size:
                os.ftruncate(fd, file_size)
        medium_bytes = os.lseek(fd, 0, os.SEEK_END)
        if file_size is not None and check_only:
            medium_bytes = max(medium_bytes, file_size)

        super_block = read_at(fd, SUPER_OFFSET, SUPER_SIZE)
        geometry = Geometry(super_block)
        refuse_unknown_volume(geometry, super_block, fd, medium_bytes)

        new_fragments, new_ncg = plan_size(geometry, medium_bytes)
        old_last = geometry.ncg - 1
        if new_ncg <= geometry.ncg and new_fragments <= geometry.fragments:
            print('ufs-grow: the volume already fills the medium (%d MiB, %d groups)'
                  % (geometry.fragments // 1024, geometry.ncg))
            return 0
        if new_ncg < geometry.ncg:
            fail('internal: the grown volume has fewer groups')

        print('ufs-grow: %d MiB in %d groups -> %d MiB in %d groups (%d fragments a group)'
              % (geometry.fragments // 1024, geometry.ncg,
                 new_fragments // 1024, new_ncg, geometry.fpg))
        if check_only:
            return 0

        # The new groups first: until the superblock names them they are unused space.
        inode_table = bytes((geometry.dblk - geometry.iblk) * geometry.fsize)
        for cg in range(geometry.ncg, new_ncg):
            ndblk = geometry.ndblk(cg, new_fragments)
            write_at(fd, geometry.group_bytes(cg, geometry.iblk), inode_table)
            write_at(fd, geometry.group_bytes(cg, geometry.cblk),
                     new_group_header(geometry, cg, ndblk))

        # The last old group grows to its share of the new size.
        old_ndblk = geometry.ndblk(old_last, geometry.fragments)
        last = read_group(fd, geometry, old_last, old_ndblk)
        extend_last_group(last, geometry, old_ndblk,
                          geometry.ndblk(old_last, new_fragments))
        write_at(fd, geometry.group_bytes(old_last, geometry.cblk), last)

        # The volume's totals, summed over every group's header.
        totals = [0, 0, 0, 0]
        for cg in range(new_ncg):
            header = read_group(fd, geometry, cg, geometry.ndblk(cg, new_fragments))
            totals[0] += get32(header, CG_NDIR)
            totals[1] += get32(header, CG_NBFREE)
            totals[2] += get32(header, CG_NIFREE)
            totals[3] += get32(header, CG_NFFREE)

        # The superblock: the new size, then every backup and finally the primary.
        put32(super_block, FS_NCG, new_ncg)
        put64(super_block, FS_SIZE, new_fragments)
        put64(super_block, FS_DSIZE, new_fragments - new_ncg * geometry.dblk)
        put64(super_block, FS_CSTOTAL_NDIR, totals[0])
        put64(super_block, FS_CSTOTAL_NBFREE, totals[1])
        put64(super_block, FS_CSTOTAL_NIFREE, totals[2])
        put64(super_block, FS_CSTOTAL_NFFREE, totals[3])
        for cg in range(1, new_ncg):
            write_at(fd, geometry.group_bytes(cg, geometry.sblk), super_block)
        os.fsync(fd)
        write_at(fd, SUPER_OFFSET, super_block)
        os.fsync(fd)

        print('ufs-grow: grown; %d free blocks, %d free inodes' % (totals[1], totals[2]))
        return 0
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description='Grows a zedBSD UFS volume to its medium.')
    parser.add_argument('--check', action='store_true', help='say what would change, write nothing')
    parser.add_argument('--size', type=int, help='grow a file image to this many bytes')
    parser.add_argument('path')
    args = parser.parse_args()
    return grow(args.path, args.check, args.size)


if __name__ == '__main__':
    sys.exit(main())
