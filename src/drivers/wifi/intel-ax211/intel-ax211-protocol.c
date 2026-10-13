/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private firmware protocol codecs.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * fw/api/alive.h, fw/api/nvm-reg.h, fw/api/commands.h and fw/file.h.  They
 * were cross-checked against OpenBSD sys/dev/pci/if_iwxreg.h and if_iwx.c at
 * commit 0f464d413c50.  Each block of values below names the header it was
 * taken from.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#include "intel-ax211-protocol.h"
#include <kern/kcrt.h>


/* One row of the command version table that the driver requires. */
struct ax211_required_version {
	uint8_t group;
	uint8_t opcode;
	uint8_t command_version;
	uint8_t notification_version;
};

static void ax211_protocol_version_at( const struct intel_ax211_protocol_command_table *table, size_t index, struct intel_ax211_protocol_command_version *version);
static int ax211_protocol_required_version( const struct intel_ax211_protocol_command_table *table, const struct ax211_required_version *required);
static int ax211_protocol_message_validate( const struct intel_ax211_protocol_message *message, uint8_t group, uint8_t opcode, uint8_t version, uint32_t generation, size_t minimum_payload_length, size_t maximum_payload_length);
static uint16_t ax211_protocol_get_le16(const uint8_t *bytes);
static void ax211_protocol_lmac_decode(const uint8_t *bytes, struct intel_ax211_protocol_lmac_alive *lmac);
static uint32_t ax211_protocol_get_le32(const uint8_t *bytes);
static uint64_t ax211_protocol_get_le64(const uint8_t *bytes);

/*
 * The command versions the boot path depends on, as the pinned firmware
 * lists them.
 *
 * Source: the command version record (type 48) of the pinned API 89 image
 * from linux-firmware 20260410, read by zedBSD; the family and opcode
 * numbers are those of Linux iwlwifi v6.17 fw/api/commands.h and
 * fw/api/nvm-reg.h.  Each row is family, opcode, command layout version and
 * notification layout version: ALIVE (0, 1) has no command and
 * notification version 6; SCAN_SETUP (1, 0x0c) is version 5 and SCAN_START
 * (1, 0x0d) version 17; NVM_HANDOFF (12, 0) is version 1; NVM_QUERY (12, 2)
 * is command version 1 with a version 4 answer; PNVM_ACCEPTED (12, 0xfe) has
 * no command and notification version 1.  99 is the image's "no version".
 * The boot is refused when the image lists any of them differently, because
 * the layouts the driver writes and reads are those versions.
 */
static const struct ax211_required_version ax211_api89_required[] = {
	{0x00U, 0x01U, 99U, 6U}, {0x01U, 0x0cU, 5U, 0U},
	{0x01U, 0x0dU, 17U, 0U}, {0x0cU, 0x00U, 1U, 0U},
	{0x0cU, 0x02U, 1U, 4U},	 {0x0cU, 0xfeU, 99U, 1U}};

/*
 * Implements the drv intel ax211 protocol command table parse operation.
 */
int
drv_intel_ax211_protocol_command_table_parse(
	const uint8_t *bytes,
	size_t length,
	struct intel_ax211_protocol_command_table *table)
{
	struct intel_ax211_protocol_command_table parsed;

	/* Handles the bytes availability. */
	if (bytes == NULL || table == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Checks the current data length. */
	if (length == 0U ||
	    length % INTEL_AX211_PROTOCOL_COMMAND_VERSION_ENTRY_SIZE != 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_TRUNCATED;
	}
	parsed.count = length / INTEL_AX211_PROTOCOL_COMMAND_VERSION_ENTRY_SIZE;

	/* Handles the parsed condition. */
	if (parsed.count > INTEL_AX211_PROTOCOL_API89_COMMAND_COUNT)
		return INTEL_AX211_PROTOCOL_OVERSIZED;
	parsed.bytes = bytes;
	*table = parsed;
	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/*
 * Implements the drv intel ax211 protocol command version lookup operation.
 */
int
drv_intel_ax211_protocol_command_version_lookup(
	const struct intel_ax211_protocol_command_table *table,
	uint8_t group,
	uint8_t opcode,
	struct intel_ax211_protocol_command_version *version)
{
	struct intel_ax211_protocol_command_version entry;
	struct intel_ax211_protocol_command_version candidate;
	size_t index;
	int found = 0;

	/* Handles the table availability. */
	if (table == NULL || version == NULL || table->bytes == NULL ||
	    table->count == 0U ||
	    table->count > INTEL_AX211_PROTOCOL_API89_COMMAND_COUNT) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_INVALID;
	}
	kern_memset(&candidate, 0, sizeof(candidate));
	/* Process each remaining element. */
	for (index = 0; index < table->count; index++) {
		ax211_protocol_version_at(table, index, &entry);

		/* Handles the entry condition. */
		if (entry.group == group && entry.opcode == opcode) {
			/* Handles the found condition. */
			if (found)
				return INTEL_AX211_PROTOCOL_DUPLICATE;
			candidate = entry;
			found = 1;
		}
	}

	/* Handles the found condition. */
	if (!found)
		return INTEL_AX211_PROTOCOL_MISSING;
	*version = candidate;
	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/*
 * Implements the drv intel ax211 protocol command table validate api89 operation.
 */
int
drv_intel_ax211_protocol_command_table_validate_api89(
	const struct intel_ax211_protocol_command_table *table)
{
	int result;
	struct intel_ax211_protocol_command_version sentinel;
	size_t index;

	/* Handles the table availability. */
	if (table == NULL || table->bytes == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Handles the table condition. */
	if (table->count != INTEL_AX211_PROTOCOL_API89_COMMAND_COUNT)
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	ax211_protocol_version_at(table, table->count - 1U, &sentinel);

	/* Handles the sentinel condition. */
	if (sentinel.opcode != 0U || sentinel.group != 0U ||
	    sentinel.command_version != 0U ||
	    sentinel.notification_version != 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	}
	/* Process each remaining element. */
	for (index = 0; index < sizeof(ax211_api89_required) /
					sizeof(ax211_api89_required[0]);
	     index++) {
		/* Checks the operation result. */
		result = ax211_protocol_required_version(
			table, &ax211_api89_required[index]);
		if (result != INTEL_AX211_PROTOCOL_OK)
			return result;
	}

	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/*
 * Implements the drv intel ax211 protocol command response validate operation.
 */
int
drv_intel_ax211_protocol_command_response_validate(
	const struct intel_ax211_protocol_message *message,
	const struct intel_ax211_protocol_pending_command *pending)
{
	int result;

	/* Handles the pending availability. */
	if (pending == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Handles the pending condition. */
	if (pending->response_version == INTEL_AX211_PROTOCOL_UNKNOWN_VERSION)
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;

	/* Handles the pending condition. */
	if (pending->minimum_response_length >
		    pending->maximum_response_length ||
	    pending->maximum_response_length >
		    INTEL_AX211_PROTOCOL_COMMAND_RESPONSE_MAX) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_INVALID;
	}

	/* Checks the operation result. */
	result = ax211_protocol_message_validate(
		message, pending->group, pending->opcode,
		pending->response_version, pending->generation,
		pending->minimum_response_length,
		pending->maximum_response_length);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;

	/* Handles the message condition. */
	if (message->queue != pending->queue ||
	    message->index != pending->index) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_TOKEN_MISMATCH;
	}

	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/*
 * Implements the drv intel ax211 protocol alive decode operation.
 */
int
drv_intel_ax211_protocol_alive_decode(
	const struct intel_ax211_protocol_message *message,
	uint32_t generation,
	struct intel_ax211_protocol_alive *alive)
{
	struct intel_ax211_protocol_alive decoded;
	const uint8_t *bytes;
	int result;

	/* Handles the alive availability. */
	if (alive == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Checks the operation result. */
	result = ax211_protocol_message_validate(
		message, INTEL_AX211_PROTOCOL_FAMILY_CLASSIC,
		INTEL_AX211_PROTOCOL_ALIVE_OPCODE,
		INTEL_AX211_PROTOCOL_ALIVE_VERSION, generation,
		INTEL_AX211_PROTOCOL_ALIVE_SIZE,
		INTEL_AX211_PROTOCOL_ALIVE_SIZE);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;
	bytes = message->payload;
	kern_memset(&decoded, 0, sizeof(decoded));
	decoded.status = ax211_protocol_get_le16(bytes);
	decoded.flags = ax211_protocol_get_le16(bytes + 2U);
	ax211_protocol_lmac_decode(bytes + 4U, &decoded.lmac[0]);
	ax211_protocol_lmac_decode(bytes + 52U, &decoded.lmac[1]);
	decoded.umac.major = ax211_protocol_get_le32(bytes + 100U);
	decoded.umac.minor = ax211_protocol_get_le32(bytes + 104U);
	decoded.umac.upper_error_log_address = ax211_protocol_get_le32(bytes + 108U);
	decoded.umac.trace_buffer_address = ax211_protocol_get_le32(bytes + 112U);
	decoded.sku[0] = ax211_protocol_get_le32(bytes + 116U);
	decoded.sku[1] = ax211_protocol_get_le32(bytes + 120U);
	decoded.sku[2] = ax211_protocol_get_le32(bytes + 124U);
	decoded.restore_region_base = ax211_protocol_get_le64(bytes + 128U);
	decoded.restore_region_size = ax211_protocol_get_le32(bytes + 136U);
	decoded.restore_region_enabled = ax211_protocol_get_le32(bytes + 140U);

	/* Handles the decoded condition. */
	if (decoded.status != INTEL_AX211_PROTOCOL_ALIVE_HEALTHY)
		return INTEL_AX211_PROTOCOL_FAILED;

	/* Handles the decoded condition. */
	if ((decoded.sku[0] | decoded.sku[1] | decoded.sku[2]) == 0U)
		return INTEL_AX211_PROTOCOL_MISSING;

	/* Handles the decoded condition. */
	if (decoded.restore_region_enabled != 0U) {
		/* Handles the decoded condition. */
		if (decoded.restore_region_base == 0U || decoded.restore_region_size == 0U ||
		    UINT64_MAX - decoded.restore_region_base < decoded.restore_region_size) {
			/* Returns the computed result. */
			return INTEL_AX211_PROTOCOL_INVALID;
		}

		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	}

	/* Handles the decoded condition. */
	if (decoded.restore_region_base != 0U || decoded.restore_region_size != 0U)
		return INTEL_AX211_PROTOCOL_INVALID;
	*alive = decoded;
	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/*
 * Implements the drv intel ax211 protocol pnvm init complete operation.
 */
int
drv_intel_ax211_protocol_pnvm_accepted(
	const struct intel_ax211_protocol_message *message,
	uint32_t generation)
{
	int error;

	/* Obtains the ax211 protocol message validate result. */
	error = ax211_protocol_message_validate(
		message, INTEL_AX211_PROTOCOL_FAMILY_REGULATORY_STORE,
		INTEL_AX211_PROTOCOL_PNVM_ACCEPTED_OPCODE,
		INTEL_AX211_PROTOCOL_PNVM_ACCEPTED_VERSION, generation,
		INTEL_AX211_PROTOCOL_PNVM_ACCEPTED_SIZE,
		INTEL_AX211_PROTOCOL_PNVM_ACCEPTED_SIZE);

	/* Returns the computed result. */
	return error;
}

/*
 * Implements the drv intel ax211 protocol init complete operation.
 */
int
drv_intel_ax211_protocol_init_finished(
	const struct intel_ax211_protocol_message *message,
	uint32_t generation)
{
	int error;

	/* Obtains the ax211 protocol message validate result. */
	error = ax211_protocol_message_validate(
		message, INTEL_AX211_PROTOCOL_FAMILY_CLASSIC,
		INTEL_AX211_PROTOCOL_INIT_FINISHED_OPCODE,
		INTEL_AX211_PROTOCOL_UNKNOWN_VERSION, generation,
		INTEL_AX211_PROTOCOL_INIT_FINISHED_SIZE,
		INTEL_AX211_PROTOCOL_INIT_FINISHED_SIZE);

	/* Returns the computed result. */
	return error;
}

/*
 * Implements the drv intel ax211 protocol nvm get info decode operation.
 */
int
drv_intel_ax211_protocol_nvm_query_decode(
	const struct intel_ax211_protocol_message *message,
	const struct intel_ax211_protocol_pending_command *pending,
	struct intel_ax211_protocol_nvm *nvm)
{
	struct intel_ax211_protocol_channel *channel_local;
	struct intel_ax211_protocol_channel *channel_local1;
	/*
	 * The 5 GHz channel numbers (IEEE Std 802.11-2020 Annex E) in the order
	 * the firmware lists their flag words after the 14 channels of 2.4 GHz.
	 */
	static const uint8_t
		channel_5ghz[INTEL_AX211_PROTOCOL_5GHZ_CHANNEL_LIMIT] = {
			36U,  40U,  44U,  48U,	52U,  56U,  60U,  64U,
			68U,  72U,  76U,  80U,	84U,  88U,  92U,  96U,
			100U, 104U, 108U, 112U, 116U, 120U, 124U, 128U,
			132U, 136U, 140U, 144U, 149U, 153U, 157U, 161U,
			165U, 169U, 173U, 177U, 181U};
	struct intel_ax211_protocol_nvm decoded;
	const uint8_t *bytes;
	uint32_t tx_chains, rx_chains;
	size_t index, channel_count, channel_offset;
	int result;

	/* Handles the pending availability. */
	if (pending == NULL || nvm == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Handles the pending condition. */
	if (pending->group != INTEL_AX211_PROTOCOL_FAMILY_REGULATORY_STORE ||
	    pending->opcode != INTEL_AX211_PROTOCOL_NVM_QUERY_OPCODE ||
	    pending->response_version !=
		    INTEL_AX211_PROTOCOL_NVM_QUERY_VERSION ||
	    pending->minimum_response_length !=
		    INTEL_AX211_PROTOCOL_NVM_QUERY_SIZE ||
	    pending->maximum_response_length !=
		    INTEL_AX211_PROTOCOL_NVM_QUERY_SIZE) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	}

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_response_validate(message,
								    pending);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;
	bytes = message->payload;
	kern_memset(&decoded, 0, sizeof(decoded));
	decoded.nvm_flags = ax211_protocol_get_le32(bytes);
	decoded.nvm_revision = ax211_protocol_get_le16(bytes + 4U);
	decoded.board_kind = bytes[6];
	decoded.hardware_address_count = bytes[7];
	decoded.standard_flags = ax211_protocol_get_le32(bytes + 8U);
	tx_chains = ax211_protocol_get_le32(bytes + 12U);
	rx_chains = ax211_protocol_get_le32(bytes + 16U);
	decoded.firmware_regulatory = ax211_protocol_get_le32(bytes + 20U) != 0U;
	decoded.channel_total = ax211_protocol_get_le32(bytes + 24U);

	/* Handles the decoded condition. */
	if ((decoded.nvm_flags &
	     INTEL_AX211_PROTOCOL_NVM_FUSES_BLANK) != 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_FAILED;
	}
	decoded.band_24_enabled =
		(decoded.standard_flags &
		 INTEL_AX211_PROTOCOL_NVM_BAND_24_ENABLED) != 0U;
	decoded.band_52_enabled =
		(decoded.standard_flags &
		 INTEL_AX211_PROTOCOL_NVM_BAND_52_ENABLED) != 0U;
	decoded.standard_11n = (decoded.standard_flags &
			      INTEL_AX211_PROTOCOL_NVM_11N_ENABLED) != 0U;
	decoded.standard_11ac = (decoded.standard_flags &
			       INTEL_AX211_PROTOCOL_NVM_11AC_ENABLED) != 0U;
	decoded.standard_11ax = (decoded.standard_flags &
			      INTEL_AX211_PROTOCOL_NVM_11AX_ENABLED) != 0U;
	decoded.mimo_disabled = (decoded.standard_flags &
				 INTEL_AX211_PROTOCOL_NVM_MIMO_DISABLED) != 0U;

	/* Handles the decoded condition. */
	if (!decoded.band_24_enabled || tx_chains == 0U || rx_chains == 0U)
		return INTEL_AX211_PROTOCOL_MISSING;

	/* Handles the tx chains condition. */
	if (tx_chains > UINT8_MAX || rx_chains > UINT8_MAX)
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;

	/* Handles the decoded condition. */
	if (decoded.channel_total > INTEL_AX211_PROTOCOL_NVM_CHANNEL_LIMIT)
		return INTEL_AX211_PROTOCOL_OVERSIZED;
	decoded.tx_chain_mask = (uint8_t)tx_chains;
	decoded.rx_chain_mask = (uint8_t)rx_chains;

	/* Handles the channel count condition. */
	channel_count = decoded.channel_total;
	if (channel_count > INTEL_AX211_PROTOCOL_24GHZ_CHANNEL_LIMIT)
		channel_count = INTEL_AX211_PROTOCOL_24GHZ_CHANNEL_LIMIT;
	decoded.channel_24ghz_count = channel_count;
	/* Process each remaining element. */
	for (index = 0; index < channel_count; index++) {
		channel_local = &decoded.channel_24ghz[index];

		/* Decodes one channel of the 2.4 GHz band. */
		channel_local->number = (uint8_t)(index + 1U);
		channel_local->flags = ax211_protocol_get_le32(
			bytes + 28U + index * sizeof(uint32_t));
		channel_local->valid =
			(channel_local->flags &
			 INTEL_AX211_PROTOCOL_CHANNEL_FLAG_USABLE) != 0U;
		channel_local->active =
			channel_local->valid &&
			(channel_local->flags &
			 INTEL_AX211_PROTOCOL_CHANNEL_FLAG_MAY_TRANSMIT) != 0U;

		/* Handles the channel local condition. */
		if (channel_local->valid)
			decoded.valid_24ghz_count++;
	}

	channel_offset = INTEL_AX211_PROTOCOL_24GHZ_CHANNEL_LIMIT;

	/* Handles the channel count condition. */
	channel_count = decoded.channel_total > channel_offset
				? decoded.channel_total - channel_offset
				: 0U;
	if (channel_count > INTEL_AX211_PROTOCOL_5GHZ_CHANNEL_LIMIT)
		channel_count = INTEL_AX211_PROTOCOL_5GHZ_CHANNEL_LIMIT;
	decoded.channel_5ghz_count = channel_count;
	/* Process each remaining element. */
	for (index = 0U; index < channel_count; index++) {
		channel_local1 = &decoded.channel_5ghz[index];

		/* Decodes one channel of the 5 GHz band. */
		channel_local1->number = channel_5ghz[index];
		channel_local1->flags = ax211_protocol_get_le32(
			bytes + 28U +
			(channel_offset + index) * sizeof(uint32_t));
		channel_local1->valid =
			decoded.band_52_enabled &&
			(channel_local1->flags &
			 INTEL_AX211_PROTOCOL_CHANNEL_FLAG_USABLE) != 0U;
		channel_local1->active =
			channel_local1->valid &&
			(channel_local1->flags &
			 INTEL_AX211_PROTOCOL_CHANNEL_FLAG_MAY_TRANSMIT) != 0U;

		/* Handles the channel local1 condition. */
		if (channel_local1->valid)
			decoded.valid_5ghz_count++;
	}

	/*
	 * With LAR enabled, MCC is authoritative for regulatory validity.  NVM
	 * still supplies the hardware channel table, but its VALID bit is not a
	 * prerequisite for using a channel accepted by MCC.
	 */
	if (decoded.valid_24ghz_count == 0U && !decoded.firmware_regulatory)
		return INTEL_AX211_PROTOCOL_MISSING;
	*nvm = decoded;
	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/* Supports the ax211 protocol version at operation. */
static void
ax211_protocol_version_at(
	const struct intel_ax211_protocol_command_table *table,
	size_t index,
	struct intel_ax211_protocol_command_version *version)
{
	const uint8_t *entry =
		table->bytes +
		index * INTEL_AX211_PROTOCOL_COMMAND_VERSION_ENTRY_SIZE;

	version->opcode = entry[0];
	version->group = entry[1];
	version->command_version = entry[2];
	version->notification_version = entry[3];
}

/* Supports the ax211 protocol required version operation. */
static int
ax211_protocol_required_version(
	const struct intel_ax211_protocol_command_table *table,
	const struct ax211_required_version *required)
{
	struct intel_ax211_protocol_command_version version;
	int result;

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_version_lookup(
		table, required->group, required->opcode, &version);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return result;

	/* Handles the version condition. */
	if (version.command_version != required->command_version ||
	    version.notification_version != required->notification_version) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	}

	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/* Supports the ax211 protocol message validate operation. */
static int
ax211_protocol_message_validate(
	const struct intel_ax211_protocol_message *message,
	uint8_t group,
	uint8_t opcode,
	uint8_t version,
	uint32_t generation,
	size_t minimum_payload_length,
	size_t maximum_payload_length)
{
	/* Handles the message availability. */
	if (message == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Handles the minimum payload length condition. */
	if (minimum_payload_length > maximum_payload_length)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Handles the payload availability. */
	if (message->payload_length != 0U && message->payload == NULL)
		return INTEL_AX211_PROTOCOL_INVALID;

	/* Handles the message condition. */
	if (message->generation != generation)
		return INTEL_AX211_PROTOCOL_STALE;

	/* Handles the message condition. */
	if (message->group != group || message->opcode != opcode ||
	    message->version != version) {
		/* Returns the computed result. */
		return INTEL_AX211_PROTOCOL_UNSUPPORTED;
	}

	/* Checks the operation status. */
	if ((message->flags & INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK) != 0U)
		return INTEL_AX211_PROTOCOL_FAILED;

	/* Handles the message condition. */
	if (message->payload_length < minimum_payload_length)
		return INTEL_AX211_PROTOCOL_TRUNCATED;

	/* Handles the message condition. */
	if (message->payload_length > maximum_payload_length)
		return INTEL_AX211_PROTOCOL_OVERSIZED;

	/* Returns the computed result. */
	return INTEL_AX211_PROTOCOL_OK;
}

/* Supports the ax211 protocol get le16 operation. */
static uint16_t
ax211_protocol_get_le16(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

/* Supports the ax211 protocol lmac decode operation. */
static void
ax211_protocol_lmac_decode(
	const uint8_t *bytes,
	struct intel_ax211_protocol_lmac_alive *lmac)
{
	lmac->major = ax211_protocol_get_le32(bytes);
	lmac->minor = ax211_protocol_get_le32(bytes + 4U);
	lmac->version_subtype = bytes[8];
	lmac->version_type = bytes[9];
	lmac->mac = bytes[10];
	lmac->option = bytes[11];
	lmac->timestamp = ax211_protocol_get_le32(bytes + 12U);
	lmac->error_log_address = ax211_protocol_get_le32(bytes + 16U);
	lmac->event_log_address = ax211_protocol_get_le32(bytes + 20U);
	lmac->cpu_state_address = ax211_protocol_get_le32(bytes + 24U);
	lmac->trace_setup_address = ax211_protocol_get_le32(bytes + 28U);
	lmac->alive_counter_address = ax211_protocol_get_le32(bytes + 32U);
	lmac->scheduler_sram_base = ax211_protocol_get_le32(bytes + 36U);
	lmac->relay_buffer_address = ax211_protocol_get_le32(bytes + 40U);
	lmac->relay_buffer_size = ax211_protocol_get_le32(bytes + 44U);
}

/* Supports the ax211 protocol get le32 operation. */
static uint32_t
ax211_protocol_get_le32(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	       ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Supports the ax211 protocol get le64 operation. */
static uint64_t
ax211_protocol_get_le64(
	const uint8_t *bytes)
{
	uint64_t value = 0;
	unsigned int index;

	/* Process each remaining element. */
	for (index = 0; index < 8U; index++)
		value |= (uint64_t)bytes[index] << (index * 8U);

	/* Returns the computed result. */
	return value;
}
