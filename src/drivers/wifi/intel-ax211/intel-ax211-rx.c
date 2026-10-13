/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private API89 receive implementation.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * fw/api/rx.h.  They were cross-checked against OpenBSD
 * sys/dev/pci/if_iwxreg.h and if_iwx.c at commit 0f464d413c50.  Each block of
 * values below names the header it was taken from.
 *
 * Values of the 802.11 frame itself (frame control bits, element identifiers,
 * rates and channel numbers) come from IEEE Std 802.11-2020, and the WMM
 * element from the Wi-Fi Alliance WMM specification; the blocks that hold
 * them say so.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#include "intel-ax211-rx.h"
#include <kern/kcrt.h>

#include <stddef.h>
#include <stdint.h>

/*
 * The status word of the receive descriptor (bytes 12 to 15).
 *
 * Source: Linux iwlwifi v6.17 fw/api/rx.h (enum iwl_mvm_rx_status).  A frame
 * is delivered only when FCS_GOOD (bit 0, the frame check sequence was
 * right) and NO_OVERRUN (bit 1) are both set; a frame with REPEATED_FRAME
 * (bit 22, the firmware's duplicate detection) is dropped.  Bits 10:8 name
 * the cipher the firmware applied: 0 none, 2 CCMP.  A CCMP frame is
 * accepted only with PLAINTEXT_RESTORED (bit 11, the firmware decrypted it)
 * and MIC_GOOD (bit 6, its integrity check passed).
 */
#define AX211_RX_CHECK_FCS_GOOD 0x00000001U
#define AX211_RX_CHECK_NO_OVERRUN 0x00000002U
#define AX211_RX_CHECK_MIC_GOOD 0x00000040U
#define AX211_RX_CIPHER_FIELD_MASK 0x00000700U
#define AX211_RX_CIPHER_FIELD_NONE 0x00000000U
#define AX211_RX_CIPHER_FIELD_CCMP 0x00000200U
#define AX211_RX_PLAINTEXT_RESTORED 0x00000800U
#define AX211_RX_REPEATED_FRAME 0x00400000U

/*
 * The MAC flags and radio information of the receive descriptor.
 *
 * Source: Linux iwlwifi v6.17 fw/api/rx.h (the MAC flag bytes and the PHY
 * information word of struct iwl_rx_mpdu_desc).  Byte 3: PADDING (bit 5)
 * says the firmware inserted two bytes after the MAC header (and the CCMP
 * header) to align the body, which the driver removes; AMSDU (bit 6) marks
 * a frame of an aggregate, which the driver does not accept.  Bits 7:4 of
 * byte 2 give the length of the trailer the hardware left at the end, in
 * units of two bytes; the driver cuts it off and appends a zeroed 8-byte
 * MIC field in its place for a CCMP frame, so the frame handed on has the
 * usual 802.11 shape.  The 16-bit radio information at bytes 5 and 6 has
 * RADIO_TIME_REPLACED (bit 8) set when the 64-bit word at byte 48 holds
 * something other than the TSF; only otherwise is the TSF used.  The other
 * fields used are the two antennas' energies at bytes 40 and 41 (turned
 * into a signal strength), the channel at byte 42 and, at byte 44, the
 * device clock at the moment the frame started on the air.
 */
#define AX211_RX_MAC_FLAG_PADDING 0x20U
#define AX211_RX_MAC_FLAG_AMSDU 0x40U
#define AX211_RX_MAC_FLAG1_MIC_CRC_LENGTH_MASK 0xf0U
#define AX211_RX_MAC_FLAG1_MIC_CRC_LENGTH_SHIFT 3U
#define AX211_RX_RADIO_TIME_REPLACED 0x0100U

/*
 * The 802.11 frame control field.
 *
 * Source: IEEE Std 802.11-2020 9.2.4.1 (frame control: protocol version,
 * type in bits 3:2, subtype in bits 7:4, To DS, From DS, Protected Frame and
 * +HTC/Order) and 9.2.4.1.3 (the control subtypes CTS 12 and ACK 13).  The
 * driver uses them to find the header length of a received frame.
 */
#define AX211_FRAME_TYPE_MASK 0x000cU
#define AX211_FRAME_TYPE_MANAGEMENT 0x0000U
#define AX211_FRAME_TYPE_CONTROL 0x0004U
#define AX211_FRAME_TYPE_DATA 0x0008U
#define AX211_FRAME_SUBTYPE_MASK 0x00f0U
#define AX211_FRAME_SUBTYPE_QOS 0x0080U
#define AX211_FRAME_TO_DS 0x0100U
#define AX211_FRAME_FROM_DS 0x0200U
#define AX211_FRAME_PROTECTED 0x4000U
#define AX211_FRAME_ORDER 0x8000U
#define AX211_FRAME_CONTROL_CTS 0x00c0U
#define AX211_FRAME_CONTROL_ACK 0x00d0U

/*
 * The CCMP header and MIC.
 *
 * Source: IEEE Std 802.11-2020 12.5.3.2 (the 8-byte CCMP header with the
 * Ext IV bit, bit 5 of its fourth byte, always set, and the 8-byte MIC of
 * CCMP-128).
 */
#define AX211_CCMP_HEADER_SIZE 8U
#define AX211_CCMP_TAG_SIZE 8U
#define AX211_CCMP_EXTENDED_IV 0x20U

static uint16_t ax211_rx_get_le16(const uint8_t *bytes);
static uint32_t ax211_rx_get_le32(const uint8_t *bytes);
static int32_t ax211_rx_rssi(uint8_t energy_a, uint8_t energy_b);
static uint64_t ax211_rx_get_le64(const uint8_t *bytes);
static int ax211_rx_header_length(const uint8_t *frame, size_t length, size_t *header_length);
static int ax211_rx_security(struct intel_ax211_rx_delivery *mpdu, const uint8_t *frame, size_t length, size_t header_length);

/*
 * Implements the drv intel ax211 rx api89 validate operation.
 */
int
drv_intel_ax211_rx_api89_validate(
	const struct intel_ax211_protocol_command_table *table)
{
	struct intel_ax211_protocol_command_version version;
	int result;

	/* Handles the table availability. */
	if (table == NULL)
		return INTEL_AX211_RX_INVALID;

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_version_lookup(
		table, INTEL_AX211_RX_FRAME_FAMILY, INTEL_AX211_RX_FRAME_OPCODE,
		&version);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return INTEL_AX211_RX_UNSUPPORTED;

	/* Handles the version condition. */
	if (version.command_version != INTEL_AX211_PROTOCOL_UNKNOWN_VERSION ||
	    version.notification_version !=
		    INTEL_AX211_RX_FRAME_NOTIFICATION_VERSION) {
		/* Returns the computed result. */
		return INTEL_AX211_RX_UNSUPPORTED;
	}

	/* Returns the computed result. */
	return INTEL_AX211_RX_OK;
}

/*
 * Implements the drv intel ax211 rx mpdu decode operation.
 */
int
drv_intel_ax211_rx_mpdu_decode(
	const struct intel_ax211_protocol_message *message,
	uint32_t generation,
	uint8_t *output,
	size_t output_capacity,
	struct intel_ax211_rx_delivery *mpdu)
{
	struct intel_ax211_rx_delivery decoded;
	const uint8_t *descriptor;
	const uint8_t *frame;
	size_t frame_length;
	size_t header_length;
	size_t hardware_trailer_length;
	size_t common_trailer_length;
	size_t copied_length;
	size_t normalized_length;
	size_t padding_offset;
	uint16_t frame_control;
	uint16_t phy_info;
	uint8_t mac_flags;
	int result;

	/* Handles the message availability. */
	if (message == NULL || output == NULL || mpdu == NULL ||
	    generation == 0U || message->generation == 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_RX_INVALID;
	}

	/* Handles the message condition. */
	if (message->generation != generation)
		return INTEL_AX211_RX_STALE;

	/* Handles the message condition. */
	if (message->group != INTEL_AX211_RX_FRAME_FAMILY ||
	    message->opcode != INTEL_AX211_RX_FRAME_OPCODE ||
	    message->version != INTEL_AX211_RX_FRAME_NOTIFICATION_VERSION) {
		/* Returns the computed result. */
		return INTEL_AX211_RX_UNSUPPORTED;
	}

	/* Checks the operation status. */
	if ((message->flags & INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK) != 0U)
		return INTEL_AX211_RX_FAILED;

	/* Handles the payload availability. */
	if (message->payload == NULL)
		return INTEL_AX211_RX_INVALID;

	/* Handles the message condition. */
	if (message->payload_length < INTEL_AX211_RX_FRAME_DESCRIPTOR_SIZE)
		return INTEL_AX211_RX_TRUNCATED;

	descriptor = message->payload;

	/* Handles the frame length condition. */
	frame_length = ax211_rx_get_le16(descriptor);
	if (frame_length > INTEL_AX211_RX_FRAME_BODY_MAX)
		return INTEL_AX211_RX_OVERSIZED;

	/* Handles the message condition. */
	if (message->payload_length - INTEL_AX211_RX_FRAME_DESCRIPTOR_SIZE <
	    frame_length) {
		/* Returns the computed result. */
		return INTEL_AX211_RX_TRUNCATED;
	}

	/*
	 * The transport may round an RX packet up and leave bytes after the
	 * descriptor-declared MPDU.  They are not part of the 802.11 frame.
	 */
	if (frame_length < 2U)
		return INTEL_AX211_RX_TRUNCATED;

	kern_memset(&decoded, 0, sizeof(decoded));
	decoded.status = ax211_rx_get_le32(descriptor + 12U);

	/* Handles the decoded condition. */
	if ((decoded.status &
	     (AX211_RX_CHECK_FCS_GOOD | AX211_RX_CHECK_NO_OVERRUN)) !=
	    (AX211_RX_CHECK_FCS_GOOD | AX211_RX_CHECK_NO_OVERRUN)) {
		/* Returns the computed result. */
		return INTEL_AX211_RX_FAILED;
	}

	/* Handles the decoded condition. */
	if ((decoded.status & AX211_RX_REPEATED_FRAME) != 0U)
		return INTEL_AX211_RX_DUPLICATE;

	/* Handles the mac flags condition. */
	mac_flags = descriptor[3U];
	if ((mac_flags & AX211_RX_MAC_FLAG_AMSDU) != 0U)
		return INTEL_AX211_RX_UNSUPPORTED;
	decoded.rssi_dbm = ax211_rx_rssi(descriptor[40U], descriptor[41U]);
	decoded.channel = descriptor[42U];

	/* Handles the decoded condition. */
	if (decoded.channel == 0U)
		return INTEL_AX211_RX_FAILED;
	decoded.air_start_clock = ax211_rx_get_le32(descriptor + 44U);

	/* Handles the phy info condition. */
	phy_info = ax211_rx_get_le16(descriptor + 5U);
	if ((phy_info & AX211_RX_RADIO_TIME_REPLACED) == 0U) {
		decoded.tsf = ax211_rx_get_le64(descriptor + 48U);
		decoded.tsf_valid = 1U;
	}

	frame = descriptor + INTEL_AX211_RX_FRAME_DESCRIPTOR_SIZE;

	/* Checks the operation result. */
	result = ax211_rx_header_length(frame, frame_length, &header_length);
	if (result != INTEL_AX211_RX_OK)
		return result;
	hardware_trailer_length =
		(descriptor[2U] & AX211_RX_MAC_FLAG1_MIC_CRC_LENGTH_MASK) >>
		AX211_RX_MAC_FLAG1_MIC_CRC_LENGTH_SHIFT;

	/* Handles the hardware trailer length condition. */
	if (hardware_trailer_length > frame_length)
		return INTEL_AX211_RX_TRUNCATED;
	common_trailer_length =
		(decoded.status & AX211_RX_CIPHER_FIELD_MASK) ==
				AX211_RX_CIPHER_FIELD_CCMP
			? AX211_CCMP_TAG_SIZE
			: 0U;
	copied_length = frame_length - hardware_trailer_length;

	/* Handles the decoded condition. */
	padding_offset = header_length;
	if ((decoded.status & AX211_RX_CIPHER_FIELD_MASK) ==
	    AX211_RX_CIPHER_FIELD_CCMP) {
		/* Handles the padding offset condition. */
		if (padding_offset > copied_length ||
		    copied_length - padding_offset < AX211_CCMP_HEADER_SIZE) {
			/* Returns the computed result. */
			return INTEL_AX211_RX_TRUNCATED;
		}
		padding_offset += AX211_CCMP_HEADER_SIZE;
	}

	/* Handles the mac flags condition. */
	if ((mac_flags & AX211_RX_MAC_FLAG_PADDING) != 0U) {
		/* Handles the padding offset condition. */
		if (padding_offset > copied_length ||
		    copied_length - padding_offset < 2U) {
			/* Returns the computed result. */
			return INTEL_AX211_RX_TRUNCATED;
		}
		copied_length -= 2U;
	}

	/* Handles the copied length condition. */
	if (copied_length > SIZE_MAX - common_trailer_length)
		return INTEL_AX211_RX_OVERSIZED;

	/* Handles the normalized length condition. */
	normalized_length = copied_length + common_trailer_length;
	if (normalized_length > output_capacity)
		return INTEL_AX211_RX_BUFFER_TOO_SMALL;

	/* Handles the mac flags condition. */
	if ((mac_flags & AX211_RX_MAC_FLAG_PADDING) != 0U) {
		kern_memcpy(output, frame, padding_offset);
		kern_memcpy(output + padding_offset, frame + padding_offset + 2U,
		       copied_length - padding_offset);
	} else {
		kern_memcpy(output, frame, copied_length);
	}

	/* Handles the common trailer length condition. */
	if (common_trailer_length != 0U)
		kern_memset(output + copied_length, 0, common_trailer_length);
	frame_control = ax211_rx_get_le16(output);

	/* Checks the operation result. */
	result = ax211_rx_header_length(output, normalized_length,
					&header_length);
	if (result != INTEL_AX211_RX_OK)
		return result;
	decoded.frame = output;
	decoded.length = normalized_length;

	/* Checks the operation result. */
	result = ax211_rx_security(&decoded, output, normalized_length,
				   header_length);
	if (result != INTEL_AX211_RX_OK)
		return result;

	/* Handles the frame control condition. */
	if ((frame_control & AX211_FRAME_PROTECTED) == 0U &&
	    decoded.cipher != INTEL_AX211_RX_PROTECTION_NONE) {
		/* Returns the computed result. */
		return INTEL_AX211_RX_FAILED;
	}
	*mpdu = decoded;
	/* Returns the computed result. */
	return INTEL_AX211_RX_OK;
}

/* Supports the ax211 rx get le16 operation. */
static uint16_t
ax211_rx_get_le16(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

/* Supports the ax211 rx get le32 operation. */
static uint32_t
ax211_rx_get_le32(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	       ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Supports the ax211 rx rssi operation. */
static int32_t
ax211_rx_rssi(
	uint8_t energy_a,
	uint8_t energy_b)
{
	int32_t signal_a;
	int32_t signal_b;

	signal_a = energy_a == 0U ? -256 : -(int32_t)energy_a;
	signal_b = energy_b == 0U ? -256 : -(int32_t)energy_b;

	/* Returns the computed result. */
	return signal_a > signal_b ? signal_a : signal_b;
}

/* Supports the ax211 rx get le64 operation. */
static uint64_t
ax211_rx_get_le64(
	const uint8_t *bytes)
{
	uint64_t function_result;

	/* Computes the function result. */
	function_result = (uint64_t)ax211_rx_get_le32(bytes) |
			  ((uint64_t)ax211_rx_get_le32(bytes + 4U) << 32);

	/* Returns the computed result. */
	return function_result;
}

/* Supports the ax211 rx header length operation. */
static int
ax211_rx_header_length(
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
		return INTEL_AX211_RX_INVALID;
	frame_control = ax211_rx_get_le16(frame);
	type = frame_control & AX211_FRAME_TYPE_MASK;

	/* Handles the type condition. */
	subtype = frame_control & AX211_FRAME_SUBTYPE_MASK;
	if (type == AX211_FRAME_TYPE_MANAGEMENT) {
		required = 24U;
	} else if (type == AX211_FRAME_TYPE_CONTROL) {
		required = subtype == AX211_FRAME_CONTROL_CTS ||
					   subtype == AX211_FRAME_CONTROL_ACK
				   ? 10U
				   : 16U;
	} else if (type == AX211_FRAME_TYPE_DATA) {
		/* Handles the frame control condition. */
		required = 24U;
		if ((frame_control &
		     (AX211_FRAME_TO_DS | AX211_FRAME_FROM_DS)) ==
		    (AX211_FRAME_TO_DS | AX211_FRAME_FROM_DS))
			required += 6U;

		/* Handles the subtype condition. */
		if ((subtype & AX211_FRAME_SUBTYPE_QOS) != 0U) {
			required += 2U;

			/* Handles the frame control condition. */
			if ((frame_control & AX211_FRAME_ORDER) != 0U)
				required += 4U;
		}
	} else {
		/* Returns the computed result. */
		return INTEL_AX211_RX_UNSUPPORTED;
	}

	/* Checks the current data length. */
	if (length < required)
		return INTEL_AX211_RX_TRUNCATED;
	*header_length = required;
	/* Returns the computed result. */
	return INTEL_AX211_RX_OK;
}

/* Supports the ax211 rx security operation. */
static int
ax211_rx_security(
	struct intel_ax211_rx_delivery *mpdu,
	const uint8_t *frame,
	size_t length,
	size_t header_length)
{
	const uint8_t *ccmp;
	uint16_t frame_control;
	uint32_t security;

	frame_control = ax211_rx_get_le16(frame);

	/* Handles the security condition. */
	security = mpdu->status & AX211_RX_CIPHER_FIELD_MASK;
	if (security == AX211_RX_CIPHER_FIELD_NONE) {
		/* Handles the frame control condition. */
		if ((frame_control & AX211_FRAME_PROTECTED) != 0U)
			return INTEL_AX211_RX_FAILED;

		/* Returns the computed result. */
		return INTEL_AX211_RX_OK;
	}

	/* Handles the security condition. */
	if (security != AX211_RX_CIPHER_FIELD_CCMP)
		return INTEL_AX211_RX_UNSUPPORTED;

	/* Handles the frame control condition. */
	if ((frame_control & AX211_FRAME_PROTECTED) == 0U ||
	    (mpdu->status &
	     (AX211_RX_PLAINTEXT_RESTORED | AX211_RX_CHECK_MIC_GOOD)) !=
		    (AX211_RX_PLAINTEXT_RESTORED | AX211_RX_CHECK_MIC_GOOD)) {
		/* Returns the computed result. */
		return INTEL_AX211_RX_FAILED;
	}

	/* Checks the current data length. */
	if (length < header_length + AX211_CCMP_HEADER_SIZE)
		return INTEL_AX211_RX_TRUNCATED;

	/* Handles the ccmp condition. */
	ccmp = frame + header_length;
	if (ccmp[2U] != 0U || (ccmp[3U] & AX211_CCMP_EXTENDED_IV) == 0U)
		return INTEL_AX211_RX_FAILED;
	mpdu->packet_number =
		(uint64_t)ccmp[0U] | ((uint64_t)ccmp[1U] << 8) |
		((uint64_t)ccmp[4U] << 16) | ((uint64_t)ccmp[5U] << 24) |
		((uint64_t)ccmp[6U] << 32) | ((uint64_t)ccmp[7U] << 40);

	/* Handles the mpdu condition. */
	if (mpdu->packet_number == 0U)
		return INTEL_AX211_RX_FAILED;
	mpdu->key_index = (uint8_t)((ccmp[3U] >> 6) & 0x03U);
	mpdu->cipher = INTEL_AX211_RX_PROTECTION_CCMP;
	mpdu->decrypted = 1U;

	/* Returns the computed result. */
	return INTEL_AX211_RX_OK;
}
