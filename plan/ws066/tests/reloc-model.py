#!/usr/bin/env python3
"""ws066-p001: the symbol work the dynamic loader does to start a program.

Reads a program and its DT_NEEDED closure from a guest root (a staged
rootfs, or a mounted image) and follows what src/rtld/rtld.c does at
startup, without running anything:

  - the objects in the loader's order: the program, ld.so, then the
    dependencies depth first as load_dependencies finds them (load_object
    shares one already loaded);
  - every relocation of every object: RELATIVE ones need no symbol; a
    symbol relocation whose symbol is a defined local one is resolved in
    its own object; every other one is a lookup (lookup_symbol_version):
    the program first, then the other objects in order, ld.so last;
  - each probe of an object: with a GNU hash, the bloom word, then the
    bucket's chain, comparing the hash before the name; with only a SysV
    hash, the chain with a name comparison at every entry.

It counts the lookups, probes, bloom checks, chain steps and name
comparisons, per object and in all, and what four candidates would leave:

  cache     one lookup per (object, symbol index), not per relocation;
  gnu-exe   the program probed through a GNU hash instead of its SysV one
            (estimated: a probe that misses costs one bloom check);
  symbolic  -Bsymbolic-functions for the shared libraries: a relocation to
            a function the same library defines needs no lookup;
  lazy      JUMP_SLOT relocations left to the first call.

Symbol versions are not compared (a name match is a match), TLS
relocations are counted as lookups like the others, and copy relocations
search past the program as the loader does.  The counts are a model of the
loader's work, not a time: ws066-p001 measures the times in the guest.

    reloc-model.py ROOT PROGRAM...      (PROGRAM is a path inside ROOT)

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import os
import struct
import sys

PT_LOAD = 1
PT_DYNAMIC = 2
DT_NULL, DT_NEEDED, DT_PLTRELSZ, DT_HASH, DT_STRTAB, DT_SYMTAB = 0, 1, 2, 4, 5, 6
DT_RELA, DT_RELASZ, DT_STRSZ, DT_JMPREL, DT_RUNPATH, DT_RPATH = 7, 8, 10, 23, 29, 15
DT_GNU_HASH = 0x6ffffef5
R_X86_64_COPY, R_X86_64_JUMP_SLOT, R_X86_64_RELATIVE = 5, 7, 8
R_X86_64_IRELATIVE = 37
STT_FUNC = 2
STB_LOCAL = 0
SEARCH = ("/lib", "/usr/lib")


def gnu_hash(name):
    value = 5381
    for byte in name:
        value = (value * 33 + byte) & 0xffffffff
    return value


def elf_hash(name):
    value = 0
    for byte in name:
        value = ((value << 4) + byte) & 0xffffffff
        high = value & 0xf0000000
        value ^= high >> 24
        value &= ~high & 0xffffffff
    return value


class Image:
    """One ELF64 little-endian object, read through its dynamic section."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as stream:
            self.data = stream.read()
        data = self.data
        if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
            raise ValueError(f"{path}: not ELF64 LSB")
        phoff = struct.unpack_from("<Q", data, 32)[0]
        phentsize, phnum = struct.unpack_from("<HH", data, 54)
        self.loads = []
        dynamic = None
        for i in range(phnum):
            kind, _flags, offset, vaddr, _paddr, filesz, _memsz, _align = \
                struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize)
            if kind == PT_LOAD:
                self.loads.append((vaddr, offset, filesz))
            elif kind == PT_DYNAMIC:
                dynamic = (offset, filesz)
        self.tags = {}
        self.needed = []
        offset, size = dynamic
        for i in range(size // 16):
            tag, value = struct.unpack_from("<qQ", data, offset + i * 16)
            if tag == DT_NULL:
                break
            if tag == DT_NEEDED:
                self.needed.append(value)
            else:
                self.tags[tag] = value
        self.strtab = self.offset(self.tags[DT_STRTAB])
        self.needed = [self.string(value) for value in self.needed]
        self.symtab = self.offset(self.tags[DT_SYMTAB])
        self.read_hashes()

    def offset(self, vaddr):
        for start, offset, size in self.loads:
            if start <= vaddr < start + size:
                return offset + (vaddr - start)
        raise ValueError(f"{self.path}: address {vaddr:#x} outside the file")

    def string(self, position):
        start = self.strtab + position
        end = self.data.index(b"\0", start)
        return self.data[start:end]

    def symbol(self, index):
        name, info, _other, shndx, value, _size = \
            struct.unpack_from("<IBBHQQ", self.data, self.symtab + index * 24)
        return self.string(name), info >> 4, info & 15, shndx

    def read_hashes(self):
        data = self.data
        self.gnu = None
        self.sysv = None
        if DT_GNU_HASH in self.tags:
            base = self.offset(self.tags[DT_GNU_HASH])
            nbuckets, symoffset, bloom_size, shift = struct.unpack_from("<IIII", data, base)
            bloom = struct.unpack_from(f"<{bloom_size}Q", data, base + 16)
            buckets_at = base + 16 + bloom_size * 8
            buckets = struct.unpack_from(f"<{nbuckets}I", data, buckets_at)
            chain_at = buckets_at + nbuckets * 4
            count = symoffset
            last = max(buckets) if buckets else 0
            if last >= symoffset:
                index = last
                while True:
                    entry = struct.unpack_from("<I", data, chain_at + (index - symoffset) * 4)[0]
                    index += 1
                    if entry & 1:
                        break
                count = index
            self.gnu = (nbuckets, symoffset, bloom, shift, buckets, chain_at)
            self.symbol_count = count
        if DT_HASH in self.tags:
            base = self.offset(self.tags[DT_HASH])
            nbucket, nchain = struct.unpack_from("<II", data, base)
            self.sysv = (nbucket, base + 8, base + 8 + nbucket * 4)
            self.symbol_count = nchain

    def relocations(self):
        """Yields (type, symbol index) of every RELA and JMPREL entry."""
        for table, size in ((DT_RELA, DT_RELASZ), (DT_JMPREL, DT_PLTRELSZ)):
            if table not in self.tags:
                continue
            base = self.offset(self.tags[table])
            for i in range(self.tags[size] // 24):
                _where, info, _addend = struct.unpack_from("<QQq", self.data, base + i * 24)
                yield info & 0xffffffff, info >> 32


class Counter:
    def __init__(self):
        self.lookups = 0
        self.probes = 0
        self.sysv_probes = 0
        self.bloom_checks = 0
        self.bloom_passes = 0
        self.chain_steps = 0
        self.compares = 0
        self.hash_bytes = 0
        self.unresolved = 0

    def add(self, other):
        for key, value in vars(other).items():
            setattr(self, key, getattr(self, key) + value)


def probe(image, name, hashes, counter):
    """One object's lookup of a name; returns True when it defines it."""
    counter.probes += 1
    if image.gnu is not None:
        nbuckets, symoffset, bloom, shift, buckets, chain_at = image.gnu
        value = hashes[0]
        word = bloom[(value // 64) % len(bloom)]
        mask = (1 << (value % 64)) | (1 << ((value >> shift) % 64))
        counter.bloom_checks += 1
        if word & mask != mask:
            return False
        counter.bloom_passes += 1
        index = buckets[value % nbuckets]
        if index == 0:
            return False
        while True:
            entry = struct.unpack_from("<I", image.data, chain_at + (index - symoffset) * 4)[0]
            counter.chain_steps += 1
            if (entry | 1) == (value | 1):
                counter.compares += 1
                if defines(image, index, name):
                    return True
            if entry & 1:
                return False
            index += 1
    if image.sysv is not None:
        counter.sysv_probes += 1
        nbucket, buckets_at, chains_at = image.sysv
        index = struct.unpack_from("<I", image.data, buckets_at + (hashes[1] % nbucket) * 4)[0]
        while index != 0:
            counter.chain_steps += 1
            counter.compares += 1
            if defines(image, index, name):
                return True
            index = struct.unpack_from("<I", image.data, chains_at + index * 4)[0]
    return False


def defines(image, index, name):
    symbol_name, binding, _kind, shndx = image.symbol(index)
    return symbol_name == name and shndx != 0 and binding != STB_LOCAL


def find(root, name, runpaths):
    for directory in runpaths + list(SEARCH):
        path = os.path.join(root, directory.lstrip("/"), name.decode())
        if os.path.exists(path):
            return os.path.realpath(path)
    raise FileNotFoundError(name.decode())


def load_order(root, program):
    main = Image(os.path.join(root, program.lstrip("/")))
    interpreter = Image(os.path.join(root, "lib/ld.so"))
    order = [main, interpreter]
    seen = {}

    def load(image):
        runpaths = []
        for tag in (DT_RUNPATH, DT_RPATH):
            if tag in image.tags:
                runpaths += image.string(image.tags[tag]).decode().split(":")
        for name in image.needed:
            path = find(root, name, runpaths)
            if path in seen:
                continue
            child = Image(path)
            seen[path] = child
            order.append(child)
            load(child)

    load(main)
    return order


def model(root, program):
    order = load_order(root, program)
    main, interpreter = order[0], order[1]
    search = [main] + order[2:] + [interpreter]
    rows = []
    totals = {key: Counter() for key in ("now", "cache", "symbolic", "lazy")}
    exe_misses = 0
    for image in order:
        counter = Counter()
        cached = Counter()
        symbolic = Counter()
        lazy = Counter()
        seen_index = set()
        relative = 0
        symbolic_count = 0
        jump_slots = 0
        for kind, index in image.relocations():
            if kind in (R_X86_64_RELATIVE, R_X86_64_IRELATIVE) or index == 0:
                relative += 1
                continue
            name, binding, sym_kind, shndx = image.symbol(index)
            if shndx != 0 and binding == STB_LOCAL:
                continue
            symbolic_count += 1
            one = Counter()
            one.lookups = 1
            one.hash_bytes = len(name)
            hashes = (gnu_hash(name), elf_hash(name))
            found = False
            objects = search[1:] if kind == R_X86_64_COPY else search
            for candidate in objects:
                if probe(candidate, name, hashes, one):
                    found = True
                    break
                if candidate is main and main.gnu is None:
                    exe_misses += 1
            if not found:
                one.unresolved = 1
            counter.add(one)
            if index not in seen_index:
                seen_index.add(index)
                cached.add(one)
            if not (image is not main and shndx != 0 and sym_kind == STT_FUNC):
                symbolic.add(one)
            if kind == R_X86_64_JUMP_SLOT:
                jump_slots += 1
            else:
                lazy.add(one)
        rows.append((os.path.basename(image.path), relative, symbolic_count, jump_slots,
                     len(seen_index), counter, image.gnu is not None))
        totals["now"].add(counter)
        totals["cache"].add(cached)
        totals["symbolic"].add(symbolic)
        totals["lazy"].add(lazy)
    return order, rows, totals, exe_misses


def main():
    root = sys.argv[1]
    for program in sys.argv[2:]:
        order, rows, totals, exe_misses = model(root, program)
        print(f"== {program}: {len(order)} objects, executable hash "
              f"{'GNU' if order[0].gnu else 'SysV only'}")
        print(f"{'object':28} {'RELATIVE':>9} {'symbol':>7} {'JUMP':>6} {'uniq':>6} "
              f"{'probes':>8} {'sysv':>7} {'bloom':>8} {'pass':>7} {'steps':>8} {'strcmp':>8} gnu")
        for name, relative, symbolic, jumps, unique, counter, gnu in rows:
            print(f"{name:28} {relative:9} {symbolic:7} {jumps:6} {unique:6} {counter.probes:8} "
                  f"{counter.sysv_probes:7} {counter.bloom_checks:8} {counter.bloom_passes:7} "
                  f"{counter.chain_steps:8} {counter.compares:8} {'y' if gnu else 'n'}")
        now = totals["now"]
        print(f"total: lookups={now.lookups} probes={now.probes} sysv-probes={now.sysv_probes} "
              f"bloom={now.bloom_checks} pass={now.bloom_passes} steps={now.chain_steps} "
              f"strcmp={now.compares} hash-bytes={now.hash_bytes} unresolved={now.unresolved}")
        for key in ("cache", "symbolic", "lazy"):
            left = totals[key]
            print(f"  {key:9} lookups={left.lookups} probes={left.probes} steps={left.chain_steps} "
                  f"strcmp={left.compares} hash-bytes={left.hash_bytes}")
        print(f"  gnu-exe   SysV probes of the program that miss (each would be one bloom check): "
              f"{exe_misses}")


if __name__ == "__main__":
    main()
