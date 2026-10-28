#!/bin/sh
# ws291: reads the display's memory-clock registers of the RX 5600 XT under Linux (amdgpu's debugfs amdgpu_regs, as root
# by sudo) on the Debian system, from the host: the hub's DRAM state control and watermark set select, the four sets'
# memory-clock change, urgency and self-refresh watermarks, each pipe's plane control (underflow bits 30:28), its
# global TTU, its vblank and prefetch fields, and each timing generator's underflow word and VSTARTUP; printed as
# "name dword-address value".  With "clear" first it clears the underflow bits (plane bit 31, timing bit 12).
#   usage: plan/ws291/tests/linux-dcn-regs.sh [clear]
# Register offsets: Linux v6.12 dcn_2_0_0_offset.h, base index 2 = 0x34c0 (navi10_ip_offset.h DCN_BASE).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
DEBIAN=${DEBIAN:-awe@10.0.10.3}
clear=${1:-}
timeout 120 ssh -o BatchMode=yes -o ConnectTimeout=8 "$DEBIAN" "sudo -n python3 -I - $clear" <<'PY'
import os, sys, struct
path = '/sys/kernel/debug/dri/0000:03:00.0/amdgpu_regs'
fd = os.open(path, os.O_RDWR)
def rd(dw):
    os.lseek(fd, dw * 4, 0)
    return struct.unpack('<I', os.read(fd, 4))[0]
def wr(dw, v):
    os.lseek(fd, dw * 4, 0)
    os.write(fd, struct.pack('<I', v))
base = 0x34c0
hub = {'DRAM_STATE_CNTL': 0x508, 'WATERMARK_CHANGE_CNTL': 0x51d}
for i, s in enumerate('ABCD'):
    o = 0x509 + 5 * i
    hub['URGENCY_' + s] = o
    hub['SR_ENTER_' + s] = o + 2
    hub['SR_EXIT_' + s] = o + 3
    hub['DRAM_CLK_CHANGE_' + s] = o + 4
if len(sys.argv) > 1 and sys.argv[1] == 'clear':
    for p in range(6):
        a = base + 0x5f3 + p * 0xdc
        wr(a, rd(a) | 0x80000000)
    for t in range(6):
        a = base + 0x1aca + t * 0x10
        wr(a, rd(a) | 0x1000)
for n, o in hub.items():
    print('hub', n, hex(base + o), hex(rd(base + o)))
for p in range(6):
    a = base + 0x5f3 + p * 0xdc
    v = rd(a)
    print('plane%d' % p, 'DCHUBP_CNTL', hex(a), hex(v), 'underflow', (v >> 28) & 7)
    for n, o in (('GLOBAL_TTU', 0x62e), ('VBLANK_PARAMETERS_0', 0x64c), ('DST_AFTER_SCALER', 0x649), ('BLANK_OFFSET_0', 0x646), ('DST_Y_DELTA_DRQ_LIMIT', 0x660), ('TTU_QOS_WM', 0x62d)):
        print('plane%d' % p, n, hex(base + o + p * 0xdc), hex(rd(base + o + p * 0xdc)))
for t in range(6):
    a = base + 0x1aca + t * 0x10
    v = rd(a)
    print('timing%d' % t, 'OPTC_INPUT_GLOBAL_CONTROL', hex(a), hex(v), 'underflow', (v >> 10) & 1)
    print('timing%d' % t, 'VSTARTUP_PARAM', hex(base + 0x1b87 + t * 0x80), hex(rd(base + 0x1b87 + t * 0x80)))
PY
