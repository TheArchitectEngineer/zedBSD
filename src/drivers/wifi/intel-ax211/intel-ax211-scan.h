/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private API89 passive-scan contract.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * fw/api/scan.h and fw/api/commands.h.  They were cross-checked against
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

#ifndef KERN_DRIVERS_INTEL_AX211_SCAN_H
#define KERN_DRIVERS_INTEL_AX211_SCAN_H

#include <stddef.h>
#include <stdint.h>

#include "intel-ax211-protocol.h"
#include "intel-ax211-runtime.h"

/*
 * The scan commands and notifications of the upper CPU.
 *
 * Source: Linux iwlwifi v6.17 fw/api/commands.h (opcodes) and fw/api/scan.h
 * (layouts); the versions are those the pinned API 89 image lists.  The
 * driver sends the commands in family 1 and receives both notifications,
 * version 1, in family 0: SCAN_START (0x0d, request v17, 1940 bytes),
 * SCAN_CANCEL (0x0e, v1, 8 bytes), SCAN_FINISHED (0x0f), which ends a scan,
 * and SCAN_PASS_FINISHED (0xb5, at most 912 bytes), which reports one pass
 * over the channel list.  The scan setup command of version 5 is sent
 * once at runtime start (intel-ax211-runtime.h).
 */
#define INTEL_AX211_SCAN_FAMILY_CLASSIC_WIDE 1U
#define INTEL_AX211_SCAN_FAMILY_CLASSIC 0U
#define INTEL_AX211_SCAN_REQUEST_OPCODE 0x0dU
#define INTEL_AX211_SCAN_CANCEL_OPCODE 0x0eU
#define INTEL_AX211_SCAN_FINISHED_OPCODE 0x0fU
#define INTEL_AX211_SCAN_PASS_FINISHED_OPCODE 0xb5U

#define INTEL_AX211_SCAN_CONFIG_VERSION 5U
#define INTEL_AX211_SCAN_REQUEST_VERSION 17U
#define INTEL_AX211_SCAN_CANCEL_VERSION 1U
#define INTEL_AX211_SCAN_NOTIFICATION_VERSION 1U

/*
 * The parameters of one scan request, and its sizes.
 *
 * Source: Linux iwlwifi v6.17 fw/api/scan.h (struct iwl_scan_req_umac_v17
 * and its parts).  The driver runs one scan at a time with scan id 0, which
 * the firmware echoes in every scan notification; a notification with
 * another id is not ours.  Precedence 6 is written both as the request's
 * out-of-channel priority and as the scan priority of the general section.
 * The firmware stays 10 TU on a channel when probing and 110 TU when only
 * listening, with an adaptive budget of 300 TU.  The request has room for
 * 67 channel entries; the driver lists at most 51 (14 at 2.4 GHz and 37 at
 * 5 GHz), and the probe template has 512 bytes.  A pass report holds a
 * 16-byte header and up to 112 8-byte channel results, 912 bytes in all.
 * The time limits are zedBSD's: one second for the firmware to acknowledge
 * a command, five seconds for a scan to finish.
 */
#define INTEL_AX211_SCAN_UID 0U
#define INTEL_AX211_SCAN_PRECEDENCE 6U
#define INTEL_AX211_SCAN_PROBING_STAY_TU 10U
#define INTEL_AX211_SCAN_LISTENING_STAY_TU 110U
#define INTEL_AX211_SCAN_ADAPTIVE_BUDGET 300U
#define INTEL_AX211_SCAN_CHANNEL_WIDTH_MHZ 20U
#define INTEL_AX211_SCAN_CHANNEL_LIMIT 51U
#define INTEL_AX211_SCAN_FIRMWARE_CHANNEL_LIMIT 67U
#define INTEL_AX211_SCAN_PROBE_LIMIT 512U
#define INTEL_AX211_SCAN_REQUEST_SIZE 1940U
#define INTEL_AX211_SCAN_ITERATION_NOTIFICATION_SIZE 912U
#define INTEL_AX211_SCAN_CANCEL_SIZE 8U
#define INTEL_AX211_SCAN_ACK_TIMEOUT_US 1000000U
#define INTEL_AX211_SCAN_WATCHDOG_US 5000000U

enum intel_ax211_scan_result {
	INTEL_AX211_SCAN_OK = 0,
	INTEL_AX211_SCAN_INVALID = 1,
	INTEL_AX211_SCAN_UNSUPPORTED = 2,
	INTEL_AX211_SCAN_TRUNCATED = 3,
	INTEL_AX211_SCAN_OVERSIZED = 4,
	INTEL_AX211_SCAN_STALE = 5,
	INTEL_AX211_SCAN_DUPLICATE = 6,
	INTEL_AX211_SCAN_OUT_OF_ORDER = 7,
	INTEL_AX211_SCAN_TIMED_OUT = 8,
	INTEL_AX211_SCAN_COMPLETE = 9,
	INTEL_AX211_SCAN_ABORTED = 10,
	INTEL_AX211_SCAN_FAILED = 11
};

enum intel_ax211_scan_phase {
	INTEL_AX211_SCAN_PHASE_IDLE = 0,
	INTEL_AX211_SCAN_PHASE_WAIT_ACK = 1,
	INTEL_AX211_SCAN_PHASE_RUNNING = 2,
	INTEL_AX211_SCAN_PHASE_TERMINAL = 3
};

enum intel_ax211_scan_event_kind {
	INTEL_AX211_SCAN_EVENT_COMPLETE = 1,
	INTEL_AX211_SCAN_EVENT_ITERATION_COMPLETE = 2
};

struct intel_ax211_scan_profile {
	uint8_t station_address[6];
	uint8_t channel_width_mhz;
	uint8_t channel[INTEL_AX211_SCAN_CHANNEL_LIMIT];
	size_t channel_count;
};

struct intel_ax211_scan_channel_result {
	uint8_t channel;
	uint8_t probe_result;
	uint8_t probe_not_sent;
	uint32_t duration;
};

struct intel_ax211_scan_event {
	enum intel_ax211_scan_event_kind kind;
	uint8_t status;
	uint8_t energy_skip_status;
	uint8_t final_schedule;
	uint8_t last_iteration;
	uint8_t last_channel;
	uint8_t bluetooth_status;
	uint64_t tsf;
	size_t channel_count;
	struct intel_ax211_scan_channel_result
		channel[INTEL_AX211_SCAN_CHANNEL_LIMIT];
};

struct intel_ax211_scan_state {
	uint32_t generation;
	uint64_t acknowledgement_deadline;
	uint64_t scan_deadline;
	uint8_t requested_channels[32U];
	enum intel_ax211_scan_phase phase;
	uint8_t abort_required;
};

int drv_intel_ax211_scan_profile_from_nvm(
	const struct intel_ax211_protocol_nvm *nvm,
	const struct intel_ax211_runtime_mcc *mcc,
	const uint8_t station_address[6],
	struct intel_ax211_scan_profile *profile);
int drv_intel_ax211_scan_api89_validate(
	const struct intel_ax211_protocol_command_table *table);
int drv_intel_ax211_scan_request_encode(
	const struct intel_ax211_scan_profile *profile,
	uint8_t output[INTEL_AX211_SCAN_REQUEST_SIZE]);
int
drv_intel_ax211_scan_abort_encode(uint8_t output[INTEL_AX211_SCAN_CANCEL_SIZE]);

int drv_intel_ax211_scan_begin(
	struct intel_ax211_scan_state *state,
	const struct intel_ax211_protocol_command_table *table,
	const struct intel_ax211_scan_profile *profile, uint32_t generation,
	uint64_t now_us);
int drv_intel_ax211_scan_request_ack(struct intel_ax211_scan_state *state,
				     uint32_t generation, uint64_t now_us);
int drv_intel_ax211_scan_event_accept(
	struct intel_ax211_scan_state *state,
	const struct intel_ax211_protocol_message *message, uint64_t now_us,
	struct intel_ax211_scan_event *event);
int drv_intel_ax211_scan_expire(struct intel_ax211_scan_state *state,
				uint64_t now_us);

#endif
