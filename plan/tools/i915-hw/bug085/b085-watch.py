#!/usr/bin/env python3
"""ws075-p015 (BUG-085): watches one run of b085-qemu.sh on the 5330.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Every second it reads the kernel's log (run.log, the debugcon) and the serial port (serial.log):

- init's power-off ("init: executing system action") ends the run: QEMU is told to stop eight seconds later.
- the captured frames ("i915: capture: frame=N", one line per presentation) not advancing for STALL_SECONDS while
  the run has not ended writes the file "stall" (the last frame, the seconds since it, the kernel log's size) and
  leaves QEMU running, for a debugger on the gdbstub (127.0.0.1:1235) or the monitor.
- a kernel fault or fatal report writes the file "fault" the same way.
- no captured frame at all FIRST_FRAME_SECONDS after the start (the compositor never presented: the start-up
  stall of BUG-094) writes the file "stall" the same way.

A stall or a fault is written once; the watcher then only waits for the power-off (which may never come).

- the compositor giving the capture lease back ("i915: capture: lease N released") is the end of the session: the
  frames stop by design.  No stall is marked after it; SESSION_END_SECONDS later the watcher asks gdb where the
  CPUs are (halted by init's power-off, or still scheduling: the power-off did not complete, ws075-p015) and
  writes the answer to the file "poweroff", then ends QEMU.  init's own "executing system action" line goes to
  the console, which is neither the debugcon nor the serial port in these images.
"""
import os
import re
import subprocess
import time

DIR = '/home/awe/bigbang/b085'
STALL_SECONDS = 25
FIRST_FRAME_SECONDS = 180
SESSION_END_SECONDS = 60
# The local APIC timer's calibrated period (HAL: "A64 TIMER CAL READY ticks=N"): about 62700 at 1 kHz on the 5330's
# KVM.  A period far above it is the miscalibration of BUG-094 (the tick runs that many times slow: `sleep 45` of the
# service chain takes minutes); the run is marked and ended at once, its cause being known.
CALIBRATION = re.compile(rb'A64 TIMER CAL READY ticks=(\d+)')
CALIBRATION_LIMIT = 100000
RELEASED = re.compile(rb'i915: capture: lease \d+ released')
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


def cpus():
    """Where each CPU is, from gdb on the gdbstub: one line per CPU."""
    result = subprocess.run(['timeout', '60', 'gdb', '-q', '-batch', os.path.join(DIR, 'vmunix'),
                             '-ex', 'target remote 127.0.0.1:1235', '-ex', 'info threads', '-ex', 'detach'],
                            capture_output=True, text=True)
    return [line.strip() for line in result.stdout.splitlines() if 'Thread 1.' in line]


def main():
    start = time.time()
    last_frame, last_change = -1, time.time()
    stalled = faulted = False
    released_at = None
    while True:
        time.sleep(1.0)
        kernel = read('run.log')
        serial = read('serial.log')
        if END.search(serial) or END.search(kernel):
            print(f'{time.strftime("%H:%M:%S")} power-off seen; ending QEMU in 8 s', flush=True)
            time.sleep(8.0)
            subprocess.run(['sudo', '-n', 'pkill', '-TERM', '-f', 'qemu-system-x86_6[4].*b085/guest'])
            return
        calibration = CALIBRATION.search(kernel)
        if calibration and int(calibration.group(1)) > CALIBRATION_LIMIT and not stalled:
            stalled = True
            mark('stall', f'at {int(time.time() - start)} s: timer miscalibrated (BUG-094): '
                          f'A64 TIMER CAL READY ticks={int(calibration.group(1))}, the tick runs '
                          f'{int(calibration.group(1)) / 62700:.1f} times slow')
            time.sleep(20.0)
            subprocess.run(['sudo', '-n', 'pkill', '-TERM', '-f', 'qemu-system-x86_6[4].*b085/guest'])
            return
        if released_at is None and RELEASED.search(kernel) and not faulted:
            released_at = time.time()
            print(f'{time.strftime("%H:%M:%S")} capture lease released at {int(released_at - start)} s '
                  f'(frame {last_frame}); the session is over', flush=True)
        if released_at is not None and time.time() - released_at >= SESSION_END_SECONDS:
            where = cpus()
            halted = bool(where) and all('panic_all' in line or 'cpu_park' in line for line in where)
            mark('poweroff', ('halted' if halted else 'NOT halted') + f' {SESSION_END_SECONDS} s after the release: '
                 + ' | '.join(where))
            subprocess.run(['sudo', '-n', 'pkill', '-TERM', '-f', 'qemu-system-x86_6[4].*b085/guest'])
            return
        if released_at is not None:
            continue
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
        if not stalled and last_frame < 0 and time.time() - start >= FIRST_FRAME_SECONDS:
            stalled = True
            mark('stall', f'at {int(time.time() - start)} s: no captured frame yet (start-up stall), '
                          f'kernel log {len(kernel)} bytes')


if __name__ == '__main__':
    main()
