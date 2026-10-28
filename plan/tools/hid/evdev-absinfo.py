#!/usr/bin/env python3
# ws159-p001: prints the name, properties and absolute axes of the evdev
# nodes whose name contains a word (the touchpad's on the 5330's Linux).
#   python3 evdev-absinfo.py Touchpad
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import fcntl, glob, struct, sys

def ioc(direction, kind, number, size):
    return (direction << 30) | (size << 16) | (ord(kind) << 8) | number

NAMES = {0x00: "ABS_X", 0x01: "ABS_Y", 0x2f: "ABS_MT_SLOT", 0x30: "ABS_MT_TOUCH_MAJOR",
         0x35: "ABS_MT_POSITION_X", 0x36: "ABS_MT_POSITION_Y", 0x37: "ABS_MT_TOOL_TYPE",
         0x39: "ABS_MT_TRACKING_ID"}
word = sys.argv[1]
for path in sorted(glob.glob("/dev/input/event*")):
    try:
        with open(path, "rb") as f:
            name = fcntl.ioctl(f, ioc(2, "E", 0x06, 256), bytes(256)).split(b"\0")[0].decode()
            if word not in name:
                continue
            props = fcntl.ioctl(f, ioc(2, "E", 0x09, 8), bytes(8))
            print(path, repr(name), "props", props.hex())
            bits = fcntl.ioctl(f, ioc(2, "E", 0x20 + 3, 8), bytes(8))
            mask = int.from_bytes(bits, "little")
            for code in range(64):
                if mask >> code & 1:
                    info = struct.unpack("6i", fcntl.ioctl(f, ioc(2, "E", 0x40 + code, 24), bytes(24)))
                    print("  abs 0x%02x %-20s value=%d min=%d max=%d fuzz=%d flat=%d resolution=%d" % ((code, NAMES.get(code, "?")) + info))
            keys = fcntl.ioctl(f, ioc(2, "E", 0x20 + 1, 96), bytes(96))
            kmask = int.from_bytes(keys, "little")
            print("  keys", [hex(c) for c in range(768) if kmask >> c & 1])
    except OSError as error:
        print(path, error)
