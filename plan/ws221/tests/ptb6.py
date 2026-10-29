#!/usr/bin/env python3
"""Sends one ICMPv6 Packet Too Big (RFC 4443 section 3.2) to a host, as a router on its path would.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    sudo python3 -I ptb6.py SENDER DESTINATION MTU [SOURCE_PORT DESTINATION_PORT]

SENDER is the host whose packet was too big (the message goes to it); the
invoking packet inside is an IPv6 header from SENDER to DESTINATION with
the next header TCP and, when the ports are given, the start of a TCP
header with them.  MTU is the size the message says the path carries.
Run as root (a raw ICMPv6 socket); Linux fills the checksum.  Prints
"ptb6: sent MTU to SENDER about DESTINATION".
"""
from __future__ import annotations

import socket
import struct
import sys


def main() -> int:
	if len(sys.argv) not in (4, 6):
		print(__doc__.strip().splitlines()[4].strip(), file=sys.stderr)
		return 2
	sender, destination, mtu = sys.argv[1], sys.argv[2], int(sys.argv[3])
	ports = b""
	if len(sys.argv) == 6:
		ports = struct.pack("!HHII", int(sys.argv[4]), int(sys.argv[5]), 0, 0)
	payload_length = max(len(ports), 20)
	inner = struct.pack("!IHBB", 0x60000000, payload_length, 6, 64)
	inner += socket.inet_pton(socket.AF_INET6, sender) + socket.inet_pton(socket.AF_INET6, destination)
	inner += ports.ljust(payload_length, b"\0")
	message = struct.pack("!BBHI", 2, 0, 0, mtu) + inner
	raw = socket.socket(socket.AF_INET6, socket.SOCK_RAW, socket.IPPROTO_ICMPV6)
	raw.sendto(message, (sender, 0))
	raw.close()
	print(f"ptb6: sent {mtu} to {sender} about {destination}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
