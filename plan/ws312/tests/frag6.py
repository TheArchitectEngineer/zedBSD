#!/usr/bin/env python3
"""The host's side of plan/ws312/tests/qemu-frag.sh: IPv6 datagrams and hand-made fragments to a guest.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    frag6.py fnv LENGTH
    frag6.py udp DESTINATION PORT LENGTH
    frag6.py craft INTERFACE SOURCE_MAC DESTINATION_MAC SOURCE DESTINATION CASE PORT
    frag6.py listen SECONDS TYPE CODE

fnv prints the FNV-1a hash of the test pattern of LENGTH bytes.  udp sends
one datagram of the pattern through the host's own stack (which cuts it in
fragments by the path MTU).  craft sends Ethernet frames on INTERFACE
(AF_PACKET, root) for CASE: reverse, overlap, atomic, timeout (two
datagrams with a middle piece missing, the first with its offset-zero
piece and the second without), unknown (an atomic fragment of an unknown
next header), hbh (an atomic fragment followed by hop-by-hop options),
nested (two fragment headers), ns-plain and ns-fragment (a Neighbor
Solicitation for DESTINATION, plain or as an atomic fragment).  listen
waits up to SECONDS for ICMPv6 messages of TYPE and CODE (a raw socket,
root) and prints one line for each: "frag6: got type=T code=C data=D
id=I target=X" (I the identification of a quoted fragment header, X the
target of a Neighbor Advertisement), then "frag6: listened N".
"""
from __future__ import annotations

import select
import socket
import struct
import sys
import time

# The identifications the crafted datagrams use, by case.
IDENTIFICATIONS = {
	"reverse": 0x31200001,
	"overlap": 0x31200002,
	"timeout-first": 0x31200003,
	"timeout-nofirst": 0x31200004,
	"atomic": 0x31200005,
	"unknown": 0x31200006,
	"hbh": 0x31200007,
	"nested": 0x31200008,
	"ns": 0x31200009,
}

# The source port of the crafted UDP datagrams.
SOURCE_PORT = 40000


def pattern(length: int) -> bytes:
	"""Gives the test pattern: each byte a function of its place."""
	return bytes(((index * 7) + (index >> 8)) & 0xFF for index in range(length))


def fnv(data: bytes) -> int:
	"""Gives the 32-bit FNV-1a hash of some bytes."""
	value = 2166136261
	for byte in data:
		value ^= byte
		value = (value * 16777619) & 0xFFFFFFFF
	return value


def checksum(data: bytes) -> int:
	"""Gives the Internet checksum of some bytes."""
	if len(data) % 2:
		data += b"\0"
	total = sum(struct.unpack(f"!{len(data) // 2}H", data))
	while total >> 16:
		total = (total & 0xFFFF) + (total >> 16)
	return (~total) & 0xFFFF


def upper_checksum(source: bytes, destination: bytes, next_header: int, message: bytes) -> int:
	"""Gives an upper layer's checksum with the IPv6 pseudo header (RFC 8200 section 8.1)."""
	pseudo = source + destination + struct.pack("!IxxxB", len(message), next_header)
	return checksum(pseudo + message)


def udp_datagram(source: bytes, destination: bytes, port: int, payload: bytes) -> bytes:
	"""Gives a UDP header and payload with its checksum."""
	header = struct.pack("!HHHH", SOURCE_PORT, port, 8 + len(payload), 0)
	value = upper_checksum(source, destination, 17, header + payload)
	if value == 0:
		value = 0xFFFF
	return struct.pack("!HHHH", SOURCE_PORT, port, 8 + len(payload), value) + payload


def ipv6_header(source: bytes, destination: bytes, next_header: int, payload_length: int, hop_limit: int = 64) -> bytes:
	"""Gives an IPv6 header."""
	return struct.pack("!IHBB", 0x60000000, payload_length, next_header, hop_limit) + source + destination


def fragment_header(next_header: int, offset: int, more: bool, identification: int) -> bytes:
	"""Gives a fragment header."""
	return struct.pack("!BBHI", next_header, 0, offset | (1 if more else 0), identification)


def frame(source_mac: bytes, destination_mac: bytes, packet: bytes) -> bytes:
	"""Gives an Ethernet frame of an IPv6 packet."""
	return destination_mac + source_mac + struct.pack("!H", 0x86DD) + packet


def fragments(source: bytes, destination: bytes, next_header: int, part: bytes, cuts: list[tuple[int, int]], identification: int) -> list[bytes]:
	"""Gives the IPv6 packets of the pieces (offset, length) of a fragmentable part."""
	packets = []
	for offset, length in cuts:
		more = offset + length < len(part)
		piece = part[offset:offset + length]
		header = fragment_header(next_header, offset, more, identification)
		packets.append(ipv6_header(source, destination, 44, len(header) + len(piece)) + header + piece)
	return packets


def cut(length: int, size: int) -> list[tuple[int, int]]:
	"""Gives the pieces of a fragmentable part of length bytes, size bytes each but the last."""
	return [(offset, min(size, length - offset)) for offset in range(0, length, size)]


def neighbor_solicitation(source: bytes, destination: bytes, source_mac: bytes) -> bytes:
	"""Gives a Neighbor Solicitation for destination with the source's link address (RFC 4861 section 4.3)."""
	body = struct.pack("!BBHI", 135, 0, 0, 0) + destination + struct.pack("!BB", 1, 1) + source_mac
	value = upper_checksum(source, destination, 58, body)
	return body[:2] + struct.pack("!H", value) + body[4:]


def craft(arguments: list[str]) -> int:
	"""Sends the frames of one case."""
	interface, source_mac_text, destination_mac_text, source_text, destination_text, case, port_text = arguments
	source_mac = bytes.fromhex(source_mac_text.replace(":", ""))
	destination_mac = bytes.fromhex(destination_mac_text.replace(":", ""))
	source = socket.inet_pton(socket.AF_INET6, source_text)
	destination = socket.inet_pton(socket.AF_INET6, destination_text)
	port = int(port_text)
	packets: list[bytes] = []
	if case == "reverse":
		part = udp_datagram(source, destination, port, pattern(3000))
		packets = list(reversed(fragments(source, destination, 17, part, cut(len(part), 1232), IDENTIFICATIONS[case])))
	elif case == "overlap":
		part = udp_datagram(source, destination, port, pattern(3000))
		cuts = [(0, 1232), (1224, 1232), (2456, len(part) - 2456)]
		packets = fragments(source, destination, 17, part, cuts, IDENTIFICATIONS[case])
	elif case == "atomic":
		part = udp_datagram(source, destination, port, pattern(500))
		packets = fragments(source, destination, 17, part, [(0, len(part))], IDENTIFICATIONS[case])
	elif case == "timeout":
		part = udp_datagram(source, destination, port, pattern(3000))
		pieces = cut(len(part), 1232)
		packets = fragments(source, destination, 17, part, [pieces[0], pieces[2]], IDENTIFICATIONS["timeout-first"])
		packets += fragments(source, destination, 17, part, [pieces[1], pieces[2]], IDENTIFICATIONS["timeout-nofirst"])
	elif case == "unknown":
		packets = fragments(source, destination, 253, bytes(8), [(0, 8)], IDENTIFICATIONS[case])
	elif case == "hbh":
		options = struct.pack("!BBBB", 59, 0, 1, 4) + bytes(4)
		packets = fragments(source, destination, 0, options, [(0, 8)], IDENTIFICATIONS[case])
	elif case == "nested":
		inner = udp_datagram(source, destination, port, pattern(100))
		part = fragment_header(17, 0, False, IDENTIFICATIONS[case] + 0x100) + inner
		packets = fragments(source, destination, 44, part, [(0, len(part))], IDENTIFICATIONS[case])
	elif case in ("ns-plain", "ns-fragment"):
		message = neighbor_solicitation(source, destination, source_mac)
		if case == "ns-plain":
			packets = [ipv6_header(source, destination, 58, len(message), 255) + message]
		else:
			header = fragment_header(58, 0, False, IDENTIFICATIONS["ns"])
			packets = [ipv6_header(source, destination, 44, len(header) + len(message), 255) + header + message]
	else:
		print(f"frag6: no case {case}", file=sys.stderr)
		return 2
	raw = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
	raw.bind((interface, 0))
	for packet in packets:
		raw.send(frame(source_mac, destination_mac, packet))
		time.sleep(0.02)
	raw.close()
	print(f"frag6: sent {case} ({len(packets)} frames)")
	return 0


def listen(arguments: list[str]) -> int:
	"""Prints the ICMPv6 messages of a type and code that arrive within some seconds."""
	seconds, wanted_type, wanted_code = float(arguments[0]), int(arguments[1]), int(arguments[2])
	raw = socket.socket(socket.AF_INET6, socket.SOCK_RAW, socket.IPPROTO_ICMPV6)
	print("frag6: listening", flush=True)
	deadline = time.monotonic() + seconds
	count = 0
	while True:
		left = deadline - time.monotonic()
		if left <= 0:
			break
		readable, _, _ = select.select([raw], [], [], left)
		if not readable:
			break
		message, _ = raw.recvfrom(65536)
		if len(message) < 8 or message[0] != wanted_type or message[1] != wanted_code:
			continue
		data = struct.unpack("!I", message[4:8])[0]
		identification = 0
		target = ""
		quoted = message[8:]
		if wanted_type < 128 and len(quoted) >= 48 and quoted[6] == 44:
			identification = struct.unpack("!I", quoted[44:48])[0]
		if wanted_type == 136 and len(message) >= 24:
			target = socket.inet_ntop(socket.AF_INET6, message[8:24])
		print(f"frag6: got type={message[0]} code={message[1]} data={data} id={identification:#x} target={target}", flush=True)
		count += 1
	raw.close()
	print(f"frag6: listened {count}", flush=True)
	return 0


def main() -> int:
	if len(sys.argv) < 2:
		print(__doc__.strip(), file=sys.stderr)
		return 2
	command, arguments = sys.argv[1], sys.argv[2:]
	if command == "fnv" and len(arguments) == 1:
		print(f"{fnv(pattern(int(arguments[0]))):08x}")
		return 0
	if command == "udp" and len(arguments) == 3:
		destination, port, length = arguments[0], int(arguments[1]), int(arguments[2])
		data = pattern(length)
		sender = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
		sender.sendto(data, (destination, port))
		sender.close()
		print(f"frag6: sent udp {length} fnv={fnv(data):08x}")
		return 0
	if command == "craft" and len(arguments) == 7:
		return craft(arguments)
	if command == "listen" and len(arguments) == 3:
		return listen(arguments)
	print(__doc__.strip(), file=sys.stderr)
	return 2


if __name__ == "__main__":
	sys.exit(main())
