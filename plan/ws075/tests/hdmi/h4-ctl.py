#!/usr/bin/env python3
"""ws075-p013 (H4): drives and observes the demonstration run of h4-qemu.sh, on the 5330, as root.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Every command opens its own QMP connection (qmp.sock; `load` uses qmp-load.sock) and closes it, so the
commands can be run one after another from the host over ssh.  The resident display's buffers are found in the
kernel's log (run.log, the debugcon: every record, also on a quiet boot).

    h4-ctl.py shot NAME           the standard VGA (the firmware's GOP display) as shots/NAME-vga.ppm and, once the
                                  resident display has its buffers, both of them as shots/NAME-{A,B}.raw; NAME.json
                                  names the one on the screen ("live"): the plane's live surface register
                                  (PLANE_SURFLIVE of pipe A or B, read with xp through the passthrough BAR), or else
                                  the kernel log's last flip of the current lease (exact only up to the lease's third
                                  flip, the last one the kernel logs); h4-png.py makes NAME-live.png of it
    h4-ctl.py splash SECONDS MS   the standard VGA every MS milliseconds until the resident buffers exist or
                                  SECONDS pass (shots/splash-<ms since start>.ppm)
    h4-ctl.py pointer STEP...     the tablet in output pixels: move X Y, down, up, sleep MS, drag X0 Y0 X1 Y1
    h4-ctl.py keys TEXT           key presses (US layout, "\\n" is Enter)
    h4-ctl.py hmp COMMAND...      a monitor command; prints its output
    h4-ctl.py load SECONDS PERIOD X0 Y0 X1 Y1
                                  the periodic load: every PERIOD seconds drags from (X0, Y0) to (X1, Y1), the next
                                  time back, for SECONDS; one line per drag in load.log
    h4-ctl.py quit                ends QEMU
"""
import json
import os
import re
import socket
import sys
import time

DIR = '/home/awe/bigbang/h4'
SHOTS = os.path.join(DIR, 'shots')
BUFFERS = re.compile(rb'resident display: buffers (\d+)x(\d+) pitch (\d+): A surf 0x([0-9a-f]+) cpu (0x[0-9a-f]+), '
                     rb'B surf 0x([0-9a-f]+) cpu (0x[0-9a-f]+)')
PICTURE = re.compile(rb'resident display: picture up \(buffer A surf 0x([0-9a-f]+)\)')
FLIP = re.compile(rb'resident display: flip (\d+): surf 0x([0-9a-f]+) -> 0x([0-9a-f]+)')
# The primary plane's live surface address (bits 31:12, the GGTT address being scanned out) of pipes A and B.
SURFLIVE = {'A': 0x701ac, 'B': 0x711ac}
# The kernel logs the first three flips of a lease only.
LOGGED_FLIPS = 3
PLAIN = {' ': 'spc', '-': 'minus', '=': 'equal', '.': 'dot', '/': 'slash', '\n': 'ret', '\t': 'tab'}


def qmp_open(name='qmp.sock'):
    """Connects to one of QEMU's QMP sockets and negotiates the capabilities."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(os.path.join(DIR, name))
    f = s.makefile('rw')
    f.readline()
    qmp(f, 'qmp_capabilities')
    return f


def qmp(f, command, arguments=None):
    """Sends one command and returns its reply, skipping events."""
    message = {'execute': command}
    if arguments is not None:
        message['arguments'] = arguments
    f.write(json.dumps(message) + '\n')
    f.flush()
    while True:
        reply = json.loads(f.readline())
        if 'event' not in reply:
            return reply


def hmp(f, line):
    """Runs one monitor command and returns its text."""
    reply = qmp(f, 'human-monitor-command', {'command-line': line})
    return reply.get('return', json.dumps(reply))


def kernel_log():
    """The kernel's log so far, or nothing before it exists."""
    try:
        with open(os.path.join(DIR, 'run.log'), 'rb') as log:
            return log.read()
    except OSError:
        return b''


def buffers():
    """The resident buffers (width, height, pitch, {A, B: address}, {A, B: surface}) from the kernel's log, or None."""
    found = None
    for found in BUFFERS.finditer(kernel_log()):
        pass
    if found is None:
        return None
    return (int(found.group(1)), int(found.group(2)), int(found.group(3)),
            {'A': int(found.group(5), 16), 'B': int(found.group(7), 16)},
            {'A': int(found.group(4), 16), 'B': int(found.group(6), 16)})


def graphics_bar(f):
    """The guest-physical address of the passthrough iGPU's register BAR (BAR0), from the monitor's PCI list."""
    text = hmp(f, 'info pci')
    for block in text.split('Bus ')[1:]:
        if 'VGA controller: PCI device 8086:' not in block:
            continue
        found = re.search(r'BAR0: 64 bit memory at (0x[0-9a-f]+)', block)
        if found:
            return int(found.group(1), 16)
    return None


def live_from_register(f, surfaces):
    """Which resident buffer a pipe's primary plane scans out now, by its live surface register, or None."""
    bar = graphics_bar(f)
    if bar is None:
        return None
    for pipe, offset in SURFLIVE.items():
        text = hmp(f, f'xp /1wx 0x{bar + offset:x}')
        found = re.search(r':\s*(0x[0-9a-f]+)', text)
        if not found:
            continue
        live = int(found.group(1), 16) & 0xfffff000
        for key, surface in surfaces.items():
            if live == surface & 0xfffff000:
                return key, f'PLANE_SURFLIVE pipe {pipe} = 0x{live:08x}'
    return None


def live_from_log(surfaces):
    """Which resident buffer the current lease shows by the kernel log's last flip, and whether that is exact."""
    text = kernel_log()
    picture = None
    for picture in PICTURE.finditer(text):
        pass
    if picture is None:
        return None
    surface = int(picture.group(1), 16)
    count = 0
    for flip in FLIP.finditer(text, picture.end()):
        count, surface = int(flip.group(1)), int(flip.group(3), 16)
    where = 'picture up' if count == 0 else f'flip {count}'
    caveat = '' if count < LOGGED_FLIPS else ' (the kernel logs no later flip: may be stale)'
    for key, known in surfaces.items():
        if surface == known:
            return key, f'kernel log: {where} of the lease{caveat}'
    return None


def shot(f, name):
    """Saves the standard VGA and, when they exist, both resident buffers."""
    os.makedirs(SHOTS, exist_ok=True)
    vga = os.path.join(SHOTS, name + '-vga.ppm')
    reply = qmp(f, 'screendump', {'filename': vga, 'format': 'ppm'})
    print(f'{name}: vga {"ok" if "return" in reply else reply}')
    geometry = buffers()
    if geometry is None:
        print(f'{name}: no resident buffers yet')
        return
    width, height, pitch, addresses, surfaces = geometry
    for key, address in addresses.items():
        path = os.path.join(SHOTS, f'{name}-{key}.raw')
        text = hmp(f, f'memsave 0x{address:x} {pitch * height} "{path}"')
        print(f'{name}: {key} {hex(address)} {text.strip() or "ok"}')

    # The buffer on the screen: the register when the monitor can read it, the kernel log otherwise.
    live = live_from_register(f, surfaces)
    if live is None:
        live = live_from_log(surfaces)
    if live is None:
        print(f'{name}: the live buffer is unknown')
    else:
        print(f'{name}: live buffer {live[0]} ({live[1]})')
    with open(os.path.join(SHOTS, name + '.json'), 'w') as j:
        json.dump({'width': width, 'height': height, 'pitch': pitch, 'time': time.time(),
                   'buffers': {k: hex(v) for k, v in addresses.items()},
                   'surfaces': {k: hex(v) for k, v in surfaces.items()},
                   'live': live[0] if live else None, 'live_source': live[1] if live else None}, j)


def tablet(f, x, y, width, height):
    """Moves the tablet to output pixel (x, y) (zdesktop takes v to floor(v * (size - 1) / 32767))."""
    vx = (x * 32767 + width - 2) // (width - 1)
    vy = (y * 32767 + height - 2) // (height - 1)
    qmp(f, 'input-send-event', {'events': [{'type': 'abs', 'data': {'axis': 'x', 'value': vx}},
                                           {'type': 'abs', 'data': {'axis': 'y', 'value': vy}}]})


def button(f, down):
    """Presses or releases the left button."""
    qmp(f, 'input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})


def drag(f, x0, y0, x1, y1, width, height):
    """Drags with the left button in twelve steps over about half a second."""
    tablet(f, x0, y0, width, height)
    time.sleep(0.15)
    button(f, True)
    time.sleep(0.1)
    for step in range(1, 13):
        tablet(f, x0 + (x1 - x0) * step // 12, y0 + (y1 - y0) * step // 12, width, height)
        time.sleep(0.04)
    time.sleep(0.1)
    button(f, False)


def size():
    """The output's size: the resident buffers', or 1920x1280 before they exist."""
    geometry = buffers()
    if geometry is None:
        return 1920, 1280
    return geometry[0], geometry[1]


def pointer(f, steps):
    """Runs the pointer steps in order."""
    width, height = size()
    index = 0
    while index < len(steps):
        word = steps[index]
        if word == 'move':
            tablet(f, int(steps[index + 1]), int(steps[index + 2]), width, height)
            index += 3
        elif word in ('down', 'up'):
            button(f, word == 'down')
            index += 1
        elif word == 'sleep':
            time.sleep(int(steps[index + 1]) / 1000.0)
            index += 2
        elif word == 'drag':
            drag(f, *[int(v) for v in steps[index + 1:index + 5]], width, height)
            index += 5
        else:
            raise SystemExit(f'h4-ctl: unknown pointer step {word}')


def keys(f, text):
    """Presses and releases the key of each character."""
    for character in text.replace('\\n', '\n'):
        if character.isalnum():
            code, shifted = character.lower(), character.isupper()
        else:
            code, shifted = PLAIN[character], False
        for down in (True, False):
            events = [{'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': code}}}]
            if shifted:
                events.insert(0 if down else 1,
                              {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': 'shift'}}})
            qmp(f, 'input-send-event', {'events': events})
            time.sleep(0.03)


def splash(f, seconds, interval_ms):
    """Takes the standard VGA until the resident buffers exist."""
    os.makedirs(SHOTS, exist_ok=True)
    start = time.time()
    count = 0
    while time.time() - start < seconds:
        elapsed = int((time.time() - start) * 1000)
        qmp(f, 'screendump', {'filename': os.path.join(SHOTS, f'splash-{elapsed:06d}.ppm'), 'format': 'ppm'})
        count += 1
        if buffers() is not None:
            print(f'splash: {count} screens; the resident buffers exist at {elapsed} ms')
            return
        time.sleep(interval_ms / 1000.0)
    print(f'splash: {count} screens; no resident buffers within {seconds} s')


def load(seconds, period, x0, y0, x1, y1):
    """Drags back and forth every period seconds, logging each drag."""
    f = qmp_open('qmp-load.sock')
    width, height = size()
    start = time.time()
    forward = True
    count = 0
    with open(os.path.join(DIR, 'load.log'), 'a') as log:
        while time.time() - start < seconds:
            if forward:
                drag(f, x0, y0, x1, y1, width, height)
            else:
                drag(f, x1, y1, x0, y0, width, height)
            count += 1
            log.write(f'{time.time():.1f} drag {count} {"forward" if forward else "back"}\n')
            log.flush()
            forward = not forward
            time.sleep(max(0.0, start + count * period - time.time()))
    print(f'load: {count} drags in {int(time.time() - start)} s')


def main():
    command = sys.argv[1]
    if command == 'load':
        load(*[int(v) for v in sys.argv[2:8]])
        return 0
    f = qmp_open()
    if command == 'shot':
        shot(f, sys.argv[2])
    elif command == 'splash':
        splash(f, int(sys.argv[2]), int(sys.argv[3]))
    elif command == 'pointer':
        pointer(f, sys.argv[2:])
    elif command == 'keys':
        keys(f, ' '.join(sys.argv[2:]))
    elif command == 'hmp':
        print(hmp(f, ' '.join(sys.argv[2:])))
    elif command == 'quit':
        print(qmp(f, 'quit'))
    else:
        raise SystemExit(f'h4-ctl: unknown command {command}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
