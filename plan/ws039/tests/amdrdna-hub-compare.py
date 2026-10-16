#!/usr/bin/env python3
#
# Compares the register indices of stage A2 that the amdrdna driver computes
# (amdrdna-memory-test --hub: both memory hubs, every instance, and the host
# bridge's, the host data path's and the interrupt controller's registers)
# with the indices the Linux headers and Navi10's segment bases give.
#
# Usage: amdrdna-hub-compare.py <linux drivers/gpu/drm/amd directory> < table
#
# The Linux names stay in this test; the driver has its own.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

import os
import re
import sys

# Navi10's segment bases of the blocks (navi10_ip_offset.h; the board's discovery table gives the same).
BASES = {
    "gc": [0x1260, 0xA000],
    "mmhub": [0x1A000],
    "nbio": [0x0, 0x14, 0xD20, 0x10400],
    "hdp": [0xF20],
    "oss": [0x10A0],
}

HEADERS = {
    "gc": "include/asic_reg/gc/gc_10_1_0_offset.h",
    "mmhub": "include/asic_reg/mmhub/mmhub_2_0_0_offset.h",
    "nbio": "include/asic_reg/nbio/nbio_2_3_offset.h",
    "hdp": "include/asic_reg/hdp/hdp_5_0_0_offset.h",
    "oss": "include/asic_reg/oss/osssys_5_0_0_offset.h",
}

# The Linux register of each hub register, in the driver's order; %d is the instance, the hub's prefix goes first.
HUB_NAMES = [
    "VM_L2_CNTL",
    "VM_L2_CNTL2",
    "VM_L2_CNTL3",
    "VM_L2_CNTL4",
    "VM_L2_CNTL5",
    "VM_L2_PROTECTION_FAULT_CNTL",
    "VM_L2_PROTECTION_FAULT_CNTL2",
    "VM_L2_PROTECTION_FAULT_STATUS",
    "VM_L2_PROTECTION_FAULT_ADDR_LO32",
    "VM_L2_PROTECTION_FAULT_ADDR_HI32",
    "VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32",
    "VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32",
    "VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32",
    "VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32",
    "VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32",
    "VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32",
    "VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32",
    "VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32",
    "VM_CONTEXT%d_CNTL",
    "VM_INVALIDATE_ENG%d_SEM",
    "VM_INVALIDATE_ENG%d_REQ",
    "VM_INVALIDATE_ENG%d_ACK",
    "VM_INVALIDATE_ENG%d_ADDR_RANGE_LO32",
    "VM_INVALIDATE_ENG%d_ADDR_RANGE_HI32",
    "VM_CONTEXT%d_PAGE_TABLE_BASE_ADDR_LO32",
    "VM_CONTEXT%d_PAGE_TABLE_BASE_ADDR_HI32",
    "VM_CONTEXT%d_PAGE_TABLE_START_ADDR_LO32",
    "VM_CONTEXT%d_PAGE_TABLE_START_ADDR_HI32",
    "VM_CONTEXT%d_PAGE_TABLE_END_ADDR_LO32",
    "VM_CONTEXT%d_PAGE_TABLE_END_ADDR_HI32",
    "MC_VM_FB_OFFSET",
    "MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB",
    "MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB",
    "MC_VM_FB_LOCATION_BASE",
    "MC_VM_FB_LOCATION_TOP",
    "MC_VM_AGP_TOP",
    "MC_VM_AGP_BOT",
    "MC_VM_AGP_BASE",
    "MC_VM_SYSTEM_APERTURE_LOW_ADDR",
    "MC_VM_SYSTEM_APERTURE_HIGH_ADDR",
    "MC_VM_MX_L1_TLB_CNTL",
]

HUBS = [("gc", "GC"), ("mmhub", "MM")]

# The stage's other registers, in the order the test prints them.
OTHERS = [
    ("nbio", "RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN"),
    ("nbio", "INTERRUPT_CNTL"),
    ("nbio", "INTERRUPT_CNTL2"),
    ("nbio", "BIF_BX_PF_HDP_MEM_COHERENCY_FLUSH_CNTL"),
    ("nbio", "BIF_IH_DOORBELL_RANGE"),
    ("hdp", "HDP_READ_CACHE_INVALIDATE"),
    ("hdp", "HDP_MISC_CNTL"),
    ("oss", "IH_VMID_0_LUT"),
    ("oss", "IH_RB_CNTL"),
    ("oss", "IH_RB_BASE"),
    ("oss", "IH_RB_BASE_HI"),
    ("oss", "IH_RB_RPTR"),
    ("oss", "IH_RB_WPTR"),
    ("oss", "IH_RB_WPTR_ADDR_HI"),
    ("oss", "IH_RB_WPTR_ADDR_LO"),
    ("oss", "IH_DOORBELL_RPTR"),
    ("nbio", "RCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE"),
]


def read_header(path):
    """Reads the offsets and the base indices of a register header."""
    offsets = {}
    indices = {}
    pattern = re.compile(r"#define\s+mm(\w+)\s+(0x[0-9a-fA-F]+|\d+)")
    with open(path) as stream:
        for line in stream:
            match = pattern.match(line)
            if match is None:
                continue
            if match.group(1).endswith("_BASE_IDX"):
                indices[match.group(1)[:-9]] = int(match.group(2), 0)
            else:
                offsets[match.group(1)] = int(match.group(2), 0)
    return offsets, indices


def expected(headers, block, name):
    """The absolute index the header gives a register, or None."""
    offsets, indices = headers[block]
    if name not in offsets:
        return None
    return BASES[block][indices.get(name, 0)] + offsets[name]


def main():
    headers = {}
    for block, path in HEADERS.items():
        headers[block] = read_header(os.path.join(sys.argv[1], path))
    failures = 0
    count = 0
    for line in sys.stdin:
        fields = line.split()
        if fields[0] == "hub":
            hub = int(fields[1])
            which = int(fields[2])
            instance = int(fields[3])
            value = int(fields[4], 16)
            block, prefix = HUBS[hub]
            if which >= len(HUB_NAMES):
                print("FAIL hub: register %d has no Linux name in this test" % which)
                failures += 1
                continue
            name = prefix + HUB_NAMES[which]
            if "%d" in name:
                name = name % instance
        else:
            item = int(fields[1])
            value = int(fields[2], 16)
            block, name = OTHERS[item]
        want = expected(headers, block, name)
        if want is None:
            print("FAIL hub: %s is not in the header" % name)
            failures += 1
            continue
        count += 1
        if want != value:
            print("FAIL hub: %s: driver 0x%05x, header 0x%05x" % (name, value, want))
            failures += 1
    if failures != 0:
        print("FAIL amdrdna-hub-compare (%d of %d)" % (failures, count))
        return 1
    print("PASS amdrdna-hub-compare (%d registers)" % count)
    return 0


sys.exit(main())
