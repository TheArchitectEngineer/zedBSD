#!/usr/bin/env python3
# ws075-p012: screenshots of the resident display on the 5330's i915 passthrough, taken from the scanout buffers
# themselves.  Runs on the 5330 (as root: the QMP socket is QEMU's).  It waits in the guest's serial log for the
# resident run's "buffers WxH pitch P: A surf .. cpu 0x.., B surf .. cpu 0x.." line, then at each of the given
# seconds after that line saves both buffers with the monitor's memsave (the kernel's contiguous virtual view,
# mapped in every address space) to OUT/shot-<t>-{A,B}.raw with a small JSON of the geometry.  What the pipe
# scans out is one of the two buffers; both are kept.
#
#   shot.py SERIAL_LOG QMP_SOCKET OUT T1 [T2 ...]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import json
import os
import re
import socket
import sys
import time

LINE = re.compile(rb'resident display: buffers (\d+)x(\d+) pitch (\d+): A surf 0x([0-9a-f]+) cpu (0x[0-9a-f]+), B surf 0x([0-9a-f]+) cpu (0x[0-9a-f]+)')


def qmp_open(path):
    """Connects to QMP and leaves the capabilities negotiation done."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    f = s.makefile('rwb', buffering=0)
    f.readline()
    qmp(f, {'execute': 'qmp_capabilities'})
    return f


def qmp(f, command):
    """Sends one command and returns its reply, skipping events."""
    f.write(json.dumps(command).encode() + b'\n')
    while True:
        reply = json.loads(f.readline())
        if 'event' not in reply:
            return reply


def main():
    serial, sock, out = sys.argv[1], sys.argv[2], sys.argv[3]
    times = [int(t) for t in sys.argv[4:]]
    os.makedirs(out, exist_ok=True)

    # The buffers' line, within six minutes.
    found = None
    deadline = time.time() + 360
    while time.time() < deadline and found is None:
        try:
            with open(serial, 'rb') as log:
                found = LINE.search(log.read())
        except OSError:
            pass
        if found is None:
            time.sleep(1)
    if found is None:
        print('shot: no resident buffers line in the serial log')
        return 1
    width, height, pitch = int(found.group(1)), int(found.group(2)), int(found.group(3))
    buffers = {'A': int(found.group(5), 16), 'B': int(found.group(7), 16)}
    start = time.time()
    info = {'width': width, 'height': height, 'pitch': pitch, 'format': 'XRGB8888 (B, G, R, X bytes)',
            'buffers': {k: hex(v) for k, v in buffers.items()}, 'shots': []}
    print(f'shot: buffers {width}x{height} pitch {pitch} A {hex(buffers["A"])} B {hex(buffers["B"])}')

    f = qmp_open(sock)
    for t in times:
        time.sleep(max(0.0, start + t - time.time()))
        for name, address in buffers.items():
            path = os.path.join(out, f'shot-{t:03d}-{name}.raw')
            reply = qmp(f, {'execute': 'human-monitor-command',
                            'arguments': {'command-line': f'memsave 0x{address:x} {pitch * height} "{path}"'}})
            print(f'shot: t={t}s {name} -> {path} {reply.get("return", reply)!r}')
            info['shots'].append({'t': t, 'buffer': name, 'file': os.path.basename(path)})
    with open(os.path.join(out, 'shots.json'), 'w') as j:
        json.dump(info, j, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
