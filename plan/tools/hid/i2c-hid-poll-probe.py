#!/usr/bin/env python3
# ws159-p001: checks on the 5330's Linux whether the touchpad (I2C-HID at
# 0x2c on /dev/i2c-N) answers a read of its input register without an
# interrupt with an empty report (length 0), so that zedBSD could sample it.
# Run as root with i2c_hid_acpi unbound from the device:
#   python3 i2c-hid-poll-probe.py /dev/i2c-2 0x2c [reads]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import ctypes, fcntl, os, sys, time

I2C_RDWR = 0x0707
I2C_M_RD = 0x0001

class Msg(ctypes.Structure):
    _fields_ = [("addr", ctypes.c_uint16), ("flags", ctypes.c_uint16),
                ("len", ctypes.c_uint16), ("buf", ctypes.POINTER(ctypes.c_uint8))]

class Data(ctypes.Structure):
    _fields_ = [("msgs", ctypes.POINTER(Msg)), ("nmsgs", ctypes.c_uint32)]

def xfer(fd, addr, write, read_len):
    msgs = (Msg * 2)()
    wbuf = (ctypes.c_uint8 * len(write))(*write)
    msgs[0] = Msg(addr, 0, len(write), wbuf)
    count = 1
    rbuf = None
    if read_len:
        rbuf = (ctypes.c_uint8 * read_len)()
        msgs[1] = Msg(addr, I2C_M_RD, read_len, rbuf)
        count = 2
    data = Data(msgs, count)
    fcntl.ioctl(fd, I2C_RDWR, data)
    return bytes(rbuf) if rbuf is not None else b""

def read_only(fd, addr, read_len):
    msgs = (Msg * 1)()
    rbuf = (ctypes.c_uint8 * read_len)()
    msgs[0] = Msg(addr, I2C_M_RD, read_len, rbuf)
    fcntl.ioctl(fd, I2C_RDWR, Data(msgs, 1))
    return bytes(rbuf)

def main():
    path, addr = sys.argv[1], int(sys.argv[2], 0)
    reads = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    fd = os.open(path, os.O_RDWR)
    desc = xfer(fd, addr, [0x20, 0x00], 30)
    print("hid-descriptor", desc.hex(" "))
    inreg = desc[8] | desc[9] << 8
    maxin = desc[10] | desc[11] << 8
    cmdreg = desc[16] | desc[17] << 8
    # SET_POWER ON
    xfer(fd, addr, [cmdreg & 0xff, cmdreg >> 8, 0x00, 0x08], 0)
    time.sleep(0.1)
    for i in range(reads):
        # The spec's input read is a plain read (no register write first).
        plain = read_only(fd, addr, maxin)
        print("plain-read", i, "length", plain[0] | plain[1] << 8, plain[:12].hex(" "))
        time.sleep(0.05)
    for i in range(3):
        reg = xfer(fd, addr, [inreg & 0xff, inreg >> 8], maxin)
        print("register-read", i, "length", reg[0] | reg[1] << 8, reg[:12].hex(" "))
        time.sleep(0.05)
    os.close(fd)

main()
