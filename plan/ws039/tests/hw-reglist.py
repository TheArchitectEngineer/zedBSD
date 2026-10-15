#!/usr/bin/env python3
#
# Builds the register list the hardware dump reads on the Linux side of the
# RX 5600 XT machine: every name is resolved against the register headers of
# the fetched Linux tree and the Navi10 segment bases, and printed as
# "<name> <absolute dword index>" lines.
#
# Usage: hw-reglist.py <linux drivers/gpu/drm/amd directory> > list.txt
#
# The names are the Linux header names; they stay in this test tool and never
# reach the driver source.

import os
import re
import sys

# Navi10 segment bases (dword), include/navi10_ip_offset.h, instance 0.
BASES = {
    "nbio": [0x0, 0x14, 0xD20, 0x10400],
    "gc": [0x1260, 0xA000],
    "mmhub": [0x1A000],
    "mp0": [0x16000],
    "mp1": [0x16000],
    "dcn": [0x12, 0xC0, 0x34C0, 0x9000],
    "osssys": [0x10A0],
    "hdp": [0xF20],
    "thm": [0x16600],
    "smuio": [0x16800, 0x16A00],
}

HEADERS = {
    "nbio": "include/asic_reg/nbio/nbio_2_3_offset.h",
    "gc": "include/asic_reg/gc/gc_10_1_0_offset.h",
    "mmhub": "include/asic_reg/mmhub/mmhub_2_0_0_offset.h",
    "mp0": "include/asic_reg/mp/mp_11_0_offset.h",
    "mp1": "include/asic_reg/mp/mp_11_0_offset.h",
    "dcn": "include/asic_reg/dcn/dcn_2_0_0_offset.h",
    "osssys": "include/asic_reg/oss/osssys_5_0_0_offset.h",
    "hdp": "include/asic_reg/hdp/hdp_5_0_0_offset.h",
    "thm": "include/asic_reg/thm/thm_11_0_2_offset.h",
    "smuio": "include/asic_reg/smuio/smuio_11_0_0_offset.h",
}

NAMES = {
    "nbio": ["RCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE", "RCC_DEV0_EPF0_STRAP0"]
    + ["BIOS_SCRATCH_%d" % index for index in range(16)],
    "gc": ["GRBM_STATUS", "GRBM_STATUS2", "GRBM_STATUS_SE0", "CP_STAT",
           "GCMC_VM_FB_LOCATION_BASE", "GCMC_VM_FB_LOCATION_TOP",
           "GCMC_VM_FB_OFFSET", "GCMC_VM_AGP_BASE", "GCMC_VM_AGP_BOT",
           "GCMC_VM_AGP_TOP", "GCMC_VM_SYSTEM_APERTURE_LOW_ADDR",
           "GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR", "GB_ADDR_CONFIG",
           "CC_GC_SHADER_ARRAY_CONFIG", "GC_USER_SHADER_ARRAY_CONFIG",
           "CC_RB_BACKEND_DISABLE", "GC_USER_RB_BACKEND_DISABLE",
           "RLC_RLCS_BOOTLOAD_STATUS", "CP_ME_CNTL", "CP_MEC_CNTL"],
    "mmhub": ["MMMC_VM_FB_LOCATION_BASE", "MMMC_VM_FB_LOCATION_TOP",
              "MMMC_VM_FB_OFFSET", "MMMC_VM_AGP_BASE", "MMMC_VM_AGP_BOT",
              "MMMC_VM_AGP_TOP", "MMMC_VM_SYSTEM_APERTURE_LOW_ADDR",
              "MMMC_VM_SYSTEM_APERTURE_HIGH_ADDR",
              "MMVM_CONTEXT0_CNTL", "MMVM_L2_CNTL"],
    "mp0": ["MP0_SMN_C2PMSG_33", "MP0_SMN_C2PMSG_35", "MP0_SMN_C2PMSG_36",
            "MP0_SMN_C2PMSG_64", "MP0_SMN_C2PMSG_81"],
    "mp1": ["MP1_SMN_C2PMSG_66", "MP1_SMN_C2PMSG_82", "MP1_SMN_C2PMSG_90"],
    "osssys": ["IH_RB_CNTL", "IH_RB_BASE", "IH_CNTL"],
    "hdp": ["HDP_MISC_CNTL"],
    "thm": ["CG_MULT_THERMAL_STATUS", "CG_FDO_CTRL0", "CG_FDO_CTRL1",
            "CG_FDO_CTRL2", "CG_TACH_STATUS", "THM_THERMAL_INT_CTRL"],
    "dcn": ["D%dVGA_CONTROL" % (index + 1) for index in range(6)]
    + ["DP_DTO%d_PHASE" % index for index in range(6)]
    + ["DP_DTO%d_MODULO" % index for index in range(6)]
    + ["OTG%d_PIXEL_RATE_CNTL" % index for index in range(6)]
    + ["DENTIST_DISPCLK_CNTL", "REFCLK_CNTL", "DCCG_GATE_DISABLE_CNTL",
       "DCHUBBUB_ARB_DRAM_STATE_CNTL",
       "DCHUBBUB_ARB_SAT_LEVEL", "DCHUBBUB_GLOBAL_TIMER_CNTL"],
}

# Per-instance register families: (prefix pattern with %d, suffixes).
INSTANCED = [
    ("HUBP%d_", ["DCSURF_SURFACE_CONFIG", "DCSURF_ADDR_CONFIG",
                 "DCSURF_TILING_CONFIG", "DCSURF_PRI_VIEWPORT_START",
                 "DCSURF_PRI_VIEWPORT_DIMENSION", "DCHUBP_CNTL",
                 "HUBP_CLK_CNTL", "DCHUBP_REQ_SIZE_CONFIG"]),
    ("HUBPREQ%d_", ["DCSURF_SURFACE_PITCH", "VMID_SETTINGS_0",
                    "DCSURF_PRIMARY_SURFACE_ADDRESS",
                    "DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH",
                    "DCSURF_SURFACE_CONTROL", "DCSURF_FLIP_CONTROL",
                    "DCSURF_SURFACE_FLIP_INTERRUPT",
                    "DCSURF_SURFACE_EARLIEST_INUSE",
                    "DCSURF_SURFACE_EARLIEST_INUSE_HIGH",
                    "DCN_VM_SYSTEM_APERTURE_LOW_ADDR",
                    "DCN_VM_SYSTEM_APERTURE_HIGH_ADDR", "CURSOR_SETTINGS"]),
    ("HUBPRET%d_", ["HUBPRET_CONTROL"]),
    ("CURSOR0_%d_", ["CURSOR_CONTROL", "CURSOR_SURFACE_ADDRESS",
                     "CURSOR_SURFACE_ADDRESS_HIGH", "CURSOR_SIZE",
                     "CURSOR_POSITION"]),
    ("DPP_TOP%d_", ["DPP_CONTROL"]),
    ("MPCC%d_", ["MPCC_TOP_SEL", "MPCC_BOT_SEL", "MPCC_OPP_ID",
                 "MPCC_CONTROL", "MPCC_STATUS"]),
    ("MPC_OUT%d_", ["MUX"]),
    ("ODM%d_", ["OPTC_DATA_SOURCE_SELECT", "OPTC_INPUT_GLOBAL_CONTROL"]),
    ("OTG%d_", ["OTG_H_TOTAL", "OTG_H_BLANK_START_END", "OTG_H_SYNC_A",
                "OTG_H_SYNC_A_CNTL", "OTG_V_TOTAL", "OTG_V_BLANK_START_END",
                "OTG_V_SYNC_A", "OTG_V_SYNC_A_CNTL", "OTG_CONTROL",
                "OTG_STATUS", "OTG_STATUS_FRAME_COUNT",
                "OTG_MASTER_UPDATE_LOCK", "OTG_GLOBAL_SYNC_STATUS",
                "OTG_VSTARTUP_PARAM", "OTG_DOUBLE_BUFFER_CONTROL"]),
    ("DIG%d_", ["DIG_FE_CNTL", "DIG_BE_CNTL", "DIG_BE_EN_CNTL"]),
    ("DP%d_", ["DP_VID_STREAM_CNTL", "DP_LINK_CNTL", "DP_CONFIG",
               "DP_PIXEL_FORMAT"]),
    ("HPD%d_", ["DC_HPD_INT_STATUS", "DC_HPD_INT_CONTROL"]),
]


def read_header(path):
    """Collects the offset and base index of every register in one header."""
    offsets = {}
    indices = {}
    pattern = re.compile(r"#define\s+mm(\w+)\s+(0x[0-9a-fA-F]+|\d+)")
    with open(path) as stream:
        for line in stream:
            match = pattern.match(line)
            if match is None:
                continue
            name = match.group(1)
            value = int(match.group(2), 0)
            if name.endswith("_BASE_IDX"):
                indices[name[:-len("_BASE_IDX")]] = value
            else:
                offsets[name] = value
    return offsets, indices


def main():
    root = sys.argv[1]
    for block, header in HEADERS.items():
        offsets, indices = read_header(os.path.join(root, header))
        names = list(NAMES.get(block, []))
        if block == "dcn":
            for prefix, suffixes in INSTANCED:
                for instance in range(6):
                    for suffix in suffixes:
                        names.append(prefix % instance + suffix)
        for name in names:
            if name not in offsets:
                print("# missing %s %s" % (block, name))
                continue
            base = BASES[block][indices.get(name, 0)]
            print("%s.%s 0x%05x" % (block, name, base + offsets[name]))


main()
