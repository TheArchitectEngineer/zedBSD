/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private API89 transmit implementation.
 */

#include "intel-ax211-tx.h"
#include <kern/kcrt.h>

#include <stddef.h>
#include <stdint.h>

/*
 * The fields of the transmit command (version 10) that precede the frame.
 *
 * The 28-byte fixed part holds the frame length at byte 0 (without the CCMP
 * header, which the firmware inserts itself), the flags at 2, the
 * offload-assist word at 4 and the rate at 16; the MAC header follows from
 * byte 28, padded to four bytes.
 * - Flags: COMMAND_RATE (bit 0) makes the firmware use the rate in the
 *   command; ENCRYPT_DISABLE (bit 1) sends the frame unencrypted;
 *   HIGH_PRIORITY (bit 2) queues it ahead of normal traffic.  The driver
 *   sets bits 0 and 2 on every frame, because the API 89 firmware has no
 *   rate state until rate control is configured, and bit 1 on every frame
 *   it does not encrypt.
 * - Offload assist: bits 12:8 give the MAC header length in 16-bit words,
 *   and PADDING (bit 13) says two bytes of padding follow the header.
 * - Rate: 0 is 1 Mbit/s CCK and 0x0100 marks the legacy OFDM rate table,
 *   whose code 0 is 6 Mbit/s; ANTENNA_A (bit 14) transmits on the first
 *   antenna.  The driver sends at 1 Mbit/s on 2.4 GHz and at 6 Mbit/s on
 *   5 GHz, where CCK is not allowed.
 *
 * Values referenced: Linux v6.17
 * drivers/net/wireless/intel/iwlwifi/fw/api/tx.h (struct iwl_tx_cmd_gen3, enum
 * iwl_tx_cmd_flags and the offload-assist bits) and
 * drivers/net/wireless/intel/iwlwifi/fw/api/rs.h (the rate word).
 */
#define AX211_TX_FLAG_COMMAND_RATE 0x0001U
#define AX211_TX_FLAG_ENCRYPT_DISABLE 0x0002U
#define AX211_TX_FLAG_HIGH_PRIORITY 0x0004U

#define AX211_TX_OFFLOAD_MAC_HEADER_SHIFT 8U
#define AX211_TX_OFFLOAD_MAC_HEADER_MASK 0x1fU
#define AX211_TX_OFFLOAD_PADDING 0x2000U

#define AX211_TX_RATE_1M_CCK 0U
#define AX211_TX_RATE_6M_OFDM 0x0100U
#define AX211_TX_RATE_ANTENNA_A 0x4000U

/*
 * The 802.11 frame control field and the CCMP header.
 *
 * The driver uses them to find the header length of a frame to send and to
 * check that a frame it is asked to encrypt carries a well-formed CCMP header.
 *
 * Values referenced: IEEE Std 802.11-2020 9.2.4.1 (frame control) and 12.5.3.2
 * (the 8-byte CCMP header with the Ext IV bit, bit 5 of its fourth byte).
 */
#define AX211_TX_FRAME_TYPE_MASK 0x000cU
#define AX211_TX_FRAME_TYPE_MANAGEMENT 0x0000U
#define AX211_TX_FRAME_TYPE_CONTROL 0x0004U
#define AX211_TX_FRAME_TYPE_DATA 0x0008U
#define AX211_TX_FRAME_SUBTYPE_MASK 0x00f0U
#define AX211_TX_FRAME_SUBTYPE_QOS 0x0080U
#define AX211_TX_TO_DS 0x0100U
#define AX211_TX_FROM_DS 0x0200U
#define AX211_TX_PROTECTED 0x4000U
#define AX211_TX_ORDER 0x8000U

#define AX211_TX_CCMP_HEADER_SIZE 8U
#define AX211_TX_CCMP_EXTENDED_IV 0x20U

/*
 * The transmit completion notification (version 7, 48 bytes for one frame).
 *
 * Byte 0 is the number of frames reported and must be 1; bytes 2 and 3 are the
 * RTS and frame retry counts; byte 28 is the sequence control and byte 30 the
 * byte count; byte 36 the queue; byte 40 the status word, whose low byte is
 * the outcome; byte 44 the scheduler sequence number, which must be below
 * 65536.  Outcome 1 means the frame was delivered and acknowledged, 2 that it
 * was sent without waiting for an acknowledgment (as for a group-addressed
 * frame); the driver counts both as sent, and every other outcome as a
 * failure.
 *
 * Values referenced: Linux v6.17
 * drivers/net/wireless/intel/iwlwifi/fw/api/tx.h (struct iwl_tx_resp and enum
 * iwl_tx_status).
 */
#define AX211_TX_RESPONSE_SIZE 48U
#define AX211_TX_RESPONSE_STATUS_OFFSET 40U
#define AX211_TX_RESPONSE_SSN_OFFSET 44U
#define AX211_TX_OUTCOME_MASK 0x000000ffU
#define AX211_TX_OUTCOME_DELIVERED 1U
#define AX211_TX_OUTCOME_SENT_WITHOUT_ACK 2U

static int ax211_tx_header_length(const uint8_t *frame, size_t length, size_t *header_length);
static uint16_t ax211_tx_get_le16(const uint8_t *bytes);
static int ax211_tx_request_validate(const struct intel_ax211_tx_request *request, size_t header_length);
static int ax211_tx_ccmp_validate(const struct intel_ax211_tx_request *request, size_t header_length);
static void ax211_tx_put_le16(uint8_t *bytes, uint16_t value);
static void ax211_tx_put_le32(uint8_t *bytes, uint32_t value);
static uint32_t ax211_tx_get_le32(const uint8_t *bytes);

/*
 * Implements the drv intel ax211 tx api89 validate operation.
 */
int
drv_intel_ax211_tx_api89_validate(
	const struct intel_ax211_protocol_command_table *table)
{
	struct intel_ax211_protocol_command_version version;
	int result;

	/* Handles the table availability. */
	if (table == NULL)
		return INTEL_AX211_TX_INVALID;

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_version_lookup(
		table, INTEL_AX211_TX_FAMILY, INTEL_AX211_TX_OPCODE, &version);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return INTEL_AX211_TX_UNSUPPORTED;

	/* Handles the version condition. */
	if (version.command_version != INTEL_AX211_TX_COMMAND_VERSION ||
	    version.notification_version != INTEL_AX211_TX_NOTIFICATION_VERSION) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_UNSUPPORTED;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TX_OK;
}

/*
 * Implements the drv intel ax211 tx prepare operation.
 */
int
drv_intel_ax211_tx_prepare(
	const struct intel_ax211_tx_request *request,
	struct intel_ax211_tx_prepared *prepared)
{
	struct intel_ax211_tx_prepared encoded;
	size_t header_length;
	size_t firmware_length;
	size_t padding;
	uint16_t flags;
	uint32_t offload;
	uint32_t rate;
	int result;

	/* Handles the request availability. */
	if (request == NULL || prepared == NULL || request->frame == NULL)
		return INTEL_AX211_TX_INVALID;

	/* Checks the operation result. */
	result = ax211_tx_header_length(request->frame, request->length,
					&header_length);
	if (result != INTEL_AX211_TX_OK)
		return result;

	/* Checks the operation result. */
	result = ax211_tx_request_validate(request, header_length);
	if (result != INTEL_AX211_TX_OK)
		return result;

	/* Handles the request condition. */
	firmware_length = request->length;
	if (request->encrypted)
		firmware_length -= AX211_TX_CCMP_HEADER_SIZE;

	/* Handles the firmware length condition. */
	if (firmware_length > UINT16_MAX)
		return INTEL_AX211_TX_OVERSIZED;

	kern_memset(&encoded, 0, sizeof(encoded));
	flags = 0U;
	rate = AX211_TX_RATE_ANTENNA_A |
	       (request->band_5ghz ? AX211_TX_RATE_6M_OFDM
				   : AX211_TX_RATE_1M_CCK);

	/* Handles the request condition. */
	if (!request->encrypted)
		flags |= AX211_TX_FLAG_ENCRYPT_DISABLE;

	/*
	 * API89 does not have usable data-rate state until TLC is configured.
	 * Keep the first standalone-driver path self-contained by supplying a
	 * conservative basic rate for every frame class: 1M CCK on 2.4 GHz and
	 * 6M OFDM where CCK is illegal on 5 GHz.  A later TLC phase may replace
	 * this fixed-rate policy without changing the TX ABI.
	 */
	flags |= AX211_TX_FLAG_COMMAND_RATE | AX211_TX_FLAG_HIGH_PRIORITY;

	/* Handles the padding condition. */
	padding = header_length & 3U;
	if (padding != 0U)
		padding = 4U - padding;

	/* Handles the padding condition. */
	offload = (uint32_t)((header_length / 2U) &
			     AX211_TX_OFFLOAD_MAC_HEADER_MASK)
		  << AX211_TX_OFFLOAD_MAC_HEADER_SHIFT;
	if (padding != 0U)
		offload |= AX211_TX_OFFLOAD_PADDING;

	/* Handles the header length condition. */
	if (INTEL_AX211_TX_COMMAND_FIXED_SIZE + header_length + padding >
	    sizeof(encoded.command)) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_BUFFER_TOO_SMALL;
	}

	/* Builds the command that precedes the frame on the wire. */
	ax211_tx_put_le16(encoded.command, (uint16_t)firmware_length);
	ax211_tx_put_le16(encoded.command + 2U, flags);
	ax211_tx_put_le32(encoded.command + 4U, offload);
	ax211_tx_put_le32(encoded.command + 16U, rate);
	kern_memcpy(encoded.command + INTEL_AX211_TX_COMMAND_FIXED_SIZE,
	       request->frame, header_length);
	encoded.command_length =
		INTEL_AX211_TX_COMMAND_FIXED_SIZE + header_length + padding;
	encoded.payload_offset = header_length;

	/* Handles the request condition. */
	if (request->encrypted)
		encoded.payload_offset += AX211_TX_CCMP_HEADER_SIZE;
	encoded.payload_length = request->length - encoded.payload_offset;
	encoded.connection_generation = request->connection_generation;
	encoded.cookie = request->cookie;
	encoded.key_generation = request->key_generation;
	encoded.packet_number = request->packet_number;
	encoded.frame_length = (uint16_t)firmware_length;
	encoded.encrypted = request->encrypted;
	encoded.key_index = request->key_index;
	*prepared = encoded;
	/* Returns the computed result. */
	return INTEL_AX211_TX_OK;
}

/*
 * Implements the drv intel ax211 tx completion decode operation.
 */
int
drv_intel_ax211_tx_completion_decode(
	const struct intel_ax211_protocol_message *message,
	uint32_t hardware_generation,
	uint16_t expected_queue,
	uint8_t expected_index,
	struct intel_ax211_tx_completion *completion)
{
	struct intel_ax211_tx_completion decoded;
	uint32_t scheduler_sequence;
	uint32_t status;
	uint16_t response_queue;

	/* Handles the message availability. */
	if (message == NULL || completion == NULL ||
	    hardware_generation == 0U || message->generation == 0U ||
	    expected_queue < INTEL_AX211_TX_QUEUE_MIN ||
	    expected_queue > INTEL_AX211_TX_QUEUE_MAX) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_INVALID;
	}

	/* Handles the message condition. */
	if (message->generation != hardware_generation)
		return INTEL_AX211_TX_STALE;

	/* Handles the message condition. */
	if (message->group != INTEL_AX211_TX_FAMILY ||
	    message->opcode != INTEL_AX211_TX_OPCODE ||
	    message->version != INTEL_AX211_TX_NOTIFICATION_VERSION) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_UNSUPPORTED;
	}

	/* Checks the operation status. */
	if ((message->flags & INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK) != 0U)
		return INTEL_AX211_TX_FAILED;

	/* Handles the message condition. */
	if (message->queue != (uint8_t)(expected_queue & 0x1fU) ||
	    message->index != expected_index) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_STALE;
	}

	/* Handles the payload availability. */
	if (message->payload == NULL)
		return INTEL_AX211_TX_INVALID;

	/* Handles the message condition. */
	if (message->payload_length < AX211_TX_RESPONSE_SIZE)
		return INTEL_AX211_TX_TRUNCATED;

	/* Handles the message condition. */
	if (message->payload_length > AX211_TX_RESPONSE_SIZE)
		return INTEL_AX211_TX_OVERSIZED;

	/* Handles the message condition. */
	if (message->payload[0U] != 1U)
		return INTEL_AX211_TX_UNSUPPORTED;

	/* Handles the response queue condition. */
	response_queue = ax211_tx_get_le16(message->payload + 36U);
	if (response_queue != expected_queue)
		return INTEL_AX211_TX_STALE;

	/* Handles the scheduler sequence condition. */
	scheduler_sequence = ax211_tx_get_le32(message->payload +
					       AX211_TX_RESPONSE_SSN_OFFSET);
	if (scheduler_sequence >= INTEL_AX211_TX_SCHEDULER_SEQUENCE_LIMIT)
		return INTEL_AX211_TX_FAILED;
	status = ax211_tx_get_le32(message->payload +
				   AX211_TX_RESPONSE_STATUS_OFFSET) &
		 AX211_TX_OUTCOME_MASK;

	/* Renders the notification as one completion record. */
	kern_memset(&decoded, 0, sizeof(decoded));
	decoded.hardware_generation = hardware_generation;
	decoded.scheduler_sequence = scheduler_sequence;
	decoded.queue = response_queue;
	decoded.sequence_control = ax211_tx_get_le16(message->payload + 28U);
	decoded.byte_count = ax211_tx_get_le16(message->payload + 30U);
	decoded.index = expected_index;
	decoded.acknowledged = status == AX211_TX_OUTCOME_DELIVERED ||
			       status == AX211_TX_OUTCOME_SENT_WITHOUT_ACK;
	decoded.rts_retries = message->payload[2U];
	decoded.frame_retries = message->payload[3U];
	*completion = decoded;
	/* Returns the computed result. */
	return INTEL_AX211_TX_OK;
}

/* Supports the ax211 tx header length operation. */
static int
ax211_tx_header_length(
	const uint8_t *frame,
	size_t length,
	size_t *header_length)
{
	uint16_t frame_control;
	uint16_t subtype;
	uint16_t type;
	size_t required;

	/* Handles the frame availability. */
	if (frame == NULL || header_length == NULL || length < 2U)
		return INTEL_AX211_TX_INVALID;
	frame_control = ax211_tx_get_le16(frame);
	type = frame_control & AX211_TX_FRAME_TYPE_MASK;
	subtype = frame_control & AX211_TX_FRAME_SUBTYPE_MASK;

	/* Handles the type condition. */
	if (type == AX211_TX_FRAME_TYPE_MANAGEMENT) {
		/* Handles the frame control condition. */
		required = 24U;
		if ((frame_control & AX211_TX_ORDER) != 0U)
			required += 4U;
	} else if (type == AX211_TX_FRAME_TYPE_DATA) {
		/* Handles the frame control condition. */
		required = 24U;
		if ((frame_control & (AX211_TX_TO_DS | AX211_TX_FROM_DS)) ==
		    (AX211_TX_TO_DS | AX211_TX_FROM_DS))
			required += 6U;

		/* Handles the subtype condition. */
		if ((subtype & AX211_TX_FRAME_SUBTYPE_QOS) != 0U) {
			required += 2U;

			/* Handles the frame control condition. */
			if ((frame_control & AX211_TX_ORDER) != 0U)
				required += 4U;
		}
	} else if (type == AX211_TX_FRAME_TYPE_CONTROL) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_UNSUPPORTED;
	} else {
		/* Returns the computed result. */
		return INTEL_AX211_TX_INVALID;
	}

	/* Handles the required condition. */
	if (required > INTEL_AX211_TX_MAC_HEADER_MAX)
		return INTEL_AX211_TX_OVERSIZED;

	/* Checks the current data length. */
	if (length < required)
		return INTEL_AX211_TX_TRUNCATED;
	*header_length = required;
	/* Returns the computed result. */
	return INTEL_AX211_TX_OK;
}

/* Supports the ax211 tx get le16 operation. */
static uint16_t
ax211_tx_get_le16(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint16_t)bytes[0U] | ((uint16_t)bytes[1U] << 8);
}

/* Supports the ax211 tx request validate operation. */
static int
ax211_tx_request_validate(
	const struct intel_ax211_tx_request *request,
	size_t header_length)
{
	uint16_t frame_control;
	uint16_t expected_type;
	int result;

	/* Handles the request condition. */
	if (request->connection_generation == 0U || request->cookie == 0U ||
	    request->length < header_length ||
	    request->length > INTEL_AX211_TX_FRAME_MAX ||
	    (request->encrypted != 0U && request->encrypted != 1U) ||
	    (request->band_5ghz != 0U && request->band_5ghz != 1U) ||
	    request->key_index > 3U ||
	    (request->frame_class != INTEL_AX211_TX_FRAME_MANAGEMENT &&
	     request->frame_class != INTEL_AX211_TX_FRAME_EAPOL &&
	     request->frame_class != INTEL_AX211_TX_FRAME_DATA)) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_INVALID;
	}
	frame_control = ax211_tx_get_le16(request->frame);

	/* Handles the frame control condition. */
	expected_type = request->frame_class == INTEL_AX211_TX_FRAME_MANAGEMENT
				? AX211_TX_FRAME_TYPE_MANAGEMENT
				: AX211_TX_FRAME_TYPE_DATA;
	if ((frame_control & AX211_TX_FRAME_TYPE_MASK) != expected_type)
		return INTEL_AX211_TX_INVALID;

	/* Handles the request condition. */
	if (request->frame_class == INTEL_AX211_TX_FRAME_MANAGEMENT &&
	    request->encrypted) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_UNSUPPORTED;
	}

	/* Handles the request condition. */
	if (!request->encrypted &&
	    (request->key_generation != 0U || request->packet_number != 0U ||
	     request->key_index != 0U ||
	     (frame_control & AX211_TX_PROTECTED) != 0U)) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_INVALID;
	}

	/* Handles the request condition. */
	if (request->encrypted &&
	    (request->key_generation == 0U || request->packet_number == 0U ||
	     request->packet_number > 0x0000ffffffffffffULL ||
	     (frame_control & AX211_TX_PROTECTED) == 0U)) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_INVALID;
	}

	/* Handles the request condition. */
	if (!request->encrypted)
		return INTEL_AX211_TX_OK;
	result = ax211_tx_ccmp_validate(request, header_length);

	/* Returns the computed result. */
	return result;
}

/* Supports the ax211 tx ccmp validate operation. */
static int
ax211_tx_ccmp_validate(
	const struct intel_ax211_tx_request *request,
	size_t header_length)
{
	const uint8_t *ccmp;
	uint64_t packet_number;

	/* Handles the header length condition. */
	if (header_length > request->length ||
	    request->length - header_length < AX211_TX_CCMP_HEADER_SIZE) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_TRUNCATED;
	}

	/* Handles the ccmp condition. */
	ccmp = request->frame + header_length;
	if (ccmp[2U] != 0U || (ccmp[3U] & 0x3fU) != AX211_TX_CCMP_EXTENDED_IV ||
	    (ccmp[3U] >> 6) != request->key_index) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_INVALID;
	}

	/* Handles the packet number condition. */
	packet_number = (uint64_t)ccmp[0U] | ((uint64_t)ccmp[1U] << 8) |
			((uint64_t)ccmp[4U] << 16) |
			((uint64_t)ccmp[5U] << 24) |
			((uint64_t)ccmp[6U] << 32) | ((uint64_t)ccmp[7U] << 40);
	if (packet_number != request->packet_number)
		return INTEL_AX211_TX_STALE;

	/* Returns the computed result. */
	return INTEL_AX211_TX_OK;
}

/* Supports the ax211 tx put le16 operation. */
static void
ax211_tx_put_le16(
	uint8_t *bytes,
	uint16_t value)
{
	bytes[0U] = (uint8_t)value;
	bytes[1U] = (uint8_t)(value >> 8);
}

/* Supports the ax211 tx put le32 operation. */
static void
ax211_tx_put_le32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0U] = (uint8_t)value;
	bytes[1U] = (uint8_t)(value >> 8);
	bytes[2U] = (uint8_t)(value >> 16);
	bytes[3U] = (uint8_t)(value >> 24);
}

/* Supports the ax211 tx get le32 operation. */
static uint32_t
ax211_tx_get_le32(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint32_t)bytes[0U] | ((uint32_t)bytes[1U] << 8) |
	       ((uint32_t)bytes[2U] << 16) | ((uint32_t)bytes[3U] << 24);
}
