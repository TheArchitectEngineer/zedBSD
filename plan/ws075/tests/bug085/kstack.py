# ws075-p015: the kernel functions on a sleeping thread's stack, from gdb, without DWARF.
#
#   gdb -q -batch vmunix -ex 'target remote 127.0.0.1:1235' -ex 'set $thread = 0x...' -x kstack.py
#
# Reads the thread's hal task (struct thread + 16), its resume_rsp (struct amd64_task + 40), and prints every word
# of the 2 KiB above it that points into the kernel's text as its symbol: the saved return addresses, most recent
# first (a scan, so stale words can appear between them).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import gdb

THREAD_TASK = 16
TASK_RESUME_RSP = 40
TEXT_LOW = 0xffffffff80200000
TEXT_HIGH = 0xffffffff80400000

inferior = gdb.selected_inferior()


def u64(address):
    """Reads one 64-bit word of guest memory."""
    return int.from_bytes(bytes(inferior.read_memory(address, 8)), 'little')


thread = int(gdb.parse_and_eval('$thread'))
task = u64(thread + THREAD_TASK)
rsp = u64(task + TASK_RESUME_RSP)
print(f'thread 0x{thread:x} task 0x{task:x} resume_rsp 0x{rsp:x}')
for offset in range(0, 2048, 8):
    value = u64(rsp + offset)
    if TEXT_LOW <= value < TEXT_HIGH:
        where = gdb.execute(f'info symbol 0x{value:x}', to_string=True).strip()
        print(f'  +{offset:4d} 0x{value:x} {where}')
