#!/usr/bin/env python3
"""ws075-p015 (BUG-085): watches one run of b085-qemu.sh on the 5330.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Every second it reads the kernel's log (run.log, the debugcon) and the serial port (serial.log):

- init's power-off ("init: executing system action") ends the run: QEMU is told to stop eight seconds later.
- the captured frames ("i915: capture: frame=N", one line per presentation) not advancing for STALL_SECONDS while
  the run has not ended writes the file "stall" (the last frame, the seconds since it, the kernel log's size) and
  leaves QEMU running, for a debugger on the gdbstub (127.0.0.1:1235) or the monitor.
- a kernel fault or fatal report writes the file "fault" the same way.

A stall or a fault is written once; the watcher then only waits for the power-off (which may never come).
"""
import os
import re
import subprocess
import time

DIR = '/home/awe/bigbang/b085'
STALL_SECONDS = 25
FRAME = re.compile(rb'i915: capture: frame=(\d+)')
FAULT = re.compile(rb'amd64 fault v=|fatal: |kernel panic')
END = re.compile(rb'init: executing system action')


def read(name):
    """The whole file, or nothing before it exists."""
    try:
        with open(os.path.join(DIR, name), 'rb') as f:
            return f.read()
    except OSError:
        return b''


def mark(name, text):
    """Writes one of the marker files."""
    with open(os.path.join(DIR, name), 'w') as f:
        f.write(text + '\n')
    print(f'{time.strftime("%H:%M:%S")} {name}: {text}', flush=True)


def main():
    start = time.time()
    last_frame, last_change = -1, time.time()
    stalled = faulted = False
    while True:
        time.sleep(1.0)
        kernel = read('run.log')
        serial = read('serial.log')
        if END.search(serial) or END.search(kernel):
            print(f'{time.strftime("%H:%M:%S")} power-off seen; ending QEMU in 8 s', flush=True)
            time.sleep(8.0)
            subprocess.run(['sudo', '-n', 'pkill', '-TERM', '-f', 'qemu-system-x86_6[4].*b085/guest'])
            return
        frames = FRAME.findall(kernel)
        frame = int(frames[-1]) if frames else -1
        if frame != last_frame:
            last_frame, last_change = frame, time.time()
        if not faulted and FAULT.search(kernel):
            faulted = True
            mark('fault', f'at {int(time.time() - start)} s, last frame {last_frame}')
        idle = time.time() - last_change
        if not stalled and last_frame >= 0 and idle >= STALL_SECONDS:
            stalled = True
            mark('stall', f'at {int(time.time() - start)} s: frame {last_frame} for {int(idle)} s, '
                          f'kernel log {len(kernel)} bytes')


if __name__ == '__main__':
    main()
