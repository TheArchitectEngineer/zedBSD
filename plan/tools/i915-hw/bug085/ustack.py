# ws075-p015: a sleeping thread's user stack, read through its own page tables, from gdb, without DWARF.
#
#   gdb -q -batch vmunix -ex 'target remote 127.0.0.1:1235' -ex 'set $thread = 0x...' -ex 'set $usp = 0x...' \
#       -x ustack.py
#
# The thread's hal task (struct thread + 16) names its address space (struct amd64_task + 8), whose PML4's physical
# address (struct amd64_space + 32) is walked through the kernel's direct map (0xffff800000000000) to read 4 KiB
# of the user stack from $usp.  Every word in [0x1000, 0x200000000) is printed: the program's and the libraries'
# return addresses, among other values.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import gdb

DIRECT = 0xffff800000000000
inferior = gdb.selected_inferior()


def u64(address):
    """Reads one 64-bit word of guest memory through the current mapping."""
    return int.from_bytes(bytes(inferior.read_memory(address, 8)), 'little')


def translate(pml4, va):
    """Walks the 4-level page table for a user address; None when it is not mapped."""
    table = pml4
    for shift in (39, 30, 21, 12):
        entry = u64(DIRECT + table + ((va >> shift) & 511) * 8)
        if entry & 1 == 0:
            return None
        if shift in (30, 21) and entry & 0x80:
            size = 1 << shift
            return (entry & 0x000ffffffffff000 & ~(size - 1)) + (va & (size - 1))
        table = entry & 0x000ffffffffff000
    return table + (va & 0xfff)


thread = int(gdb.parse_and_eval('$thread'))
usp = int(gdb.parse_and_eval('$usp'))
task = u64(thread + 16)
space = u64(task + 8)
pml4 = u64(space + 32)
print(f'thread 0x{thread:x} task 0x{task:x} space 0x{space:x} pml4 0x{pml4:x}')
for offset in range(0, 4096, 8):
    physical = translate(pml4, usp + offset)
    if physical is None:
        print(f'  +{offset} not mapped')
        break
    value = u64(DIRECT + physical)
    if 0x1000 <= value < 0x200000000:
        print(f'  +{offset:4d} 0x{value:x}')
