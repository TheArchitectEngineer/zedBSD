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
    h4-ctl.py watch SECONDS MS    the output's state every MS milliseconds for SECONDS through qmp-load.sock (so the
                                  other commands can run meanwhile): pipe B's TRANSCONF, PLANE_CTL, PLANE_SURFLIVE and
                                  frame counter, read with xp through the passthrough BAR; one line per change in
                                  watch.log (ms since the start, the four values); h4-blank.py reads it
    h4-ctl.py latency PIPE COUNT  (ws084) the time from a pointer move to the next flip on PIPE (A or B): COUNT times,
                                  the tablet moves 40 pixels and PLANE_SURFLIVE is read with xp until it changes (or
                                  3 s pass); then 3 s without input count the flips the idle desktop makes.  One line
                                  per trial in latency.log and on the output
    h4-ctl.py rate PIPE SECONDS   (ws075-p008) the flips per second of PIPE while the pointer keeps moving: the tablet
                                  moves back and forth by 40 pixels every 8 ms for SECONDS, and PLANE_SURFLIVE is read
                                  between the moves; one line in latency.log and on the output
    h4-ctl.py freq SECONDS MS [move]
                                  (ws075-p020) the GT frequency every MS milliseconds for SECONDS: the request (RPNSWREQ
                                  [31:23]) and the actual frequency (CAGF, RPSTAT1 [19:11]), in MHz (16.67 MHz units),
                                  read with xp through the passthrough BAR; with "move" the tablet moves back and forth
                                  by 40 pixels between the reads, as rate does.  One line per sample in freq.log, and the
                                  mean, least and most of both on the output
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
# Pipe B's transcoder configuration, primary plane control, live surface and frame counter (the HDMI output).
WATCH = (('transconf', 0x71008), ('plane_ctl', 0x71180), ('surflive', 0x711ac), ('frame', 0x71040))
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


def watch(seconds, interval_ms):
    """Samples pipe B's output registers and logs every change of their enable bits and live surface."""
    f = qmp_open('qmp-load.sock')
    bar = graphics_bar(f)
    if bar is None:
        raise SystemExit('h4-ctl: watch: no passthrough graphics BAR')
    start = time.time()
    last = None
    samples = 0
    with open(os.path.join(DIR, 'watch.log'), 'a') as log:
        log.write(f'# watch {time.strftime("%H:%M:%S")} epoch {start:.3f} bar 0x{bar:x} every {interval_ms} ms\n')
        while time.time() - start < seconds:
            values = []
            for name, offset in WATCH:
                found = re.search(r':\s*(0x[0-9a-f]+)', hmp(f, f'xp /1wx 0x{bar + offset:x}'))
                values.append(int(found.group(1), 16) if found else -1)
            samples += 1
            # The frame counter always moves: a change of the other three is what is logged, with the counter.
            key = (values[0] >> 31, values[1] >> 31, values[2] & 0xfffff000)
            if key != last:
                log.write(f'{int((time.time() - start) * 1000)} ' + ' '.join(f'{v:08x}' for v in values) + '\n')
                log.flush()
                last = key
            time.sleep(interval_ms / 1000.0)
        log.write(f'# end {samples} samples\n')
    print(f'watch: {samples} samples in {int(time.time() - start)} s')


def surflive(f, bar, pipe):
    """The live surface address of a pipe's primary plane, read through the passthrough BAR."""
    found = re.search(r':\s*(0x[0-9a-f]+)', hmp(f, f'xp /1wx 0x{bar + SURFLIVE[pipe]:x}'))
    return int(found.group(1), 16) & 0xfffff000 if found else -1


def latency(f, pipe, count):
    """Times each pointer move to the next flip on the pipe, then counts the flips of an idle desktop."""
    bar = graphics_bar(f)
    if bar is None:
        raise SystemExit('h4-ctl: latency: no passthrough graphics BAR')
    width, height = size()
    results = []
    with open(os.path.join(DIR, 'latency.log'), 'a') as log:
        log.write(f'# latency {time.strftime("%H:%M:%S")} pipe {pipe} bar 0x{bar:x} {width}x{height}\n')
        for trial in range(count):
            x = width // 2 + (40 if trial % 2 == 0 else -40)
            before = surflive(f, bar, pipe)
            start = time.monotonic()
            tablet(f, x, height // 2, width, height)
            reads = 0
            now = before
            while now == before and time.monotonic() - start < 3.0:
                now = surflive(f, bar, pipe)
                reads += 1
            ms = (time.monotonic() - start) * 1000.0
            flipped = now != before
            results.append(ms if flipped else None)
            line = f'trial {trial}: {"flip" if flipped else "NO flip"} after {ms:.1f} ms ({reads} reads, 0x{before:08x} -> 0x{now:08x})'
            log.write(line + '\n')
            print(line)
            time.sleep(0.5)
        # The idle desktop: the flips in 3 s without input.
        flips = 0
        last = surflive(f, bar, pipe)
        start = time.monotonic()
        while time.monotonic() - start < 3.0:
            now = surflive(f, bar, pipe)
            if now != last:
                flips += 1
                last = now
        line = f'idle: {flips} flips in 3 s'
        log.write(line + '\n')
        print(line)
    done = [r for r in results if r is not None]
    if done:
        done.sort()
        print(f'latency: {len(done)}/{count} flipped, median {done[len(done) // 2]:.1f} ms, min {done[0]:.1f}, max {done[-1]:.1f}')


def rate(f, pipe, seconds):
    """Counts the flips of the pipe while the pointer keeps moving (the presentation rate under input)."""
    bar = graphics_bar(f)
    if bar is None:
        raise SystemExit('h4-ctl: rate: no passthrough graphics BAR')
    width, height = size()
    flips = 0
    moves = 0
    last = surflive(f, bar, pipe)
    start = time.monotonic()
    next_move = start
    while time.monotonic() - start < seconds:
        now = time.monotonic()
        if now >= next_move:
            x = width // 2 + (40 if moves % 2 == 0 else -40)
            tablet(f, x, height // 2, width, height)
            moves += 1
            next_move = now + 0.008
        live = surflive(f, bar, pipe)
        if live != last:
            flips += 1
            last = live
    elapsed = time.monotonic() - start
    line = f'rate: {flips} flips in {elapsed:.1f} s ({flips / elapsed:.1f}/s) with {moves} pointer moves'
    with open(os.path.join(DIR, 'latency.log'), 'a') as log:
        log.write(f'# rate {time.strftime("%H:%M:%S")} pipe {pipe}\n' + line + '\n')
    print(line)


def read_register(f, bar, offset):
    """One 32-bit register of the passthrough iGPU, read with xp through its BAR (-1 when unreadable)."""
    found = re.search(r':\s*(0x[0-9a-f]+)', hmp(f, f'xp /1wx 0x{bar + offset:x}'))
    return int(found.group(1), 16) if found else -1


def freq(f, seconds, interval_ms, move):
    """Samples the GT's requested and actual frequency (ws075-p020), with the pointer moving or not."""
    bar = graphics_bar(f)
    if bar is None:
        raise SystemExit('h4-ctl: freq: no passthrough graphics BAR')
    width, height = size()
    requested = []
    actual = []
    moves = 0
    start = time.monotonic()
    with open(os.path.join(DIR, 'freq.log'), 'a') as log:
        log.write(f'# freq {time.strftime("%H:%M:%S")} {seconds} s every {interval_ms} ms move={move}\n')
        while time.monotonic() - start < seconds:
            if move:
                x = width // 2 + (40 if moves % 2 == 0 else -40)
                tablet(f, x, height // 2, width, height)
                moves += 1
            swreq = read_register(f, bar, 0xa008)
            rpstat = read_register(f, bar, 0x1381b4)
            req_mhz = ((swreq >> 23) & 0x1ff) * 50.0 / 3.0 if swreq >= 0 else -1.0
            cagf_mhz = ((rpstat >> 11) & 0x1ff) * 50.0 / 3.0 if rpstat >= 0 else -1.0
            requested.append(req_mhz)
            actual.append(cagf_mhz)
            log.write(f'{(time.monotonic() - start) * 1000.0:.0f} ms: RPNSWREQ 0x{swreq & 0xffffffff:08x} ({req_mhz:.0f} MHz) '
                      f'RPSTAT1 0x{rpstat & 0xffffffff:08x} (CAGF {cagf_mhz:.0f} MHz)\n')
            time.sleep(interval_ms / 1000.0)
    line = (f'freq: {len(requested)} samples move={move}: request mean {sum(requested) / len(requested):.0f} MHz '
            f'({min(requested):.0f}..{max(requested):.0f}), actual mean {sum(actual) / len(actual):.0f} MHz '
            f'({min(actual):.0f}..{max(actual):.0f})')
    with open(os.path.join(DIR, 'freq.log'), 'a') as log:
        log.write(line + '\n')
    print(line)


def main():
    command = sys.argv[1]
    if command == 'watch':
        watch(int(sys.argv[2]), int(sys.argv[3]))
        return 0
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
    elif command == 'latency':
        latency(f, sys.argv[2], int(sys.argv[3]))
    elif command == 'rate':
        rate(f, sys.argv[2], float(sys.argv[3]))
    elif command == 'freq':
        freq(f, float(sys.argv[2]), int(sys.argv[3]), len(sys.argv) > 4 and sys.argv[4] == 'move')
    elif command == 'quit':
        print(qmp(f, 'quit'))
    else:
        raise SystemExit(f'h4-ctl: unknown command {command}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
