# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib

"""Marks the Pi 5's pcie1 node okay in a device tree source on stdin, as
config.txt's dtparam=pciex1 makes the firmware do, and writes it to stdout."""

import sys

text = sys.stdin.read()
start = text.index("pcie@1000110000 {")
end = text.index("\n\t\t};", start)
block = text[start:end]
if block.count('status = "disabled";') != 1:
    sys.exit("enable-pcie1: pcie1 has no single disabled status")
sys.stdout.write(text[:start] + block.replace('status = "disabled";', 'status = "okay";') + text[end:])
