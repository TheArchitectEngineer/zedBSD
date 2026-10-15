/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private API89 passive-scan codecs and state.
 */

#include "intel-ax211-scan.h"
#include <kern/kcrt.h>

/*
 * The layout of the scan request (version 17), as byte offsets.
 *
 * drv_intel_ax211_scan_request_encode() fills it as follows:
 * - bytes 0 and 4: scan id and out-of-channel priority;
 * - the general section from byte 8: flags (16 bits) at +0, the two probing
 *   stays at +4 and +5, the adaptive default stays for 2.4 GHz, 5 GHz and
 *   the social channels at +6, +7 and +8 (2, 8 and 10), the adaptive budget
 *   at +10, the scan priority at +28 and the two listening stays at +32 and
 *   +33;
 * - the channel section from byte 44: its flags, the channel count, and the
 *   two access-point count overrides (10 and 2) at +2 and +3;
 * - one 8-byte entry per channel from byte 48: a 32-bit flag word whose bit
 *   30 holds the band (1 for 2.4 GHz, 0 for 5 GHz), the channel number at
 *   +4, the power spectral density limit 0x80 (none known) at +5 and the
 *   repetition count 1 at +6;
 * - the schedule from byte 584, of which the first plan runs once (its
 *   repetition count at +2 is 1);
 * - the probe template from byte 596.
 *
 * Values referenced: Linux v6.17
 * drivers/net/wireless/intel/iwlwifi/fw/api/scan.h (struct
 * iwl_scan_req_umac_v17, struct iwl_scan_general_params_v11, struct
 * iwl_scan_channel_params_v7, struct iwl_scan_channel_cfg_umac, struct
 * iwl_scan_periodic_parms_v1 and struct iwl_scan_probe_params_v4).
 */
#define AX211_SCAN_GENERAL_OFFSET 8U
#define AX211_SCAN_CHANNEL_OFFSET 44U
#define AX211_SCAN_CHANNEL_CONFIG_OFFSET 48U
#define AX211_SCAN_CHANNEL_CONFIG_SIZE 8U
#define AX211_SCAN_PERIODIC_OFFSET 584U
#define AX211_SCAN_PROBE_OFFSET 596U


/*
 * The flag values of the scan request and the outcome codes of its
 * completion.
 *
 * The general flags set are REPORT_ALL (bit 1: pass every beacon and probe
 * response to the host, not only matches), REPORT_EACH_PASS (bit 2: send the
 * pass-finished notice), ADAPTIVE_STAY (bit 7: use the adaptive stay times of
 * the general section) and LISTEN_ONLY (bit 11: scan every channel passively).
 * CHANNELS_IN_GIVEN_ORDER (bit 5 of the channel section's flags) makes the
 * firmware visit the channels in the listed order.  A completion reports 1
 * when the scan finished and 2 when it was cancelled; anything else is a
 * failure.
 *
 * Values referenced: Linux v6.17
 * drivers/net/wireless/intel/iwlwifi/fw/api/scan.h (enum
 * iwl_umac_scan_general_flags_v2, enum iwl_scan_channel_flags and the scan
 * completion status).
 */
#define AX211_SCAN_FLAG_REPORT_ALL 0x02U
#define AX211_SCAN_FLAG_REPORT_EACH_PASS 0x04U
#define AX211_SCAN_FLAG_ADAPTIVE_STAY 0x80U
#define AX211_SCAN_FLAG_LISTEN_ONLY 0x0800U
#define AX211_SCAN_CHANNELS_IN_GIVEN_ORDER 0x20U
#define AX211_SCAN_24GHZ_BAND_FLAG 0x40000000U
#define AX211_SCAN_OUTCOME_FINISHED 1U
#define AX211_SCAN_OUTCOME_CANCELLED 2U

static int ax211_scan_station_valid(const uint8_t address[6]);
static void ax211_scan_profile_add(struct intel_ax211_scan_profile *profile, const struct intel_ax211_protocol_channel *candidate, const struct intel_ax211_runtime_mcc *mcc, size_t regulatory_index, int firmware_regulatory);
static int ax211_scan_profile_valid(const struct intel_ax211_scan_profile *profile);
static int ax211_scan_required_version( const struct intel_ax211_protocol_command_table *table, uint8_t opcode, uint8_t command_version);
static void ax211_scan_put_le32(uint8_t *bytes, uint32_t value);
static void ax211_scan_put_le16(uint8_t *bytes, uint16_t value);
static void ax211_scan_probe_encode(const struct intel_ax211_scan_profile *profile, uint8_t *probe);
static int ax211_scan_event_header(const struct intel_ax211_scan_state *state, const struct intel_ax211_protocol_message *message, uint64_t now_us);
static int ax211_scan_complete_decode(const struct intel_ax211_protocol_message *message, struct intel_ax211_scan_event *event);
static uint32_t ax211_scan_get_le32(const uint8_t *bytes);
static int ax211_scan_status_result(uint8_t status);
static int ax211_scan_iteration_decode(const struct intel_ax211_protocol_message *message, struct intel_ax211_scan_event *event);

/*
 * Implements the drv intel ax211 scan profile from nvm operation.
 */
int
drv_intel_ax211_scan_profile_from_nvm(
	const struct intel_ax211_protocol_nvm *nvm,
	const struct intel_ax211_runtime_mcc *mcc,
	const uint8_t station_address[6],
	struct intel_ax211_scan_profile *profile)
{
	struct intel_ax211_scan_profile parsed;
	size_t index;

	/* Checks the ax211 scan station valid result. */
	if (nvm == NULL || station_address == NULL || profile == NULL ||
	    !nvm->band_24_enabled ||
	    nvm->channel_24ghz_count >
		    INTEL_AX211_PROTOCOL_24GHZ_CHANNEL_LIMIT ||
	    nvm->channel_5ghz_count > INTEL_AX211_PROTOCOL_5GHZ_CHANNEL_LIMIT ||
	    (nvm->firmware_regulatory &&
	     (mcc == NULL || mcc->status > INTEL_AX211_RUNTIME_COUNTRY_STATUS_MAX ||
	      mcc->channel_count == 0U ||
	      mcc->channel_count > INTEL_AX211_RUNTIME_COUNTRY_CHANNEL_LIMIT)) ||
	    !ax211_scan_station_valid(station_address)) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_INVALID;
	}
	kern_memset(&parsed, 0, sizeof(parsed));
	kern_memcpy(parsed.station_address, station_address, 6U);
	parsed.channel_width_mhz = INTEL_AX211_SCAN_CHANNEL_WIDTH_MHZ;
	/* Process each remaining element. */
	for (index = 0U; index < nvm->channel_24ghz_count; index++) {
		ax211_scan_profile_add(&parsed, &nvm->channel_24ghz[index], mcc,
				       index, nvm->firmware_regulatory);
	}

	/* Handles the nvm condition. */
	if (nvm->band_52_enabled) {
		/* Process each remaining element. */
		for (index = 0U; index < nvm->channel_5ghz_count; index++) {
			ax211_scan_profile_add(
				&parsed, &nvm->channel_5ghz[index], mcc,
				INTEL_AX211_PROTOCOL_24GHZ_CHANNEL_LIMIT +
					index,
				nvm->firmware_regulatory);
		}
	}

	/* Checks the ax211 scan profile valid result. */
	if (!ax211_scan_profile_valid(&parsed))
		return INTEL_AX211_SCAN_UNSUPPORTED;
	*profile = parsed;
	/* Returns the computed result. */
	return INTEL_AX211_SCAN_OK;
}

/*
 * Implements the drv intel ax211 scan api89 validate operation.
 */
int
drv_intel_ax211_scan_api89_validate(
	const struct intel_ax211_protocol_command_table *table)
{
	int error;
	int result;

	/* Handles the table availability. */
	if (table == NULL)
		return INTEL_AX211_SCAN_INVALID;

	/* Checks the drv intel ax211 protocol command table validate api89 result. */
	if (drv_intel_ax211_protocol_command_table_validate_api89(table) !=
	    INTEL_AX211_PROTOCOL_OK) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_UNSUPPORTED;
	}

	/* Checks the operation result. */
	result = ax211_scan_required_version(
		table, INTEL_AX211_PROTOCOL_SCAN_SETUP_OPCODE,
		INTEL_AX211_SCAN_CONFIG_VERSION);
	if (result != INTEL_AX211_SCAN_OK)
		return result;

	/* Checks the operation result. */
	result = ax211_scan_required_version(table,
					     INTEL_AX211_SCAN_REQUEST_OPCODE,
					     INTEL_AX211_SCAN_REQUEST_VERSION);
	if (result != INTEL_AX211_SCAN_OK)
		return result;

	/* Obtains the ax211 scan required version result. */
	error = ax211_scan_required_version(
		table, INTEL_AX211_SCAN_CANCEL_OPCODE,
		INTEL_AX211_SCAN_CANCEL_VERSION);

	/* Returns the computed result. */
	return error;
}

/*
 * Implements the drv intel ax211 scan request encode operation.
 */
int
drv_intel_ax211_scan_request_encode(
	const struct intel_ax211_scan_profile *profile,
	uint8_t output[INTEL_AX211_SCAN_REQUEST_SIZE])
{
	uint8_t *channel;
	uint16_t flags;
	size_t index;

	/* Checks the ax211 scan profile valid result. */
	if (!ax211_scan_profile_valid(profile) || output == NULL)
		return INTEL_AX211_SCAN_INVALID;
	kern_memset(output, 0, INTEL_AX211_SCAN_REQUEST_SIZE);
	ax211_scan_put_le32(output, INTEL_AX211_SCAN_UID);
	ax211_scan_put_le32(output + 4U, INTEL_AX211_SCAN_PRECEDENCE);
	flags = AX211_SCAN_FLAG_REPORT_ALL | AX211_SCAN_FLAG_REPORT_EACH_PASS |
		AX211_SCAN_FLAG_ADAPTIVE_STAY | AX211_SCAN_FLAG_LISTEN_ONLY;
	ax211_scan_put_le16(output + AX211_SCAN_GENERAL_OFFSET, flags);
	output[AX211_SCAN_GENERAL_OFFSET + 4U] = INTEL_AX211_SCAN_PROBING_STAY_TU;
	output[AX211_SCAN_GENERAL_OFFSET + 5U] = INTEL_AX211_SCAN_PROBING_STAY_TU;
	output[AX211_SCAN_GENERAL_OFFSET + 6U] = 2U;
	output[AX211_SCAN_GENERAL_OFFSET + 7U] = 8U;
	output[AX211_SCAN_GENERAL_OFFSET + 8U] = 10U;
	ax211_scan_put_le16(output + AX211_SCAN_GENERAL_OFFSET + 10U,
			    INTEL_AX211_SCAN_ADAPTIVE_BUDGET);
	ax211_scan_put_le32(output + AX211_SCAN_GENERAL_OFFSET + 28U,
			    INTEL_AX211_SCAN_PRECEDENCE);
	output[AX211_SCAN_GENERAL_OFFSET + 32U] =
		INTEL_AX211_SCAN_LISTENING_STAY_TU;
	output[AX211_SCAN_GENERAL_OFFSET + 33U] =
		INTEL_AX211_SCAN_LISTENING_STAY_TU;

	output[AX211_SCAN_CHANNEL_OFFSET] = AX211_SCAN_CHANNELS_IN_GIVEN_ORDER;
	output[AX211_SCAN_CHANNEL_OFFSET + 1U] =
		(uint8_t)profile->channel_count;
	output[AX211_SCAN_CHANNEL_OFFSET + 2U] = 10U;
	output[AX211_SCAN_CHANNEL_OFFSET + 3U] = 2U;
	/* Process each remaining element. */
	for (index = 0U; index < profile->channel_count; index++) {
		channel = output + AX211_SCAN_CHANNEL_CONFIG_OFFSET +
			  index * AX211_SCAN_CHANNEL_CONFIG_SIZE;
		ax211_scan_put_le32(channel,
				    profile->channel[index] <= 14U
					    ? AX211_SCAN_24GHZ_BAND_FLAG
					    : 0U);
		channel[4U] = profile->channel[index];
		channel[5U] = 0x80U;
		channel[6U] = 1U;
	}

	output[AX211_SCAN_PERIODIC_OFFSET + 2U] = 1U;
	ax211_scan_probe_encode(profile, output + AX211_SCAN_PROBE_OFFSET);

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_OK;
}

/*
 * Implements the drv intel ax211 scan abort encode operation.
 */
int
drv_intel_ax211_scan_abort_encode(
	uint8_t output[INTEL_AX211_SCAN_CANCEL_SIZE])
{
	/* Handles the output availability. */
	if (output == NULL)
		return INTEL_AX211_SCAN_INVALID;
	kern_memset(output, 0, INTEL_AX211_SCAN_CANCEL_SIZE);

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_OK;
}

/*
 * Implements the drv intel ax211 scan begin operation.
 */
int
drv_intel_ax211_scan_begin(
	struct intel_ax211_scan_state *state,
	const struct intel_ax211_protocol_command_table *table,
	const struct intel_ax211_scan_profile *profile,
	uint32_t generation,
	uint64_t now_us)
{
	struct intel_ax211_scan_state started;
	size_t index;
	int result;

	/* Checks the ax211 scan profile valid result. */
	if (state == NULL || !ax211_scan_profile_valid(profile) ||
	    generation == 0U ||
	    now_us > UINT64_MAX - INTEL_AX211_SCAN_WATCHDOG_US) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_INVALID;
	}

	/* Checks the operation result. */
	result = drv_intel_ax211_scan_api89_validate(table);
	if (result != INTEL_AX211_SCAN_OK)
		return result;
	kern_memset(&started, 0, sizeof(started));
	started.generation = generation;
	started.acknowledgement_deadline =
		now_us + INTEL_AX211_SCAN_ACK_TIMEOUT_US;
	started.scan_deadline = now_us + INTEL_AX211_SCAN_WATCHDOG_US;
	started.phase = INTEL_AX211_SCAN_PHASE_WAIT_ACK;
	/* Process each remaining element. */
	for (index = 0U; index < profile->channel_count; index++) {
		started.requested_channels[profile->channel[index] / 8U] |=
			(uint8_t)(UINT8_C(1) << (profile->channel[index] % 8U));
	}

	*state = started;
	/* Returns the computed result. */
	return INTEL_AX211_SCAN_OK;
}

/*
 * Implements the drv intel ax211 scan request ack operation.
 */
int
drv_intel_ax211_scan_request_ack(
	struct intel_ax211_scan_state *state,
	uint32_t generation,
	uint64_t now_us)
{
	/* Handles the state availability. */
	if (state == NULL || generation == 0U)
		return INTEL_AX211_SCAN_INVALID;

	/* Handles the generation condition. */
	if (generation != state->generation)
		return INTEL_AX211_SCAN_STALE;

	/* Handles the state condition. */
	if (state->phase == INTEL_AX211_SCAN_PHASE_RUNNING ||
	    state->phase == INTEL_AX211_SCAN_PHASE_TERMINAL) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_DUPLICATE;
	}

	/* Handles the state condition. */
	if (state->phase != INTEL_AX211_SCAN_PHASE_WAIT_ACK)
		return INTEL_AX211_SCAN_OUT_OF_ORDER;

	/* Handles the now us condition. */
	if (now_us >= state->acknowledgement_deadline) {
		state->phase = INTEL_AX211_SCAN_PHASE_TERMINAL;

		/* Returns the computed result. */
		return INTEL_AX211_SCAN_TIMED_OUT;
	}

	state->phase = INTEL_AX211_SCAN_PHASE_RUNNING;

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_OK;
}

/*
 * Implements the drv intel ax211 scan event accept operation.
 */
int
drv_intel_ax211_scan_event_accept(
	struct intel_ax211_scan_state *state,
	const struct intel_ax211_protocol_message *message,
	uint64_t now_us,
	struct intel_ax211_scan_event *event)
{
	int result;

	/* Handles the state availability. */
	if (state == NULL || event == NULL)
		return INTEL_AX211_SCAN_INVALID;

	/* Checks the operation result. */
	result = ax211_scan_event_header(state, message, now_us);
	if (result == INTEL_AX211_SCAN_TIMED_OUT) {
		state->phase = INTEL_AX211_SCAN_PHASE_TERMINAL;
		state->abort_required = 1U;

		/* Failed. */
		return result;
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_SCAN_OK)
		return result;

	/* Handles the message condition. */
	if (message->opcode == INTEL_AX211_SCAN_FINISHED_OPCODE)
		result = ax211_scan_complete_decode(message, event);
	else if (message->opcode == INTEL_AX211_SCAN_PASS_FINISHED_OPCODE)
		result = ax211_scan_iteration_decode(message, event);
	else {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_UNSUPPORTED;
	}

	/* Checks the operation status. */
	if (result == INTEL_AX211_SCAN_COMPLETE ||
	    result == INTEL_AX211_SCAN_ABORTED ||
	    result == INTEL_AX211_SCAN_FAILED)
		state->phase = INTEL_AX211_SCAN_PHASE_TERMINAL;

	/* Returns the computed result. */
	return result;
}

/*
 * Implements the drv intel ax211 scan expire operation.
 */
int
drv_intel_ax211_scan_expire(
	struct intel_ax211_scan_state *state,
	uint64_t now_us)
{
	/* Handles the state availability. */
	if (state == NULL || state->generation == 0U)
		return INTEL_AX211_SCAN_INVALID;

	/* Handles the state condition. */
	if (state->phase == INTEL_AX211_SCAN_PHASE_TERMINAL)
		return INTEL_AX211_SCAN_DUPLICATE;

	/* Handles the state condition. */
	if (state->phase == INTEL_AX211_SCAN_PHASE_WAIT_ACK) {
		/* Handles the now us condition. */
		if (now_us < state->acknowledgement_deadline)
			return INTEL_AX211_SCAN_OK;
		state->phase = INTEL_AX211_SCAN_PHASE_TERMINAL;

		/* Returns the computed result. */
		return INTEL_AX211_SCAN_TIMED_OUT;
	}

	/* Handles the state condition. */
	if (state->phase == INTEL_AX211_SCAN_PHASE_RUNNING) {
		/* Handles the now us condition. */
		if (now_us < state->scan_deadline)
			return INTEL_AX211_SCAN_OK;
		state->phase = INTEL_AX211_SCAN_PHASE_TERMINAL;
		state->abort_required = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_SCAN_TIMED_OUT;
	}

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_OUT_OF_ORDER;
}

/* Supports the ax211 scan station valid operation. */
static int
ax211_scan_station_valid(
	const uint8_t address[6])
{
	size_t index;
	int all_zero;
	int all_ones;

	/* Handles the address availability. */
	if (address == NULL || (address[0] & 1U) != 0U)
		return 0;
	all_zero = 1;
	all_ones = 1;
	/* Process each remaining element. */
	for (index = 0U; index < 6U; index++) {
		/* Handles the address condition. */
		if (address[index] != 0U)
			all_zero = 0;

		/* Handles the address condition. */
		if (address[index] != 0xffU)
			all_ones = 0;
	}

	/* Returns the computed result. */
	return !all_zero && !all_ones;
}

/* Supports the ax211 scan profile add operation. */
static void
ax211_scan_profile_add(
	struct intel_ax211_scan_profile *profile,
	const struct intel_ax211_protocol_channel *candidate,
	const struct intel_ax211_runtime_mcc *mcc,
	size_t regulatory_index,
	int firmware_regulatory)
{
	/* Handles the candidate condition. */
	if (candidate->number == 0U ||
	    profile->channel_count >= INTEL_AX211_SCAN_CHANNEL_LIMIT) {
		/* Returns the computed result. */
		return;
	}

	/* Handles the lar enabled condition. */
	if (firmware_regulatory) {
		/* Handles the mcc availability. */
		if (mcc == NULL || regulatory_index >= mcc->channel_count ||
		    (mcc->channel[regulatory_index] &
		     INTEL_AX211_PROTOCOL_CHANNEL_FLAG_USABLE) == 0U) {
			/* Returns the computed result. */
			return;
		}
	} else if (!candidate->valid) {
		/* Returns the computed result. */
		return;
	}

	profile->channel[profile->channel_count++] = candidate->number;
}

/* Supports the ax211 scan profile valid operation. */
static int
ax211_scan_profile_valid(
	const struct intel_ax211_scan_profile *profile)
{
	uint8_t channel;
	size_t index, earlier;

	/* Checks the ax211 scan station valid result. */
	if (profile == NULL ||
	    !ax211_scan_station_valid(profile->station_address) ||
	    profile->channel_width_mhz != INTEL_AX211_SCAN_CHANNEL_WIDTH_MHZ ||
	    profile->channel_count == 0U ||
	    profile->channel_count > INTEL_AX211_SCAN_CHANNEL_LIMIT) {
		/* Succeeded. */
		return 0;
	}
	/* Process each remaining element. */
	for (index = 0U; index < profile->channel_count; index++) {
		/* Handles the channel condition. */
		channel = profile->channel[index];
		if (!((channel >= 1U && channel <= 14U) ||
		      (channel >= 36U && channel <= 144U &&
		       ((channel - 36U) % 4U) == 0U) ||
		      (channel >= 149U && channel <= 181U &&
		       ((channel - 149U) % 4U) == 0U))) {
			/* Succeeded. */
			return 0;
		}
		/* Process each remaining element. */
		for (earlier = 0U; earlier < index; earlier++) {
			/* Handles the profile condition. */
			if (profile->channel[earlier] == channel)
				return 0;
		}
	}

	/* Reports operation failure. */
	return 1;
}

/* Supports the ax211 scan required version operation. */
static int
ax211_scan_required_version(
	const struct intel_ax211_protocol_command_table *table,
	uint8_t opcode,
	uint8_t command_version)
{
	struct intel_ax211_protocol_command_version version;
	int result;

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_version_lookup(
		table, INTEL_AX211_SCAN_FAMILY_CLASSIC_WIDE, opcode, &version);
	if (result != INTEL_AX211_PROTOCOL_OK ||
	    version.command_version != command_version ||
	    version.notification_version != 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_UNSUPPORTED;
	}

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_OK;
}

/* Supports the ax211 scan put le32 operation. */
static void
ax211_scan_put_le32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

/* Supports the ax211 scan put le16 operation. */
static void
ax211_scan_put_le16(
	uint8_t *bytes,
	uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

/* Supports the ax211 scan probe encode operation. */
static void
ax211_scan_probe_encode(
	const struct intel_ax211_scan_profile *profile,
	uint8_t *probe)
{
	static const uint8_t rates[12] = {2U,  4U,  11U, 22U, 12U, 18U,
					  24U, 36U, 48U, 72U, 96U, 108U};
	uint8_t *frame;
	size_t offset;

	/*
	 * Segment descriptors precede the fixed 512-byte frame area.  This
	 * first profile is forced passive, so firmware never transmits this
	 * frame.  Keep a valid legacy 11g probe body here; a later active-scan
	 * profile must add its negotiated HT/HE information.
	 *
	 * The descriptors give an offset and a length for the MAC header, for
	 * each band part and for the common part.  Here the MAC header part is
	 * at offset 0 with 26 bytes (the 24-byte header and the empty SSID
	 * element), the first band part at offset 26 with 19 bytes (both rate
	 * elements and the DS parameter element), the other band parts are
	 * empty and the common part starts at offset 45 with no bytes.  The
	 * frame has frame control 0x40 (a probe request), broadcast receiver
	 * and BSSID, a zero-length SSID element (element 0), the supported
	 * rates element (1) with the eight rates 1, 2, 5.5, 11, 6, 9, 12 and
	 * 18 Mbit/s, the extended rates element (50) with 24, 36, 48 and 54
	 * Mbit/s, all in units of 500 kbit/s, and the DS parameter element (3)
	 * whose channel byte the firmware fills.
	 *
	 * Values referenced: Linux v6.17
	 * drivers/net/wireless/intel/iwlwifi/fw/api/scan.h (struct
	 * iwl_scan_probe_req) and IEEE Std 802.11-2020 (the probe request
	 * frame and its elements).
	 *
	 * Procedure referenced: OpenBSD 0f464d413c50 sys/dev/pci/if_iwx.c
	 * (iwx_fill_probe_req, the legacy 11g probe body).
	 */
	ax211_scan_put_le16(probe, 0U);
	ax211_scan_put_le16(probe + 2U, 26U);
	ax211_scan_put_le16(probe + 4U, 26U);
	ax211_scan_put_le16(probe + 6U, 19U);
	ax211_scan_put_le16(probe + 16U, 45U);
	ax211_scan_put_le16(probe + 18U, 0U);

	/* Builds a broadcast probe request with the basic rate elements. */
	frame = probe + 20U;
	frame[0] = 0x40U;
	kern_memset(frame + 4U, 0xff, 6U);
	kern_memcpy(frame + 10U, profile->station_address, 6U);
	kern_memset(frame + 16U, 0xff, 6U);
	frame[24U] = 0U;
	frame[25U] = 0U;
	offset = 26U;
	frame[offset++] = 1U;
	frame[offset++] = 8U;
	kern_memcpy(frame + offset, rates, 8U);
	offset += 8U;
	frame[offset++] = 50U;
	frame[offset++] = 4U;
	kern_memcpy(frame + offset, rates + 8U, 4U);
	offset += 4U;
	frame[offset++] = 3U;
	frame[offset++] = 1U;
	frame[offset] = 0U;
}

/* Supports the ax211 scan event header operation. */
static int
ax211_scan_event_header(
	const struct intel_ax211_scan_state *state,
	const struct intel_ax211_protocol_message *message,
	uint64_t now_us)
{
	/* Handles the message availability. */
	if (message == NULL || message->generation == 0U)
		return INTEL_AX211_SCAN_INVALID;

	/* Handles the message condition. */
	if (message->generation != state->generation)
		return INTEL_AX211_SCAN_STALE;

	/* Handles the state condition. */
	if (state->phase == INTEL_AX211_SCAN_PHASE_TERMINAL)
		return INTEL_AX211_SCAN_DUPLICATE;

	/* Handles the state condition. */
	if (state->phase != INTEL_AX211_SCAN_PHASE_RUNNING)
		return INTEL_AX211_SCAN_OUT_OF_ORDER;

	/* Handles the now us condition. */
	if (now_us >= state->scan_deadline)
		return INTEL_AX211_SCAN_TIMED_OUT;

	/* Handles the message condition. */
	if (message->group != INTEL_AX211_SCAN_FAMILY_CLASSIC ||
	    message->version != INTEL_AX211_SCAN_NOTIFICATION_VERSION) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_UNSUPPORTED;
	}

	/* Checks the operation status. */
	if ((message->flags & INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK) != 0U)
		return INTEL_AX211_SCAN_FAILED;

	/* Handles the payload availability. */
	if (message->payload == NULL)
		return INTEL_AX211_SCAN_INVALID;

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_OK;
}

/* Supports the ax211 scan complete decode operation. */
static int
ax211_scan_complete_decode(
	const struct intel_ax211_protocol_message *message,
	struct intel_ax211_scan_event *event)
{
	int error;
	const uint8_t *bytes;

	/* Handles the message condition. */
	if (message->payload_length < 16U)
		return INTEL_AX211_SCAN_TRUNCATED;

	/* Handles the message condition. */
	if (message->payload_length > 16U)
		return INTEL_AX211_SCAN_OVERSIZED;

	/* Checks the ax211 scan get le32 result. */
	bytes = message->payload;
	if (ax211_scan_get_le32(bytes) != INTEL_AX211_SCAN_UID)
		return INTEL_AX211_SCAN_OUT_OF_ORDER;
	kern_memset(event, 0, sizeof(*event));
	event->kind = INTEL_AX211_SCAN_EVENT_COMPLETE;
	event->final_schedule = bytes[4U];
	event->last_iteration = bytes[5U];
	event->status = bytes[6U];
	event->energy_skip_status = bytes[7U];

	/*
	 * Scheduling, iteration, EBS, elapsed-time, and reserved fields are
	 * firmware reports.  They do not narrow the v1 completion contract.
	 */
	error = ax211_scan_status_result(event->status);

	/* Returns the computed result. */
	return error;
}

/* Supports the ax211 scan get le32 operation. */
static uint32_t
ax211_scan_get_le32(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	       ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Supports the ax211 scan status result operation. */
static int
ax211_scan_status_result(
	uint8_t status)
{
	/* Checks the operation status. */
	if (status == AX211_SCAN_OUTCOME_FINISHED)
		return INTEL_AX211_SCAN_COMPLETE;

	/* Checks the operation status. */
	if (status == AX211_SCAN_OUTCOME_CANCELLED)
		return INTEL_AX211_SCAN_ABORTED;

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_FAILED;
}

/* Supports the ax211 scan iteration decode operation. */
static int
ax211_scan_iteration_decode(
	const struct intel_ax211_protocol_message *message,
	struct intel_ax211_scan_event *event)
{
	const uint8_t *result;
	const uint8_t *bytes;
	size_t available;
	size_t count;
	size_t expected;
	size_t index;

	/* Handles the message condition. */
	if (message->payload_length < 16U)
		return INTEL_AX211_SCAN_TRUNCATED;

	/* Handles the message condition. */
	if (message->payload_length >
		    INTEL_AX211_SCAN_ITERATION_NOTIFICATION_SIZE ||
	    (message->payload_length - 16U) % 8U != 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_OVERSIZED;
	}

	/* Checks the ax211 scan get le32 result. */
	bytes = message->payload;
	if (ax211_scan_get_le32(bytes) != INTEL_AX211_SCAN_UID)
		return INTEL_AX211_SCAN_OUT_OF_ORDER;

	/* Checks the remaining item count. */
	count = bytes[4U];
	if (count > INTEL_AX211_SCAN_CHANNEL_LIMIT)
		return INTEL_AX211_SCAN_OVERSIZED;

	/* Checks the remaining item count. */
	available = (message->payload_length - 16U) / 8U;
	if (count > available)
		return INTEL_AX211_SCAN_TRUNCATED;

	/* Handles the message condition. */
	expected = 16U + count * 8U;
	if (message->payload_length < expected)
		return INTEL_AX211_SCAN_TRUNCATED;
	kern_memset(event, 0, sizeof(*event));
	event->kind = INTEL_AX211_SCAN_EVENT_ITERATION_COMPLETE;
	event->channel_count = count;
	event->status = bytes[5U];
	event->bluetooth_status = bytes[6U];
	event->last_channel = bytes[7U];
	event->tsf = (uint64_t)ax211_scan_get_le32(bytes + 8U) |
		     ((uint64_t)ax211_scan_get_le32(bytes + 12U) << 32);
	/* Process each remaining element. */
	for (index = 0U; index < count; index++) {
		result = bytes + 16U + index * 8U;

		/*
		 * Channel, band, and per-probe fields are informational
		 * reports. The UID and bounded result count fence this
		 * notification.
		 */
		event->channel[index].channel = result[0U];
		event->channel[index].probe_result = result[2U];
		event->channel[index].probe_not_sent = result[3U];
		event->channel[index].duration =
			ax211_scan_get_le32(result + 4U);
	}

	/*
	 * API89 firmware may publish the complete 112-entry result storage even
	 * when scanned_channels says only one entry is live.  Decode exactly
	 * that bounded prefix and ignore the remaining fixed-array storage.
	 * This is an iteration report, not the terminal UMAC completion;
	 * SCAN_COMPLETE_UMAC owns lifetime.
	 */
	return INTEL_AX211_SCAN_OK;
}
