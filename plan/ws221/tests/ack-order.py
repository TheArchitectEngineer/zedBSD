#!/usr/bin/env python3
# Sums up the acknowledgements a zedBSD machine sent in a capture of one TCP transfer (plan/ws221/tests/rdna-tcp.sh
# CAPTURE=...), as the Linux sender on this host takes them: each pure acknowledgement is an advance (a new cumulative
# acknowledgement), an old one (behind one already seen: the sender ignores it), a window update (the same
# acknowledgement with a larger window), or a duplicate (the same acknowledgement and no larger window while data is
# outstanding, which the sender counts towards its fast retransmit).  Without SACK the sender's count is Reno's
# (sacked_out: +1 for each duplicate, less acked - 1 on an advance of acked segments, fast retransmit at 3): the count
# is simulated and its recoveries set against the retransmissions seen in the data the other way.  For the first
# duplicates, the old ones and each retransmission, the acknowledgements before it are printed.
#   python3 -I plan/ws221/tests/ack-order.py CAPTURE.pcap ZEDBSD_ADDRESS PORT
# Reads a classic pcap (Ethernet, IPv4) as tcpdump -w writes it.  Prints a summary, then the samples.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import collections
import socket
import struct
import sys

TCP_FIN = 0x01
TCP_SYN = 0x02
TCP_RST = 0x04
TCP_OPTION_WSCALE = 3


def sequence_after(a, b):
    """Whether sequence number a is after b (modulo 2**32)."""
    return ((a - b) & 0xffffffff) != 0 and ((a - b) & 0xffffffff) < 0x80000000


def frames(path):
    """Yields (time, IPv4 source, destination, the TCP fields) for each TCP frame of the capture."""
    with open(path, 'rb') as capture:
        header = capture.read(24)
        magic = struct.unpack('<I', header[:4])[0]
        if magic in (0xa1b2c3d4, 0xa1b23c4d):
            order = '<'
        else:
            order = '>'
        nano = magic in (0xa1b23c4d, 0x4d3cb2a1)
        linktype = struct.unpack(order + 'I', header[20:24])[0]
        if linktype != 1:
            raise SystemExit('ack-order: not an Ethernet capture (link type %d)' % linktype)
        while True:
            record = capture.read(16)
            if len(record) < 16:
                return
            seconds, fraction, length, _ = struct.unpack(order + 'IIII', record)
            data = capture.read(length)
            when = seconds + fraction / (1e9 if nano else 1e6)
            offset = 14
            ethertype = struct.unpack('>H', data[12:14])[0]
            if ethertype == 0x8100:
                ethertype = struct.unpack('>H', data[16:18])[0]
                offset = 18
            if ethertype != 0x0800 or len(data) < offset + 20:
                continue
            ihl = (data[offset] & 0x0f) * 4
            total = struct.unpack('>H', data[offset + 2:offset + 4])[0]
            ident = struct.unpack('>H', data[offset + 4:offset + 6])[0]
            if data[offset + 9] != 6:
                continue
            source = socket.inet_ntoa(data[offset + 12:offset + 16])
            destination = socket.inet_ntoa(data[offset + 16:offset + 20])
            tcp = offset + ihl
            if len(data) < tcp + 20:
                continue
            sport, dport, seq, ack, doff, flags, window = struct.unpack('>HHIIBBH', data[tcp:tcp + 16])
            doff = (doff >> 4) * 4
            payload = total - ihl - doff
            wscale = None
            if flags & TCP_SYN:
                i = tcp + 20
                end = min(tcp + doff, len(data))
                while i < end:
                    kind = data[i]
                    if kind == 0:
                        break
                    if kind == 1:
                        i += 1
                        continue
                    if i + 1 >= end:
                        break
                    size = data[i + 1]
                    if kind == TCP_OPTION_WSCALE and size == 3 and i + 2 < end:
                        wscale = data[i + 2]
                    if size < 2:
                        break
                    i += size
            yield when, source, destination, sport, dport, seq, ack, flags, window, payload, wscale, ident


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: ack-order.py CAPTURE.pcap ZEDBSD_ADDRESS PORT')
    path, zedbsd, port = sys.argv[1], sys.argv[2], int(sys.argv[3])

    # The connection that carried the most data from this host.
    carried = collections.Counter()
    for f in frames(path):
        if f[2] == zedbsd and f[3] == port:
            carried[(f[3], f[4])] += max(f[9], 0)
    if not carried:
        raise SystemExit('ack-order: no data from port %d to %s' % (port, zedbsd))
    (hport, zport), _ = carried.most_common(1)[0]

    shift = 0
    una = None
    nxt = None
    wnd = None
    mss = 1448
    sacked = 0
    counts = collections.Counter()
    dup_runs = collections.Counter()
    run = 0
    recent = collections.deque(maxlen=10)
    samples = {'dup': [], 'old': [], 'retransmit': [], 'predicted': []}
    old_distance = []
    gaps = []
    windows = []
    flight = []
    limited = 0
    advances = 0
    acked_per_advance = collections.Counter()
    first = None
    last_time = None
    data_frames = 0
    retransmits = 0
    # zedBSD numbers its IPv4 datagrams in the order its IP output takes them: an old acknowledgement whose number is
    # behind that of the acknowledgement that last advanced was overtaken after that output (the device, the wire or
    # this host's receive queues); one whose number is after it was built behind (the stack read an old point).
    advance_ident = None
    old_overtaken = 0
    old_built = 0
    for when, source, destination, sport, dport, seq, ack, flags, window, payload, wscale, ident in frames(path):
        if first is None:
            first = when
        t = when - first
        if source == zedbsd and sport == zport and dport == hport:
            if flags & TCP_SYN:
                if wscale is not None:
                    shift = wscale
                continue
            if flags & (TCP_FIN | TCP_RST) or payload > 0:
                continue
            win = window << shift
            outstanding = nxt is not None and una is not None and sequence_after(nxt, ack)
            if una is None:
                kind = 'advance'
            elif sequence_after(ack, una):
                kind = 'advance'
            elif ack != una:
                kind = 'old'
                old_distance.append((una - ack) & 0xffffffff)
                if advance_ident is not None and ((ident - advance_ident) & 0xffff) >= 0x8000:
                    old_overtaken += 1
                else:
                    old_built += 1
            elif wnd is not None and win > wnd:
                kind = 'window-update'
            elif outstanding:
                kind = 'duplicate'
            else:
                kind = 'idle-duplicate'
            counts[kind] += 1
            line = '%.6f %-14s ack=%u win=%u in-flight=%s' % (
                t, kind, ack, win, 'none' if nxt is None else str((nxt - ack) & 0xffffffff))
            if last_time is not None:
                gaps.append(when - last_time)
            last_time = when
            if kind == 'advance':
                if una is not None:
                    acked = (((ack - una) & 0xffffffff) + mss - 1) // mss
                    acked_per_advance[min(acked, 64)] += 1
                    advances += 1
                    if acked > 0:
                        sacked = 0 if acked - 1 >= sacked else sacked - (acked - 1)
                if run:
                    dup_runs[min(run, 16)] += 1
                run = 0
                una = ack
                advance_ident = ident
                wnd = win
                windows.append(win)
                if nxt is not None:
                    flight.append((nxt - ack) & 0xffffffff)
                if nxt is not None and ((nxt - ack) & 0xffffffff) + mss > win:
                    limited += 1
            elif kind == 'window-update':
                wnd = win
            elif kind == 'duplicate':
                run += 1
                sacked += 1
                if len(samples['dup']) < 12:
                    samples['dup'].append(list(recent) + [line])
                if sacked == 3:
                    counts['predicted-recovery'] += 1
                    if len(samples['predicted']) < 8:
                        samples['predicted'].append(list(recent) + [line])
            elif kind == 'old' and len(samples['old']) < 8:
                samples['old'].append(list(recent) + [line])
            recent.append(line)
        elif source != zedbsd and sport == hport and dport == zport and payload > 0:
            data_frames += 1
            end = (seq + payload) & 0xffffffff
            if nxt is not None and not sequence_after(end, nxt):
                retransmits += 1
                if len(samples['retransmit']) < 12:
                    samples['retransmit'].append(list(recent) + ['%.6f RETRANSMIT seq=%u len=%u' % (t, seq, payload)])
            if nxt is None or sequence_after(end, nxt):
                nxt = end
            if payload <= 9000:
                mss = min(mss, payload) if payload >= 500 else mss

    print('connection: %s:%d <- data from port %d, window shift %d' % (zedbsd, zport, hport, shift))
    print('data frames from this host: %d (retransmissions seen: %d)' % (data_frames, retransmits))
    print('pure acknowledgements: ' + ' '.join('%s=%d' % kv for kv in sorted(counts.items())))
    print('duplicate runs (length: count): ' + ' '.join('%d:%d' % kv for kv in sorted(dup_runs.items())))
    if old_distance:
        old_distance.sort()
        print('old acknowledgements behind by bytes: median %d most %d' % (
            old_distance[len(old_distance) // 2], old_distance[-1]))
        print('old acknowledgements overtaken after the machine\'s IP output (IP ID behind the last advance\'s): %d; '
              'built behind (IP ID ahead): %d' % (old_overtaken, old_built))
    if windows:
        w = sorted(windows)
        print('window on advances: least %d median %d most %d; in flight within a segment of it: %d of %d' % (
            w[0], w[len(w) // 2], w[-1], limited, len(w)))
    if flight:
        f = sorted(flight)
        print('in flight on advances (bytes): median %d p10 %d p90 %d most %d' % (
            f[len(f) // 2], f[len(f) // 10], f[len(f) * 9 // 10], f[-1]))
    if acked_per_advance:
        print('segments acknowledged per advance: ' + ' '.join(
            '%d:%d' % kv for kv in sorted(acked_per_advance.items())[:12]))
    if gaps:
        g = sorted(gaps)
        print('time between acknowledgements (us): median %.1f p90 %.1f p99 %.1f most %.1f' % (
            g[len(g) // 2] * 1e6, g[len(g) * 9 // 10] * 1e6, g[len(g) * 99 // 100] * 1e6, g[-1] * 1e6))
    for name in ('predicted', 'retransmit', 'dup', 'old'):
        for i, sample in enumerate(samples[name]):
            print('--- %s %d' % (name, i + 1))
            for line in sample:
                print('  ' + line)


main()
