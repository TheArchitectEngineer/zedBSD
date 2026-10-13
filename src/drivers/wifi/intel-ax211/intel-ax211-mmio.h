/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private MMIO contract.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * iwl-csr.h, iwl-prph.h and iwl-config.h.  They were cross-checked against
 * OpenBSD sys/dev/pci/if_iwxreg.h and if_iwx.c at commit 0f464d413c50.  Each
 * block of values below names the header it was taken from.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#ifndef KERN_DRIVERS_INTEL_AX211_MMIO_H
#define KERN_DRIVERS_INTEL_AX211_MMIO_H

#include <stdint.h>

/*
 * The device profile the MMIO layer accepts.
 *
 * Source: Linux iwlwifi v6.17 iwl-config.h (MAC type codes 0x37 So and 0x43
 * SoF, radio kind 0x10d Garfield Peak) and cfg/ax210.c (the upper-CPU
 * periphery base 0x300000 of the AX210 family).  drv_intel_ax211_mmio_init()
 * refuses any other profile, a radio that runs two bands at once, an
 * integrated part, or another periphery base, because the register
 * sequences in intel-ax211-mmio.c were established for this combination
 * only.  The image loader size is that of the loader record of the pinned
 * firmware image (13944 bytes), and publish_gen3 refuses any other length.
 */
#define INTEL_AX211_MMIO_MAC_SO 0x37U
#define INTEL_AX211_MMIO_MAC_SOF 0x43U
#define INTEL_AX211_MMIO_RF_GF 0x10dU
#define INTEL_AX211_MMIO_UPPER_CPU_PRPH_BASE 0x300000U
#define INTEL_AX211_MMIO_LOADER_IMAGE_SIZE 13944U

enum intel_ax211_mmio_result {
	INTEL_AX211_MMIO_OK = 0,
	INTEL_AX211_MMIO_INVALID = 1,
	INTEL_AX211_MMIO_IO = 2,
	INTEL_AX211_MMIO_TIMEOUT = 3,
	INTEL_AX211_MMIO_CLOCK = 4,
	INTEL_AX211_MMIO_ORDER = 5,
	INTEL_AX211_MMIO_NOT_OWNER = 6
};

enum intel_ax211_mmio_wait {
	INTEL_AX211_MMIO_WAIT_HW_READY = 1,
	INTEL_AX211_MMIO_WAIT_APM_CLOCK = 2,
	INTEL_AX211_MMIO_WAIT_NIC_OWNERSHIP = 3,
	INTEL_AX211_MMIO_WAIT_MASTER_DISABLED = 4
};

struct intel_ax211_mmio_profile {
	uint16_t mac_type;
	uint16_t rf_type;
	uint8_t concurrent_bands;
	uint8_t integrated;
	uint32_t upper_cpu_prph_base;
};

struct intel_ax211_mmio_ops {
	int (*csr_read32)(void *argument, uint32_t offset, uint32_t *value);
	int (*csr_write32)(void *argument, uint32_t offset, uint32_t value);
	int (*prph_read32)(void *argument, uint32_t address, uint32_t *value);
	int (*prph_write32)(void *argument, uint32_t address, uint32_t value);
	int (*delay_us)(void *argument, uint32_t duration_us);
	int (*clock_us)(void *argument, uint64_t *time_us);
	void (*trace_deadline)(void *argument, enum intel_ax211_mmio_wait wait,
			       uint64_t start_us, uint64_t deadline_us);
};

struct intel_ax211_mmio_boot {
	uint64_t context_address;
	uint64_t iml_address;
	uint32_t iml_size;
};

struct intel_ax211_mmio {
	const struct intel_ax211_mmio_ops *ops;
	void *argument;
	struct intel_ax211_mmio_profile profile;
	unsigned int wake_hold_depth;
	int prepared;
	int reset_done;
	int apm_ready;
	int master_disable_timed_out;
};

int drv_intel_ax211_mmio_init(struct intel_ax211_mmio *mmio,
			      const struct intel_ax211_mmio_ops *ops,
			      void *argument,
			      const struct intel_ax211_mmio_profile *profile);
int drv_intel_ax211_mmio_take_ownership(struct intel_ax211_mmio *mmio);
int drv_intel_ax211_mmio_sw_reset(struct intel_ax211_mmio *mmio);
int drv_intel_ax211_mmio_apm_init(struct intel_ax211_mmio *mmio);
/* Success guarantees a STOP_MASTER request, APM stop, and settled reset. */
int drv_intel_ax211_mmio_stop(struct intel_ax211_mmio *mmio);
int drv_intel_ax211_mmio_wake_hold(struct intel_ax211_mmio *mmio);
int drv_intel_ax211_mmio_wake_release(struct intel_ax211_mmio *mmio);
int drv_intel_ax211_mmio_read_mac(struct intel_ax211_mmio *mmio,
				  uint8_t mac_address[6]);
int drv_intel_ax211_mmio_prph_read32(struct intel_ax211_mmio *mmio,
				     uint32_t address, uint32_t *value);
int drv_intel_ax211_mmio_prph_write32(struct intel_ax211_mmio *mmio,
				      uint32_t address, uint32_t value);
int drv_intel_ax211_mmio_publish_gen3(struct intel_ax211_mmio *mmio,
				      const struct intel_ax211_mmio_boot *boot);

#endif
