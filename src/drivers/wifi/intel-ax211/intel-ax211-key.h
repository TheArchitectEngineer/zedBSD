/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private API89 CCMP key contract.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * fw/api/datapath.h and fw/api/commands.h.  They were cross-checked against
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

#ifndef KERN_DRIVERS_INTEL_AX211_KEY_H
#define KERN_DRIVERS_INTEL_AX211_KEY_H

#include <stddef.h>
#include <stdint.h>

#include "intel-ax211-protocol.h"

/*
 * The key command.
 *
 * Source: Linux iwlwifi v6.17 fw/api/datapath.h (SEC_KEY_CMD, opcode 0x18 of
 * the data path family 5, and struct iwl_sec_key_cmd); command version 1 is
 * the one the pinned API 89 image lists.  The command is 80 bytes; the wide
 * header carries 0 in its version byte and the answer has no version.  A
 * CCMP-128 key is 16 bytes (IEEE Std 802.11-2020 12.5.3), and the key index
 * is 0 to 3.
 */
#define INTEL_AX211_KEY_FAMILY 5U
#define INTEL_AX211_KEY_OPCODE 0x18U
#define INTEL_AX211_KEY_COMMAND_VERSION 1U
#define INTEL_AX211_KEY_WIRE_VERSION 0U
#define INTEL_AX211_KEY_RESPONSE_VERSION 0U
#define INTEL_AX211_KEY_COMMAND_SIZE 80U
#define INTEL_AX211_KEY_BYTES 16U
#define INTEL_AX211_KEY_INDEX_LIMIT 4U

enum intel_ax211_key_result {
	INTEL_AX211_KEY_OK = 0,
	INTEL_AX211_KEY_INVALID = 1,
	INTEL_AX211_KEY_UNSUPPORTED = 2,
	INTEL_AX211_KEY_STALE = 3,
	INTEL_AX211_KEY_DUPLICATE = 4,
	INTEL_AX211_KEY_MISSING = 5
};

enum intel_ax211_key_kind {
	INTEL_AX211_KEY_PAIRWISE = 1,
	INTEL_AX211_KEY_GROUP_KEY = 2
};

struct intel_ax211_key_request {
	uint64_t connection_generation;
	uint64_t key_generation;
	uint64_t receive_packet_number;
	uint8_t key[INTEL_AX211_KEY_BYTES];
	enum intel_ax211_key_kind kind;
	uint8_t key_index;
	uint8_t reserved[3];
};

/* This state contains identifiers only; it never retains key bytes. */
struct intel_ax211_key_state {
	uint64_t connection_generation;
	uint64_t staged_pairwise;
	uint64_t staged_group[INTEL_AX211_KEY_INDEX_LIMIT];
	uint64_t active_pairwise;
	uint64_t active_group[INTEL_AX211_KEY_INDEX_LIMIT];
	uint32_t hardware_epoch;
	uint8_t active_group_index;
	uint8_t initialized;
	uint8_t reserved[2];
};

int drv_intel_ax211_key_api89_validate(
	const struct intel_ax211_protocol_command_table *table);
int
drv_intel_ax211_key_add_encode(const struct intel_ax211_key_request *request,
			       uint8_t output[INTEL_AX211_KEY_COMMAND_SIZE]);
int drv_intel_ax211_key_remove_encode(
	uint64_t connection_generation, uint64_t key_generation,
	enum intel_ax211_key_kind kind, uint8_t key_index,
	uint8_t output[INTEL_AX211_KEY_COMMAND_SIZE]);
void drv_intel_ax211_key_command_scrub(
	uint8_t command[INTEL_AX211_KEY_COMMAND_SIZE]);

int drv_intel_ax211_key_state_init(struct intel_ax211_key_state *state,
				   uint32_t hardware_epoch,
				   uint64_t connection_generation);
int drv_intel_ax211_key_state_installed(
	struct intel_ax211_key_state *state,
	const struct intel_ax211_key_request *request, uint32_t hardware_epoch);
int drv_intel_ax211_key_state_activate(struct intel_ax211_key_state *state,
				       uint64_t connection_generation,
				       uint64_t pairwise_generation,
				       uint64_t group_generation,
				       uint32_t hardware_epoch);
int drv_intel_ax211_key_state_removed(struct intel_ax211_key_state *state,
				      uint64_t connection_generation,
				      enum intel_ax211_key_kind kind,
				      uint8_t key_index,
				      uint64_t key_generation,
				      uint32_t hardware_epoch);
int drv_intel_ax211_key_state_rx_generation(
	const struct intel_ax211_key_state *state,
	uint64_t connection_generation, enum intel_ax211_key_kind kind,
	uint8_t key_index, uint32_t hardware_epoch, uint64_t *key_generation);
int drv_intel_ax211_key_state_tx_validate(
	const struct intel_ax211_key_state *state,
	uint64_t connection_generation, uint64_t key_generation,
	uint8_t key_index, uint64_t packet_number, uint32_t hardware_epoch);

#endif
