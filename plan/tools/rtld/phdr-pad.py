#!/usr/bin/env python3
"""ws140: gives a 64-bit little-endian ELF file more program headers.

The loader keeps up to sixteen program headers inside an object and the rest
in a table of their own (src/rtld/rtld.c, object_set_programs).  A linked
library has about ten, so this adds COUNT PT_NULL headers, which every reader
skips: the table is copied to the end of the file with the new entries after
it, and e_phoff and e_phnum are pointed at the copy.  A shared library's
headers need not be mapped (the loader reads them from the file), so nothing
else moves; a PT_PHDR entry still names the old copy, which the loader reads
only for the program itself.

    phdr-pad.py FILE COUNT

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import struct
import sys

ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
PHDR_SIZE = 56


def main():
    path = sys.argv[1]
    count = int(sys.argv[2])
    with open(path, "rb") as stream:
        data = bytearray(stream.read())
    fields = list(ELF_HEADER.unpack_from(data, 0))
    ident = fields[0]
    if ident[:4] != b"\x7fELF" or ident[4] != 2 or ident[5] != 1:
        sys.exit("phdr-pad: not a 64-bit little-endian ELF file")
    phoff, phentsize, phnum = fields[5], fields[9], fields[10]
    if phentsize != PHDR_SIZE:
        sys.exit("phdr-pad: unexpected program header size")
    table = bytes(data[phoff:phoff + phnum * PHDR_SIZE])
    while len(data) % 8 != 0:
        data.append(0)
    fields[5] = len(data)
    fields[10] = phnum + count
    data += table + bytes(count * PHDR_SIZE)
    ELF_HEADER.pack_into(data, 0, *fields)
    with open(path, "wb") as stream:
        stream.write(data)
    print(f"phdr-pad: {path} {phnum} -> {phnum + count} program headers")


if __name__ == "__main__":
    main()
