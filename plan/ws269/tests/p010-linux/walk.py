# Walks a PowerVR Rogue GPU page table in physical memory (/dev/mem) from
# a page catalogue's physical address, resolves a GPU virtual address,
# and prints each level's entry and the bytes at the address.
#
# Values referenced:
# Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_mmu_defs.h
# (ROGUE_MMUCTRL_ENTRY_SIZE_PC_VALUE 32 bits, _PD_ 64, _PT_ 64;
# PC_DATA_PD_BASE_SHIFT 4 with the PD address aligned to 4 KiB,
# PC_DATA_VALID bit 0; PD_DATA_PT_BASE bits 5..39, PD_DATA_PAGE_SIZE bits
# 1..3 (0 4 KiB, 1 16 KiB, 2 64 KiB, 3 256 KiB, 4 1 MiB, 5 2 MiB),
# PD_DATA_VALID bit 0; PT_DATA_PAGE bits 12..39 for 4 KiB pages
# (PAGE_<size>_RANGE: from the page size's shift), PT_DATA_VALID bit 0,
# READ_ONLY 1, CC 2, SLC_BYPASS_CTRL 3, PM_SRC 4, PM_META_PROTECT 62) and
# pvr_mmu.c (the PC indexed by VA bits 30..39, the PD by 21..29, the PT
# by the bits from the page size's shift to 20).
#
# Use: sudo python3 -I walk.py PC_PHYS GPU_VA BYTES
#      (numbers in hex or decimal; PC_PHYS from the firmware trace's
#      "BIF Setup register set=…, PC address=0x…")

import sys

PAGE_SHIFTS = {0: 12, 1: 14, 2: 16, 3: 18, 4: 20, 5: 21}
ADDRESS_BITS = (1 << 40) - 1


def read_phys(address, size, path="/dev/mem"):
    with open(path, "rb", buffering=0) as mem:
        mem.seek(address)
        data = mem.read(size)
    if len(data) != size:
        raise OSError("short read at 0x%x" % address)
    return data


def u32(data, offset=0):
    return int.from_bytes(data[offset:offset + 4], "little")


def u64(data, offset=0):
    return int.from_bytes(data[offset:offset + 8], "little")


def translate(pc_phys, va, log=print):
    """Returns (physical address, page size) of a GPU virtual address."""
    pc_index = (va >> 30) & 0x3ff
    pc_entry = u32(read_phys(pc_phys + pc_index * 4, 4))
    log("PC[%d] @0x%x = 0x%08x" % (pc_index, pc_phys + pc_index * 4, pc_entry))
    if not pc_entry & 1:
        raise LookupError("PC entry not valid")
    pd_phys = (pc_entry >> 4) << 12

    pd_index = (va >> 21) & 0x1ff
    pd_entry = u64(read_phys(pd_phys + pd_index * 8, 8))
    page_size = (pd_entry >> 1) & 0x7
    log("PD[%d] @0x%x = 0x%016x (page size code %d)" % (pd_index, pd_phys + pd_index * 8, pd_entry, page_size))
    if not pd_entry & 1:
        raise LookupError("PD entry not valid")
    if page_size not in PAGE_SHIFTS:
        raise LookupError("page size code %d unknown" % page_size)
    shift = PAGE_SHIFTS[page_size]
    pt_phys = pd_entry & ADDRESS_BITS & ~0x1f

    pt_index = (va & ((1 << 21) - 1)) >> shift
    pt_entry = u64(read_phys(pt_phys + pt_index * 8, 8))
    flags = []
    for bit, name in ((1, "ro"), (2, "cc"), (3, "slc-bypass"), (4, "pm-src"), (62, "pm-protect")):
        if pt_entry >> bit & 1:
            flags.append(name)
    log("PT[%d] @0x%x = 0x%016x (%s)" % (pt_index, pt_phys + pt_index * 8, pt_entry, " ".join(flags) or "-"))
    if not pt_entry & 1:
        raise LookupError("PT entry not valid")
    page = pt_entry & ADDRESS_BITS & ~((1 << shift) - 1)
    return page | (va & ((1 << shift) - 1)), 1 << shift


def read_va(pc_phys, va, size, log=print):
    """Reads size bytes at a GPU virtual address, page by page."""
    out = b""
    while size > 0:
        phys, page = translate(pc_phys, va, log)
        chunk = min(size, page - (va & (page - 1)))
        log("VA 0x%x -> PA 0x%x (%d bytes)" % (va, phys, chunk))
        out += read_phys(phys, chunk)
        va += chunk
        size -= chunk
    return out


def dump(data, base, log=print):
    for offset in range(0, len(data), 32):
        words = [u32(data, o) for o in range(offset, min(offset + 32, len(data)) - 3, 4)]
        log("  0x%010x: %s" % (base + offset, " ".join("%08x" % w for w in words)))


def main():
    if len(sys.argv) != 4:
        print(__doc__ if __doc__ else "walk.py PC_PHYS GPU_VA BYTES")
        sys.exit(2)
    pc_phys, va, size = (int(arg, 0) for arg in sys.argv[1:])
    dump(read_va(pc_phys, va, size), va)


if __name__ == "__main__":
    main()
