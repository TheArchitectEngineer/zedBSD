/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private API89 transmit contract.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * fw/api/tx.h and fw/api/commands.h.  They were cross-checked against OpenBSD
 * sys/dev/pci/if_iwxreg.h and if_iwx.c at commit 0f464d413c50.  Each block of
 * values below names the header it was taken from.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#ifndef KERN_DRIVERS_INTEL_AX211_TX_H
#define KERN_DRIVERS_INTEL_AX211_TX_H

#include <stddef.h>
#include <stdint.h>

#include "intel-ax211-protocol.h"

/*
 * The transmit command and the limits of a frame and of a data queue.
 *
 * Source: Linux iwlwifi v6.17 fw/api/commands.h (TX_CMD, opcode 0x1c) and
 * fw/api/tx.h; the versions are those of the pinned API 89 image (command
 * version 10, completion version 7).  The command is sent in family 1 on the
 * data queue, not on the command ring.  Its fixed part is 28 bytes and the
 * MAC header after it at most 36 bytes, with up to 2 bytes of padding.  A
 * frame body may not exceed 2304 bytes (the 802.11 MSDU limit).  A data
 * queue has 256 descriptors; the firmware's queue numbers run from 1 to 511;
 * its scheduler sequence numbers are 16 bits wide.
 */
#define INTEL_AX211_TX_FAMILY 1U
#define INTEL_AX211_TX_OPCODE 0x1cU
#define INTEL_AX211_TX_COMMAND_VERSION 10U
#define INTEL_AX211_TX_NOTIFICATION_VERSION 7U

#define INTEL_AX211_TX_COMMAND_FIXED_SIZE 28U
#define INTEL_AX211_TX_MAC_HEADER_MAX 36U
#define INTEL_AX211_TX_COMMAND_BUFFER_SIZE                                     \
	(INTEL_AX211_TX_COMMAND_FIXED_SIZE + INTEL_AX211_TX_MAC_HEADER_MAX + 2U)
#define INTEL_AX211_TX_FRAME_MAX 2304U
#define INTEL_AX211_TX_QUEUE_SLOTS 256U
#define INTEL_AX211_TX_QUEUE_MIN 1U
#define INTEL_AX211_TX_QUEUE_MAX 511U
#define INTEL_AX211_TX_SCHEDULER_SEQUENCE_LIMIT 65536U

enum intel_ax211_tx_result {
	INTEL_AX211_TX_OK = 0,
	INTEL_AX211_TX_INVALID = 1,
	INTEL_AX211_TX_UNSUPPORTED = 2,
	INTEL_AX211_TX_TRUNCATED = 3,
	INTEL_AX211_TX_OVERSIZED = 4,
	INTEL_AX211_TX_STALE = 5,
	INTEL_AX211_TX_FAILED = 6,
	INTEL_AX211_TX_BUFFER_TOO_SMALL = 7
};

enum intel_ax211_tx_frame_class {
	INTEL_AX211_TX_FRAME_MANAGEMENT = 1,
	INTEL_AX211_TX_FRAME_EAPOL = 2,
	INTEL_AX211_TX_FRAME_DATA = 3
};

struct intel_ax211_tx_request {
	uint64_t connection_generation;
	uint64_t cookie;
	uint64_t key_generation;
	uint64_t packet_number;
	const uint8_t *frame;
	size_t length;
	enum intel_ax211_tx_frame_class frame_class;
	uint8_t encrypted;
	uint8_t key_index;
	uint8_t band_5ghz;
	uint8_t reserved[5];
};

/*
 * command contains the Gen3 v10 fixed body, the copied 802.11 header, and
 * optional alignment padding.  payload_offset/payload_length select the
 * caller-owned frame bytes which form the following TFD transfer buffer.
 * For a protected CCMP frame the common WLAN boundary supplies and validates
 * the eight-byte IV, but Intel firmware inserts that IV and the MIC from its
 * installed key state.  The prepared transfer therefore skips the supplied
 * IV and frame_length is the firmware input length with that IV removed.
 */
struct intel_ax211_tx_prepared {
	uint8_t command[INTEL_AX211_TX_COMMAND_BUFFER_SIZE];
	size_t command_length;
	size_t payload_offset;
	size_t payload_length;
	uint64_t connection_generation;
	uint64_t cookie;
	uint64_t key_generation;
	uint64_t packet_number;
	uint16_t frame_length;
	uint8_t encrypted;
	uint8_t key_index;
};

struct intel_ax211_tx_completion {
	uint32_t hardware_generation;
	uint32_t scheduler_sequence;
	uint16_t queue;
	uint16_t sequence_control;
	uint16_t byte_count;
	uint8_t index;
	uint8_t acknowledged;
	uint8_t rts_retries;
	uint8_t frame_retries;
};

int drv_intel_ax211_tx_api89_validate(
	const struct intel_ax211_protocol_command_table *table);
int drv_intel_ax211_tx_prepare(const struct intel_ax211_tx_request *request,
			       struct intel_ax211_tx_prepared *prepared);
int drv_intel_ax211_tx_completion_decode(
	const struct intel_ax211_protocol_message *message,
	uint32_t hardware_generation, uint16_t expected_queue,
	uint8_t expected_index, struct intel_ax211_tx_completion *completion);

#endif
