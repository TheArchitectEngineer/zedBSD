#!/usr/bin/env python3
"""Counts the DHCPv4 requests (UDP to port 67) in a pcap file of Ethernet frames (ws255-p001).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    pcap-dhcp.py FILE      prints the count; frames read go to standard error
"""
import struct
import sys


def main() -> int:
	"""Prints how many frames of FILE are IPv4 UDP datagrams to port 67."""
	data = open(sys.argv[1], "rb").read()
	magic = data[:4]
	if magic == b"\xd4\xc3\xb2\xa1":
		order = "<"
	elif magic == b"\xa1\xb2\xc3\xd4":
		order = ">"
	else:
		raise SystemExit("not a pcap file")
	offset = 24
	frames = 0
	dhcp = 0
	while offset + 16 <= len(data):
		_, _, captured, _ = struct.unpack(order + "IIII", data[offset:offset + 16])
		frame = data[offset + 16:offset + 16 + captured]
		offset += 16 + captured
		frames += 1
		if len(frame) < 14 + 20 + 4 or frame[12:14] != b"\x08\x00":
			continue
		header = (frame[14] & 0x0F) * 4
		if frame[14 + 9] != 17:
			continue
		port = struct.unpack("!H", frame[14 + header + 2:14 + header + 4])[0]
		if port == 67:
			dhcp += 1
	print(f"frames={frames}", file=sys.stderr)
	print(dhcp)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
