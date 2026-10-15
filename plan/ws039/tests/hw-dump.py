#!/usr/bin/env python3
#
# Reads the state of a Navi10 board from the Linux side of the test machine,
# as the oracle the read-only stages of the amdrdna driver are compared with.
#
# Usage (root): python3 -I hw-dump.py <pci address> <register list> <output directory>
#
# It reads the register list (made by hw-reglist.py) through the register
# BAR, the PCI configuration space, the IP discovery table and the PSP
# runtime database at the top of VRAM (through the indirect VRAM window), the
# SMU firmware flags (through the indirect register window), the expansion ROM
# and the ACPI VFCT table.  The only registers it writes are the two index
# registers of the indirect windows, and it puts their old values back.  It
# refuses to run when the function does not decode memory, rather than
# enabling it.

import ctypes
import mmap
import os
import struct
import sys

WINDOW_LOW = 0x0
WINDOW_DATA = 0x1
WINDOW_HIGH = 0x6
INDIRECT_INDEX = 0xE
INDIRECT_DATA = 0xF
MEMSIZE = 0xDE3
SMU_FIRMWARE_FLAGS = 0x03B10024


def read32(mapping, dword):
    """Reads one 32-bit register by its dword index."""
    return ctypes.c_uint32.from_buffer(mapping, dword * 4).value


def write32(mapping, dword, value):
    """Writes one 32-bit register by its dword index."""
    ctypes.c_uint32.from_buffer(mapping, dword * 4).value = value


def read_vram(mapping, offset, length):
    """Reads VRAM through the indirect window, one dword at a time."""
    saved_low = read32(mapping, WINDOW_LOW)
    saved_high = read32(mapping, WINDOW_HIGH)
    words = []
    position = offset
    while position < offset + length:
        write32(mapping, WINDOW_LOW, (position & 0x7FFFFFFF) | 0x80000000)
        write32(mapping, WINDOW_HIGH, position >> 31)
        words.append(read32(mapping, WINDOW_DATA))
        position += 4
    write32(mapping, WINDOW_HIGH, saved_high)
    write32(mapping, WINDOW_LOW, saved_low)
    return struct.pack("<%dI" % len(words), *words)


def read_indirect(mapping, address):
    """Reads one register outside the BAR through the indirect window."""
    saved = read32(mapping, INDIRECT_INDEX)
    write32(mapping, INDIRECT_INDEX, address)
    read32(mapping, INDIRECT_INDEX)
    value = read32(mapping, INDIRECT_DATA)
    write32(mapping, INDIRECT_INDEX, saved)
    return value


def save(directory, name, data):
    """Writes one output file."""
    with open(os.path.join(directory, name), "wb") as stream:
        stream.write(data)


def main():
    address, list_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
    device = "/sys/bus/pci/devices/" + address
    os.makedirs(out, exist_ok=True)

    with open(device + "/config", "rb") as stream:
        config = stream.read()
    save(out, "config.bin", config)
    command = struct.unpack_from("<H", config, 4)[0]
    if (command & 0x2) == 0:
        print("memory decoding is off (command 0x%04x); not touching the BAR" % command)
        return 1

    descriptor = os.open(device + "/resource5", os.O_RDWR | os.O_SYNC)
    mapping = mmap.mmap(descriptor, 0x80000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)

    lines = []
    with open(list_path) as stream:
        for line in stream:
            if line.startswith("#") or not line.strip():
                continue
            name, dword = line.split()
            value = read32(mapping, int(dword, 16))
            lines.append("%-56s %s 0x%08x" % (name, dword, value))
    smu_flags = read_indirect(mapping, SMU_FIRMWARE_FLAGS)
    lines.append("%-56s %s 0x%08x" % ("smn.MP1_FIRMWARE_FLAGS", "0x%08x" % SMU_FIRMWARE_FLAGS, smu_flags))
    save(out, "regs.txt", ("\n".join(lines) + "\n").encode())

    vram = read32(mapping, MEMSIZE) << 20
    if vram == 0 or vram >= (0xFFFFFFFF << 20):
        print("VRAM size unreadable")
        return 1
    save(out, "discovery.bin", read_vram(mapping, vram - 0x10000, 10 * 1024))
    save(out, "runtime-db.bin", read_vram(mapping, vram - 0x100000, 4096))
    mapping.close()
    os.close(descriptor)

    try:
        with open(device + "/rom", "w") as stream:
            stream.write("1")
        with open(device + "/rom", "rb") as stream:
            save(out, "rom.bin", stream.read())
        with open(device + "/rom", "w") as stream:
            stream.write("0")
    except OSError as error:
        print("ROM not read: %s" % error)

    try:
        with open("/sys/firmware/acpi/tables/VFCT", "rb") as stream:
            save(out, "vfct.bin", stream.read())
    except OSError as error:
        print("VFCT not read: %s" % error)

    print("vram %d MiB, SMU flags 0x%08x, %d registers" % (vram >> 20, smu_flags, len(lines)))
    return 0


sys.exit(main())
