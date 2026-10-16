#!/usr/bin/env python3
#
# Compares the display register indices the amdrdna driver computes
# (amdrdna-tables-test --dcn) with the indices the Linux DCN 2.0 header and
# Navi10's segment bases give for the same registers.
#
# Usage: amdrdna-dcn-compare.py <linux drivers/gpu/drm/amd directory> < table
#
# The Linux names stay in this test; the driver has its own.

import os
import re
import sys

DCN_BASES = [0x12, 0xC0, 0x34C0, 0x9000]

# The Linux register of each driver register, in the driver's order; %d is the instance.
NAMES = [
    lambda n: "D%dVGA_CONTROL" % (n + 1),
    lambda n: "DENTIST_DISPCLK_CNTL",
    lambda n: "OTG%d_PIXEL_RATE_CNTL" % n,
    lambda n: "DP_DTO%d_PHASE" % n,
    lambda n: "DP_DTO%d_MODULO" % n,
    lambda n: "HUBP%d_DCSURF_SURFACE_CONFIG" % n,
    lambda n: "HUBP%d_DCSURF_TILING_CONFIG" % n,
    lambda n: "HUBP%d_DCSURF_PRI_VIEWPORT_START" % n,
    lambda n: "HUBP%d_DCSURF_PRI_VIEWPORT_DIMENSION" % n,
    lambda n: "HUBP%d_DCHUBP_CNTL" % n,
    lambda n: "HUBP%d_HUBP_CLK_CNTL" % n,
    lambda n: "HUBPREQ%d_DCSURF_SURFACE_PITCH" % n,
    lambda n: "HUBPREQ%d_DCSURF_PRIMARY_SURFACE_ADDRESS" % n,
    lambda n: "HUBPREQ%d_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH" % n,
    lambda n: "HUBPREQ%d_DCSURF_SURFACE_CONTROL" % n,
    lambda n: "HUBPREQ%d_DCSURF_FLIP_CONTROL" % n,
    lambda n: "HUBPRET%d_HUBPRET_CONTROL" % n,
    lambda n: "DPP_TOP%d_DPP_CONTROL" % n,
    lambda n: "MPCC%d_MPCC_TOP_SEL" % n,
    lambda n: "MPCC%d_MPCC_BOT_SEL" % n,
    lambda n: "MPCC%d_MPCC_OPP_ID" % n,
    lambda n: "MPCC%d_MPCC_CONTROL" % n,
    lambda n: "MPC_OUT%d_MUX" % n,
    lambda n: "ODM%d_OPTC_DATA_SOURCE_SELECT" % n,
    lambda n: "ODM%d_OPTC_INPUT_GLOBAL_CONTROL" % n,
    lambda n: "OTG%d_OTG_H_TOTAL" % n,
    lambda n: "OTG%d_OTG_H_BLANK_START_END" % n,
    lambda n: "OTG%d_OTG_H_SYNC_A" % n,
    lambda n: "OTG%d_OTG_H_SYNC_A_CNTL" % n,
    lambda n: "OTG%d_OTG_V_TOTAL" % n,
    lambda n: "OTG%d_OTG_V_BLANK_START_END" % n,
    lambda n: "OTG%d_OTG_V_SYNC_A" % n,
    lambda n: "OTG%d_OTG_V_SYNC_A_CNTL" % n,
    lambda n: "OTG%d_OTG_CONTROL" % n,
    lambda n: "OTG%d_OTG_MASTER_UPDATE_LOCK" % n,
    lambda n: "DIG%d_DIG_FE_CNTL" % n,
    lambda n: "DIG%d_DIG_BE_CNTL" % n,
    lambda n: "DIG%d_DIG_BE_EN_CNTL" % n,
    lambda n: "DP%d_DP_VID_STREAM_CNTL" % n,
    lambda n: "DP%d_DP_LINK_CNTL" % n,
    lambda n: "DSCL%d_SCL_MODE" % n,
    lambda n: "DSCL%d_SCL_HORZ_FILTER_SCALE_RATIO" % n,
    lambda n: "DSCL%d_SCL_VERT_FILTER_SCALE_RATIO" % n,
    lambda n: "DSCL%d_RECOUT_START" % n,
    lambda n: "DSCL%d_RECOUT_SIZE" % n,
    lambda n: "DSCL%d_DSCL_AUTOCAL" % n,
    lambda n: "HUBPREQ%d_VMID_SETTINGS_0" % n,
    lambda n: "HUBPREQ%d_DCSURF_SURFACE_FLIP_INTERRUPT" % n,
    lambda n: "HUBPREQ%d_DCSURF_SURFACE_EARLIEST_INUSE" % n,
    lambda n: "HUBPREQ%d_DCSURF_SURFACE_EARLIEST_INUSE_HIGH" % n,
    lambda n: "HUBPREQ%d_CURSOR_SETTINGS" % n,
    lambda n: "CURSOR0_%d_CURSOR_CONTROL" % n,
    lambda n: "CURSOR0_%d_CURSOR_SURFACE_ADDRESS" % n,
    lambda n: "CURSOR0_%d_CURSOR_SURFACE_ADDRESS_HIGH" % n,
    lambda n: "CURSOR0_%d_CURSOR_SIZE" % n,
    lambda n: "CURSOR0_%d_CURSOR_POSITION" % n,
    lambda n: "CURSOR0_%d_CURSOR_HOT_SPOT" % n,
    lambda n: "CURSOR0_%d_CURSOR_DST_OFFSET" % n,
    lambda n: "CNVC_CUR%d_CURSOR0_CONTROL" % n,
    lambda n: "CNVC_CUR%d_CURSOR0_FP_SCALE_BIAS" % n,
    lambda n: "OTG%d_OTG_STATUS_POSITION" % n,
    lambda n: "OTG%d_OTG_STATUS_FRAME_COUNT" % n,
    lambda n: "OTG%d_OTG_GLOBAL_SYNC_STATUS" % n,
    lambda n: "HPD%d_DC_HPD_INT_STATUS" % n,
    lambda n: "HPD%d_DC_HPD_INT_CONTROL" % n,
    lambda n: "DC_I2C_CONTROL",
    lambda n: "DC_I2C_ARBITRATION",
    lambda n: "DC_I2C_SW_STATUS",
    lambda n: "DC_I2C_DDC%d_HW_STATUS" % (n + 1),
    lambda n: "DC_I2C_DDC%d_SPEED" % (n + 1),
    lambda n: "DC_I2C_DDC%d_SETUP" % (n + 1),
    lambda n: "DC_I2C_TRANSACTION%d" % min(n, 3),
    lambda n: "DC_I2C_DATA",
    lambda n: "MICROSECOND_TIME_BASE_DIV",
    lambda n: "DC_GPIO_DDC%d_MASK" % (n + 1),
    lambda n: "DP_AUX%d_AUX_CONTROL" % n,
    lambda n: "DP_AUX%d_AUX_SW_CONTROL" % n,
    lambda n: "DP_AUX%d_AUX_ARB_CONTROL" % n,
    lambda n: "DP_AUX%d_AUX_INTERRUPT_CONTROL" % n,
    lambda n: "DP_AUX%d_AUX_SW_STATUS" % n,
    lambda n: "DP_AUX%d_AUX_SW_DATA" % n,
    lambda n: "DP_AUX%d_AUX_DPHY_RX_CONTROL0" % n,
    lambda n: "OTG%d_OTG_BLANK_CONTROL" % n,
    lambda n: "OTG%d_OTG_CRC_CNTL" % n,
    lambda n: "OTG%d_OTG_CRC0_DATA_RG" % n,
    lambda n: "OTG%d_OTG_CRC0_DATA_B" % n,
    lambda n: "HUBP%d_DCSURF_SEC_VIEWPORT_START" % n,
    lambda n: "HUBP%d_DCSURF_SEC_VIEWPORT_DIMENSION" % n,
    lambda n: "HUBP%d_DCHUBP_REQ_SIZE_CONFIG" % n,
    lambda n: "HUBP%d_DCHUBP_REQ_SIZE_CONFIG_C" % n,
    lambda n: "HUBP%d_HUBPREQ_DEBUG_DB" % n,
    lambda n: "HUBPREQ%d_DCN_EXPANSION_MODE" % n,
    lambda n: "HUBPREQ%d_DCN_TTU_QOS_WM" % n,
    lambda n: "HUBPREQ%d_DCN_GLOBAL_TTU_CNTL" % n,
    lambda n: "HUBPREQ%d_DCN_SURF0_TTU_CNTL0" % n,
    lambda n: "HUBPREQ%d_DCN_SURF0_TTU_CNTL1" % n,
    lambda n: "HUBPREQ%d_DCN_SURF1_TTU_CNTL0" % n,
    lambda n: "HUBPREQ%d_DCN_SURF1_TTU_CNTL1" % n,
    lambda n: "HUBPREQ%d_DCN_CUR0_TTU_CNTL0" % n,
    lambda n: "HUBPREQ%d_DCN_CUR0_TTU_CNTL1" % n,
    lambda n: "HUBPREQ%d_DCN_CUR1_TTU_CNTL1" % n,
    lambda n: "HUBPREQ%d_BLANK_OFFSET_0" % n,
    lambda n: "HUBPREQ%d_BLANK_OFFSET_1" % n,
    lambda n: "HUBPREQ%d_DST_DIMENSIONS" % n,
    lambda n: "HUBPREQ%d_DST_AFTER_SCALER" % n,
    lambda n: "HUBPREQ%d_PREFETCH_SETTINGS" % n,
    lambda n: "HUBPREQ%d_PREFETCH_SETTINGS_C" % n,
    lambda n: "HUBPREQ%d_VBLANK_PARAMETERS_0" % n,
    lambda n: "HUBPREQ%d_VBLANK_PARAMETERS_1" % n,
    lambda n: "HUBPREQ%d_VBLANK_PARAMETERS_2" % n,
    lambda n: "HUBPREQ%d_VBLANK_PARAMETERS_3" % n,
    lambda n: "HUBPREQ%d_VBLANK_PARAMETERS_4" % n,
    lambda n: "HUBPREQ%d_FLIP_PARAMETERS_0" % n,
    lambda n: "HUBPREQ%d_FLIP_PARAMETERS_1" % n,
    lambda n: "HUBPREQ%d_FLIP_PARAMETERS_2" % n,
    lambda n: "HUBPREQ%d_NOM_PARAMETERS_0" % n,
    lambda n: "HUBPREQ%d_NOM_PARAMETERS_1" % n,
    lambda n: "HUBPREQ%d_NOM_PARAMETERS_2" % n,
    lambda n: "HUBPREQ%d_NOM_PARAMETERS_3" % n,
    lambda n: "HUBPREQ%d_NOM_PARAMETERS_4" % n,
    lambda n: "HUBPREQ%d_NOM_PARAMETERS_5" % n,
    lambda n: "HUBPREQ%d_NOM_PARAMETERS_6" % n,
    lambda n: "HUBPREQ%d_NOM_PARAMETERS_7" % n,
    lambda n: "HUBPREQ%d_PER_LINE_DELIVERY_PRE" % n,
    lambda n: "HUBPREQ%d_PER_LINE_DELIVERY" % n,
    lambda n: "HUBPREQ%d_REF_FREQ_TO_PIX_FREQ" % n,
    lambda n: "HUBPREQ%d_DST_Y_DELTA_DRQ_LIMIT" % n,
    lambda n: "DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_%s" % "ABCDDD"[n],
    lambda n: "DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_%s" % "ABCDDD"[n],
    lambda n: "DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_%s" % "ABCDDD"[n],
    lambda n: "DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_%s" % "ABCDDD"[n],
    lambda n: "DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_%s" % "ABCDDD"[n],
    lambda n: "DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL",
    lambda n: "DCHUBBUB_ARB_SAT_LEVEL",
    lambda n: "DCHUBBUB_ARB_DF_REQ_OUTSTAND",
    lambda n: "DCHUBBUB_GLOBAL_TIMER_CNTL",
    lambda n: "OTG%d_OTG_VSTARTUP_PARAM" % n,
    lambda n: "OTG%d_OTG_VUPDATE_PARAM" % n,
    lambda n: "OTG%d_OTG_VREADY_PARAM" % n,
    lambda n: "OTG%d_OTG_V_TOTAL_MIN" % n,
    lambda n: "OTG%d_OTG_V_TOTAL_MAX" % n,
    lambda n: "OTG%d_OTG_INTERLACE_CONTROL" % n,
    lambda n: "OTG%d_OTG_CLOCK_CONTROL" % n,
    lambda n: "VTG%d_CONTROL" % n,
    lambda n: "DSCL%d_OTG_H_BLANK" % n,
    lambda n: "DSCL%d_OTG_V_BLANK" % n,
    lambda n: "DSCL%d_MPC_SIZE" % n,
]


def main():
    header = os.path.join(sys.argv[1], "include/asic_reg/dcn/dcn_2_0_0_offset.h")
    offsets = {}
    indices = {}
    pattern = re.compile(r"#define\s+mm(\w+)\s+(0x[0-9a-fA-F]+|\d+)")
    with open(header) as stream:
        for line in stream:
            match = pattern.match(line)
            if match is None:
                continue
            if match.group(1).endswith("_BASE_IDX"):
                indices[match.group(1)[:-9]] = int(match.group(2), 0)
            else:
                offsets[match.group(1)] = int(match.group(2), 0)
    failures = 0
    count = 0
    for line in sys.stdin:
        which, instance, value = line.split()
        which = int(which)
        instance = int(instance)
        value = int(value, 16)
        if which >= len(NAMES):
            print("FAIL dcn: register %d has no Linux name in this test" % which)
            failures += 1
            continue
        name = NAMES[which](instance)
        if name not in offsets:
            print("FAIL dcn: %s is not in the header" % name)
            failures += 1
            continue
        expected = DCN_BASES[indices.get(name, 0)] + offsets[name]
        count += 1
        if expected != value:
            print("FAIL dcn: %s: driver 0x%05x, header 0x%05x" % (name, value, expected))
            failures += 1
    if failures != 0:
        print("FAIL amdrdna-dcn-compare (%d of %d)" % (failures, count))
        return 1
    print("PASS amdrdna-dcn-compare (%d registers)" % count)
    return 0


sys.exit(main())
