/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private initialization protocol contract.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * fw/api/commands.h and fw/api/nvm-reg.h.  They were cross-checked against
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

#ifndef KERN_DRIVERS_INTEL_AX211_INIT_H
#define KERN_DRIVERS_INTEL_AX211_INIT_H

#include <stddef.h>
#include <stdint.h>

#include "intel-ax211-protocol.h"

/*
 * The stage-select command, which tells a freshly started firmware whether
 * to stop after reading its NVM or to go straight to runtime.
 *
 * Source: Linux iwlwifi v6.17 fw/api/commands.h (INIT_EXTENDED_CFG_CMD,
 * opcode 3 of the platform family 2) and fw/api/system.h (struct
 * iwl_init_extended_cfg_cmd: one 32-bit word of flags, bit 1 asking for the
 * NVM to be read).  The pinned image lists no version for this command;
 * OpenBSD if_iwx.c at 0f464d413c50 sends the fixed 4-byte version 1 layout,
 * and so does this driver.  The first boot pass (intel-ax211-boot.c) sends
 * it with STORE_READ, then the NVM handoff, waits for INIT_FINISHED, and
 * only then queries the NVM; each command waits for its answer before the
 * next is sent.
 */
#define INTEL_AX211_INIT_FAMILY_PLATFORM 2U
#define INTEL_AX211_INIT_STAGE_SELECT_OPCODE 0x03U
#define INTEL_AX211_INIT_STAGE_SELECT_VERSION 1U
#define INTEL_AX211_INIT_STAGE_SELECT_SIZE 4U
#define INTEL_AX211_INIT_STAGE_SELECT_STORE_READ 0x02U

/*
 * The versions of the two NVM commands, as the pinned image lists them.
 *
 * Source: the command version record of the pinned API 89 image.  The NVM
 * handoff (family 12, opcode 0) is command version 1 with no notification
 * version; the NVM query (family 12, opcode 2) is command version 1.
 */
#define INTEL_AX211_INIT_NVM_HANDOFF_COMMAND_VERSION 1U
#define INTEL_AX211_INIT_NVM_HANDOFF_NOTIFICATION_VERSION 0U
#define INTEL_AX211_INIT_NVM_QUERY_COMMAND_VERSION 1U

/* What the stage-select command asks the firmware to do. */
enum intel_ax211_init_profile {
	INTEL_AX211_INIT_PROFILE_RUNTIME = 0,
	INTEL_AX211_INIT_PROFILE_STORE_READ = 1
};

int drv_intel_ax211_init_api89_validate(
	const struct intel_ax211_protocol_command_table *table);

int drv_intel_ax211_init_stage_select_encode(
	enum intel_ax211_init_profile profile,
	uint8_t output[INTEL_AX211_INIT_STAGE_SELECT_SIZE]);
int drv_intel_ax211_init_stage_select_decode(
	const uint8_t *bytes, size_t length,
	enum intel_ax211_init_profile *profile);

int drv_intel_ax211_init_finished_validate(
	const struct intel_ax211_protocol_message *message,
	uint32_t generation);

#endif
