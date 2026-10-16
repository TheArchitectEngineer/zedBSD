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

/*
 * The host bridge's registers the memory stage uses (NBIO window 2): the
 * doorbell aperture switch (bit 0 lets the doorbell BAR through), the
 * interrupt control word, the dummy-read page of the interrupt controller
 * (its address shifted right by 8), the host data path's write flush, and
 * the doorbell range that routes the interrupt controller's doorbell.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/nbio/nbio_2_3_offset.h
 * (mmRCC_DEV0_EPF0_RCC_DOORBELL_APER_EN, mmINTERRUPT_CNTL,
 * mmINTERRUPT_CNTL2, mmBIF_BX_PF_HDP_MEM_COHERENCY_FLUSH_CNTL,
 * mmBIF_IH_DOORBELL_RANGE), nbio_2_3_sh_mask.h
 * (RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN__BIF_DOORBELL_APER_EN) and
 * drivers/gpu/drm/amd/amdgpu/nbio_v2_3.c (nbio_v2_3_ih_control,
 * nbio_v2_3_get_rmmio_remap: the flush register when the remapped one is
 * not set up).
 */
#define AMDRDNA_NBIO_WINDOW_HOST		2U
#define AMDRDNA_NBIO_REG_DOORBELL_APERTURE	0x00c0U
#define AMDRDNA_NBIO_REG_INTERRUPT_CONTROL	0x00f1U
#define AMDRDNA_NBIO_REG_INTERRUPT_DUMMY	0x00f2U
#define AMDRDNA_NBIO_REG_HOST_PATH_FLUSH	0x00f7U
#define AMDRDNA_NBIO_REG_INTERRUPT_DOORBELLS	0x01d2U
#define AMDRDNA_DOORBELL_APERTURE_ON		0x00000001U
#define AMDRDNA_INTERRUPT_DUMMY_SHIFT		8U

/*
 * The host data path's registers (HDP window 0): the word that drops its
 * read cache (write 1, read back), and the control word whose bit 0 makes
 * a flush also drop the read cache.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/hdp/hdp_5_0_0_offset.h
 * (mmHDP_READ_CACHE_INVALIDATE, mmHDP_MISC_CNTL), hdp_5_0_0_sh_mask.h
 * (HDP_MISC_CNTL__FLUSH_INVALIDATE_CACHE) and
 * drivers/gpu/drm/amd/amdgpu/hdp_v5_0.c (hdp_v5_0_invalidate_hdp,
 * hdp_v5_0_init_registers).
 */
#define AMDRDNA_HDP_WINDOW_MAIN			0U
#define AMDRDNA_HDP_REG_READ_DROP		0x00d1U
#define AMDRDNA_HDP_REG_MISC			0x00d3U
#define AMDRDNA_HDP_MISC_FLUSH_DROPS		0x00000001U

/*
 * The interrupt controller's registers (OSSSYS window 0): the table that
 * names the process address space of each graphics-hub context (one word
 * per context), the ring's control word, its base (address bits 39:8) and
 * the base's high byte (bits 47:40), the read and write pointers, the
 * address of the write pointer's copy in memory (high 16 bits, low 32
 * bits) and the read-pointer doorbell word.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/oss/osssys_5_0_0_offset.h
 * (mmIH_VMID_0_LUT, mmIH_RB_CNTL, mmIH_RB_BASE, mmIH_RB_BASE_HI,
 * mmIH_RB_RPTR, mmIH_RB_WPTR, mmIH_RB_WPTR_ADDR_HI, mmIH_RB_WPTR_ADDR_LO,
 * mmIH_DOORBELL_RPTR) and drivers/gpu/drm/amd/amdgpu/navi10_ih.c
 * (navi10_ih_enable_ring: the base shifts and the 16-bit high address).
 */
#define AMDRDNA_IH_WINDOW_MAIN			0U
#define AMDRDNA_IH_REG_SPACE_TO_PROCESS		0x0000U
#define AMDRDNA_IH_REG_RING_CONTROL		0x0080U
#define AMDRDNA_IH_REG_RING_BASE		0x0081U
#define AMDRDNA_IH_REG_RING_BASE_HIGH		0x0082U
#define AMDRDNA_IH_REG_READ_POINTER		0x0083U
#define AMDRDNA_IH_REG_WRITE_POINTER		0x0084U
#define AMDRDNA_IH_REG_POINTER_COPY_HIGH	0x0085U
#define AMDRDNA_IH_REG_POINTER_COPY_LOW		0x0086U
#define AMDRDNA_IH_REG_DOORBELL			0x0087U
#define AMDRDNA_IH_RING_BASE_SHIFT		8U
#define AMDRDNA_IH_RING_BASE_HIGH_SHIFT		40U
#define AMDRDNA_IH_RING_BASE_HIGH_MASK		0x000000ffU
#define AMDRDNA_IH_POINTER_COPY_HIGH_MASK	0x0000ffffU

/*
 * The interrupt ring's size, and the doorbell of its read pointer: slot
 * 0x178 of the 64-bit doorbell slots, which is dword 0x2f0 of the
 * doorbell BAR.  The driver maps the first page of that BAR, which holds
 * every doorbell it uses.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_ih.h (IH_RING_SIZE),
 * drivers/gpu/drm/amd/amdgpu/amdgpu_doorbell.h
 * (AMDGPU_NAVI10_DOORBELL_IH) and drivers/gpu/drm/amd/amdgpu/navi10_ih.c
 * (navi10_ih_sw_init: the slot shifted left by one).
 * derived: 0x178 << 1 = 0x2f0.
 */
#define AMDRDNA_INTERRUPT_RING_BYTES		0x40000U
#define AMDRDNA_DOORBELL_INTERRUPTS		0x02f0U
#define AMDRDNA_DOORBELL_BYTES			0x1000U

/*
 * The memory hubs' settings the driver uses: the invalidation engine the
 * CPU drives (17), the bit an engine's lock word reads as 1 once taken,
 * the reach of the system-memory table of context 0 (512 MiB), the range
 * every invalidation engine covers (all of it), the values that leave the
 * bypass aperture shut (bottom above top) and the identity aperture shut
 * (first above last), the last page of a context's 48-bit space, and the
 * shifts of the aperture words (256 KiB units), the bypass words (16 MiB
 * units) and the page words (4 KiB units, high half from bit 44).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/gmc_v10_0.c (gmc_v10_0_flush_gpu_tlb:
 * engine 17 and the semaphore; gmc_v10_0_mc_init: 512 MiB for GC 10.1),
 * drivers/gpu/drm/amd/include/asic_reg/gc/gc_10_1_0_sh_mask.h
 * (GCVM_INVALIDATE_ENG0_SEM__SEMAPHORE),
 * drivers/gpu/drm/amd/amdgpu/gfxhub_v2_0.c
 * (gfxhub_v2_0_program_invalidation, gfxhub_v2_0_disable_identity_aperture,
 * gfxhub_v2_0_init_system_aperture_regs, gfxhub_v2_0_setup_vmid_config,
 * gfxhub_v2_0_init_gart_aperture_regs) and amdgpu_gmc.c
 * (amdgpu_gmc_set_agp_default).
 */
#define AMDRDNA_HUB_CPU_ENGINE			17U
#define AMDRDNA_HUB_LOCK_TAKEN			0x00000001U
#define AMDRDNA_GART_BYTES			0x20000000ULL
#define AMDRDNA_HUB_RANGE_ALL_LOW		0xffffffffU
#define AMDRDNA_HUB_RANGE_ALL_HIGH		0x0000001fU
#define AMDRDNA_HUB_BYPASS_SHUT_BOTTOM		0x00ffffffU
#define AMDRDNA_HUB_IDENTITY_SHUT_LOW		0xffffffffU
#define AMDRDNA_HUB_IDENTITY_SHUT_HIGH		0x0000000fU
#define AMDRDNA_HUB_LAST_PAGE_LOW		0xffffffffU
#define AMDRDNA_HUB_LAST_PAGE_HIGH		0x0000000fU
#define AMDRDNA_HUB_APERTURE_SHIFT		18U
#define AMDRDNA_HUB_BYPASS_SHIFT		24U
#define AMDRDNA_HUB_PAGE_SHIFT			12U
#define AMDRDNA_HUB_PAGE_HIGH_SHIFT		44U


/*
 * The security processor's mailbox words the bring-up uses beyond the
 * ones above (MP0 window 0): the boot loader's argument (an address in
 * 1 MiB units), the secure OS's control word for its ring, the ring's
 * write pointer (in dwords), the ring's address (low, high) and its size
 * in bytes.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/mp/mp_11_0_offset.h
 * (mmMP0_SMN_C2PMSG_36, mmMP0_SMN_C2PMSG_64, mmMP0_SMN_C2PMSG_67,
 * mmMP0_SMN_C2PMSG_69, mmMP0_SMN_C2PMSG_70, mmMP0_SMN_C2PMSG_71).
 */
#define AMDRDNA_PSP_REG_BOOT_ARGUMENT		0x0064U
#define AMDRDNA_PSP_REG_RING_CONTROL		0x0080U
#define AMDRDNA_PSP_REG_RING_WRITE		0x0083U
#define AMDRDNA_PSP_REG_RING_LOW		0x0085U
#define AMDRDNA_PSP_REG_RING_HIGH		0x0086U
#define AMDRDNA_PSP_REG_RING_BYTES		0x0087U

/*
 * The security processor's handshake values: the mask and value of a
 * mailbox word that reads "ready, no error" (bit 31 and a zero status in
 * bits 15:0), the boot loader's commands (load the key database, the
 * system driver, the secure OS, and run the long memory training), and
 * the secure OS's ring commands (create the kernel-mode ring, type 2 in
 * bits 23:16, and destroy every ring).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_psp.h (PSP_BL__LOAD_KEY_DATABASE,
 * PSP_BL__LOAD_SYSDRV, PSP_BL__LOAD_SOSDRV, PSP_BL__DRAM_LONG_TRAIN,
 * MBOX_TOS_READY_FLAG, MBOX_TOS_READY_MASK, enum psp_ring_type) and
 * drivers/gpu/drm/amd/amdgpu/psp_gfx_if.h (GFX_CMD_STATUS_MASK,
 * GFX_CMD_RESPONSE_MASK, GFX_CTRL_CMD_ID_INIT_GPCOM_RING,
 * GFX_CTRL_CMD_ID_DESTROY_RINGS), and psp_v11_0.c
 * (psp_v11_0_wait_for_bootloader: the 0x8000FFFF mask).
 */
#define AMDRDNA_PSP_READY_MASK			0x8000ffffU
#define AMDRDNA_PSP_READY			0x80000000U
#define AMDRDNA_PSP_BOOT_KEY_DATABASE		0x00080000U
#define AMDRDNA_PSP_BOOT_SYSTEM_DRIVER		0x00010000U
#define AMDRDNA_PSP_BOOT_SECURE_OS		0x00020000U
#define AMDRDNA_PSP_BOOT_LONG_TRAINING		0x00100000U
#define AMDRDNA_PSP_RING_CREATE			0x00020000U
#define AMDRDNA_PSP_RING_DESTROY		0x00030000U
#define AMDRDNA_PSP_ADDRESS_SHIFT		20U

/*
 * The system-management unit's mailbox (MP1 window 0): the message word,
 * its argument and its response, and the words that let the unit raise a
 * software interrupt (the interrupt's ID in bits 7:0, valid in bit 8) and
 * mask or acknowledge it (mask in bit 0, acknowledge in bit 8).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/mp/mp_11_0_offset.h
 * (mmMP1_SMN_C2PMSG_66, mmMP1_SMN_C2PMSG_82, mmMP1_SMN_C2PMSG_90,
 * mmMP1_SMN_IH_SW_INT, mmMP1_SMN_IH_SW_INT_CTRL) and mp_11_0_sh_mask.h
 * (MP1_SMN_IH_SW_INT__ID_MASK, MP1_SMN_IH_SW_INT__VALID_MASK,
 * MP1_SMN_IH_SW_INT_CTRL__INT_MASK_MASK,
 * MP1_SMN_IH_SW_INT_CTRL__INT_ACK_MASK).
 */
#define AMDRDNA_SMU_WINDOW_MAILBOX		0U
#define AMDRDNA_SMU_REG_MESSAGE			0x0282U
#define AMDRDNA_SMU_REG_ARGUMENT		0x0292U
#define AMDRDNA_SMU_REG_RESPONSE		0x029aU
#define AMDRDNA_SMU_REG_SOFT_INTERRUPT		0x02c2U
#define AMDRDNA_SMU_REG_SOFT_INTERRUPT_CONTROL	0x02c3U
#define AMDRDNA_SMU_SOFT_INTERRUPT_ID_MASK	0x000000ffU
#define AMDRDNA_SMU_SOFT_INTERRUPT_VALID	0x00000100U
#define AMDRDNA_SMU_SOFT_INTERRUPT_MASKED	0x00000001U
#define AMDRDNA_SMU_SOFT_INTERRUPT_ACKNOWLEDGE	0x00000100U

/*
 * The thermal block's registers (THM window 0): the interrupt enable and
 * control words, the temperature status, the fan controller's three
 * control words, the fan's tachometer period and the thermal status that
 * reports the duty the fan is driven at.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/thm/thm_11_0_2_offset.h
 * (mmTHM_THERMAL_INT_ENA, mmTHM_THERMAL_INT_CTRL,
 * mmCG_MULT_THERMAL_STATUS, mmCG_FDO_CTRL0, mmCG_FDO_CTRL1,
 * mmCG_FDO_CTRL2, mmCG_TACH_STATUS, mmCG_THERMAL_STATUS).
 */
#define AMDRDNA_THM_WINDOW_MAIN			0U
#define AMDRDNA_THM_REG_ALERT_ENABLE		0x000aU
#define AMDRDNA_THM_REG_ALERT_CONTROL		0x000bU
#define AMDRDNA_THM_REG_TEMPERATURE		0x005fU
#define AMDRDNA_THM_REG_FAN_DUTY		0x0067U
#define AMDRDNA_THM_REG_FAN_LIMITS		0x0068U
#define AMDRDNA_THM_REG_FAN_MODE		0x0069U
#define AMDRDNA_THM_REG_FAN_TACHOMETER		0x006bU
#define AMDRDNA_THM_REG_FAN_STATUS		0x006cU

#endif
