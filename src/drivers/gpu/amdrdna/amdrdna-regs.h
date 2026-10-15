/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The registers the amdrdna driver reads to find and describe an RDNA GPU.
 *
 * A register is named by the block it belongs to, the window of that block
 * it lives in, and its dword offset inside the window.  The GPU's own
 * table of IP blocks says where each window starts, so the absolute dword
 * index is that start plus the offset; the byte offset in the register BAR
 * is the index times four.  A few registers are read before the table is
 * known, and those carry their absolute index.
 */

#ifndef DRIVERS_GPU_AMDRDNA_AMDRDNA_REGS_H
#define DRIVERS_GPU_AMDRDNA_AMDRDNA_REGS_H

/*
 * The PCI identity: the vendor, the display class and the extended
 * capability that lets the VRAM window be resized.
 *
 * Values referenced: PCI Express Base Specification (Resizable BAR Extended
 * Capability: capability ID 0x0015, one 8-byte entry per resizable BAR
 * with its capability register at +4 and its control register at +8; the
 * control register holds the BAR index in bits 2:0, the number of entries
 * in bits 7:5 of the first one, and the size in bits 13:8 as log2 of the
 * size in MiB) and the PCI code and ID assignment (vendor 0x1002, display
 * class 0x03).
 */
#define AMDRDNA_PCI_VENDOR			0x1002U
#define AMDRDNA_PCI_CLASS_DISPLAY		0x030000U
#define AMDRDNA_PCI_CLASS_DISPLAY_MASK		0xff0000U
#define AMDRDNA_PCI_RESIZE_CAPABILITY		0x0015U
#define AMDRDNA_PCI_RESIZE_SIZES		0x4U
#define AMDRDNA_PCI_RESIZE_CONTROL		0x8U
#define AMDRDNA_PCI_RESIZE_CONTROL_INDEX	0x7U
#define AMDRDNA_PCI_RESIZE_CONTROL_COUNT_SHIFT	5U
#define AMDRDNA_PCI_RESIZE_CONTROL_COUNT_MASK	0x7U
#define AMDRDNA_PCI_RESIZE_CONTROL_SIZE_SHIFT	8U
#define AMDRDNA_PCI_RESIZE_CONTROL_SIZE_MASK	0x3fU
#define AMDRDNA_PCI_RESIZE_ENTRY_BYTES		8U

/*
 * The BARs of an RDNA function: the CPU's window on the VRAM, the doorbell
 * page and the register window.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_device.c (the register BAR of chips
 * from Bonaire on), amdgpu_doorbell_mgr.c (the doorbell BAR) and
 * gmc_v10_0.c (the VRAM aperture BAR).
 */
#define AMDRDNA_BAR_VRAM			0U
#define AMDRDNA_BAR_DOORBELL			2U
#define AMDRDNA_BAR_REGISTERS			5U

/*
 * The size of the register window this driver maps, and the part of the
 * VRAM window it uses (the first 256 MiB, even when the firmware made the
 * window larger).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/nbio_v2_3.c (the register hole at the end of
 * the first 512 KiB of the register window).
 * derived: the register window spans 0x80000 bytes; the CPU window is
 * 0x10000000 bytes.
 */
#define AMDRDNA_REGISTER_WINDOW_BYTES		0x80000U
#define AMDRDNA_CPU_WINDOW_BYTES		0x10000000ULL

/*
 * The registers read before the block table is known: the indirect VRAM
 * window (address low, data, address high), the VRAM size in MiB and the
 * word the platform security processor sets once the board firmware is up.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c (mmMM_INDEX, mmMM_DATA,
 * mmMM_INDEX_HI, mmRCC_CONFIG_MEMSIZE, mmMP0_SMN_C2PMSG_33).
 */
#define AMDRDNA_NBIO_REG_VRAM_WINDOW_LOW	0x0000U
#define AMDRDNA_NBIO_REG_VRAM_WINDOW_DATA	0x0001U
#define AMDRDNA_NBIO_REG_VRAM_WINDOW_HIGH	0x0006U
#define AMDRDNA_NBIO_REG_VRAM_SIZE_MIB		0x0de3U
#define AMDRDNA_PSP_REG_EARLY_INIT_DONE		0x16061U

/*
 * The indirect VRAM window: the low address word carries the request bit
 * and the low 31 address bits, the high word the bits above them.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_device.c (amdgpu_device_mm_access).
 */
#define AMDRDNA_VRAM_WINDOW_REQUEST		0x80000000U
#define AMDRDNA_VRAM_WINDOW_LOW_MASK		0x7fffffffULL
#define AMDRDNA_VRAM_WINDOW_HIGH_SHIFT		31U

/*
 * The indirect register window for addresses outside the register BAR:
 * the address register and the data register (NBIO window 0).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/nbio/nbio_2_3_offset.h
 * (mmPCIE_INDEX2, mmPCIE_DATA2).
 */
#define AMDRDNA_NBIO_WINDOW_FAR			0U
#define AMDRDNA_NBIO_REG_FAR_ADDRESS		0x000eU
#define AMDRDNA_NBIO_REG_FAR_DATA		0x000fU

/*
 * The scratch words the video BIOS leaves for the driver (NBIO window 1):
 * the first one, the seventh, whose bit 9 says the board was posted, and
 * the sixth, whose bit 4 says a driver owns the display.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/nbio/nbio_2_3_offset.h
 * (mmBIOS_SCRATCH_0) and drivers/gpu/drm/amd/include/atombios.h
 * (ATOM_S7_ASIC_INIT_COMPLETE_MASK, ATOM_S6_ACC_MODE).
 */
#define AMDRDNA_NBIO_WINDOW_SCRATCH		1U
#define AMDRDNA_NBIO_REG_BIOS_SCRATCH_FIRST	0x0038U
#define AMDRDNA_BIOS_SCRATCH_ACCELERATED	6U
#define AMDRDNA_BIOS_SCRATCH_POST		7U
#define AMDRDNA_BIOS_SCRATCH_7_POSTED		0x00000200U
#define AMDRDNA_BIOS_SCRATCH_6_DRIVER_OWNS	0x00000010U

/*
 * The security processor's mailbox words (MP0 window 0): the board
 * firmware's ready word, the boot loader's command word and the word the
 * secure OS sets while it runs.  Bit 31 of the first two means done.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/mp/mp_11_0_offset.h
 * (mmMP0_SMN_C2PMSG_33, mmMP0_SMN_C2PMSG_35, mmMP0_SMN_C2PMSG_81) and
 * drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c (the bit-31 test of
 * C2PMSG_33).
 */
#define AMDRDNA_PSP_WINDOW_MAILBOX		0U
#define AMDRDNA_PSP_REG_PLATFORM_READY		0x0061U
#define AMDRDNA_PSP_REG_BOOT_COMMAND		0x0063U
#define AMDRDNA_PSP_REG_SECURE_OS_ALIVE		0x0091U
#define AMDRDNA_PSP_DONE			0x80000000U

/*
 * The system-management unit's firmware state word, reached through the
 * indirect window; bit 0 says its firmware runs.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/inc/smu_v11_0.h (MP1_Public,
 * smnMP1_FIRMWARE_FLAGS) and
 * drivers/gpu/drm/amd/include/asic_reg/mp/mp_11_0_sh_mask.h
 * (MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK).
 * derived: 0x03b00000 | 0x03010024 = 0x03b10024.
 */
#define AMDRDNA_SMU_FAR_FIRMWARE_STATE		0x03b10024U
#define AMDRDNA_SMU_FIRMWARE_RUNNING		0x00000001U

/*
 * The graphics block's status word and the frame-buffer placement words
 * (GC window 0); the placement words count 16 MiB units.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_offset.h
 * (mmGRBM_STATUS, mmGCMC_VM_FB_LOCATION_BASE, mmGCMC_VM_FB_LOCATION_TOP,
 * mmGCMC_VM_FB_OFFSET) and gc_10_1_0_sh_mask.h
 * (GCMC_VM_FB_LOCATION_BASE__FB_BASE_MASK).
 */
#define AMDRDNA_GC_WINDOW_MAIN			0U
#define AMDRDNA_GC_REG_BLOCK_STATUS		0x0da4U
#define AMDRDNA_GC_REG_FRAME_BUFFER_BOTTOM	0x1720U
#define AMDRDNA_GC_REG_FRAME_BUFFER_TOP		0x1721U
#define AMDRDNA_GC_REG_FRAME_BUFFER_PHYSICAL	0x170bU
#define AMDRDNA_GC_FRAME_BUFFER_MASK		0x00ffffffU
#define AMDRDNA_GC_FRAME_BUFFER_SHIFT		24U

/*
 * The flash ROM's address and data registers (SMUIO window 0); each read
 * of the data register returns the next dword of the ROM.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/smuio/smuio_11_0_0_offset.h
 * (mmROM_INDEX, mmROM_DATA).
 */
#define AMDRDNA_SMUIO_WINDOW_MAIN		0U
#define AMDRDNA_SMUIO_REG_ROM_ADDRESS		0x00e4U
#define AMDRDNA_SMUIO_REG_ROM_DATA		0x00e5U

/*
 * Where the GPU keeps its table of IP blocks and its boot database: both
 * near the top of VRAM.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.h (DISCOVERY_TMR_SIZE,
 * DISCOVERY_TMR_OFFSET) and drivers/gpu/drm/amd/amdgpu/amdgpu_psp.h
 * (PSP_RUNTIME_DB_OFFSET).
 */
#define AMDRDNA_BLOCK_TABLE_FROM_TOP		0x10000U
#define AMDRDNA_BLOCK_TABLE_BYTES		0x2800U
#define AMDRDNA_BOOT_DATABASE_FROM_TOP		0x100000U

#endif
