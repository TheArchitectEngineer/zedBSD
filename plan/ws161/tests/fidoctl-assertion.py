#!/usr/bin/env python3
"""ws161-p004: makes an assertion for `fidoctl verify` as a security key would: a new P-256 key (its COSE form), the
authenticator data (the relying party's SHA-256, UP, a count of 5) and the ES256 signature over it and a client data
hash.  Prints the six arguments of `fidoctl verify` on one line: RP CREDENTIAL KEY CLIENT-DATA-HASH AUTH-DATA SIGNATURE.
With --tamper the signature is over other client data.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import hashlib
import os
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec

rp = "zedbsd.login"
key = ec.generate_private_key(ec.SECP256R1())
numbers = key.public_key().public_numbers()
x = numbers.x.to_bytes(32, "big")
y = numbers.y.to_bytes(32, "big")
# {1: 2 (EC2), 3: -7 (ES256), -1: 1 (P-256), -2: x, -3: y} in CTAP2's canonical order.
cose = bytes([0xa5, 0x01, 0x02, 0x03, 0x26, 0x20, 0x01, 0x21, 0x58, 0x20]) + x + bytes([0x22, 0x58, 0x20]) + y
credential = os.urandom(16)
client_data_hash = os.urandom(32)
auth_data = hashlib.sha256(rp.encode()).digest() + bytes([0x01]) + (5).to_bytes(4, "big")
signed = client_data_hash
if "--tamper" in sys.argv:
    signed = os.urandom(32)
signature = key.sign(auth_data + signed, ec.ECDSA(hashes.SHA256()))
print(rp, credential.hex(), cose.hex(), client_data_hash.hex(), auth_data.hex(), signature.hex())
