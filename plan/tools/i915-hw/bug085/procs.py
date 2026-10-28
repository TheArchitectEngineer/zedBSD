# ws075-p015 (BUG-085, BUG-094): lists the guest's processes and threads from gdb, without DWARF.
#
#   gdb -q -batch vmunix -ex 'target remote 127.0.0.1:1235' -x procs.py
#
# Walks all_processes (src/kern/process.c) with the offsets of struct process and struct thread of the
# 2026-09-28 tree (taken with `ptype /o` from src/kern/process.c compiled with -g): for each process its
# pid, state, command and, for each thread, its state, pending signals and the wait queue it sleeps on.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import gdb

PROCESS_PID = 136
PROCESS_STATE = 7488
PROCESS_ALL_NEXT = 7712
PROCESS_THREADS = 7720
PROCESS_COMMAND = 7784
THREAD_TID = 4
THREAD_STATE = 24
THREAD_PROC_NEXT = 192
THREAD_WAIT_QUEUE = 232
THREAD_SIGNAL_MASK = 256
THREAD_SIGNAL_PENDING = 264
WAIT_QUEUE_NAME = 24

inferior = gdb.selected_inferior()


def u64(address):
    """Reads one 64-bit word of guest memory."""
    return int.from_bytes(bytes(inferior.read_memory(address, 8)), 'little')


def u32(address):
    """Reads one 32-bit word of guest memory."""
    return int.from_bytes(bytes(inferior.read_memory(address, 4)), 'little')


def text(address, limit=64):
    """Reads a NUL-terminated string, or '-' for a null pointer."""
    if address == 0:
        return '-'
    raw = bytes(inferior.read_memory(address, limit))
    return raw.split(b'\0', 1)[0].decode(errors='replace')


process = u64(int(gdb.parse_and_eval('(unsigned long)&all_processes')))
count = 0
while process != 0 and count < 256:
    count += 1
    print(f'pid {u32(process + PROCESS_PID):4d} state {u32(process + PROCESS_STATE)} '
          f'{text(process + PROCESS_COMMAND)} @0x{process:x}')
    thread = u64(process + PROCESS_THREADS)
    threads = 0
    while thread != 0 and threads < 64:
        threads += 1
        queue = u64(thread + THREAD_WAIT_QUEUE)
        name = text(u64(queue + WAIT_QUEUE_NAME)) if queue else '-'
        print(f'    tid {u32(thread + THREAD_TID):4d} state {u32(thread + THREAD_STATE)} '
              f'pending 0x{u64(thread + THREAD_SIGNAL_PENDING):x} mask 0x{u64(thread + THREAD_SIGNAL_MASK):x} '
              f'wait {name} @0x{thread:x}')
        thread = u64(thread + THREAD_PROC_NEXT)
    process = u64(process + PROCESS_ALL_NEXT)
