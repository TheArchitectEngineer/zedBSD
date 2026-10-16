#!/usr/bin/env python3
"""A DHCPv4 server on a QEMU dgram network, for the ws254 retry check.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

QEMU's `-netdev dgram` sends each Ethernet frame of the guest's NIC as one UDP
datagram from REMOTE_PORT's peer to this socket, and takes frames back the same
way.  The server notes every DHCP message it sees (the time, the type, the
transaction) on standard output, and answers only once SERVE_AFTER seconds have
passed since it started: before then the guest's DHCP finds no server.

    dhcp-server.py LISTEN_PORT GUEST_PORT SERVE_AFTER

Offers 192.168.77.10/24 from 192.168.77.1, with no router and no DNS server,
so that the lease does not change the guest's default route or resolver.
"""
from __future__ import annotations

import socket
import struct
import sys
import time

SERVER_ADDRESS = bytes([192, 168, 77, 1])
CLIENT_ADDRESS = bytes([192, 168, 77, 10])
NETMASK = bytes([255, 255, 255, 0])
SERVER_MAC = bytes([0x52, 0x54, 0x00, 0x77, 0x00, 0x01])
LEASE_SECONDS = 3600
MAGIC = b"\x63\x82\x53\x63"
NAMES = {1: "DISCOVER", 2: "OFFER", 3: "REQUEST", 4: "DECLINE", 5: "ACK",
	 6: "NAK", 7: "RELEASE", 8: "INFORM"}


def checksum(data: bytes) -> int:
	"""Returns the Internet checksum of an IPv4 header."""
	if len(data) % 2:
		data += b"\x00"
	total = sum(struct.unpack(f"!{len(data) // 2}H", data))
	while total >> 16:
		total = (total & 0xFFFF) + (total >> 16)
	return ~total & 0xFFFF


def parse(frame: bytes):
	"""Returns (type, xid, chaddr, bootp) of a DHCP request frame, or None."""
	if len(frame) < 14 + 20 + 8 + 240:
		return None
	if frame[12:14] != b"\x08\x00":
		return None
	ip = frame[14:]
	header = (ip[0] & 0x0F) * 4
	if ip[9] != 17:
		return None
	udp = ip[header:]
	source_port, destination_port = struct.unpack("!HH", udp[:4])
	if destination_port != 67:
		return None
	bootp = udp[8:]
	if bootp[0] != 1 or bootp[236:240] != MAGIC:
		return None
	xid = bootp[4:8]
	chaddr = bootp[28:34]
	kind = 0
	options = bootp[240:]
	index = 0
	while index < len(options):
		code = options[index]
		if code == 255:
			break
		if code == 0:
			index += 1
			continue
		length = options[index + 1]
		if code == 53 and length >= 1:
			kind = options[index + 2]
		index += 2 + length
	return kind, xid, chaddr, bootp


def reply(kind: int, xid: bytes, chaddr: bytes, flags: bytes) -> bytes:
	"""Builds one broadcast OFFER or ACK frame."""
	bootp = bytearray(240)
	bootp[0] = 2
	bootp[1] = 1
	bootp[2] = 6
	bootp[4:8] = xid
	bootp[10:12] = flags
	bootp[16:20] = CLIENT_ADDRESS
	bootp[20:24] = SERVER_ADDRESS
	bootp[28:34] = chaddr
	bootp[236:240] = MAGIC
	bootp += bytes([53, 1, kind])
	bootp += bytes([54, 4]) + SERVER_ADDRESS
	bootp += bytes([51, 4]) + struct.pack("!I", LEASE_SECONDS)
	bootp += bytes([1, 4]) + NETMASK
	bootp += bytes([255])
	bootp += bytes(max(0, 300 - len(bootp)))
	udp = struct.pack("!HHHH", 67, 68, 8 + len(bootp), 0) + bytes(bootp)
	ip = bytearray(struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 0, 0,
				   64, 17, 0, SERVER_ADDRESS, b"\xff\xff\xff\xff"))
	ip[10:12] = struct.pack("!H", checksum(bytes(ip)))
	return b"\xff" * 6 + SERVER_MAC + b"\x08\x00" + bytes(ip) + udp


def main() -> int:
	"""Notes the guest's DHCP and answers it after SERVE_AFTER seconds."""
	if len(sys.argv) != 4:
		print(__doc__, file=sys.stderr)
		return 2
	listen_port = int(sys.argv[1])
	guest_port = int(sys.argv[2])
	serve_after = float(sys.argv[3])
	server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	server.bind(("127.0.0.1", listen_port))
	started = time.monotonic()
	print(f"start serve-after={serve_after:g}s", flush=True)
	while True:
		frame, _ = server.recvfrom(65536)
		message = parse(frame)
		if message is None:
			continue
		kind, xid, chaddr, bootp = message
		elapsed = time.monotonic() - started
		serving = elapsed >= serve_after
		print(f"{elapsed:8.1f}s {time.strftime('%H:%M:%S')} {NAMES.get(kind, kind)} "
		      f"xid={xid.hex()} mac={chaddr.hex(':')} "
		      f"{'answered' if serving else 'ignored'}", flush=True)
		if not serving:
			continue
		if kind == 1:
			server.sendto(reply(2, xid, chaddr, bootp[10:12]), ("127.0.0.1", guest_port))
		elif kind == 3:
			server.sendto(reply(5, xid, chaddr, bootp[10:12]), ("127.0.0.1", guest_port))


if __name__ == "__main__":
	raise SystemExit(main())
