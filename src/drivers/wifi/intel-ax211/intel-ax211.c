/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Begin consolidated intel-ax211.c. */
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 firmware and descriptor core.
 *
 * Parses the pinned firmware and PNVM files and encodes the descriptors and
 * the boot context the device reads.
 */

#include "intel-ax211-internal.h"
#include <kern/kcrt.h>

/*
 * The record types of the firmware file and of the PNVM file.
 *
 * What the pinned API 89 image and its PNVM contain, and what the loaders in
 * this file do with each type:
 * - RUNTIME_SECTION (19) is the only code record the driver loads.  Its body
 *   is a 32-bit device destination followed by the section bytes; the two
 *   mark destinations of intel-ax211-internal.h split the list into the
 *   LMAC, UMAC and paged groups, which the scratch area then lists
 *   separately.  The same type carries the sections of the PNVM file.
 * - INIT_SECTION (20) and WAKE_SECTION (21) hold the images of the
 *   initialization and wake-on-WLAN modes.  The driver counts them against
 *   the section limit but never loads them: the driver loads only the runtime
 *   image and selects the initialization stage inside it with the
 *   stage-select command (intel-ax211-init.h).
 * - RADIO_PROFILE (23), CPU_COUNT (27), IMAGE_VERSION (36), LOADER_IMAGE (52)
 *   and COMMAND_VERSION_TABLE (48) are required and may appear only once; an
 *   image without one of them is refused.  CPU_COUNT must be 1 or 2 and is 2
 *   on this image.  INTERFACE_FLAGS (29) and FEATURE_FLAGS (30) each carry an
 *   index and a word of capability bits, at most once per index (4 and 5
 *   words); the runtime code tests single bits of them
 *   (intel-ax211-runtime.h).  IMAGE_VERSION is three words: major (89),
 *   minor and serial, which must equal the pinned values.  LOADER_IMAGE is
 *   the 13944-byte image loader that the device runs first from the address
 *   in the loader image register.  COMMAND_VERSION_TABLE is 868 bytes, 217
 *   entries of four bytes, and is the only source of the layout version of
 *   each command.
 * - PNVM_BOARD (64), PNVM_REVISION (62) and HARDWARE_KIND (58) frame the
 *   boards of the PNVM file.  A board starts with its 12-byte signature; the
 *   revision word, the 4-byte hardware-kind records (MAC type and radio kind,
 *   16 bits each) and the sections that follow belong to it until the next
 *   signature.  The pinned file holds 4 boards with 8 hardware-kind records
 *   and 8 sections between them.
 * - Every other listed type is recognised and skipped.  An unknown type
 *   makes the loader refuse the image, so an image that grew a record the
 *   driver has never seen is not booted with a guessed meaning.
 * - TRACE_RANGE_FIRST to TRACE_RANGE_LAST is the block of debug
 *   configuration records (0x1000005 to 0x100000a).  0x1000000 to 0x1000004,
 *   0x100000b, 0x100000c, 65, 69, 0x101 and 1092 are further types that
 *   ax211_ignored_firmware_tlv() skips by number.
 *
 * Values referenced: Linux v6.17 drivers/net/wireless/intel/iwlwifi/fw/file.h
 * (enum iwl_ucode_tlv_type) and OpenBSD 0f464d413c50 sys/dev/pci/if_iwxreg.h
 * (IWX_UCODE_TLV_*, which also names types 35, 53 and 59).
 */
enum {
	AX211_RECORD_PROBE_LIMIT = 6,
	AX211_RECORD_PAN_SUPPORT = 7,
	AX211_RECORD_OLD_FLAGS = 18,
	AX211_RECORD_RUNTIME_SECTION = 19,
	AX211_RECORD_INIT_SECTION = 20,
	AX211_RECORD_WAKE_SECTION = 21,
	AX211_RECORD_CALIBRATION_DEFAULTS = 22,
	AX211_RECORD_RADIO_PROFILE = 23,
	AX211_RECORD_CPU_COUNT = 27,
	AX211_RECORD_CIPHER_SCHEMES = 28,
	AX211_RECORD_INTERFACE_FLAGS = 29,
	AX211_RECORD_FEATURE_FLAGS = 30,
	AX211_RECORD_SCAN_CHANNEL_LIMIT = 31,
	AX211_RECORD_PAGED_SIZE = 32,
	AX211_RECORD_SNIFFER_SECTION = 34,
	AX211_RECORD_SDIO_DMA_ADDRESS = 35,
	AX211_RECORD_IMAGE_VERSION = 36,
	AX211_RECORD_TRACE_DESTINATION = 38,
	AX211_RECORD_TRACE_SETUP = 39,
	AX211_RECORD_TRACE_TRIGGER = 40,
	AX211_RECORD_COMMAND_VERSION_TABLE = 48,
	AX211_RECORD_BACKGROUND_SCAN_LIMITS = 50,
	AX211_RECORD_MEMORY_SEGMENTS = 51,
	AX211_RECORD_LOADER_IMAGE = 52,
	AX211_RECORD_FMAC_INTERFACE = 53,
	AX211_RECORD_UPPER_CPU_TRACE_ADDRESSES = 54,
	AX211_RECORD_LOWER_CPU_TRACE_ADDRESSES = 55,
	AX211_RECORD_RECOVERY_BUFFER = 57,
	AX211_RECORD_HARDWARE_KIND = 58,
	AX211_RECORD_FMAC_RECOVERY_BUFFER = 59,
	AX211_RECORD_SEQUENCER_REVISION = 60,
	AX211_RECORD_RADIO_INTEGRATION_REVISION = 61,
	AX211_RECORD_PNVM_REVISION = 62,
	AX211_RECORD_PNVM_BOARD = 64,
	AX211_RECORD_SECTION_TABLE_ADDRESS = 66,
	AX211_RECORD_SUSPEND_KEY_ADDRESS = 67,
	AX211_RECORD_PROGRAM_COUNTER_ADDRESS = 68,
	AX211_RECORD_PNVM_PAYLOAD = 74,
	AX211_RECORD_STATION_LIMIT = 0x100,
	AX211_RECORD_BEACON_LIMIT = 0x102,
	AX211_RECORD_TRACE_RANGE_FIRST = 0x1000005,
	AX211_RECORD_TRACE_RANGE_LAST = 0x100000a
};

static uint16_t ax211_get_le16(const uint8_t *bytes);
static uint32_t ax211_get_le32(const uint8_t *bytes);
static void ax211_put_le16(uint8_t *bytes, uint16_t value);
static void ax211_put_le32(uint8_t *bytes, uint32_t value);
static void ax211_put_le64(uint8_t *bytes, uint64_t value);
static int ax211_tlv_span(uint32_t value, size_t remaining, size_t *span);
static int ax211_ignored_firmware_tlv(uint32_t type);
static int ax211_pnvm_finish(const struct intel_ax211_pnvm_manifest *candidate, int active, int version_seen, int hardware_match, uint16_t mac_type, uint16_t rf_id, struct intel_ax211_pnvm_manifest *manifest);

/*
 * Implements the drv intel ax211 identity matches operation.
 */
int
drv_intel_ax211_identity_matches(
	const struct intel_ax211_identity *identity)
{
	/* Handles the identity availability. */
	if (identity == NULL)
		return 0;

	/* Returns the computed result. */
	return identity->vendor == INTEL_AX211_PCI_VENDOR_ID &&
	       identity->device == INTEL_AX211_PCI_DEVICE_ID &&
	       identity->subvendor == INTEL_AX211_PCI_SUBVENDOR_ID &&
	       identity->subdevice == INTEL_AX211_PCI_SUBDEVICE_ID &&
	       identity->revision == INTEL_AX211_PCI_REVISION;
}

/*
 * Implements the drv intel ax211 mac type supported operation.
 */
int
drv_intel_ax211_mac_type_supported(
	uint16_t mac_type)
{
	/* Returns the computed result. */
	return mac_type == INTEL_AX211_SILICON_SO ||
	       mac_type == INTEL_AX211_SILICON_SOF;
}

/*
 * Implements the drv intel ax211 firmware parse operation.
 */
int
drv_intel_ax211_firmware_parse(
	const uint8_t *bytes,
	size_t length,
	struct intel_ax211_firmware_manifest *manifest)
{
	uint32_t index_local;
	uint32_t index_local1;
	struct intel_ax211_section *section;
	uint32_t destination;
	int separator;
	uint32_t type, tlv_length;
	const uint8_t *data;
	int result;
	struct intel_ax211_firmware_manifest parsed;
	size_t offset, span;
	size_t cpu_separator = SIZE_MAX;
	size_t paging_separator = SIZE_MAX;
	uint32_t seen = 0;
	uint8_t api_seen = 0;
	uint8_t capa_seen = 0;
	size_t init_sections = 0;
	size_t wow_sections = 0;

	/* Handles the bytes availability. */
	if (bytes == NULL || manifest == NULL)
		return INTEL_AX211_INVALID;

	/* Checks the current data length. */
	if (length < INTEL_AX211_FIRMWARE_FILE_HEADER_SIZE)
		return INTEL_AX211_TRUNCATED;

	/* Checks the ax211 get le32 result. */
	if (ax211_get_le32(bytes) != 0U ||
	    ax211_get_le32(bytes + 4U) != INTEL_AX211_FIRMWARE_FILE_MAGIC) {
		/* Returns the computed result. */
		return INTEL_AX211_INVALID;
	}

	kern_memset(&parsed, 0, sizeof(parsed));
	parsed.header_version = ax211_get_le32(bytes + 72U);
	parsed.build = ax211_get_le32(bytes + 76U);
	offset = INTEL_AX211_FIRMWARE_FILE_HEADER_SIZE;

	/* Process each remaining element. */
	while (offset < length) {
		/* Checks the current data length. */
		if (length - offset < INTEL_AX211_FIRMWARE_RECORD_HEADER_SIZE)
			return INTEL_AX211_TRUNCATED;
		type = ax211_get_le32(bytes + offset);
		tlv_length = ax211_get_le32(bytes + offset + 4U);
		offset += INTEL_AX211_FIRMWARE_RECORD_HEADER_SIZE;

		/* Checks the operation result. */
		result = ax211_tlv_span(tlv_length, length - offset, &span);
		if (result != INTEL_AX211_SUCCESS)
			return result;
		data = bytes + offset;

		/* Dispatch the selected syntax or record type. */
		switch (type) {
		case AX211_RECORD_RUNTIME_SECTION:

			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;

			/* Handles the parsed condition. */
			if (parsed.runtime_count >= INTEL_AX211_FIRMWARE_SECTION_LIMIT)
				return INTEL_AX211_OVERFLOW;
			destination = ax211_get_le32(data);
			separator = 0;

			/* Handles the destination condition. */
			if (destination == INTEL_AX211_UPPER_CPU_IMAGE_MARK) {
				/* Checks the ax211 get le32 result. */
				if (tlv_length != 8U ||
				    ax211_get_le32(data + 4U) != 0U) {
					/* Returns the computed result. */
					return INTEL_AX211_INVALID;
				}

				/* Handles the cpu separator condition. */
				if (cpu_separator != SIZE_MAX)
					return INTEL_AX211_DUPLICATE;
				cpu_separator = parsed.runtime_count;
				separator = 1;
			} else if (destination ==
				   INTEL_AX211_PAGED_IMAGE_MARK) {
				/* Checks the ax211 get le32 result. */
				if (tlv_length != 8U ||
				    ax211_get_le32(data + 4U) != 0U) {
					/* Returns the computed result. */
					return INTEL_AX211_INVALID;
				}

				/* Handles the paging separator condition. */
				if (paging_separator != SIZE_MAX)
					return INTEL_AX211_DUPLICATE;
				paging_separator = parsed.runtime_count;
				separator = 1;
			} else if (tlv_length == 4U) {
				/* Returns the computed result. */
				return INTEL_AX211_INVALID;
			}

			section = &parsed.runtime[parsed.runtime_count++];
			section->destination = destination;
			section->file_offset = offset + 4U;
			section->length =
				separator ? 0U : (size_t)tlv_length - 4U;
			break;
		case AX211_RECORD_INIT_SECTION:
			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;

			/* Handles the init sections condition. */
			if (++init_sections > INTEL_AX211_FIRMWARE_SECTION_LIMIT)
				return INTEL_AX211_OVERFLOW;
			break;
		case AX211_RECORD_WAKE_SECTION:
			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;

			/* Handles the wow sections condition. */
			if (++wow_sections > INTEL_AX211_FIRMWARE_SECTION_LIMIT)
				return INTEL_AX211_OVERFLOW;
			break;
		case AX211_RECORD_RADIO_PROFILE:
			/* Handles the seen condition. */
			if ((seen & 1U) != 0U)
				return INTEL_AX211_DUPLICATE;

			/* Handles the tlv length condition. */
			if (tlv_length != 4U)
				return INTEL_AX211_INVALID;
			seen |= 1U;
			parsed.radio_profile = ax211_get_le32(data);
			break;
		case AX211_RECORD_CPU_COUNT:
			/* Handles the seen condition. */
			if ((seen & 2U) != 0U)
				return INTEL_AX211_DUPLICATE;

			/* Handles the tlv length condition. */
			if (tlv_length != 4U)
				return INTEL_AX211_INVALID;
			seen |= 2U;
			parsed.cpu_count = ax211_get_le32(data);

			/* Handles the parsed condition. */
			if (parsed.cpu_count == 0U || parsed.cpu_count > 2U)
				return INTEL_AX211_INVALID;
			break;
		case AX211_RECORD_INTERFACE_FLAGS:

			/* Handles the tlv length condition. */
			if (tlv_length != 8U)
				return INTEL_AX211_INVALID;

			/* Handles the index local condition. */
			index_local = ax211_get_le32(data);
			if (index_local >= 4U)
				return INTEL_AX211_OVERFLOW;

			/* Handles the api seen condition. */
			if ((api_seen & (uint8_t)(1U << index_local)) != 0U)
				return INTEL_AX211_DUPLICATE;
			api_seen |= (uint8_t)(1U << index_local);
			parsed.api_changes[index_local] =
				ax211_get_le32(data + 4U);
			break;
		case AX211_RECORD_FEATURE_FLAGS:

			/* Handles the tlv length condition. */
			if (tlv_length != 8U)
				return INTEL_AX211_INVALID;

			/* Handles the index local1 condition. */
			index_local1 = ax211_get_le32(data);
			if (index_local1 >= 5U)
				return INTEL_AX211_OVERFLOW;

			/* Handles the capa seen condition. */
			if ((capa_seen & (uint8_t)(1U << index_local1)) != 0U)
				return INTEL_AX211_DUPLICATE;
			capa_seen |= (uint8_t)(1U << index_local1);
			parsed.capabilities[index_local1] =
				ax211_get_le32(data + 4U);
			break;
		case AX211_RECORD_IMAGE_VERSION:
			/* Handles the seen condition. */
			if ((seen & 4U) != 0U)
				return INTEL_AX211_DUPLICATE;

			/* Handles the tlv length condition. */
			if (tlv_length != 12U)
				return INTEL_AX211_INVALID;
			seen |= 4U;
			parsed.api_major = ax211_get_le32(data);
			parsed.api_minor = ax211_get_le32(data + 4U);
			parsed.api_serial = ax211_get_le32(data + 8U);
			break;
		case AX211_RECORD_LOADER_IMAGE:
			/* Handles the seen condition. */
			if ((seen & 8U) != 0U)
				return INTEL_AX211_DUPLICATE;

			/* Handles the tlv length condition. */
			if (tlv_length == 0U)
				return INTEL_AX211_INVALID;
			seen |= 8U;
			parsed.iml_offset = offset;
			parsed.iml_length = tlv_length;
			break;
		case AX211_RECORD_COMMAND_VERSION_TABLE:
			/* Handles the seen condition. */
			if ((seen & 16U) != 0U)
				return INTEL_AX211_DUPLICATE;

			/* Handles the tlv length condition. */
			if (tlv_length == 0U || (tlv_length & 3U) != 0U)
				return INTEL_AX211_INVALID;
			seen |= 16U;
			parsed.command_versions_offset = offset;
			parsed.command_versions_length = tlv_length;
			break;
		default:
			/* Checks the ax211 ignored firmware tlv result. */
			if (!ax211_ignored_firmware_tlv(type))
				return INTEL_AX211_UNSUPPORTED;
			break;
		}

		offset += span;
	}

	/* Handles the seen condition. */
	if ((seen & 31U) != 31U || parsed.runtime_count == 0U ||
	    cpu_separator == SIZE_MAX || paging_separator == SIZE_MAX) {
		/* Returns the computed result. */
		return INTEL_AX211_MISSING;
	}

	/* Handles the cpu separator condition. */
	if (cpu_separator == 0U || paging_separator <= cpu_separator + 1U ||
	    paging_separator + 1U >= parsed.runtime_count) {
		/* Returns the computed result. */
		return INTEL_AX211_MISSING;
	}

	/* Handles the parsed condition. */
	if (parsed.api_major != INTEL_AX211_FIRMWARE_API ||
	    parsed.api_minor != INTEL_AX211_FIRMWARE_MINOR ||
	    parsed.api_serial != INTEL_AX211_FIRMWARE_SERIAL) {
		/* Returns the computed result. */
		return INTEL_AX211_IDENTITY_MISMATCH;
	}
	parsed.lmac_count = cpu_separator;
	parsed.umac_count = paging_separator - cpu_separator - 1U;
	parsed.paging_count = parsed.runtime_count - paging_separator - 1U;
	*manifest = parsed;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 sku equal operation.
 */
int
drv_intel_ax211_sku_equal(
	const struct intel_ax211_board_signature *left,
	const struct intel_ax211_board_signature *right)
{
	/* Handles the left availability. */
	if (left == NULL || right == NULL)
		return 0;

	/* Returns the computed result. */
	return left->data[0] == right->data[0] &&
	       left->data[1] == right->data[1] &&
	       left->data[2] == right->data[2];
}

/*
 * Implements the drv intel ax211 pnvm parse operation.
 */
int
drv_intel_ax211_pnvm_select(
	const uint8_t *bytes,
	size_t length,
	const struct intel_ax211_board_signature *sku,
	uint16_t mac_type,
	uint16_t rf_id,
	struct intel_ax211_pnvm_manifest *manifest)
{
	int error;
	struct intel_ax211_board_signature found;
	uint16_t found_mac;
	uint16_t found_rf;
	struct intel_ax211_section *section;
	size_t data_length;
	uint32_t destination;
	uint32_t type, tlv_length;
	size_t span;
	const uint8_t *data;
	int result;
	struct intel_ax211_pnvm_manifest candidate;
	size_t offset = 0;
	int active = 0;
	int version_seen = 0;
	int hardware_match = 0;

	/* Handles the bytes availability. */
	if (bytes == NULL || sku == NULL || manifest == NULL)
		return INTEL_AX211_INVALID;
	kern_memset(&candidate, 0, sizeof(candidate));

	/* Process each remaining element. */
	while (offset < length) {
		/* Checks the current data length. */
		if (length - offset < INTEL_AX211_FIRMWARE_RECORD_HEADER_SIZE)
			return INTEL_AX211_TRUNCATED;
		type = ax211_get_le32(bytes + offset);
		tlv_length = ax211_get_le32(bytes + offset + 4U);
		offset += INTEL_AX211_FIRMWARE_RECORD_HEADER_SIZE;

		/* Checks the operation result. */
		result = ax211_tlv_span(tlv_length, length - offset, &span);
		if (result != INTEL_AX211_SUCCESS)
			return result;
		data = bytes + offset;

		/* Handles the type condition. */
		if (type == AX211_RECORD_PNVM_BOARD) {
			/* Handles the tlv length condition. */
			if (tlv_length != 12U)
				return INTEL_AX211_INVALID;

			/* Handles the active condition. */
			if (active) {
				/* Checks the operation result. */
				result = ax211_pnvm_finish(
					&candidate, active, version_seen,
					hardware_match, mac_type, rf_id,
					manifest);
				if (result == INTEL_AX211_SUCCESS)
					return result;
				if (result != INTEL_AX211_IDENTITY_MISMATCH)
					return result;
			}

			found.data[0] = ax211_get_le32(data);
			found.data[1] = ax211_get_le32(data + 4U);
			found.data[2] = ax211_get_le32(data + 8U);
			active = drv_intel_ax211_sku_equal(&found, sku);
			version_seen = 0;
			hardware_match = 0;
			kern_memset(&candidate, 0, sizeof(candidate));
			candidate.sku = found;
		} else if (type == AX211_RECORD_PNVM_REVISION) {
			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;

			/* Handles the active condition. */
			if (active) {
				/* Handles the version seen condition. */
				if (version_seen)
					return INTEL_AX211_DUPLICATE;
				version_seen = 1;
				candidate.version = ax211_get_le32(data);
			}
		} else if (type == AX211_RECORD_HARDWARE_KIND) {
			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;

			/* Handles the active condition. */
			if (active) {
				found_mac = ax211_get_le16(data);

				/* Handles the found mac condition. */
				found_rf = ax211_get_le16(data + 2U);
				if (found_mac == mac_type &&
				    found_rf == rf_id) {
					hardware_match = 1;
					candidate.mac_type = found_mac;
					candidate.rf_id = found_rf;
				}
			}
		} else if (type == AX211_RECORD_RUNTIME_SECTION) {
			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;

			/* Handles the active condition. */
			destination = ax211_get_le32(data);
			if (active &&
			    destination != INTEL_AX211_PNVM_IMAGE_MARK) {
				/* Handles the data length condition. */
				data_length = (size_t)tlv_length - 4U;
				if (data_length == 0U)
					return INTEL_AX211_INVALID;

				/* Handles the candidate condition. */
				if (candidate.section_count >=
				    INTEL_AX211_PNVM_SECTION_LIMIT) {
					/* Returns the computed result. */
					return INTEL_AX211_OVERFLOW;
				}

				/* Handles the candidate condition. */
				if (candidate.total_length >
				    SIZE_MAX - data_length) {
					/* Returns the computed result. */
					return INTEL_AX211_OVERFLOW;
				}
				section = &candidate.section
						   [candidate.section_count++];
				section->destination = destination;
				section->file_offset = offset + 4U;
				section->length = data_length;
				candidate.total_length += data_length;
			}
		} else {
			/* Returns the computed result. */
			return INTEL_AX211_UNSUPPORTED;
		}

		offset += span;
	}

	/* Obtains the ax211 pnvm finish result. */
	error =
		ax211_pnvm_finish(&candidate, active, version_seen,
				  hardware_match, mac_type, rf_id, manifest);

	/* Returns the computed result. */
	return error;
}

/*
 * Implements the drv intel ax211 pnvm inspect operation.
 */
int
drv_intel_ax211_pnvm_inspect(
	const uint8_t *bytes,
	size_t length,
	struct intel_ax211_pnvm_inventory *inventory)
{
	uint16_t mac_type, rf_type;
	size_t section_length;
	uint32_t type, tlv_length;
	size_t span;
	const uint8_t *data;
	int result;
	struct intel_ax211_pnvm_inventory found;
	size_t offset = 0;

	/* Handles the bytes availability. */
	if (bytes == NULL || inventory == NULL)
		return INTEL_AX211_INVALID;
	kern_memset(&found, 0, sizeof(found));

	/* Process each remaining element. */
	while (offset < length) {
		/* Checks the current data length. */
		if (length - offset < INTEL_AX211_FIRMWARE_RECORD_HEADER_SIZE)
			return INTEL_AX211_TRUNCATED;
		type = ax211_get_le32(bytes + offset);
		tlv_length = ax211_get_le32(bytes + offset + 4U);
		offset += INTEL_AX211_FIRMWARE_RECORD_HEADER_SIZE;

		/* Checks the operation result. */
		result = ax211_tlv_span(tlv_length, length - offset, &span);
		if (result != INTEL_AX211_SUCCESS)
			return result;
		data = bytes + offset;

		/* Handles the type condition. */
		if (type == AX211_RECORD_PNVM_BOARD) {
			/* Handles the tlv length condition. */
			if (tlv_length != 12U)
				return INTEL_AX211_INVALID;
			found.sku_count++;
		} else if (type == AX211_RECORD_PNVM_REVISION) {
			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;
			found.version_count++;
		} else if (type == AX211_RECORD_HARDWARE_KIND) {
			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;
			mac_type = ax211_get_le16(data);
			rf_type = ax211_get_le16(data + 2U);
			found.hardware_type_count++;

			/*
			 * Checks the drv intel ax211 mac type supported result.
			 */
			if (drv_intel_ax211_mac_type_supported(mac_type) &&
			    rf_type == INTEL_AX211_RADIO_GF)
				found.supported_hardware_type_count++;
		} else if (type == AX211_RECORD_RUNTIME_SECTION) {
			/* Handles the tlv length condition. */
			if (tlv_length < 4U)
				return INTEL_AX211_INVALID;

			/* Checks the ax211 get le32 result. */
			if (ax211_get_le32(data) !=
			    INTEL_AX211_PNVM_IMAGE_MARK) {
				/* Handles the section length condition. */
				section_length = (size_t)tlv_length - 4U;
				if (section_length == 0U)
					return INTEL_AX211_INVALID;

				/* Handles the found condition. */
				if (found.total_section_length >
				    SIZE_MAX - section_length) {
					/* Returns the computed result. */
					return INTEL_AX211_OVERFLOW;
				}
				found.total_section_length += section_length;
				found.section_count++;
			}
		} else {
			/* Returns the computed result. */
			return INTEL_AX211_UNSUPPORTED;
		}

		offset += span;
	}

	/* Handles the found condition. */
	if (found.sku_count == 0U || found.version_count == 0U ||
	    found.hardware_type_count == 0U || found.section_count == 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_MISSING;
	}
	*inventory = found;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 context info gen3 encode operation.
 */
int
drv_intel_ax211_boot_context_encode(
	uint8_t output[104],
	const struct intel_ax211_boot_context *context)
{
	/* Handles the output availability. */
	if (output == NULL || context == NULL)
		return INTEL_AX211_INVALID;

	/* Handles the context condition. */
	if (context->command_ring_size_code !=
		    INTEL_AX211_COMMAND_RING_CB_SIZE ||
	    context->response_ring_size_code !=
		    INTEL_AX211_RX_RING_CB_SIZE) {
		/* Returns the computed result. */
		return INTEL_AX211_INVALID;
	}
	kern_memset(output, 0, INTEL_AX211_BOOT_CONTEXT_SIZE);
	ax211_put_le16(output, context->version);
	ax211_put_le16(output + 2U, INTEL_AX211_BOOT_CONTEXT_SIZE / 4U);
	ax211_put_le32(output + 4U, context->config);
	ax211_put_le64(output + 8U, context->status_page_address);
	ax211_put_le64(output + 16U, context->completion_head_array_address);
	ax211_put_le64(output + 24U, context->transfer_tail_array_address);
	ax211_put_le64(output + 32U, context->completion_tail_array_address);
	ax211_put_le64(output + 40U, context->transfer_head_array_address);
	ax211_put_le16(output + 48U, context->completion_index_slots);
	ax211_put_le16(output + 50U, context->transfer_index_slots);
	ax211_put_le64(output + 52U, context->command_ring_address);
	ax211_put_le64(output + 60U, context->response_ring_address);
	ax211_put_le16(output + 68U, context->command_ring_size_code);
	ax211_put_le16(output + 70U, context->response_ring_size_code);
	ax211_put_le16(output + 72U, context->command_ring_doorbell);
	ax211_put_le16(output + 74U, context->response_ring_doorbell);
	ax211_put_le16(output + 76U, context->command_ring_vector);
	ax211_put_le16(output + 78U, context->response_ring_vector);
	output[80] = context->command_entry_header_words;
	output[81] = context->command_entry_footer_words;
	output[82] = context->response_entry_header_words;
	output[83] = context->response_entry_footer_words;
	ax211_put_le16(output + 84U, context->ring_control_flags);
	ax211_put_le16(output + 86U, context->status_page_vector);
	ax211_put_le64(output + 88U, context->scratch_area_address);
	ax211_put_le32(output + 96U, context->scratch_area_size);

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 rx transfer descriptor encode operation.
 */
int
drv_intel_ax211_rx_transfer_descriptor_encode(
	uint8_t output[16],
	uint16_t buffer_id,
	uint64_t address)
{
	/* Handles the output availability. */
	if (output == NULL)
		return INTEL_AX211_INVALID;
	kern_memset(output, 0, INTEL_AX211_RX_TRANSFER_DESCRIPTOR_SIZE);
	ax211_put_le16(output, buffer_id);
	ax211_put_le64(output + 8U, address);

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 rx completion descriptor decode operation.
 */
int
drv_intel_ax211_rx_completion_descriptor_decode(
	const uint8_t input[32],
	uint16_t *buffer_id,
	uint8_t *flags)
{
	/* Handles the input availability. */
	if (input == NULL || buffer_id == NULL || flags == NULL)
		return INTEL_AX211_INVALID;
	*buffer_id = ax211_get_le16(input + 4U);
	*flags = input[6];

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 tfd encode operation.
 */
int
drv_intel_ax211_tfd_encode(
	uint8_t output[256],
	const struct intel_ax211_tfd_buffer *buffers,
	size_t buffer_count)
{
	size_t offset;
	size_t index;

	/* Handles the output availability. */
	if (output == NULL || buffers == NULL || buffer_count == 0U ||
	    buffer_count > INTEL_AX211_TRANSFER_DESCRIPTOR_BUFFERS) {
		/* Returns the computed result. */
		return INTEL_AX211_INVALID;
	}
	kern_memset(output, 0, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE);
	ax211_put_le16(output, (uint16_t)buffer_count);

	/* Process each remaining element. */
	for (index = 0; index < buffer_count; index++) {
		offset = 2U + index * 10U;

		/* Handles the buffers condition. */
		if (buffers[index].length == 0U ||
		    buffers[index].length > INTEL_AX211_TRANSFER_BUFFER_LENGTH_MAX) {
			drv_intel_ax211_scrub(output, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE);

			/* Returns the computed result. */
			return INTEL_AX211_INVALID;
		}

		ax211_put_le16(output + offset, buffers[index].length);
		ax211_put_le64(output + offset + 2U, buffers[index].address);
	}

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 narrow command encode operation.
 */
int
drv_intel_ax211_narrow_command_encode(
	uint8_t output[4],
	uint8_t opcode,
	uint8_t flags,
	const struct intel_ax211_ring_token *token)
{
	/* Handles the output availability. */
	if (output == NULL || token == NULL)
		return INTEL_AX211_INVALID;
	output[0] = opcode;
	output[1] = flags;
	output[2] = token->index;
	output[3] = token->queue;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 wide command encode operation.
 */
int
drv_intel_ax211_wide_command_encode(
	uint8_t output[8],
	const struct intel_ax211_command_id *command,
	uint16_t payload_length,
	const struct intel_ax211_ring_token *token)
{
	/* Handles the output availability. */
	if (output == NULL || command == NULL || token == NULL ||
	    payload_length > INTEL_AX211_MAX_COMMAND_PAYLOAD) {
		/* Returns the computed result. */
		return INTEL_AX211_INVALID;
	}
	output[0] = command->opcode;
	output[1] = command->group;
	output[2] = token->index;
	output[3] = token->queue;
	ax211_put_le16(output + 4U, payload_length);
	output[6] = 0;
	output[7] = command->version;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 event decode operation.
 */
int
drv_intel_ax211_event_decode(
	const uint8_t *bytes,
	size_t length,
	struct intel_ax211_event *event)
{
	uint32_t length_flags;
	size_t frame_length;

	/* Handles the bytes availability. */
	if (bytes == NULL || event == NULL)
		return INTEL_AX211_INVALID;

	/* Checks the current data length. */
	if (length < INTEL_AX211_EVENT_HEADER_SIZE)
		return INTEL_AX211_TRUNCATED;
	length_flags = ax211_get_le32(bytes);

	/* Handles the frame length condition. */
	frame_length = (size_t)(length_flags & 0x3fffU);
	if (frame_length < INTEL_AX211_NARROW_COMMAND_HEADER_SIZE)
		return INTEL_AX211_INVALID;

	/* Handles the frame length condition. */
	if (frame_length > length - 4U)
		return INTEL_AX211_TRUNCATED;
	kern_memset(event, 0, sizeof(*event));
	event->command.opcode = bytes[4];
	event->flags = bytes[5];
	event->index = bytes[6];
	event->queue = bytes[7];
	event->rx_queue = (uint8_t)((length_flags >> 16) & 0x3fU);
	event->payload_offset = INTEL_AX211_EVENT_HEADER_SIZE;
	event->payload_length = frame_length - 4U;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 ring init operation.
 */
int
drv_intel_ax211_ring_init(
	struct intel_ax211_ring *ring,
	uint8_t queue,
	uint16_t capacity)
{
	/* Handles the ring availability. */
	if (ring == NULL || capacity == 0U ||
	    capacity > INTEL_AX211_COMMAND_RING_SIZE ||
	    (capacity & (uint16_t)(capacity - 1U)) != 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_INVALID;
	}
	kern_memset(ring, 0, sizeof(*ring));
	ring->capacity = capacity;
	ring->queue = queue;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 ring reserve operation.
 */
int
drv_intel_ax211_ring_reserve(
	struct intel_ax211_ring *ring,
	struct intel_ax211_ring_token *token)
{
	/* Handles the ring availability. */
	if (ring == NULL || token == NULL || ring->capacity == 0U)
		return INTEL_AX211_INVALID;

	/* Handles the ring condition. */
	if (ring->used == ring->capacity)
		return INTEL_AX211_FULL;
	token->queue = ring->queue;
	token->index = (uint8_t)ring->head;
	ring->head = (uint16_t)((ring->head + 1U) & (ring->capacity - 1U));
	ring->used++;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 ring complete operation.
 */
int
drv_intel_ax211_ring_complete(
	struct intel_ax211_ring *ring,
	const struct intel_ax211_ring_token *token)
{
	/* Handles the ring availability. */
	if (ring == NULL || token == NULL || ring->capacity == 0U)
		return INTEL_AX211_INVALID;

	/* Handles the ring condition. */
	if (ring->used == 0U || token->queue != ring->queue ||
	    token->index != (uint8_t)ring->tail) {
		/* Returns the computed result. */
		return INTEL_AX211_STALE;
	}
	ring->tail = (uint16_t)((ring->tail + 1U) & (ring->capacity - 1U));
	ring->used--;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 ring available operation.
 */
size_t
drv_intel_ax211_ring_available(
	const struct intel_ax211_ring *ring)
{
	/* Handles the ring availability. */
	if (ring == NULL || ring->capacity == 0U || ring->used > ring->capacity)
		return 0;

	/* Returns the computed result. */
	return (size_t)(ring->capacity - ring->used);
}

/*
 * Implements the drv intel ax211 scrub operation.
 */
void
drv_intel_ax211_scrub(
	void *memory,
	size_t length)
{
	volatile uint8_t *bytes = (volatile uint8_t *)memory;

	/* Handles the memory availability. */
	if (memory == NULL)
		return;

	/* Process each remaining element. */
	while (length-- != 0U)
		*bytes++ = 0;
}

/*
 * Implements the drv intel ax211 staging set operation.
 */
int
drv_intel_ax211_staging_set(
	struct intel_ax211_staging *staging,
	const void *data,
	size_t length)
{
	/* Handles the staging availability. */
	if (staging == NULL || (data == NULL && length != 0U))
		return INTEL_AX211_INVALID;
	drv_intel_ax211_staging_clear(staging);

	/* Checks the current data length. */
	if (length > sizeof(staging->bytes))
		return INTEL_AX211_OVERFLOW;

	/* Checks the current data length. */
	if (length != 0U)
		kern_memcpy(staging->bytes, data, length);
	staging->length = length;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/*
 * Implements the drv intel ax211 staging clear operation.
 */
void
drv_intel_ax211_staging_clear(
	struct intel_ax211_staging *staging)
{
	/* Handles the staging availability. */
	if (staging == NULL)
		return;
	drv_intel_ax211_scrub(staging->bytes, sizeof(staging->bytes));
	staging->length = 0;
}
/* Supports the ax211 get le16 operation. */
static uint16_t
ax211_get_le16(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

/* Supports the ax211 get le32 operation. */
static uint32_t
ax211_get_le32(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	       ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Supports the ax211 put le16 operation. */
static void
ax211_put_le16(
	uint8_t *bytes,
	uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

/* Supports the ax211 put le32 operation. */
static void
ax211_put_le32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

/* Supports the ax211 put le64 operation. */
static void
ax211_put_le64(
	uint8_t *bytes,
	uint64_t value)
{
	unsigned int index;

	/* Process each remaining element. */
	for (index = 0; index < 8; index++)
		bytes[index] = (uint8_t)(value >> (index * 8));
}

/* Supports the ax211 tlv span operation. */
static int
ax211_tlv_span(
	uint32_t value,
	size_t remaining,
	size_t *span)
{
	size_t length = (size_t)value;

	/* Checks the current data length. */
	if (length > SIZE_MAX - 3U)
		return INTEL_AX211_OVERFLOW;

	/* Checks the current data length. */
	length = (length + 3U) & ~(size_t)3U;
	if (length > remaining)
		return INTEL_AX211_TRUNCATED;
	*span = length;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/* Supports the ax211 ignored firmware tlv operation. */
static int
ax211_ignored_firmware_tlv(
	uint32_t type)
{
	/* Handles the type condition. */
	if ((type >= 1U && type <= 18U) ||
	    (type >= AX211_RECORD_WAKE_SECTION && type <= AX211_RECORD_CALIBRATION_DEFAULTS) ||
	    (type >= AX211_RECORD_CIPHER_SCHEMES && type <= AX211_RECORD_SCAN_CHANNEL_LIMIT) ||
	    type == AX211_RECORD_PAGED_SIZE || type == AX211_RECORD_SNIFFER_SECTION ||
	    type == AX211_RECORD_SDIO_DMA_ADDRESS || type == AX211_RECORD_TRACE_DESTINATION ||
	    type == AX211_RECORD_TRACE_SETUP || type == AX211_RECORD_TRACE_TRIGGER ||
	    (type >= AX211_RECORD_BACKGROUND_SCAN_LIMITS && type <= AX211_RECORD_LOWER_CPU_TRACE_ADDRESSES) ||
	    (type >= AX211_RECORD_RECOVERY_BUFFER && type <= AX211_RECORD_RADIO_INTEGRATION_REVISION) ||
	    type == AX211_RECORD_PNVM_REVISION || type == AX211_RECORD_PNVM_BOARD ||
	    type == 65U || (type >= AX211_RECORD_SECTION_TABLE_ADDRESS && type <= 69U) ||
	    type == AX211_RECORD_PNVM_PAYLOAD || type == AX211_RECORD_STATION_LIMIT ||
	    type == 0x101U || type == AX211_RECORD_BEACON_LIMIT ||
	    (type >= 0x1000000U && type <= 0x1000004U) ||
	    (type >= AX211_RECORD_TRACE_RANGE_FIRST && type <= AX211_RECORD_TRACE_RANGE_LAST) ||
	    type == 0x100000bU || type == 0x100000cU || type == 1092U) {
		/* Reports operation failure. */
		return 1;
	}

	/* Succeeded. */
	return 0;
}

/* Supports the ax211 pnvm finish operation. */
static int
ax211_pnvm_finish(
	const struct intel_ax211_pnvm_manifest *candidate,
	int active,
	int version_seen,
	int hardware_match,
	uint16_t mac_type,
	uint16_t rf_id,
	struct intel_ax211_pnvm_manifest *manifest)
{
	/* Handles the active condition. */
	if (!active)
		return INTEL_AX211_MISSING;

	/* Handles the version seen condition. */
	if (!version_seen || candidate->section_count == 0U)
		return INTEL_AX211_MISSING;

	/* Handles the hardware match condition. */
	if (!hardware_match || candidate->mac_type != mac_type ||
	    candidate->rf_id != rf_id) {
		/* Returns the computed result. */
		return INTEL_AX211_IDENTITY_MISMATCH;
	}
	*manifest = *candidate;

	/* Returns the computed result. */
	return INTEL_AX211_SUCCESS;
}

/* End consolidated intel-ax211.c. */

/* Begin consolidated pci-intel-ax211.c. */

/*
 * Intel AX211 PCI/CNVio2 transport.
 *
 * Binds the PCI function, runs the firmware epochs and connects the device to
 * the network interface and to the common WLAN layer.
 */

#include <drivers/wifi/intel-ax211/pci-intel-ax211.h>
#include <drivers/pci/pci.h>

#include "intel-ax211-assoc.h"
#include "intel-ax211-bss.h"
#include "intel-ax211-boot.h"
#include "intel-ax211-key.h"
#include "intel-ax211-mmio.h"
#include "intel-ax211-pci-mmio.h"
#include "intel-ax211-rx.h"
#include "intel-ax211-runtime-start.h"
#include "intel-ax211-scan-session.h"
#include "intel-ax211-transport-backend.h"
#include "intel-ax211-tx-ring.h"

#include <uapi/errno.h>
#include <limits.h>
#include <kern/clock.h>
#include <kern/lock.h>
#include <kern/net/net-device.h>
#include <kern/net/packet-buf.h>
#include <kern/net/wlan.h>
#include <kern/sched.h>
#include <stdint.h>
#include "kern/klog.h"
#include "kern/kmem.h"
#include "kern/device-io.h"

/*
 * The PCI identity and the PCI configuration values the probe checks.
 *
 * The identity is the one the hardware intake of the test machines (ws004
 * HW-T37) found.  The class code is 0x0280 (other network controller) and the
 * command register bits are memory space 1 and bus master 2.  BAR0 must map at
 * least 16 KiB: the MSI-X cause and vector registers this driver uses lie up
 * to 0x2890, and the receive free-index doorbell at 0x1c80.
 *
 * Values referenced: the PCI ID database (vendor 8086, device 51f0) for the
 * identity, and the PCI Local Bus Specification (the class code and the
 * command register bits).
 */
#define AX211_PCI_VENDOR 0x8086U
#define AX211_PCI_PRODUCT 0x51f0U
#define AX211_PCI_SUBVENDOR 0x8086U
#define AX211_PCI_SUBPRODUCT 0x4090U
#define AX211_PCI_REVISION 0x01U
#define AX211_PCI_CLASS 0x028000U
#define AX211_PCI_CLASS_MASK 0xffffffU
#define AX211_PCI_COMMAND 0x04U
#define AX211_PCI_COMMAND_MEMORY 0x0002U
#define AX211_PCI_COMMAND_MASTER 0x0004U
#define AX211_BAR0_MINIMUM_SIZE 0x4000U

/* zedBSD's own limits and timings of the network interface and its events. */
#define AX211_MTU 1500U
#define AX211_MAC_ADDRESS_SIZE 6U
#define AX211_PASSIVE_CHANNEL_COUNT 11U
#define AX211_RECEIVE_POLL_DELAY_US 50U
#define AX211_DIRECT_EVENT_LIMIT 64U
#define AX211_DEFERRED_EVENT_LIMIT 16U
#define AX211_DIRECT_TIMEOUT_US 1000000U
#define AX211_LIFECYCLE_JOIN_TICKS (5U * KERN_CLOCK_HZ)

/*
 * How many times in a row a recovery may restart the epoch by itself before
 * the interface is left down for the user to cycle (BUG-158).  A scan that
 * completes resets the count.
 */
#define AX211_REOPEN_ATTEMPTS_MAX 3U

/*
 * The station slot of the access point, and the rate and capability bits of
 * the link.
 *
 * The capability bits are short preamble at bit 5 and short slot time at bit
 * 10.  The CCK bitmap 0x0f is 1, 2, 5.5 and 11 Mbit/s and the OFDM bitmap 0x15
 * is 6, 12 and 24 Mbit/s, the mandatory rates.  On 5 GHz the CCK bitmap is
 * reduced to bit 0, because the link command keeps a non-empty CCK set even
 * where the firmware sends no CCK frame.  The access point always takes
 * station slot 0.
 *
 * Values referenced: IEEE Std 802.11-2020 9.4.1.4 (the capability bits and
 * the basic rates) and Linux v6.17
 * drivers/net/wireless/intel/iwlwifi/fw/api/mac.h (the firmware's rate
 * bitmaps).
 */
#define AX211_PEER_SLOT 0U
#define AX211_ASSOC_CCK_ACK_RATES 0x0fU
#define AX211_ASSOC_OFDM_ACK_RATES 0x15U
#define AX211_CAPABILITY_SHORT_PREAMBLE 0x0020U
#define AX211_CAPABILITY_SHORT_SLOT 0x0400U

/*
 * The firmware's report that it rejected a command.
 *
 * The event arrives unsolicited (bit 7 of the ring byte of its header set);
 * the driver logs the error type at byte 0, the rejected opcode at byte 4, the
 * rejected sequence number at byte 6, which names the ring slot of the command
 * that failed, and the word at byte 8.
 *
 * Values referenced: Linux v6.17
 * drivers/net/wireless/intel/iwlwifi/fw/api/commands.h (opcode 2 of the
 * classic family) and drivers/net/wireless/intel/iwlwifi/fw/api/alive.h
 * (struct iwl_error_resp, 20 bytes).
 */
#define AX211_COMMAND_REJECTION_OPCODE 0x02U
#define AX211_COMMAND_REJECTION_SIZE 20U

/*
 * The doorbell that tells the firmware the PNVM sections are in place.
 *
 * The register is in the periphery space of the upper CPU, so it is written at
 * the upper-CPU base (0x300000 on this device) plus 0xa05c04, while the driver
 * holds the device awake.  The driver rings it after the PNVM sections and
 * their address table have been written to DMA memory and published in the
 * scratch area; the firmware answers with the PNVM-accepted notification.
 *
 * Values referenced: Linux v6.17 drivers/net/wireless/intel/iwlwifi/iwl-prph.h
 * (the doorbell register of the upper CPU and its PNVM bit).
 */
#define AX211_PRPH_FIRMWARE_DOORBELL 0xa05c04U
#define AX211_FIRMWARE_DOORBELL_PNVM_READY (1U << 20)

/*
 * The PCI Express latency tolerance (LTR) enable of the root port side.
 *
 * Device Control 2 is at offset 0x28 of the PCI Express capability and its
 * LTR mechanism enable is bit 10.  The driver reads it to decide whether the
 * firmware's latency tolerance command is sent at runtime.
 *
 * Values referenced: the PCI Express Base Specification (Device Control 2,
 * LTR Mechanism Enable).
 */
#define AX211_PCIE_CAPABILITY 0x10U
#define AX211_PCIE_DEVICE_CONTROL2 0x28U
#define AX211_PCIE_DEVICE_CONTROL2_LTR (1U << 10)

/*
 * The window through which the host reads the firmware's internal SRAM, and
 * the error tables found there.
 *
 * Writing an SRAM address to 0x40c and then reading 0x41c repeatedly returns
 * consecutive words; the address advances by four bytes after each read.  The
 * ALIVE notification gives the address of each table; the UMAC pointer carries
 * tag bits 31:30 that are not part of the address and are masked off.  The
 * driver refuses addresses below 0x400000 as a sanity bound, and reads the
 * tables only when a command failed while a fault cause was raised, to log the
 * error identifier, the data words, the interrupt status words and the last
 * host command.
 *
 * Values referenced: Linux v6.17 drivers/net/wireless/intel/iwlwifi/iwl-csr.h
 * (the target memory read address and data registers),
 * drivers/net/wireless/intel/iwlwifi/fw/img.h (the cache-control tag bits of
 * a firmware pointer) and drivers/net/wireless/intel/iwlwifi/fw/dump.c (the
 * LMAC error table of 30 words and the UMAC error table of 15 words).
 */
#define AX211_CSR_SRAM_READ_ADDRESS 0x40cU
#define AX211_CSR_SRAM_READ_DATA 0x41cU
#define AX211_SRAM_POINTER_TAG_BITS 0xc0000000U
#define AX211_ERROR_LOG_MIN_ADDRESS 0x00400000U
#define AX211_LMAC_ERROR_WORD_COUNT 30U
#define AX211_UMAC_ERROR_WORD_COUNT 15U

struct ax211_pci_staged_key {
	struct intel_ax211_key_request request;
	uint8_t valid;
	uint8_t programmed;
	uint8_t reserved[6];
};

struct ax211_pci_deferred_event {
	struct intel_ax211_boot_received_event received;
	uint8_t bytes[INTEL_AX211_BOOT_EVENT_CAPACITY];
};

/*
 * The silicon revision and radio identity registers, read at attach.
 *
 * The MAC type is bits 15:4 of the silicon revision register (0x37 for So,
 * 0x43 for SoF); the radio kind is bits 23:12 of the radio identity register
 * (0x10d for Garfield Peak), and bit 28 of that register is set on a radio
 * that can run two bands at once.  The driver attaches only to an So or SoF
 * MAC with a single-band Garfield Peak radio, the combination the pinned
 * firmware and PNVM files are built for (ws004 p038).
 *
 * Values referenced: Linux v6.17 drivers/net/wireless/intel/iwlwifi/iwl-csr.h
 * (the two registers and the field positions) and
 * drivers/net/wireless/intel/iwlwifi/iwl-config.h (the MAC and radio codes).
 */
#define AX211_CSR_SILICON_REVISION 0x028U
#define AX211_CSR_RADIO_IDENTITY 0x09cU
#define AX211_SILICON_FAMILY_MASK 0x0000fff0U
#define AX211_SILICON_FAMILY_SHIFT 4U
#define AX211_SILICON_FAMILY_SO 0x037U
#define AX211_SILICON_FAMILY_SOF 0x043U
#define AX211_RADIO_KIND_MASK 0x00fff000U
#define AX211_RADIO_KIND_SHIFT 12U
#define AX211_RADIO_KIND_GF 0x10dU
#define AX211_RADIO_CONCURRENT_BANDS_FLAG 0x10000000U

struct ax211_pci_controller {
	struct mutex lifecycle_lock;
	struct spinlock interrupt_lock;
	struct drv_pci_device *device;
	struct drv_pci_mapping mapping;
	struct drv_pci_enable_state enable_state;
	struct drv_pci_bar original_bar;
	struct intel_ax211_pci_mmio_backend backend;
	struct intel_ax211_mmio mmio;
	struct intel_ax211_transport_backend transport_backend;
	struct intel_ax211_transport transport;
	struct intel_ax211_boot boot;
	struct intel_ax211_runtime_start runtime_start;
	struct intel_ax211_scan_session scan_session;
	struct intel_ax211_bss_cache bss_staging_cache;
	struct intel_ax211_bss_cache bss_published_cache;
	struct intel_ax211_bss_entry selected_bss;
	struct drv_intel_ax211_bss_assoc_metadata selected_metadata;
	struct intel_ax211_assoc_state association;
	struct intel_ax211_key_state keys;
	struct ax211_pci_staged_key staged_pairwise_key;
	struct ax211_pci_staged_key
		staged_group_key[INTEL_AX211_KEY_INDEX_LIMIT];
	struct ax211_pci_deferred_event
		deferred_event[AX211_DEFERRED_EVENT_LIMIT];
	uint8_t deferred_dispatch_event[INTEL_AX211_BOOT_EVENT_CAPACITY];
	uint64_t retired_pairwise_key_generation;
	uint64_t retired_group_key_generation[INTEL_AX211_KEY_INDEX_LIMIT];
	uint8_t retired_group_key_remove[INTEL_AX211_KEY_INDEX_LIMIT];
	struct intel_ax211_frame_ring frame_ring;
	struct intel_ax211_tx_queue_config tx_queue_config;
	struct intel_ax211_dma_resources *active_dma;
	struct drv_pci_irq irq;
	void *irq_cookie;
	struct net_device *net_device;
	struct wlan_station *station;
	struct ax211_pci_controller *next;
	uint32_t hardware_revision;
	uint32_t radio_identity;
	uint32_t hardware_epoch;
	uint8_t runtime_event[INTEL_AX211_BOOT_EVENT_CAPACITY];
	uint8_t runtime_frame[INTEL_AX211_RX_FRAME_BODY_MAX];
	uint8_t last_receive_header[INTEL_AX211_EVENT_HEADER_SIZE];
	size_t last_receive_length;
	uint8_t last_receive_version;
	uint32_t command_fh_causes;
	uint32_t command_hw_causes;
	uint32_t command_raw_fh_causes;
	uint32_t command_raw_hw_causes;
	uint8_t tx_report_completion[INTEL_AX211_TX_RING_DESCRIPTOR_SLOTS];
	uint32_t scan_step_index;
	uint64_t bss_staging_generation;
	uint64_t bss_published_generation;
	uint64_t connection_generation;
	uint64_t recovery_generation;
	uint64_t next_management_cookie;
	uint64_t control_deadline_ticks;
	int recovery_error;
	unsigned refresh_epoch;
	unsigned deferred_event_head;
	unsigned deferred_event_count;
	unsigned deferred_event_draining;
	unsigned operations_active;
	unsigned irq_count;
	unsigned irq_allocated;
	unsigned irq_established;
	unsigned receive_enabled;
	unsigned irq_latched;
	unsigned boot_initialized;
	unsigned runtime_initialized;
	unsigned runtime_active;
	unsigned operation_admission_open;
	unsigned recovery_pending;
	unsigned recovery_running;
	unsigned poll_active;
	unsigned poll_reschedule;
	unsigned scan_initialized;
	unsigned bss_staging_initialized;
	unsigned bss_published_initialized;
	unsigned selected_bss_valid;
	unsigned association_initialized;
	unsigned keys_initialized;
	unsigned tx_ring_allocated;
	unsigned bar_claimed;
	unsigned bar_mapped;
	unsigned bar_may_have_moved;
	unsigned original_bar_valid;
	unsigned state_saved;
	unsigned driver_data_set;
	unsigned listed;
	unsigned refresh_busy;
	unsigned detaching;
	unsigned ready;
	unsigned quarantined;

	/* True only after every runtime/DMA owner passed the checked stop. */
	unsigned session_stopped;
	unsigned close_pending;
	uint64_t reject_log_deadline;
	unsigned net_live;
	unsigned station_attached;

	/*
	 * The net device is open: ax211_net_open succeeded and no close
	 * followed.
	 */
	unsigned net_opened;

	/*
	 * A recovery stopped the epoch while the device stayed open and asked the
	 * WLAN retirement thread to restart it (BUG-158).  Cleared by the restart,
	 * by an explicit open or close, and by detach.
	 */
	unsigned reopen_pending;

	/*
	 * Restarts since the last scan that completed; bounded by
	 * AX211_REOPEN_ATTEMPTS_MAX.
	 */
	unsigned reopen_attempts;

	/*
	 * Set by a suspend that found the radio off, until its resume
	 * (ws052-p005): an open is refused meanwhile.  Under lifecycle_lock.
	 */
	unsigned suspended;
};

static int ax211_pci_match(struct drv_pci_device *device, const struct drv_pci_id *identity);
static int ax211_pci_attach(struct drv_pci_device *device, const struct drv_pci_id *identity);
static int ax211_pci_detach(struct drv_pci_device *device, unsigned flags);
static int ax211_pci_suspend(struct drv_pci_device *device);
static int ax211_pci_resume(struct drv_pci_device *device);
static int ax211_pci_identity_matches(const struct drv_pci_device *device);
static int ax211_pci_bar_validate(const struct drv_pci_bar *bar);
static uint32_t ax211_pci_read32(const struct ax211_pci_controller *controller, unsigned offset);
static int ax211_pci_hardware_validate(uint32_t hardware_revision, uint32_t radio_identity);
static int ax211_pci_profile(struct ax211_pci_controller *controller, struct intel_ax211_mmio_profile *profile);
static int ax211_pci_acquire(struct ax211_pci_controller *controller);
static int ax211_pci_restore_bar(struct ax211_pci_controller *controller);
static int ax211_pci_release_resources(struct ax211_pci_controller *controller, int result, int retain_on_failure);
static int ax211_pci_quarantine(struct ax211_pci_controller *controller, int failure);
static uint64_t ax211_pci_lifecycle_deadline(void);
static int ax211_pci_station_pin_locked(struct ax211_pci_controller *controller, struct wlan_station **station);
static int ax211_pci_operation_enter_locked(struct ax211_pci_controller *controller, struct wlan_station **station);
static void ax211_pci_operation_leave_locked(struct ax211_pci_controller *controller);
static int ax211_pci_operations_join_locked(struct ax211_pci_controller *controller, uint64_t deadline);
static int ax211_pci_station_close_wait(struct wlan_station *station, uint64_t deadline);
static int ax211_pci_close_locked(struct ax211_pci_controller *);
static int ax211_pci_log_rejection(struct ax211_pci_controller *);
static void ax211_pci_recovery_latch_locked(struct ax211_pci_controller *controller, int error);
static void ax211_pci_stop_defer_locked(struct ax211_pci_controller *controller, int error);
static int ax211_pci_recovery_run_locked(struct ax211_pci_controller *controller);
static void ax211_pci_list_add(struct ax211_pci_controller *controller);
static int ax211_pci_list_remove(struct ax211_pci_controller *controller);
static void ax211_pci_list_forget(struct ax211_pci_controller *controller);
static struct ax211_pci_controller * ax211_pci_list_find_device(struct drv_pci_device *device);
static struct ax211_pci_controller *ax211_pci_refresh_claim(unsigned epoch);
static void ax211_pci_refresh_release(struct ax211_pci_controller *controller);
static int ax211_pci_refresh_one(struct ax211_pci_controller *controller);
static int ax211_pci_publish(struct ax211_pci_controller *controller, const uint8_t mac_address[AX211_MAC_ADDRESS_SIZE]);
static void ax211_pci_scan_profile(struct wlan_scan_profile *profile);
static int ax211_pci_partial_discard(struct ax211_pci_controller *controller);
static int ax211_pci_graph_detach(struct ax211_pci_controller *controller);
static int ax211_pci_session_stop(struct ax211_pci_controller *controller);
static int ax211_pci_ltr_enabled(struct ax211_pci_controller *controller, int *enabled);
static int ax211_pci_irq(void *argument);
static int ax211_pci_receive_epoch_begin(void *argument, uint32_t generation);
static int ax211_pci_transport_bind(void *argument, struct intel_ax211_dma_resources *dma, struct intel_ax211_mmio *mmio, struct intel_ax211_transport *transport, uint32_t generation);
static int ax211_pci_receive_event(void *argument, uint64_t deadline_us, uint8_t *bytes, size_t capacity, struct intel_ax211_boot_received_event *event);
static int ax211_pci_publish_pnvm(void *argument, struct intel_ax211_dma_resources *dma);
static int ax211_pci_after_alive(void *argument, const struct intel_ax211_protocol_alive *alive);
static int ax211_pci_interrupt_drain(void *argument);
static int ax211_pci_bus_master_disable(void *argument);
static int ax211_pci_clock_us(void *argument, uint64_t *time_us);
static int ax211_pci_wake_hold(void *argument);
static int ax211_pci_wake_release(void *argument);
static int ax211_pci_tx_event(const struct intel_ax211_event *event);
static uint8_t ax211_pci_notification_version(const uint8_t *bytes, size_t length);
static int ax211_pci_scan_initialize(struct ax211_pci_controller *controller);
static uint32_t ax211_pci_channel_frequency(uint8_t channel);
static int ax211_pci_runtime_scan_profile(const struct ax211_pci_controller *controller, struct wlan_scan_profile *profile);
static int ax211_pci_scan_channel_present(const struct ax211_pci_controller *controller, uint8_t channel);
static void ax211_pci_scan_clear(struct ax211_pci_controller *controller);
static void ax211_pci_bss_staging_discard(struct ax211_pci_controller *controller);
static int ax211_pci_bss_staging_publish(struct ax211_pci_controller *controller, uint64_t generation);
static int ax211_pci_event_message(const uint8_t *bytes, size_t length, const struct intel_ax211_boot_received_event *received, struct intel_ax211_event *event, struct intel_ax211_protocol_message *message);
static int ax211_pci_scan_command_dispatch(struct ax211_pci_controller *controller, const uint8_t *bytes, size_t length, uint64_t now);
static int ax211_pci_scan_notification_dispatch( struct ax211_pci_controller *controller, const struct intel_ax211_protocol_message *message, uint64_t now);
static int ax211_pci_rx_dispatch(struct ax211_pci_controller *controller, const struct intel_ax211_protocol_message *message);
static void ax211_pci_scan_report_error(struct ax211_pci_controller *controller, int result);
static int ax211_pci_scan_result_errno(int result);
static int ax211_pci_runtime_event_dispatch( struct ax211_pci_controller *controller, const uint8_t *bytes, size_t length, const struct intel_ax211_boot_received_event *event);
static int ax211_pci_deferred_event_enqueue( struct ax211_pci_controller *controller, const uint8_t *bytes, const struct intel_ax211_boot_received_event *received);
static int ax211_pci_deferred_event_drain_one(struct ax211_pci_controller *controller);
static void ax211_pci_deferred_event_clear(struct ax211_pci_controller *controller);
static uint32_t ax211_pci_get_le32(const uint8_t bytes[4]);
static void ax211_pci_scrub(void *memory, size_t length);
static int ax211_pci_tx_sync_for_device(void *argument, const struct drv_dma_buffer *buffer, size_t offset, size_t length);
static int ax211_pci_tx_write32(void *argument, uint32_t offset, uint32_t value);
static uint64_t ax211_pci_assoc_clock_us(void *argument);
static int ax211_pci_assoc_exchange(void *argument, const struct intel_ax211_assoc_command *command, struct intel_ax211_assoc_reply *reply);
static int ax211_pci_direct_command( struct ax211_pci_controller *controller, const struct intel_ax211_command_request *request, uint64_t deadline_ticks, uint8_t *response, size_t response_capacity, size_t *response_length, struct intel_ax211_protocol_message *completed_message, struct intel_ax211_protocol_pending_command *completed_pending);
static int ax211_pci_command_timeout(struct ax211_pci_controller *controller, uint64_t deadline_ticks, uint64_t now_us, uint64_t *timeout_us, uint64_t *deadline_us);
static int ax211_pci_sram_read_locked(struct ax211_pci_controller *controller, uint32_t address, uint32_t *words, size_t count);
static void ax211_pci_firmware_error_dump(struct ax211_pci_controller *controller);
static int ax211_pci_assoc_result_errno(int result);
static int ax211_pci_tx_ring_result_errno(int result);
static int ax211_pci_key_result_errno(int result);
static int ax211_pci_assoc_profile(struct ax211_pci_controller *controller, const struct wlan_bss_record *bss, uint64_t connection_generation, struct intel_ax211_assoc_profile *profile);
static int ax211_pci_assoc_rollback(struct ax211_pci_controller *controller, uint64_t generation);
static int ax211_pci_mcast_filter_configure(struct ax211_pci_controller *controller, uint64_t deadline_ticks);
static int ax211_pci_mac_power_configure(struct ax211_pci_controller *controller, uint64_t deadline_ticks);
static int ax211_pci_keys_remove_all(struct ax211_pci_controller *controller, uint64_t deadline_ticks);
static void ax211_pci_connection_clear(struct ax211_pci_controller *controller);
static int ax211_pci_key_command(struct ax211_pci_controller *controller, const uint8_t *payload, uint64_t deadline_ticks);
static int ax211_pci_staged_key_store(struct ax211_pci_staged_key *staged, const struct intel_ax211_key_request *request);
static int ax211_pci_staged_key_program(struct ax211_pci_controller *controller, struct ax211_pci_staged_key *staged, uint64_t deadline_ticks);
static void ax211_pci_staged_key_clear(struct ax211_pci_staged_key *staged);
static int ax211_pci_keys_have_active(const struct ax211_pci_controller *controller);
static int ax211_pci_key_request_address_valid( const struct ax211_pci_controller *controller, const struct wlan_radio_key_request *request);
static int ax211_pci_key_fail_closed(struct ax211_pci_controller *controller, int error);
static int ax211_pci_tx_submit(struct ax211_pci_controller *controller, const struct intel_ax211_tx_request *request, uint64_t deadline_ticks, int report_completion);
static int ax211_pci_tx_dispatch(struct ax211_pci_controller *controller, const struct intel_ax211_protocol_message *message);
static int ax211_pci_tx_timeout_check(struct ax211_pci_controller *controller, uint64_t now_us);
static int ax211_pci_connection_rx_dispatch(struct ax211_pci_controller *controller, const struct intel_ax211_rx_delivery *mpdu, uint16_t frame_control);

static int ax211_net_open(struct net_device *device);
static int ax211_pci_open_locked(struct ax211_pci_controller *controller, int restarting);
static void ax211_pci_reopen_request_locked(struct ax211_pci_controller *controller);
static void ax211_net_close(struct net_device *device);
static int ax211_net_transmit(struct net_device *device, struct packet_buf *packet);
static unsigned ax211_net_poll_receive(struct net_device *device, unsigned budget);
static int ax211_net_ioctl(struct net_device *device, unsigned long request, void *argument);
static void ax211_net_release(void *driver_data);

static int ax211_radio_scan_channel_start(void *context, uint64_t generation, uint32_t step_index, uint32_t channel, uint64_t deadline);
static int ax211_radio_scan_stop(void *context, uint64_t generation);
static int ax211_radio_connect_start(void *context, uint64_t generation, const struct wlan_bss_record *bss, uint64_t deadline);
static int ax211_radio_disconnect(void *context, uint64_t generation);
static int ax211_radio_management_transmit(void *context, uint64_t generation, const uint8_t *frame, size_t length, uint64_t deadline);
static int ax211_radio_association_set(void *context, uint64_t generation, const uint8_t bssid[6], uint16_t aid, uint64_t deadline);
static int ax211_radio_association_clear(void *context, uint64_t generation, uint64_t deadline);
static int ax211_radio_frame_transmit(void *context, const struct wlan_radio_tx_request *request);
static int ax211_radio_key_install(void *context, const struct wlan_radio_key_request *request);
static int ax211_radio_key_delete(void *context, uint64_t generation, enum wlan_radio_key_kind kind, uint8_t key_index, uint64_t key_generation, uint64_t deadline);
static int ax211_radio_keys_activate(void *context, uint64_t generation, uint64_t pairwise_key_generation, uint64_t group_key_generation, uint64_t deadline);
static int ax211_radio_quiesce(void *context);
static int ax211_radio_stop_retry(void *context);
static int ax211_radio_restart(void *context);

static const struct drv_pci_id ax211_pci_ids[] = {
	{
		.vendor = AX211_PCI_VENDOR,
		.device = AX211_PCI_PRODUCT,
		.subvendor = AX211_PCI_SUBVENDOR,
		.subdevice = AX211_PCI_SUBPRODUCT,
		.class_code = AX211_PCI_CLASS,
		.class_mask = AX211_PCI_CLASS_MASK,
	},
};

static struct drv_pci_driver ax211_pci_driver = {
	.name = "intel-ax211",
	.ids = ax211_pci_ids,
	.id_count = sizeof(ax211_pci_ids) / sizeof(ax211_pci_ids[0]),
	.match = ax211_pci_match,
	.attach = ax211_pci_attach,
	.detach = ax211_pci_detach,
	.suspend = ax211_pci_suspend,
	.resume = ax211_pci_resume,
};

/* This table is the permanent common-WLAN boundary for the AX211 backend. */
static const struct wlan_radio_ops ax211_radio_ops = {
	.scan_channel_start = ax211_radio_scan_channel_start,
	.scan_stop = ax211_radio_scan_stop,
	.connect_start = ax211_radio_connect_start,
	.disconnect = ax211_radio_disconnect,
	.management_transmit = ax211_radio_management_transmit,
	.association_set = ax211_radio_association_set,
	.association_clear = ax211_radio_association_clear,
	.frame_transmit = ax211_radio_frame_transmit,
	.key_install = ax211_radio_key_install,
	.key_delete = ax211_radio_key_delete,
	.keys_activate = ax211_radio_keys_activate,
	.quiesce = ax211_radio_quiesce,
	.stop_retry = ax211_radio_stop_retry,
	.restart = ax211_radio_restart};

static const struct net_device_ops ax211_net_ops = {
	.open = ax211_net_open,
	.close = ax211_net_close,
	.transmit = ax211_net_transmit,
	.poll_receive = ax211_net_poll_receive,
	.ioctl = ax211_net_ioctl,
	.release = ax211_net_release};

static const struct intel_ax211_runtime_start_ops ax211_runtime_start_ops = {
	.boot = {.receive_epoch_begin = ax211_pci_receive_epoch_begin,
		 .transport_bind = ax211_pci_transport_bind,
		 .receive_event = ax211_pci_receive_event,
		 .publish_pnvm = ax211_pci_publish_pnvm,
		 .after_alive = ax211_pci_after_alive,
		 .interrupt_drain = ax211_pci_interrupt_drain,
		 .bus_master_disable = ax211_pci_bus_master_disable,
		 .clock_us = ax211_pci_clock_us},
	.wake_hold = ax211_pci_wake_hold,
	.wake_release = ax211_pci_wake_release};

static const struct intel_ax211_tx_ring_ops ax211_tx_ring_ops = {
	.sync_for_device = ax211_pci_tx_sync_for_device,
	.write32 = ax211_pci_tx_write32};

static const struct intel_ax211_assoc_ops ax211_assoc_ops = {
	.clock_us = ax211_pci_assoc_clock_us,
	.exchange = ax211_pci_assoc_exchange};

static struct ax211_pci_controller *ax211_controllers;
static struct spinlock ax211_registry_lock;
static unsigned ax211_refresh_epoch;
static unsigned ax211_registry_initialized;

/*
 * Registers the exact Intel AX211 PCI transport driver.
 */
int
drv_pci_intel_ax211_driver_register(
	void)
{
	int function_result;
	int error;

	/* Handles the ax211 registry initialized condition. */
	if (ax211_registry_initialized) {
		/* Obtains the drv pci driver register result. */
		function_result = drv_pci_driver_register(&ax211_pci_driver);

		/* Returns the computed result. */
		return function_result;
	}

	spin_init(&ax211_registry_lock, LOCK_RANK_DEVICE,
		  "Intel AX211 registry");
	ax211_controllers = NULL;
	ax211_refresh_epoch = 0U;

	/* Checks the operation status. */
	error = drv_pci_driver_register(&ax211_pci_driver);
	if (error == 0)
		ax211_registry_initialized = 1U;

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Publishes persistent controllers after platform interrupt bring-up.
 */
void
drv_pci_intel_ax211_devices_ready(
	void)
{
	struct ax211_pci_controller *controller;
	unsigned long enabled;
	unsigned epoch;
	int error;

	enabled = spin_lock_irqsave(&ax211_registry_lock);

	ax211_refresh_epoch++;

	/* Handles the ax211 refresh epoch condition. */
	if (ax211_refresh_epoch == 0U)
		ax211_refresh_epoch++;
	epoch = ax211_refresh_epoch;

	spin_unlock_irqrestore(&ax211_registry_lock, enabled);

	/* Continue until the operation reaches a terminal state. */
	for (;;) {
		/* Handles the controller availability. */
		controller = ax211_pci_refresh_claim(epoch);
		if (controller == NULL)
			return;
		error = ax211_pci_refresh_one(controller);
		ax211_pci_refresh_release(controller);
		if (error != 0) {
			kern_logf("intel-ax211: deferred WLAN publication "
				   "failed (%d)\n",
				   error);
		}
	}
}

/* Matches only the frozen P038 PCI identity. */
static int
ax211_pci_match(
	struct drv_pci_device *device,
	const struct drv_pci_id *identity)
{
	int error;

	(void)identity;

	/* Computes the function result. */
	error = ax211_pci_identity_matches(device)
				  ? DRV_PCI_MATCH_EXACT
				  : DRV_PCI_MATCH_NONE;

	/* Returns the computed result. */
	return error;
}

/* Retains a validated, DMA-disabled controller without waiting or I/O. */
static int
ax211_pci_attach(
	struct drv_pci_device *device,
	const struct drv_pci_id *identity)
{
	int function_result;
	struct ax211_pci_controller *controller;
	struct intel_ax211_mmio_profile profile;
	const char *stage;
	int cleanup_error;
	int error;

	(void)identity;

	/* Checks the ax211 pci identity matches result. */
	if (!ax211_pci_identity_matches(device))
		return ENODEV;

	/* Checks the drv pci device driver data result. */
	if (drv_pci_device_driver_data(device) != NULL)
		return EBUSY;

	/* Handles the controller availability. */
	controller = kern_malloc(sizeof(*controller));
	if (controller == NULL)
		return ENOMEM;
	kern_memset(controller, 0, sizeof(*controller));
	kern_memset(&profile, 0, sizeof(profile));
	controller->device = device;
	stage = "lifecycle-lock";

	/* Checks the operation status. */
	error = mutex_init(&controller->lifecycle_lock, LOCK_RANK_DEVICE,
			   "Intel AX211 lifecycle");
	if (error == 0) {
		spin_init(&controller->interrupt_lock, LOCK_RANK_DEVICE,
			  "Intel AX211 interrupt");
		controller->hardware_epoch = 1U;
	}

	/* Checks the operation status. */
	if (error == 0) {
		stage = "pci-acquire";
		error = ax211_pci_acquire(controller);
	}

	/* Checks the operation status. */
	if (error == 0) {
		stage = "mmio-backend";
		error = drv_intel_ax211_pci_mmio_backend_init(
			&controller->backend, controller->mapping.address,
			controller->mapping.size);
	}

	/* Checks the operation status. */
	if (error == 0) {
		stage = "mmio-profile";
		error = ax211_pci_profile(controller, &profile);
	}

	/* Checks the operation status. */
	if (error == 0) {
		stage = "mmio-init";
		error = drv_intel_ax211_mmio_init(
			&controller->mmio, drv_intel_ax211_pci_mmio_ops(),
			&controller->backend, &profile);
	}

	ax211_pci_scrub(&profile, sizeof(profile));

	/* Checks the operation status. */
	if (error == 0) {
		stage = "driver-data";

		/* Checks the operation status. */
		error = drv_pci_device_set_driver_data(device, controller);
		if (error == 0)
			controller->driver_data_set = 1U;
	}

	/* Checks the operation status. */
	if (error == 0) {
		controller->ready = 1U;
		ax211_pci_list_add(controller);
		kern_logf("intel-ax211: controller retained; WLAN publication "
			   "deferred\n");

		/* Succeeded. */
		return 0;
	}

	kern_logf("intel-ax211: attach failed stage=%s error=%d "
		   "hw-rev=%08x rf-id=%08x\n",
		   stage, error, controller->hardware_revision,
		   controller->radio_identity);

	/* Handles the controller condition. */
	cleanup_error = ax211_pci_release_resources(controller, error, 1);
	if (controller->bar_claimed) {
		/* Obtains the ax211 pci quarantine result. */
		function_result =
			ax211_pci_quarantine(controller, cleanup_error);

		/* Returns the computed result. */
		return function_result;
	}

	/* Handles the controller condition. */
	if (controller->driver_data_set)
		(void)drv_pci_device_set_driver_data(device, NULL);
	ax211_pci_scrub(controller, sizeof(*controller));
	kern_free(controller);

	/* Returns the computed result. */
	return cleanup_error;
}

/* Checked detach retires publication before releasing the persistent BAR. */
static int
ax211_pci_detach(
	struct drv_pci_device *device,
	unsigned flags)
{
	struct ax211_pci_controller *controller;
	struct net_device *net_device;
	unsigned net_live;
	int error;

	(void)flags;

	/* Handles the controller availability. */
	controller = drv_pci_device_driver_data(device);
	if (controller == NULL)
		controller = ax211_pci_list_find_device(device);

	/* Handles the controller availability. */
	if (controller == NULL || controller->device != device)
		return ENODEV;
	mutex_lock(&controller->lifecycle_lock);

	/* Checks the operation status. */
	error = ax211_pci_list_remove(controller);
	if (error != 0) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return error;
	}

	controller->ready = 0U;
	controller->operation_admission_open = 0U;
	net_device = controller->net_device;
	net_live = controller->net_live;

	mutex_unlock(&controller->lifecycle_lock);

	/* Handles the net device availability. */
	if (net_device != NULL)
		(void)net_device_set_carrier(net_device, 0);

	/* Handles the net device availability. */
	if (net_device != NULL && net_live) {
		/* Checks the operation status. */
		error = net_device_gone(net_device);
		if (error != 0)
			return error;
	}

	mutex_lock(&controller->lifecycle_lock);

	/* Handles the net live condition. */
	if (net_live)
		controller->net_live = 0U;

	/* Checks the operation status. */
	error = ax211_pci_graph_detach(controller);
	if (error != 0) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return error;
	}

	/* Checks the operation status. */
	error = ax211_pci_session_stop(controller);
	if (error != 0) {
		controller->quarantined = 1U;
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return error;
	}

	/* Checks the operation status. */
	error = ax211_pci_release_resources(controller, 0, 1);
	if (error != 0) {
		controller->quarantined = 1U;
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return error;
	}

	controller->quarantined = 0U;

	/* Handles the controller condition. */
	if (controller->driver_data_set)
		error = drv_pci_device_set_driver_data(device, NULL);
	else
		error = 0;
	if (error != 0) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return error;
	}

	controller->driver_data_set = 0U;
	ax211_pci_list_forget(controller);
	controller->device = NULL;
	controller->net_device = NULL;

	mutex_unlock(&controller->lifecycle_lock);

	/* Handles the net device availability. */
	if (net_device != NULL) {
		net_device_destroy(net_device);
	} else {
		ax211_pci_scrub(controller, sizeof(*controller));
		kern_free(controller);
	}

	/* Succeeded. */
	return 0;
}

/* Checks the complete frozen P038 PCI tuple. */
static int
ax211_pci_identity_matches(
	const struct drv_pci_device *device)
{
	int error;

	/* Handles the device availability. */
	if (device == NULL)
		return 0;

	/* Checks the drv pci device vendor result. */
	if (drv_pci_device_vendor(device) != AX211_PCI_VENDOR ||
	    drv_pci_device_product(device) != AX211_PCI_PRODUCT) {
		/* Succeeded. */
		return 0;
	}

	/* Checks the drv pci device subvendor result. */
	if (drv_pci_device_subvendor(device) != AX211_PCI_SUBVENDOR ||
	    drv_pci_device_subproduct(device) != AX211_PCI_SUBPRODUCT) {
		/* Succeeded. */
		return 0;
	}

	/* Checks the drv pci device revision result. */
	if (drv_pci_device_revision(device) != AX211_PCI_REVISION)
		return 0;

	/* Computes the function result. */
	error = (drv_pci_device_class(device) &
			   AX211_PCI_CLASS_MASK) == AX211_PCI_CLASS;

	/* Returns the computed result. */
	return error;
}

/* Validates BAR0 before PCI decode or MMIO is changed. */
static int
ax211_pci_bar_validate(
	const struct drv_pci_bar *bar)
{
	/* Handles the bar availability. */
	if (bar == NULL)
		return EINVAL;

	/* Handles the bar condition. */
	if (bar->type != DRV_PCI_BAR_MEMORY32 &&
	    bar->type != DRV_PCI_BAR_MEMORY64) {
		/* Failed. */
		return ENODEV;
	}

	/* Returns the computed result. */
	return bar->size >= AX211_BAR0_MINIMUM_SIZE ? 0 : ENODEV;
}

/* Reads one naturally aligned identity CSR from retained BAR0. */
static uint32_t
ax211_pci_read32(
	const struct ax211_pci_controller *controller,
	unsigned offset)
{
	volatile uint8_t *registers;
	uint32_t value;

	registers = controller->mapping.address;
	value = *(volatile uint32_t *)(registers + offset);
	kern_io_read_barrier();

	/* Returns the computed result. */
	return value;
}

/* Validates the frozen SO-or-SOF MAC and Garfield Peak RF identities. */
static int
ax211_pci_hardware_validate(
	uint32_t hardware_revision,
	uint32_t radio_identity)
{
	uint32_t mac_type;
	uint32_t radio_type;

	mac_type = (hardware_revision & AX211_SILICON_FAMILY_MASK) >>
		   AX211_SILICON_FAMILY_SHIFT;
	radio_type = (radio_identity & AX211_RADIO_KIND_MASK) >>
		     AX211_RADIO_KIND_SHIFT;

	/* Handles the mac type condition. */
	if (mac_type != AX211_SILICON_FAMILY_SO &&
	    mac_type != AX211_SILICON_FAMILY_SOF) {
		/* Failed. */
		return ENODEV;
	}

	/* Handles the radio type condition. */
	if (radio_type != AX211_RADIO_KIND_GF)
		return ENODEV;

	/* Returns the computed result. */
	return (radio_identity & AX211_RADIO_CONCURRENT_BANDS_FLAG) == 0U ? 0 : ENODEV;
}

/* Derives the already validated private MMIO profile. */
static int
ax211_pci_profile(
	struct ax211_pci_controller *controller,
	struct intel_ax211_mmio_profile *profile)
{
	uint32_t mac_type;

	/* Handles the controller availability. */
	if (controller == NULL || profile == NULL)
		return EINVAL;
	kern_memset(profile, 0, sizeof(*profile));
	mac_type =
		(controller->hardware_revision & AX211_SILICON_FAMILY_MASK) >>
		AX211_SILICON_FAMILY_SHIFT;
	profile->mac_type = (uint16_t)mac_type;
	profile->rf_type = INTEL_AX211_MMIO_RF_GF;
	profile->upper_cpu_prph_base = INTEL_AX211_MMIO_UPPER_CPU_PRPH_BASE;

	/* Succeeded. */
	return 0;
}

/* Acquires the persistent BAR/decode lease with bus mastering disabled. */
static int
ax211_pci_acquire(
	struct ax211_pci_controller *controller)
{
	int function_result;
	struct drv_pci_bar bar;
	uint16_t command;
	int error;

	/* Checks the operation status. */
	error = drv_pci_device_claim_bar(controller->device, 0U);
	if (error != 0)
		return error;
	controller->bar_claimed = 1U;

	/* Checks the operation status. */
	error = drv_pci_device_bar(controller->device, 0U, &bar);
	if (error != 0)
		return error;

	/* Checks the operation status. */
	error = ax211_pci_bar_validate(&bar);
	if (error != 0)
		return error;
	controller->original_bar = bar;
	controller->original_bar_valid = 1U;

	/* Checks the operation status. */
	error = drv_pci_device_save_enable_state(controller->device,
						 &controller->enable_state);
	if (error != 0)
		return error;
	controller->state_saved = 1U;

	/* Checks the operation status. */
	error = drv_pci_device_set_bus_master(controller->device, false);
	if (error != 0)
		return error;
	controller->bar_may_have_moved = 1U;

	/* Checks the operation status. */
	error = drv_pci_device_map_bar(controller->device, 0U,
				       DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE |
					       DRV_PCI_MAP_NOCACHE,
				       &controller->mapping);
	if (error != 0)
		return error;
	controller->bar_mapped = 1U;

	/* Handles the address availability. */
	if (controller->mapping.address == NULL ||
	    (controller->mapping.type != DRV_PCI_BAR_MEMORY32 &&
	     controller->mapping.type != DRV_PCI_BAR_MEMORY64) ||
	    controller->mapping.size < AX211_BAR0_MINIMUM_SIZE ||
	    controller->mapping.size > bar.size) {
		/* Failed. */
		return EIO;
	}

	/* Checks the operation status. */
	error = drv_pci_device_enable_memory(controller->device);
	if (error == 0) {
		error = drv_pci_device_set_bus_master(controller->device,
						      false);
	}

	/* Checks the operation status. */
	if (error == 0) {
		error = drv_pci_device_config_read16(
			controller->device, AX211_PCI_COMMAND, &command);
	}

	/* Checks the operation status. */
	if (error != 0)
		return error;

	/* Handles the command condition. */
	if ((command & AX211_PCI_COMMAND_MEMORY) == 0U ||
	    (command & AX211_PCI_COMMAND_MASTER) != 0U) {
		/* Failed. */
		return EIO;
	}
	controller->hardware_revision =
		ax211_pci_read32(controller, AX211_CSR_SILICON_REVISION);
	controller->radio_identity =
		ax211_pci_read32(controller, AX211_CSR_RADIO_IDENTITY);

	/* Obtains the ax211 pci hardware validate result. */
	function_result = ax211_pci_hardware_validate(
		controller->hardware_revision, controller->radio_identity);

	/* Returns the computed result. */
	return function_result;
}

/* Restores BAR0 if the host mapper assigned a temporary MMIO address. */
static int
ax211_pci_restore_bar(
	struct ax211_pci_controller *controller)
{
	struct drv_pci_bar current_bar;
	int error;

	/* Handles the controller condition. */
	if (!controller->bar_may_have_moved)
		return 0;

	/* Handles the controller condition. */
	if (!controller->original_bar_valid)
		return EIO;

	/* Checks the operation status. */
	error = drv_pci_device_bar(controller->device, 0U, &current_bar);
	if (error != 0)
		return error;

	/* Handles the current bar condition. */
	if (current_bar.bus_address != controller->original_bar.bus_address) {
		/* Checks the operation status. */
		error = drv_pci_device_assign_bar(
			controller->device, 0U,
			controller->original_bar.bus_address);
		if (error != 0)
			return error;
	}

	controller->bar_may_have_moved = 0U;
	controller->original_bar_valid = 0U;

	/* Succeeded. */
	return 0;
}

/* Releases the PCI lease in reverse order, optionally retaining for retry. */
static int
ax211_pci_release_resources(
	struct ax211_pci_controller *controller,
	int result,
	int retain_on_failure)
{
	int bar_error;
	int quiesce_error;
	int restore_error;

	bar_error = 0;
	restore_error = 0;

	/* Handles the controller condition. */
	if (controller->bar_mapped) {
		drv_pci_device_unmap_bar(controller->device,
					 &controller->mapping);
		controller->bar_mapped = 0U;
	}

	/* Checks the operation status. */
	bar_error = ax211_pci_restore_bar(controller);
	if (bar_error == 0 && controller->state_saved) {
		/* Checks the operation status. */
		restore_error = drv_pci_device_restore_enable_state(
			controller->device, &controller->enable_state);
		if (restore_error == 0)
			controller->state_saved = 0U;
	}

	/* Checks the operation status. */
	if (bar_error != 0 || restore_error != 0) {
		/* Checks the operation status. */
		quiesce_error = drv_pci_device_set_bus_master(
			controller->device, false);
		if (result == 0 && quiesce_error != 0)
			result = quiesce_error;

		/* Checks the operation status. */
		if (retain_on_failure)
			return bar_error != 0 ? bar_error : restore_error;
	}

	/* Handles the controller condition. */
	if (controller->bar_claimed) {
		drv_pci_device_release_bar(controller->device, 0U);
		controller->bar_claimed = 0U;
	}

	/* Checks the operation status. */
	if (bar_error != 0)
		return bar_error;

	/* Checks the operation status. */
	if (restore_error != 0)
		return restore_error;

	/* Returns the computed result. */
	return result;
}

/* Keeps an incompletely restored controller bound for checked detach retry. */
static int
ax211_pci_quarantine(
	struct ax211_pci_controller *controller,
	int failure)
{
	uint16_t command;
	int error;

	controller->ready = 0U;
	controller->quarantined = 1U;

	/* Checks the operation status. */
	error = drv_pci_device_set_bus_master(controller->device, false);
	if (error == 0) {
		error = drv_pci_device_config_read16(
			controller->device, AX211_PCI_COMMAND, &command);
	}

	/* Checks the operation status. */
	if (error == 0 && (command & AX211_PCI_COMMAND_MASTER) != 0U)
		error = EIO;
	if (error != 0)
		failure = error;

	/* Handles the controller condition. */
	if (!controller->driver_data_set) {
		/* Checks the operation status. */
		error = drv_pci_device_set_driver_data(controller->device,
						       controller);
		if (error == 0)
			controller->driver_data_set = 1U;
	}

	ax211_pci_list_add(controller);
	kern_logf("intel-ax211: controller restoration quarantined (%d)\n",
		   failure);

	/* Returning success is intentional: the PCI core must bind detach. */
	return 0;
}

/* Publishes one persistent controller to the refresh registry. */
static void
ax211_pci_list_add(
	struct ax211_pci_controller *controller)
{
	unsigned long enabled;

	enabled = spin_lock_irqsave(&ax211_registry_lock);

	controller->next = ax211_controllers;
	ax211_controllers = controller;
	controller->listed = 1U;

	spin_unlock_irqrestore(&ax211_registry_lock, enabled);
}

/* Retires a controller from admission unless refresh still owns it. */
static int
ax211_pci_list_remove(
	struct ax211_pci_controller *controller)
{
	unsigned long enabled;

	/* Handles the controller condition. */
	enabled = spin_lock_irqsave(&ax211_registry_lock);
	if (controller->refresh_busy) {
		spin_unlock_irqrestore(&ax211_registry_lock, enabled);

		/* Failed. */
		return EBUSY;
	}

	/* Handles the controller condition. */
	if (!controller->detaching)
		controller->detaching = 1U;

	spin_unlock_irqrestore(&ax211_registry_lock, enabled);

	/* Succeeded. */
	return 0;
}

/* Forgets a fully restored controller immediately before final release. */
static void
ax211_pci_list_forget(
	struct ax211_pci_controller *controller)
{
	struct ax211_pci_controller **link;
	unsigned long enabled;

	enabled = spin_lock_irqsave(&ax211_registry_lock);

	/* Process each linked entry. */
	for (link = &ax211_controllers; *link != NULL; link = &(*link)->next) {
		/* Handles the link condition. */
		if (*link != controller)
			continue;
		*link = controller->next;
		controller->next = NULL;
		controller->listed = 0U;
		break;
	}

	spin_unlock_irqrestore(&ax211_registry_lock, enabled);
}

/* Recovers a quarantined controller whose PCI driver-data write failed. */
static struct ax211_pci_controller *
ax211_pci_list_find_device(
	struct drv_pci_device *device)
{
	struct ax211_pci_controller *controller;
	unsigned long enabled;

	enabled = spin_lock_irqsave(&ax211_registry_lock);

	/* Process each linked entry. */
	for (controller = ax211_controllers; controller != NULL;
	     controller = controller->next) {
		/* Handles the controller condition. */
		if (controller->device == device)
			break;
	}

	spin_unlock_irqrestore(&ax211_registry_lock, enabled);

	/* Returns the computed result. */
	return controller;
}

/* Claims one unpublished controller exactly once in this refresh epoch. */
static struct ax211_pci_controller *
ax211_pci_refresh_claim(
	unsigned epoch)
{
	struct ax211_pci_controller *controller;
	unsigned long enabled;

	enabled = spin_lock_irqsave(&ax211_registry_lock);

	/* Process each linked entry. */
	for (controller = ax211_controllers; controller != NULL;
	     controller = controller->next) {
		/* Handles the controller condition. */
		if (controller->detaching || controller->refresh_busy ||
		    !controller->ready || controller->quarantined ||
		    controller->station_attached ||
		    controller->refresh_epoch == epoch)
			continue;
		controller->refresh_epoch = epoch;
		controller->refresh_busy = 1U;
		break;
	}

	spin_unlock_irqrestore(&ax211_registry_lock, enabled);

	/* Returns the computed result. */
	return controller;
}

/* Releases the registry lease after MMIO and publication have finished. */
static void
ax211_pci_refresh_release(
	struct ax211_pci_controller *controller)
{
	unsigned long enabled;

	enabled = spin_lock_irqsave(&ax211_registry_lock);

	controller->refresh_busy = 0U;

	spin_unlock_irqrestore(&ax211_registry_lock, enabled);
}

/* Reads identity under NIC ownership, stops again, then publishes once. */
static int
ax211_pci_refresh_one(
	struct ax211_pci_controller *controller)
{
	uint8_t mac_address[AX211_MAC_ADDRESS_SIZE];
	int hardware_attempted;
	int error;
	int stop_error;

	kern_memset(mac_address, 0, sizeof(mac_address));
	mutex_lock(&controller->lifecycle_lock);

	hardware_attempted = 0;

	/* Handles the controller condition. */
	if (controller->detaching || !controller->ready ||
	    controller->quarantined)
		error = ENODEV;
	else
		error = ax211_pci_partial_discard(controller);
	if (error == 0) {
		hardware_attempted = 1;
		error = drv_intel_ax211_mmio_take_ownership(&controller->mmio);
	}

	/* Checks the operation status. */
	if (error == INTEL_AX211_MMIO_OK)
		error = drv_intel_ax211_mmio_sw_reset(&controller->mmio);
	if (error == INTEL_AX211_MMIO_OK)
		error = drv_intel_ax211_mmio_apm_init(&controller->mmio);
	if (error == INTEL_AX211_MMIO_OK) {
		error = drv_intel_ax211_mmio_read_mac(&controller->mmio,
						      mac_address);
	}

	/* Handles the hardware attempted condition. */
	stop_error = INTEL_AX211_MMIO_OK;
	if (hardware_attempted)
		stop_error = drv_intel_ax211_mmio_stop(&controller->mmio);
	if (error == INTEL_AX211_MMIO_OK && stop_error != INTEL_AX211_MMIO_OK)
		error = stop_error;
	if (error == INTEL_AX211_MMIO_OK)
		error = ax211_pci_publish(controller, mac_address);
	ax211_pci_scrub(mac_address, sizeof(mac_address));

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Transactionally publishes an administratively-down WLAN station graph. */
static int
ax211_pci_publish(
	struct ax211_pci_controller *controller,
	const uint8_t mac_address[AX211_MAC_ADDRESS_SIZE])
{
	struct wlan_scan_profile profile;
	struct net_device *device;
	struct wlan_station *station;
	unsigned index;
	int error;
	int gone_error;

	/* Handles the device availability. */
	device = net_device_alloc();
	if (device == NULL)
		return ENOSPC;
	device->flags = NET_DEVICE_BROADCAST | NET_DEVICE_MULTICAST;
	device->mtu = AX211_MTU;
	kern_memcpy(device->hwaddr, mac_address, AX211_MAC_ADDRESS_SIZE);
	device->hwaddr_len = AX211_MAC_ADDRESS_SIZE;
	device->capabilities = NET_DEVICE_CAP_WLAN;
	device->ops = &ax211_net_ops;
	device->driver_data = controller;
	error = ENOSPC;

	/* Process each remaining element. */
	for (index = 0U; index < NET_DEVICE_MAX; index++) {
		kern_memcpy(device->name, "wlan", 4U);
		device->name[4] = (char)('0' + index);
		device->name[5] = '\0';

		/* Checks the operation status. */
		error = net_device_create(device);
		if (error != EEXIST)
			break;
	}

	/* Checks the operation status. */
	if (error != 0) {
		device->driver_data = NULL;
		net_device_destroy(device);

		/* Failed. */
		return error;
	}

	controller->net_device = device;
	controller->net_live = 1U;
	station = NULL;

	/* Checks the operation status. */
	error = net_device_set_carrier(device, 0);
	if (error == 0) {
		ax211_pci_scan_profile(&profile);
		error = wlan_station_attach(device, &ax211_radio_ops,
					    controller, &profile, &station);
	} else {
		kern_memset(&profile, 0, sizeof(profile));
	}

	ax211_pci_scrub(&profile, sizeof(profile));

	/* Checks the operation status. */
	if (error == 0 && station == NULL)
		error = EIO;
	if (error != 0) {
		/* Checks the operation status. */
		gone_error = ax211_pci_partial_discard(controller);
		if (gone_error != 0)
			return gone_error;

		/* Failed. */
		return error;
	}

	controller->station = station;
	controller->station_attached = 1U;

	/* Succeeded. */
	return 0;
}

/* Builds the deliberately passive, fixed 2.4-GHz channel 1--11 profile. */
static void
ax211_pci_scan_profile(
	struct wlan_scan_profile *profile)
{
	unsigned channel;

	kern_memset(profile, 0, sizeof(*profile));
	profile->channel_count = AX211_PASSIVE_CHANNEL_COUNT;

	/* Process each remaining element. */
	for (channel = 1U; channel <= profile->channel_count; channel++) {
		profile->channels[channel - 1U].channel = channel;
		profile->channels[channel - 1U].center_frequency_mhz =
			2407U + channel * 5U;
	}
}

/* Retires an unpublished net-device shell before a publication retry. */
static int
ax211_pci_partial_discard(
	struct ax211_pci_controller *controller)
{
	struct net_device *device;
	unsigned net_live;
	int error;

	/* Handles the station availability. */
	if (controller->station_attached || controller->station != NULL)
		return 0;

	/* Handles the device availability. */
	device = controller->net_device;
	if (device == NULL)
		return 0;
	net_live = controller->net_live;

	mutex_unlock(&controller->lifecycle_lock);

	(void)net_device_set_carrier(device, 0);

	/* Handles the net live condition. */
	if (net_live) {
		error = net_device_gone(device);
		mutex_lock(&controller->lifecycle_lock);
		if (error != 0)
			return error;
		controller->net_live = 0U;
	} else {
		mutex_lock(&controller->lifecycle_lock);
	}

	device->driver_data = NULL;
	controller->net_device = NULL;

	mutex_unlock(&controller->lifecycle_lock);

	net_device_destroy(device);
	mutex_lock(&controller->lifecycle_lock);

	/* Succeeded. */
	return 0;
}

/* Supports the ax211 pci lifecycle deadline operation. */
static uint64_t
ax211_pci_lifecycle_deadline(
	void)
{
	uint64_t now;

	/* Handles the now condition. */
	now = clock_ticks();
	if (now > UINT64_MAX - AX211_LIFECYCLE_JOIN_TICKS)
		return UINT64_MAX;

	/* Returns the computed result. */
	return now + AX211_LIFECYCLE_JOIN_TICKS;
}

/* Pins the common station while the lifecycle lock excludes retirement. */
static int
ax211_pci_station_pin_locked(
	struct ax211_pci_controller *controller,
	struct wlan_station **station)
{
	/* Handles the controller availability. */
	if (controller == NULL || station == NULL)
		return EINVAL;
	*station = NULL;

	/* Handles the station availability. */
	if (!controller->station_attached || controller->station == NULL)
		return ENODEV;

	/* Handles the controller condition. */
	if (controller->operations_active == UINT_MAX)
		return EOVERFLOW;
	controller->operations_active++;
	*station = controller->station;

	/* Succeeded. */
	return 0;
}

/* Pins the common station across a deliberate lifecycle-lock drop. */
static int
ax211_pci_operation_enter_locked(
	struct ax211_pci_controller *controller,
	struct wlan_station **station)
{
	int error;

	/* Handles the controller availability. */
	if (controller == NULL || station == NULL)
		return EINVAL;
	*station = NULL;

	/* Handles the station availability. */
	if (!controller->operation_admission_open ||
	    !controller->station_attached || controller->station == NULL) {
		/* Failed. */
		return ENODEV;
	}

	/* Obtains the ax211 pci station pin locked result. */
	error = ax211_pci_station_pin_locked(controller, station);

	/* Returns the computed result. */
	return error;
}

/* Supports the ax211 pci operation leave locked operation. */
static void
ax211_pci_operation_leave_locked(
	struct ax211_pci_controller *controller)
{
	/* Handles the controller availability. */
	if (controller == NULL || controller->operations_active == 0U)
		__builtin_trap();
	controller->operations_active--;
}

/* Joins callers that already copied the station pointer before retirement. */
static int
ax211_pci_operations_join_locked(
	struct ax211_pci_controller *controller,
	uint64_t deadline)
{
	/* Handles the controller availability. */
	if (controller == NULL || deadline == 0U)
		return EINVAL;

	/* Continue while the operation condition remains true. */
	while (controller->operations_active != 0U) {
		/* Checks the clock ticks result. */
		if (clock_ticks() >= deadline)
			return ETIMEDOUT;
		mutex_unlock(&controller->lifecycle_lock);
		sched_yield();
		mutex_lock(&controller->lifecycle_lock);
	}

	/* Succeeded. */
	return 0;
}

/* Common close is a checked admission join and may need a bounded retry. */
static int
ax211_pci_station_close_wait(
	struct wlan_station *station,
	uint64_t deadline)
{
	int result;

	/* Handles the station availability. */
	if (station == NULL || deadline == 0U)
		return ENODEV;

	/* Continue until the operation reaches a terminal state. */
	for (;;) {
		/* Checks the operation result. */
		result = wlan_station_close(station);
		if (result != EBUSY)
			return result;

		/* Checks the clock ticks result. */
		if (clock_ticks() >= deadline)
			return ETIMEDOUT;
		sched_yield();
	}
}

/* Records one uncertain live-runtime failure for the poll recovery owner. */
static void
ax211_pci_recovery_latch_locked(
	struct ax211_pci_controller *controller,
	int error)
{
	/* Handles the controller availability. */
	if (controller == NULL)
		return;

	/* Handles the controller condition. */
	if (!controller->recovery_pending && !controller->recovery_running) {
		controller->recovery_error = error > 0 ? error : EIO;
		controller->recovery_generation =
			controller->connection_generation;
		controller->recovery_pending = 1U;
		kern_logf("intel-ax211: recovery latched error=%d "
			   "generation=%u\n",
			   controller->recovery_error,
			   (unsigned)controller->recovery_generation);
	}

	controller->operation_admission_open = 0U;

	/* Handles the net device availability. */
	if (controller->net_device != NULL)
		net_device_schedule_poll(controller->net_device);
}

/* Retires common link truth before globally stopping an uncertain epoch. */
static int
ax211_pci_recovery_run_locked(
	struct ax211_pci_controller *controller)
{
	struct net_device *device;
	struct wlan_station *station;
	uint64_t deadline;
	uint64_t generation;
	int carrier_error;
	int join_error;
	int link_error;
	int pin_error;
	int stop_error;
	int failure;

	/* Handles the controller availability. */
	if (controller == NULL)
		return EINVAL;
	failure = controller->recovery_error > 0 ? controller->recovery_error
						 : EIO;
	generation = controller->recovery_generation != 0U
			     ? controller->recovery_generation
			     : controller->connection_generation;
	controller->operation_admission_open = 0U;
	deadline = ax211_pci_lifecycle_deadline();
	join_error = ax211_pci_operations_join_locked(controller, deadline);
	device = controller->net_device;

	/* Checks the operation status. */
	if (join_error != 0) {
		/*
		 * The active lease may still be inside common->radio code. Keep
		 * every DMA owner intact and retry from a later poll rather
		 * than racing a reset or release against that caller.
		 */
		mutex_unlock(&controller->lifecycle_lock);
		carrier_error =
			device != NULL ? net_device_set_carrier(device, 0) : 0;
		mutex_lock(&controller->lifecycle_lock);

		/* Checks the operation status. */
		if (carrier_error != 0 && carrier_error != ENODEV) {
			kern_logf("intel-ax211: recovery carrier-down failed "
				   "(%d)\n",
				   carrier_error);
		}

		kern_logf("intel-ax211: recovery operation join failed (%d)\n",
			   join_error);

		/* Returns the computed result. */
		return join_error;
	}

	controller->recovery_pending = 0U;
	controller->recovery_error = 0;
	controller->recovery_generation = 0U;
	controller->recovery_running = 1U;
	station = NULL;
	pin_error = generation != 0U
			    ? ax211_pci_station_pin_locked(controller, &station)
			    : 0;

	mutex_unlock(&controller->lifecycle_lock);

	carrier_error = device != NULL ? net_device_set_carrier(device, 0) : 0;
	link_error = station != NULL ? wlan_station_report_link_loss(
					       station, generation, failure)
				     : 0;
	mutex_lock(&controller->lifecycle_lock);

	/* Handles the station availability. */
	if (station != NULL)
		ax211_pci_operation_leave_locked(controller);
	stop_error = ax211_pci_session_stop(controller);
	controller->recovery_running = 0U;
	controller->quarantined = stop_error != 0;

	/* Checks the operation status. */
	if (pin_error != 0 && pin_error != ENODEV) {
		kern_logf("intel-ax211: recovery station pin failed (%d)\n",
			   pin_error);
	}

	/* Checks the operation status. */
	if (carrier_error != 0 && carrier_error != ENODEV) {
		kern_logf("intel-ax211: recovery carrier-down failed (%d)\n",
			   carrier_error);
	}

	/* Checks the operation status. */
	if (link_error != 0 && link_error != ESTALE && link_error != ENODEV &&
	    link_error != ENOTCONN && link_error != EALREADY) {
		kern_logf(
			"intel-ax211: recovery link retirement failed (%d)\n",
			link_error);
	}

	/* Checks the operation status. */
	if (stop_error != 0) {
		/*
		 * An inactive, quarantined runtime cannot drive another poll
		 * retry.
		 */
		ax211_pci_stop_defer_locked(controller, stop_error);
		kern_logf("intel-ax211: fatal poll cleanup failed (%d)\n",
			   stop_error);
	}

	/*
	 * The device is still up: the epoch is started again from the WLAN
	 * retirement thread, after a deferred stop when one is owed, so the
	 * interface does not stay down until the user cycles it (BUG-158).
	 */
	ax211_pci_reopen_request_locked(controller);

	/* Returns the computed result. */
	return stop_error != 0 ? stop_error : failure;
}

/* Retires the visible network graph through every checked common barrier. */
static int
ax211_pci_graph_detach(
	struct ax211_pci_controller *controller)
{
	struct wlan_station *station;
	uint64_t deadline;
	int error;

	/* Checks the operation status. */
	error = wlan_station_stop_cancel(controller->station);
	if (error != 0)
		return error;
	controller->operation_admission_open = 0U;
	deadline = ax211_pci_lifecycle_deadline();

	/* Checks the operation status. */
	error = ax211_pci_operations_join_locked(controller, deadline);
	if (error != 0)
		return error;

	/* Handles the station availability. */
	station = controller->station;
	if (station == NULL || !controller->station_attached)
		return 0;

	/*
	 * Both joins may synchronously invoke radio callbacks.  Admission is
	 * already closed; drop the hardware lifecycle lock across those edges.
	 */

	mutex_unlock(&controller->lifecycle_lock);

	/* Checks the operation status. */
	error = ax211_pci_station_close_wait(station,
					     ax211_pci_lifecycle_deadline());
	if (error == 0 || error == ENODEV)
		error = wlan_station_detach(station);
	mutex_lock(&controller->lifecycle_lock);
	if (error != 0 && error != ENODEV)
		return error;

	/* Handles the controller condition. */
	if (controller->station == station) {
		controller->station = NULL;
		controller->station_attached = 0U;
	}

	/* Succeeded. */
	return 0;
}

/* Stops every open-generation owner while the lifecycle mutex is held. */
static int
ax211_pci_session_stop(
	struct ax211_pci_controller *controller)
{
	int association_result;
	int coordinator_result;
	int hardware_quiesced;
	int release_result;
	int result;
	unsigned long enabled;

	/* Handles the controller availability. */
	if (controller == NULL)
		return EINVAL;

	/* Handles the controller condition. */
	if (controller->runtime_active &&
	    controller->connection_generation != 0U) {
		kern_logf("intel-ax211: stopping connection generation=%u "
			   "association-phase=%u step=%u\n",
			   (unsigned)controller->connection_generation,
			   controller->association.phase,
			   controller->association.step);
	}

	controller->operation_admission_open = 0U;
	controller->session_stopped = 0U;
	controller->recovery_pending = 0U;
	controller->recovery_error = 0;
	controller->recovery_generation = 0U;
	association_result = 0;
	hardware_quiesced = 0;

	/* Handles the controller condition. */
	if (controller->association_initialized && controller->runtime_active) {
		controller->control_deadline_ticks =
			ax211_pci_lifecycle_deadline();
		association_result = ax211_pci_assoc_rollback(
			controller, controller->connection_generation);
	}

	enabled = spin_lock_irqsave(&controller->interrupt_lock);

	controller->runtime_active = 0U;
	controller->poll_reschedule = 0U;

	spin_unlock_irqrestore(&controller->interrupt_lock, enabled);

	ax211_pci_deferred_event_clear(controller);
	ax211_pci_scan_clear(controller);
	result = 0;

	/* Handles the controller condition. */
	if (controller->runtime_initialized) {
		/* Handles the controller condition. */
		coordinator_result = INTEL_AX211_RUNTIME_START_OK;
		if (controller->runtime_start.state ==
		    INTEL_AX211_RUNTIME_START_STATE_RUNNING) {
			coordinator_result = drv_intel_ax211_runtime_start_stop(
				&controller->runtime_start);
		} else if (
			controller->runtime_start.state ==
				INTEL_AX211_RUNTIME_START_STATE_STOP_REQUIRED ||
			controller->runtime_start.state ==
				INTEL_AX211_RUNTIME_START_STATE_STOP_REQUIRED_NO_DMA) {
			coordinator_result =
				drv_intel_ax211_runtime_start_cleanup(
					&controller->runtime_start);
		}

		/* Handles the coordinator result condition. */
		if (coordinator_result != INTEL_AX211_RUNTIME_START_OK) {
			result = EIO;
		} else {
			controller->runtime_initialized = 0U;
			hardware_quiesced = 1;
		}
	}

	/* Checks the operation result. */
	if (result == 0 && controller->boot_initialized &&
	    (controller->boot.state == INTEL_AX211_BOOT_STATE_STOP_REQUIRED ||
	     controller->boot.state ==
		     INTEL_AX211_BOOT_STATE_STOP_REQUIRED_NO_DMA)) {
		coordinator_result =
			drv_intel_ax211_boot_cleanup(&controller->boot);

		/* Handles the coordinator result condition. */
		if (coordinator_result != INTEL_AX211_BOOT_OK)
			result = EIO;
	}

	/*
	 * Drains an interrupt lifetime no coordinator owns any more.  No
	 * firmware runs in this case, so bus mastering is turned off with it,
	 * as the drain did before BUG-158.
	 */
	if (result == 0 &&
	    (controller->irq_established || controller->irq_allocated)) {
		result = ax211_pci_interrupt_drain(controller);

		/* Ends the DMA ownership once the handler is gone. */
		if (result == 0)
			result = ax211_pci_bus_master_disable(controller);

		/* The tx ring may now be released as quiesced hardware. */
		if (result == 0)
			hardware_quiesced = 1;
	}

	/*
	 * A successful runtime stop includes interrupt drain, RX DMA quiesce,
	 * controller reset (STOP_MASTER and SW_RESET), and only then PCI
	 * bus-master disable, which is the global queue-1 DMA barrier.
	 */
	if (result == 0 && controller->tx_ring_allocated) {
		/* Handles the release result condition. */
		release_result = drv_intel_ax211_tx_ring_release(
			&controller->frame_ring, hardware_quiesced);
		if (release_result != INTEL_AX211_TX_RING_OK)
			result = EIO;
		else
			controller->tx_ring_allocated = 0U;
	}

	/* Checks the operation result. */
	if (result == 0) {
		ax211_pci_connection_clear(controller);
		controller->session_stopped = 1U;

		/*
		 * A global reset is stronger than a failed per-resource
		 * rollback.
		 */
		association_result = 0;
	}

	/* Checks the operation result. */
	if (result == 0 && controller->boot_initialized)
		controller->boot_initialized = 0U;

	/* Checks the operation result. */
	if (result == 0 && association_result != 0)
		result = association_result;

	/* Returns the computed result. */
	return result;
}

/* Transfers an incomplete global stop to the independent retirement owner. */
static void
ax211_pci_stop_defer_locked(
	struct ax211_pci_controller *controller,
	int error)
{
	/* An existing close owner will publish its own checked result. */
	if (controller->close_pending)
		return;

	/*
	 * Keeps new operations out until the retained hardware epoch is
	 * retired.
	 */
	controller->close_pending = 1U;
	controller->operation_admission_open = 0U;

	/*
	 * Stops common timer callbacks and arms its bounded retirement retry.
	 */
	wlan_station_stop_request(controller->station);
	wlan_station_stop_complete(controller->station, error);

	/*
	 * Preserves the first failure without repeating firmware rollback logs.
	 */
	kern_logf("intel-ax211: global stop deferred error=%d runtime-state=%u "
		   "irq=%u/%u dma-retained=%u\n",
		   error, controller->runtime_start.state,
		   controller->irq_established, controller->irq_allocated,
		   controller->runtime_start.dma_prepared);
}

/*
 * Asks the WLAN retirement thread to start a new epoch after a recovery.
 *
 * Only an open device is restarted, at most AX211_REOPEN_ATTEMPTS_MAX times
 * in a row; past that the interface stays down until the user cycles it, as
 * before (BUG-158).
 */
static void
ax211_pci_reopen_request_locked(
	struct ax211_pci_controller *controller)
{
	/* A closed, detaching, or stationless device has nothing to restart. */
	if (!controller->net_opened || controller->detaching ||
	    !controller->station_attached || controller->station == NULL)
		return;

	/*
	 * A run of restarts that never completed a scan is not repeated
	 * forever.
	 */
	if (controller->reopen_attempts >= AX211_REOPEN_ATTEMPTS_MAX) {
		kern_logf("intel-ax211: recovery left the interface down after "
			   "%u restarts; cycle WiFi to retry\n",
			   controller->reopen_attempts);
		return;
	}

	/* The retirement thread runs the restart outside the network worker. */
	controller->reopen_pending = 1U;
	wlan_station_restart_request(controller->station);
}

/* Reads the PCIe LTR-enable policy without broadening the device match. */
static int
ax211_pci_ltr_enabled(
	struct ax211_pci_controller *controller,
	int *enabled)
{
	uint16_t control;
	unsigned capability;
	int error;

	/* Handles the controller availability. */
	if (controller == NULL || enabled == NULL)
		return EINVAL;
	*enabled = 0;
	capability = 0U;

	/* Checks the operation status. */
	error = drv_pci_device_find_capability(
		controller->device, AX211_PCIE_CAPABILITY, &capability);
	if (error == ENOENT)
		return 0;
	if (error != 0 || capability > UINT32_MAX - AX211_PCIE_DEVICE_CONTROL2)
		return error != 0 ? error : EIO;

	/* Checks the operation status. */
	error = drv_pci_device_config_read16(
		controller->device, capability + AX211_PCIE_DEVICE_CONTROL2,
		&control);
	if (error == 0)
		*enabled = (control & AX211_PCIE_DEVICE_CONTROL2_LTR) != 0U;

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* The exclusive MSI-X handler only latches work for a safe context. */
static int
ax211_pci_irq(
	void *argument)
{
	struct ax211_pci_controller *controller;
	struct net_device *device;
	unsigned long enabled;
	int schedule;

	/* Handles the controller availability. */
	controller = argument;
	if (controller == NULL)
		return 0;
	enabled = spin_lock_irqsave(&controller->interrupt_lock);

	controller->irq_latched = 1U;
	device = controller->net_device;
	schedule = controller->receive_enabled && controller->runtime_active &&
		   device != NULL;

	spin_unlock_irqrestore(&controller->interrupt_lock, enabled);

	/* Handles the schedule condition. */
	if (schedule)
		net_device_schedule_poll(device);

	/* Reports operation failure. */
	return 1;
}

/* Flushes the software latch before admitting one new nonzero epoch. */
static int
ax211_pci_receive_epoch_begin(
	void *argument,
	uint32_t generation)
{
	struct ax211_pci_controller *controller;
	unsigned long enabled;
	int result;

	/* Handles the controller availability. */
	controller = argument;
	if (controller == NULL || generation == 0U)
		return -1;
	enabled = spin_lock_irqsave(&controller->interrupt_lock);

	controller->receive_enabled = 0U;
	controller->irq_latched = 0U;
	controller->active_dma = NULL;
	kern_memset(controller->last_receive_header, 0,
	       sizeof(controller->last_receive_header));
	controller->last_receive_length = 0U;
	controller->last_receive_version = 0U;

	/* Checks the operation result. */
	result = controller->irq_allocated || controller->irq_established ? -1
									  : 0;
	if (result == 0) {
		controller->hardware_epoch = generation;
		controller->receive_enabled = 1U;
	}

	spin_unlock_irqrestore(&controller->interrupt_lock, enabled);

	/* Returns the computed result. */
	return result;
}

/* Binds coherent rings and one exact MSI-X vector transactionally. */
static int
ax211_pci_transport_bind(
	void *argument,
	struct intel_ax211_dma_resources *dma,
	struct intel_ax211_mmio *mmio,
	struct intel_ax211_transport *transport,
	uint32_t generation)
{
	struct ax211_pci_controller *controller;
	struct intel_ax211_transport_ring_memory memory;
	unsigned count;
	unsigned long enabled;
	int bus_master_enabled;
	int result;

	/* Checks the drv pci device dma result. */
	controller = argument;
	if (controller == NULL || dma == NULL || mmio != &controller->mmio ||
	    transport != &controller->transport || generation == 0U ||
	    dma->device == NULL ||
	    dma->device != drv_pci_device_dma(controller->device)) {
		/* Reports operation failure. */
		return -1;
	}
	enabled = spin_lock_irqsave(&controller->interrupt_lock);

	result = !controller->receive_enabled ||
				 controller->hardware_epoch != generation ||
				 controller->irq_allocated ||
				 controller->irq_established
			 ? -1
			 : 0;

	spin_unlock_irqrestore(&controller->interrupt_lock, enabled);

	/* Checks the drv dma device is coherent result. */
	if (result != 0 || !drv_dma_device_is_coherent(dma->device))
		return -1;

	kern_memset(&memory, 0, sizeof(memory));
	bus_master_enabled = 0;

	/* Checks the operation result. */
	result = drv_pci_device_set_bus_master(controller->device, true);
	if (result == 0) {
		bus_master_enabled = 1;
		result = drv_intel_ax211_transport_backend_init(
			&controller->transport_backend, mmio,
			&controller->backend, dma);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_BACKEND_OK) {
		result = drv_intel_ax211_transport_backend_ring_memory(
			&controller->transport_backend, &memory);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_BACKEND_OK) {
		result = drv_intel_ax211_transport_init(
			transport, drv_intel_ax211_transport_backend_ops(),
			&controller->transport_backend, &mmio->profile,
			&memory);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK) {
		count = 0U;
		result = drv_pci_device_allocate_irqs(
			controller->device, DRV_PCI_IRQ_ALLOW_MSIX, 1U, 1U,
			&controller->irq, &count);

		/* Checks the remaining item count. */
		if (count != 0U) {
			controller->irq_count = count;
			controller->irq_allocated = 1U;
		}

		/* Checks the operation result. */
		if (result == 0 &&
		    (count != 1U || controller->irq.type != DRV_PCI_IRQ_MSIX))
			result = EIO;
	}

	/* Checks the operation result. */
	if (result == 0) {
		/* Checks the operation result. */
		result = drv_pci_device_establish_irq(
			controller->device, &controller->irq, ax211_pci_irq,
			controller, "intel-ax211", &controller->irq_cookie);
		if (result == 0)
			controller->irq_established = 1U;
	}

	/* Checks the operation result. */
	if (result == 0) {
		enabled = spin_lock_irqsave(&controller->interrupt_lock);
		controller->active_dma = dma;
		spin_unlock_irqrestore(&controller->interrupt_lock, enabled);

		/* Succeeded. */
		return 0;
	}

	/* Handles the controller condition. */
	if (controller->irq_established) {
		/* Checks the drv pci device disestablish irq checked result. */
		if (drv_pci_device_disestablish_irq_checked(
			    controller->device, controller->irq_cookie) == 0) {
			controller->irq_cookie = NULL;
			controller->irq_established = 0U;
		}
	}

	/* Handles the controller condition. */
	if (!controller->irq_established && controller->irq_allocated) {
		drv_pci_device_free_irqs(controller->device, &controller->irq,
					 controller->irq_count);
		controller->irq_count = 0U;
		controller->irq_allocated = 0U;
	}

	/* Checks the drv pci device set bus master result. */
	if (bus_master_enabled &&
	    drv_pci_device_set_bus_master(controller->device, false) != 0)
		result = EIO;
	enabled = spin_lock_irqsave(&controller->interrupt_lock);

	controller->receive_enabled = 0U;
	controller->irq_latched = 0U;
	controller->active_dma = NULL;

	spin_unlock_irqrestore(&controller->interrupt_lock, enabled);

	/* Returns the computed result. */
	return result == 0 ? -1 : result;
}

/* Copies one exact event before returning its DMA buffer to firmware. */
static int
ax211_pci_receive_event(
	void *argument,
	uint64_t deadline_us,
	uint8_t *bytes,
	size_t capacity,
	struct intel_ax211_boot_received_event *event)
{
	struct ax211_pci_controller *controller;
	struct intel_ax211_transport_causes causes;
	struct intel_ax211_transport_rx_completion completion;
	struct drv_dma_buffer *buffer;
	uint64_t now;
	uint64_t remaining;
	uint32_t frame_length;
	uint32_t generation;
	size_t total_length;
	unsigned long enabled;
	int delay_result;
	int receive_result;
	int replenish_result;
	int rearm_result;
	int result;

	/* Handles the controller availability. */
	controller = argument;
	if (controller == NULL || bytes == NULL || capacity == 0U ||
	    event == NULL) {
		/* Returns the computed result. */
		return INTEL_AX211_BOOT_RECEIVE_IO;
	}
	kern_memset(event, 0, sizeof(*event));
	receive_result = INTEL_AX211_BOOT_RECEIVE_IO;

	/* Continue until the operation reaches a terminal state. */
	for (;;) {
		enabled = spin_lock_irqsave(&controller->interrupt_lock);
		generation = controller->hardware_epoch;
		result = controller->receive_enabled && generation != 0U &&
					 controller->active_dma != NULL
				 ? 0
				 : -1;
		controller->irq_latched = 0U;
		spin_unlock_irqrestore(&controller->interrupt_lock, enabled);
		if (result != 0)
			return INTEL_AX211_BOOT_RECEIVE_IO;
		kern_memset(&causes, 0, sizeof(causes));

		/* Checks the operation result. */
		result = drv_intel_ax211_transport_interrupt_claim(
			&controller->transport, &causes);
		if (result != INTEL_AX211_TRANSPORT_OK)
			return INTEL_AX211_BOOT_RECEIVE_IO;
		controller->command_fh_causes |= causes.queue_causes;
		controller->command_hw_causes |= causes.hardware;
		controller->command_raw_fh_causes |= causes.raw_queue_causes;
		controller->command_raw_hw_causes |= causes.raw_hardware;

		/* Checks the operation status. */
		if ((causes.queue_causes &
		     INTEL_AX211_TRANSPORT_QUEUE_CAUSE_FAULT) != 0U ||
		    (causes.hardware & INTEL_AX211_TRANSPORT_EVENT_FATAL_CAUSES) !=
			    0U) {
			kern_logf("intel-ax211: fatal firmware interrupt "
				   "fh=%08x hw=%08x raw-fh=%08x raw-hw=%08x\n",
				   causes.queue_causes, causes.hardware,
				   causes.raw_queue_causes,
				   causes.raw_hardware);

			/* Returns the computed result. */
			return INTEL_AX211_BOOT_RECEIVE_IO;
		}

		/*
		 * AX210-generation firmware configures RFH before raising the
		 * hardware-ALIVE cause.  Only then may the host publish its
		 * first receive credit; an earlier doorbell can be lost across
		 * that setup.
		 */
		if (!controller->transport.rx_active &&
		    (causes.hardware & INTEL_AX211_TRANSPORT_EVENT_CAUSE_ALIVE) !=
			    0U) {
			/* Checks the operation result. */
			result = drv_intel_ax211_transport_activate_rx(
				&controller->transport);
			if (result != INTEL_AX211_TRANSPORT_OK)
				return INTEL_AX211_BOOT_RECEIVE_IO;
		}

		kern_memset(&completion, 0, sizeof(completion));

		/* Checks the operation result. */
		result = controller->transport.rx_active
				 ? drv_intel_ax211_transport_rx_next(
					   &controller->transport, &completion)
				 : INTEL_AX211_TRANSPORT_STALE;
		if (result == INTEL_AX211_TRANSPORT_OK) {
			/* Handles the completion condition. */
			if (completion.buffer_id >=
			    controller->active_dma->rx_buffer_count) {
				/* Returns the computed result. */
				return INTEL_AX211_BOOT_RECEIVE_IO;
			}
			buffer = &controller->active_dma
					  ->rx_buffer[completion.buffer_id];
			kern_io_read_barrier();
			frame_length =
				buffer->address == NULL || buffer->size < 4U
					? 0U
					: ax211_pci_get_le32(buffer->address) &
						  0x3fffU;

			/* Handles the frame length condition. */
			total_length = (size_t)frame_length + 4U;
			if (frame_length >= 4U &&
			    total_length <= INTEL_AX211_BOOT_EVENT_CAPACITY &&
			    total_length <= buffer->size &&
			    total_length <= capacity) {
				kern_memcpy(bytes, buffer->address, total_length);
				receive_result = INTEL_AX211_BOOT_RECEIVE_OK;
			} else {
				receive_result = INTEL_AX211_BOOT_RECEIVE_IO;
			}

			replenish_result =
				drv_intel_ax211_transport_rx_replenish(
					&controller->transport,
					buffer->device_address);
			rearm_result =
				drv_intel_ax211_transport_interrupt_rearm(
					&controller->transport);

			/* Handles the replenish result condition. */
			if (replenish_result != INTEL_AX211_TRANSPORT_OK ||
			    rearm_result != INTEL_AX211_TRANSPORT_OK) {
				/* Returns the computed result. */
				return INTEL_AX211_BOOT_RECEIVE_IO;
			}

			/* Handles the receive result condition. */
			if (receive_result != INTEL_AX211_BOOT_RECEIVE_OK)
				return receive_result;
			event->length = total_length;
			event->generation = generation;
			event->notification_version =
				ax211_pci_notification_version(bytes,
							       total_length);
			kern_memcpy(controller->last_receive_header, bytes,
			       sizeof(controller->last_receive_header));
			controller->last_receive_length = total_length;
			controller->last_receive_version =
				event->notification_version;

			/* Returns the computed result. */
			return INTEL_AX211_BOOT_RECEIVE_OK;
		}

		/* Checks the operation result. */
		rearm_result = drv_intel_ax211_transport_interrupt_rearm(
			&controller->transport);
		if (result != INTEL_AX211_TRANSPORT_STALE ||
		    rearm_result != INTEL_AX211_TRANSPORT_OK) {
			/* Returns the computed result. */
			return INTEL_AX211_BOOT_RECEIVE_IO;
		}

		/* Checks the ax211 pci clock us result. */
		if (ax211_pci_clock_us(controller, &now) != 0)
			return INTEL_AX211_BOOT_RECEIVE_IO;

		/* Handles the now condition. */
		if (now >= deadline_us)
			return INTEL_AX211_BOOT_RECEIVE_TIMEOUT;
		remaining = deadline_us - now;

		/* Handles the delay result condition. */
		delay_result = controller->mmio.ops->delay_us(
			controller->mmio.argument,
			(uint32_t)(remaining < AX211_RECEIVE_POLL_DELAY_US
					   ? remaining
					   : AX211_RECEIVE_POLL_DELAY_US));
		if (delay_result != 0)
			return INTEL_AX211_BOOT_RECEIVE_IO;
	}
}

/* Publishes the prepared fragmented PNVM table under NIC ownership. */
static int
ax211_pci_publish_pnvm(
	void *argument,
	struct intel_ax211_dma_resources *dma)
{
	struct ax211_pci_controller *controller;
	int result;
	int unlock_result;

	/*
	 * Refuses DMA memory that is not the active one or holds no PNVM table.
	 */
	controller = argument;
	if (controller == NULL ||
	    dma == NULL ||
	    dma != controller->active_dma ||
	    !dma->pnvm_prepared ||
	    dma->pnvm_table.address == NULL ||
	    dma->pnvm_table.device_address == 0U ||
	    dma->pnvm_count == 0U) {
		return -1;
	}
	kern_io_write_barrier();

	/* Rings the PNVM doorbell while the NIC is held awake. */
	result = drv_intel_ax211_mmio_wake_hold(&controller->mmio);
	if (result != INTEL_AX211_MMIO_OK)
		return -1;
	result = drv_intel_ax211_mmio_prph_write32(
		&controller->mmio,
		controller->mmio.profile.upper_cpu_prph_base +
			AX211_PRPH_FIRMWARE_DOORBELL,
		AX211_FIRMWARE_DOORBELL_PNVM_READY);
	unlock_result = drv_intel_ax211_mmio_wake_release(&controller->mmio);

	/* Reports a failed write or release. */
	if (result != INTEL_AX211_MMIO_OK ||
	    unlock_result != INTEL_AX211_MMIO_OK) {
		return -1;
	}

	/* Succeeded: the firmware may read the table. */
	return 0;
}

/* The MSI-X path needs no ICT reset, but ownership must still be balanced. */
static int
ax211_pci_after_alive(
	void *argument,
	const struct intel_ax211_protocol_alive *alive)
{
	struct ax211_pci_controller *controller;
	int result;
	int unlock_result;

	/* Refuses a missing controller and an unhealthy ALIVE. */
	controller = argument;
	if (controller == NULL ||
	    alive == NULL ||
	    alive->status != INTEL_AX211_PROTOCOL_ALIVE_HEALTHY) {
		return -1;
	}

	/* Holds and releases the NIC once. */
	result = drv_intel_ax211_mmio_wake_hold(&controller->mmio);
	if (result != INTEL_AX211_MMIO_OK)
		return -1;
	unlock_result = drv_intel_ax211_mmio_wake_release(&controller->mmio);

	/* Reports a failed release. */
	if (unlock_result != INTEL_AX211_MMIO_OK)
		return -1;

	/* Succeeded. */
	return 0;
}

/* Masks, disestablishes, and retires the sole MSI-X lifetime in order. */
static int
ax211_pci_interrupt_drain(
	void *argument)
{
	struct ax211_pci_controller *controller;
	unsigned long enabled;
	int disable_result;
	int result;

	/* Refuses a missing controller. */
	controller = argument;
	if (controller == NULL)
		return -1;

	/* Stops the interrupt handler from scheduling receives. */
	enabled = spin_lock_irqsave(&controller->interrupt_lock);

	controller->receive_enabled = 0U;
	controller->irq_latched = 0U;

	spin_unlock_irqrestore(&controller->interrupt_lock, enabled);

	/* Masks every cause of a routed vector. */
	disable_result = INTEL_AX211_TRANSPORT_OK;
	if (controller->transport.msix_configured &&
	    (controller->irq_established || controller->irq_allocated)) {
		disable_result = drv_intel_ax211_transport_disable_interrupts(
			&controller->transport);
	}

	/* Disestablishes the handler, which waits for a running one. */
	result = 0;
	if (controller->irq_established) {
		/* Forgets the handler once it is gone. */
		result = drv_pci_device_disestablish_irq_checked(
			controller->device, controller->irq_cookie);
		if (result == 0) {
			controller->irq_cookie = NULL;
			controller->irq_established = 0U;
		}
	}

	/* Frees the vector once no handler can run. */
	if (result == 0 && controller->irq_allocated) {
		drv_pci_device_free_irqs(controller->device, &controller->irq,
					 controller->irq_count);
		controller->irq_count = 0U;
		controller->irq_allocated = 0U;
	}

	/*
	 * Bus mastering stays on: the firmware may still be writing RX DMA, and
	 * turning it off under that DMA hangs the integrated CNVi platform.  The
	 * stop turns it off after the controller reset (BUG-158).
	 */
	if (result == 0) {
		enabled = spin_lock_irqsave(&controller->interrupt_lock);
		controller->active_dma = NULL;
		spin_unlock_irqrestore(&controller->interrupt_lock, enabled);
	}

	/* Reports a handler that could not be disestablished. */
	if (result != 0)
		return -1;

	/* Reports a mask that failed. */
	if (disable_result != INTEL_AX211_TRANSPORT_OK)
		return -1;

	/* Succeeded: no interrupt reaches the driver. */
	return 0;
}

/* Turns PCI bus mastering off once the controller reset stopped its DMA. */
static int
ax211_pci_bus_master_disable(
	void *argument)
{
	struct ax211_pci_controller *controller;
	int error;

	/* Refuses a stop without its controller. */
	controller = argument;
	if (controller == NULL)
		return -1;

	/* Clears Bus Master Enable in the PCI command register. */
	error = drv_pci_device_set_bus_master(controller->device, false);
	if (error != 0)
		return -1;

	/* Succeeded: the device can no longer reach host memory. */
	return 0;
}

/* Reads the device clock in microseconds. */
static int
ax211_pci_clock_us(
	void *argument,
	uint64_t *time_us)
{
	struct ax211_pci_controller *controller;
	int result;

	/* Refuses missing arguments and a missing clock. */
	controller = argument;
	if (controller == NULL ||
	    time_us == NULL ||
	    controller->mmio.ops == NULL ||
	    controller->mmio.ops->clock_us == NULL) {
		return -1;
	}

	/* Reads the clock. */
	result = controller->mmio.ops->clock_us(
		controller->mmio.argument, time_us);
	if (result != 0)
		return result;

	/* Succeeded: *time_us holds the time. */
	return 0;
}

/* Holds the NIC awake: 0 on success, -1 on failure. */
static int
ax211_pci_wake_hold(
	void *argument)
{
	struct ax211_pci_controller *controller;
	int result;

	/* Refuses a missing controller. */
	controller = argument;
	if (controller == NULL)
		return -1;

	/* Holds the NIC. */
	result = drv_intel_ax211_mmio_wake_hold(&controller->mmio);
	if (result != INTEL_AX211_MMIO_OK)
		return -1;

	/* Succeeded. */
	return 0;
}

/* Releases the NIC: 0 on success, -1 on failure. */
static int
ax211_pci_wake_release(
	void *argument)
{
	struct ax211_pci_controller *controller;
	int result;

	/* Refuses a missing controller. */
	controller = argument;
	if (controller == NULL)
		return -1;

	/* Releases the NIC. */
	result = drv_intel_ax211_mmio_wake_release(&controller->mmio);
	if (result != INTEL_AX211_MMIO_OK)
		return -1;

	/* Succeeded. */
	return 0;
}

/*
 * Reports whether an event is a transmit response: 1 when it is, 0 when not.
 * TX_CMD uses the narrow data-queue header, whose wire group is legacy.
 */
static int
ax211_pci_tx_event(
	const struct intel_ax211_event *event)
{
	uint8_t group;

	/* Rejects a missing event and any other opcode. */
	if (event == NULL || event->command.opcode != INTEL_AX211_TX_OPCODE)
		return 0;

	/* Rejects any other family. */
	group = event->flags &
		(uint8_t)~INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK;
	if (group != INTEL_AX211_PROTOCOL_FAMILY_CLASSIC &&
	    group != INTEL_AX211_TX_FAMILY) {
		return 0;
	}

	/* The event is a transmit response. */
	return 1;
}

/* Pins layouts absent from the RX wire header at the PCI boundary. */
static uint8_t
ax211_pci_notification_version(
	const uint8_t *bytes,
	size_t length)
{
	uint8_t group;
	uint8_t opcode;
	uint8_t queue;

	/* An event without a header has no version. */
	if (bytes == NULL || length < INTEL_AX211_EVENT_HEADER_SIZE)
		return 0U;

	/* Reads the opcode, the family and the queue. */
	opcode = bytes[4U];
	group = bytes[5U] & (uint8_t)~INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK;
	queue = bytes[7U];

	/* A transmit response has the transmit layout. */
	if ((group == INTEL_AX211_PROTOCOL_FAMILY_CLASSIC ||
	     group == INTEL_AX211_TX_FAMILY) &&
	    opcode == INTEL_AX211_TX_OPCODE) {
		return INTEL_AX211_TX_NOTIFICATION_VERSION;
	}

	/* Any other command answer has no pinned version. */
	if ((queue & 0x80U) == 0U)
		return 0U;

	/* ALIVE. */
	if (group == INTEL_AX211_PROTOCOL_FAMILY_CLASSIC &&
	    opcode == INTEL_AX211_PROTOCOL_ALIVE_OPCODE) {
		return INTEL_AX211_PROTOCOL_ALIVE_VERSION;
	}

	/* PNVM accepted. */
	if (group == INTEL_AX211_PROTOCOL_FAMILY_REGULATORY_STORE &&
	    opcode == INTEL_AX211_PROTOCOL_PNVM_ACCEPTED_OPCODE) {
		return INTEL_AX211_PROTOCOL_PNVM_ACCEPTED_VERSION;
	}

	/* Init finished, whose version is not pinned. */
	if (group == INTEL_AX211_PROTOCOL_FAMILY_CLASSIC &&
	    opcode == INTEL_AX211_PROTOCOL_INIT_FINISHED_OPCODE) {
		return INTEL_AX211_PROTOCOL_UNKNOWN_VERSION;
	}

	/* Notification 0/0xc1 has layout version 5. */
	if (group == 0U && opcode == 0xc1U)
		return 5U;

	/* The time reservation notice. */
	if (group == INTEL_AX211_ASSOC_FAMILY_LINK_SETUP &&
	    opcode == INTEL_AX211_ASSOC_TIME_RESERVATION_NOTICE_OPCODE) {
		return INTEL_AX211_ASSOC_TIME_RESERVATION_NOTICE_LAYOUT_VERSION;
	}

	/* Notification 1/0xc8 has layout version 6. */
	if (group == 1U && opcode == 0xc8U)
		return 6U;

	/* Notifications 0/0x0f and 0/0xb5 have layout version 1. */
	if (group == 0U && (opcode == 0x0fU || opcode == 0xb5U))
		return 1U;

	/* Any other notification's version is not pinned. */
	return INTEL_AX211_PROTOCOL_UNKNOWN_VERSION;
}

/* Binds the live command owner to one firmware epoch and copied radio data. */
static int
ax211_pci_scan_initialize(
	struct ax211_pci_controller *controller)
{
	struct intel_ax211_protocol_command_table table;
	struct intel_ax211_runtime_mcc mcc;
	struct wlan_scan_profile profile;
	int validate_result;
	int result;

	/* Refuses a controller whose operational firmware is not running. */
	if (controller == NULL ||
	    !controller->runtime_initialized ||
	    controller->runtime_start.state !=
		    INTEL_AX211_RUNTIME_START_STATE_RUNNING ||
	    controller->hardware_epoch == 0U ||
	    controller->net_device == NULL) {
		return EINVAL;
	}

	/* Describes the pinned command table. */
	kern_memset(&table, 0, sizeof(table));
	kern_memset(&mcc, 0, sizeof(mcc));
	table.bytes = controller->runtime_start.command_version_bytes;
	table.count = INTEL_AX211_PROTOCOL_API89_COMMAND_COUNT;

	/* Takes the country answer and requires the receive versions. */
	result = drv_intel_ax211_runtime_start_mcc(&controller->runtime_start,
						   &mcc);
	if (result == INTEL_AX211_RUNTIME_START_OK) {
		/* Requires the receive command versions. */
		validate_result = drv_intel_ax211_rx_api89_validate(&table);
		if (validate_result == INTEL_AX211_RX_OK)
			result = INTEL_AX211_SCAN_SESSION_OK;
		else
			result = INTEL_AX211_SCAN_SESSION_UNSUPPORTED;
	} else {
		result = INTEL_AX211_SCAN_SESSION_FAILED;
	}

	/* Initializes the scan session. */
	if (result == INTEL_AX211_SCAN_SESSION_OK) {
		result = drv_intel_ax211_scan_session_init(
			&controller->scan_session,
			&controller->runtime_start.commands, &table,
			&controller->runtime_start.nvm, &mcc,
			controller->net_device->hwaddr,
			controller->hardware_epoch);
	}

	ax211_pci_scrub(&mcc, sizeof(mcc));

	/* Reports a failed initialization. */
	if (result != INTEL_AX211_SCAN_SESSION_OK) {
		result = ax211_pci_scan_result_errno(result);
		return result;
	}

	kern_memset(&profile, 0, sizeof(profile));

	/* Gives the station the channels the firmware scans. */
	result = ax211_pci_runtime_scan_profile(controller, &profile);
	if (result == 0) {
		/* A controller without a station has no device. */
		if (controller->station == NULL) {
			result = ENODEV;
		} else {
			result = wlan_station_scan_profile_update(
				controller->station, &profile);
		}
	}

	ax211_pci_scrub(&profile, sizeof(profile));

	/* Reports a failed update. */
	if (result != 0)
		return result;

	/* Succeeded: scanning is initialized. */
	controller->scan_step_index = 0U;
	controller->scan_initialized = 1U;
	return 0;
}

/* Returns the center frequency in MHz of a channel, or 0 for none. */
static uint32_t
ax211_pci_channel_frequency(
	uint8_t channel)
{
	/* Channels 1 to 13 of 2.4 GHz. */
	if (channel >= 1U && channel <= 13U)
		return 2407U + 5U * (uint32_t)channel;

	/* Channel 14. */
	if (channel == 14U)
		return 2484U;

	/* The 5 GHz channels. */
	if ((channel >= 36U && channel <= 144U && (channel - 36U) % 4U == 0U) ||
	    (channel >= 149U && channel <= 181U && (channel - 149U) % 4U == 0U)) {
		return 5000U + 5U * (uint32_t)channel;
	}

	/* Any other channel has no frequency. */
	return 0U;
}

/* Describes the scan session's channels as the station's scan profile. */
static int
ax211_pci_runtime_scan_profile(
	const struct ax211_pci_controller *controller,
	struct wlan_scan_profile *profile)
{
	uint32_t frequency;
	const struct intel_ax211_scan_profile *source;
	size_t index;

	/* Refuses missing arguments. */
	if (controller == NULL || profile == NULL)
		return EINVAL;

	/* Refuses an empty or oversized channel list. */
	source = &controller->scan_session.full_profile;
	if (source->channel_count == 0U ||
	    source->channel_count > WLAN_SCAN_CHANNEL_MAX) {
		return EINVAL;
	}
	kern_memset(profile, 0, sizeof(*profile));
	profile->channel_count = (uint32_t)source->channel_count;

	/* Describes each channel. */
	for (index = 0U; index < source->channel_count; index++) {
		/* Refuses a channel without a frequency. */
		frequency = ax211_pci_channel_frequency(source->channel[index]);
		if (frequency == 0U)
			return EINVAL;
		profile->channels[index].channel = source->channel[index];
		profile->channels[index].center_frequency_mhz = frequency;
		profile->channels[index].flags = WLAN_SCAN_CHANNEL_OFFLOADED_DWELL;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Reports whether the scan covers a channel: 1 when it does, 0 when not.
 */
static int
ax211_pci_scan_channel_present(
	const struct ax211_pci_controller *controller,
	uint8_t channel)
{
	size_t index;

	/* No channel is covered without a scan session. */
	if (controller == NULL || !controller->scan_initialized)
		return 0;

	/* Looks for the channel. */
	for (index = 0U;
	     index < controller->scan_session.full_profile.channel_count;
	     index++) {
		/* The channel is covered. */
		if (controller->scan_session.full_profile.channel[index] ==
		    channel) {
			return 1;
		}
	}

	/* The channel is not covered. */
	return 0;
}

/* Retires every common-generation reference before the hardware epoch stops. */
static void
ax211_pci_scan_clear(
	struct ax211_pci_controller *controller)
{
	/* Nothing to clear without a controller. */
	if (controller == NULL)
		return;

	/* Forgets the scan session and both BSS sets. */
	controller->scan_initialized = 0U;
	controller->scan_step_index = 0U;
	controller->bss_staging_generation = 0U;
	controller->bss_published_generation = 0U;
	controller->bss_staging_initialized = 0U;
	controller->bss_published_initialized = 0U;
	ax211_pci_scrub(&controller->scan_session,
			sizeof(controller->scan_session));
	ax211_pci_scrub(&controller->bss_staging_cache,
			sizeof(controller->bss_staging_cache));
	ax211_pci_scrub(&controller->bss_published_cache,
			sizeof(controller->bss_published_cache));
	ax211_pci_scrub(controller->runtime_frame,
			sizeof(controller->runtime_frame));
}

/* Drops an unpublished scan generation without touching the last snapshot. */
static void
ax211_pci_bss_staging_discard(
	struct ax211_pci_controller *controller)
{
	/* Nothing to drop without a controller. */
	if (controller == NULL)
		return;

	/* Forgets the staging set. */
	controller->bss_staging_generation = 0U;
	controller->bss_staging_initialized = 0U;
	ax211_pci_scrub(&controller->bss_staging_cache,
			sizeof(controller->bss_staging_cache));
}

/* Atomically replaces the private snapshot after the complete scan succeeds. */
static int
ax211_pci_bss_staging_publish(
	struct ax211_pci_controller *controller,
	uint64_t generation)
{
	/* Refuses missing arguments and a missing staging set. */
	if (controller == NULL ||
	    generation == 0U ||
	    !controller->bss_staging_initialized) {
		return EINVAL;
	}

	/* Refuses another generation's set. */
	if (controller->bss_staging_generation != generation)
		return ESTALE;

	/* Succeeded: the staging set becomes the published one. */
	controller->bss_published_cache = controller->bss_staging_cache;
	controller->bss_published_generation = generation;
	controller->bss_published_initialized = 1U;
	ax211_pci_bss_staging_discard(controller);
	return 0;
}

/* Decodes only the already copied, epoch-tagged event envelope. */
static int
ax211_pci_event_message(
	const uint8_t *bytes,
	size_t length,
	const struct intel_ax211_boot_received_event *received,
	struct intel_ax211_event *event,
	struct intel_ax211_protocol_message *message)
{
	int tx_event;
	int result;

	/* Refuses missing arguments, an unset epoch and another length. */
	if (bytes == NULL ||
	    received == NULL ||
	    event == NULL ||
	    message == NULL ||
	    received->generation == 0U ||
	    received->length != length) {
		return EIO;
	}

	/* Refuses an envelope that does not decode. */
	result = drv_intel_ax211_event_decode(bytes, length, event);
	if (result != INTEL_AX211_SUCCESS)
		return EIO;

	/* Refuses a payload that does not end where the event ends. */
	if (event->payload_offset > length ||
	    event->payload_length != length - event->payload_offset) {
		return EIO;
	}

	/* Builds the message the protocol layers validate. */
	kern_memset(message, 0, sizeof(*message));
	message->opcode = event->command.opcode;
	message->group = event->flags &
			 (uint8_t)~INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK;
	message->version = received->notification_version;
	message->flags = event->flags;
	message->queue = event->queue;
	message->index = event->index;
	message->generation = received->generation;
	message->payload = bytes + event->payload_offset;
	message->payload_length = event->payload_length;

	/* A transmit event takes the transmit family and version. */
	tx_event = ax211_pci_tx_event(event);
	if (tx_event) {
		message->group = INTEL_AX211_TX_FAMILY;
		message->version = INTEL_AX211_TX_NOTIFICATION_VERSION;
	}

	/* Succeeded: *message describes the event. */
	return 0;
}

/* Delivers the asynchronous request/abort acknowledgement in poll context. */
static int
ax211_pci_scan_command_dispatch(
	struct ax211_pci_controller *controller,
	const uint8_t *bytes,
	size_t length,
	uint64_t now)
{
	uint8_t phase;
	unsigned opcode;
	unsigned group;
	unsigned slot;
	unsigned queue;
	int report_result;
	int result;

	/* Nothing to deliver without a scan session. */
	if (!controller->scan_initialized)
		return 0;

	/* Delivers the acknowledgement the session waits for. */
	phase = controller->scan_session.phase;
	if (phase == INTEL_AX211_SCAN_SESSION_WAIT_START_ACK) {
		/* Reports a started channel to the station. */
		result = drv_intel_ax211_scan_session_start_ack(
			&controller->scan_session, bytes, length,
			controller->hardware_epoch, now);
		if (result == INTEL_AX211_SCAN_SESSION_OK) {
			/*
			 * This report only latches under station->lock and
			 * wakes the worker; unlike TX completion, it cannot
			 * synchronously call a radio op.
			 */
			report_result = wlan_station_report_scan_channel_ready(
				controller->station,
				controller->scan_session.common_generation,
				controller->scan_step_index);
			if (report_result != 0 &&
			    report_result != ESTALE &&
			    report_result != ENODEV) {
				ax211_pci_scan_report_error(
					controller,
					INTEL_AX211_SCAN_SESSION_FAILED);
			}

			/* Succeeded: the channel scan runs. */
			return 0;
		}
	} else if (phase == INTEL_AX211_SCAN_SESSION_WAIT_ABORT_ACK) {
		result = drv_intel_ax211_scan_session_abort_ack(
			&controller->scan_session, bytes, length,
			controller->hardware_epoch, now);
	} else {
		/* Nothing is awaited. */
		return 0;
	}

	/* An aborted scan discards its staging set. */
	if (result == INTEL_AX211_SCAN_SESSION_ABORTED)
		ax211_pci_bss_staging_discard(controller);

	/* An expected, repeated or stale acknowledgement is not a failure. */
	if (result == INTEL_AX211_SCAN_SESSION_OK ||
	    result == INTEL_AX211_SCAN_SESSION_ABORTED ||
	    result == INTEL_AX211_SCAN_SESSION_DUPLICATE ||
	    result == INTEL_AX211_SCAN_SESSION_STALE ||
	    result == INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER) {
		return 0;
	}

	/* Takes the header bytes that arrived, for the log. */
	opcode = 0U;
	group = 0U;
	slot = 0U;
	queue = 0U;
	if (length > 4U)
		opcode = bytes[4U];
	if (length > 5U)
		group = bytes[5U];
	if (length > 6U)
		slot = bytes[6U];
	if (length > 7U)
		queue = bytes[7U];

	/* Logs the failure and reports it to the station. */
	kern_logf("intel-ax211: scan command failed phase=%u result=%d "
		   "length=%u opcode=%02x group=%02x index=%02x queue=%02x\n",
		   phase, result, (unsigned)length, opcode, group, slot, queue);
	ax211_pci_scan_report_error(controller, result);

	/* The failure is reported through the scan error latch. */
	return 0;
}

/* Accepts only scan notifications belonging to the current hardware epoch. */
static int
ax211_pci_scan_notification_dispatch(
	struct ax211_pci_controller *controller,
	const struct intel_ax211_protocol_message *message,
	uint64_t now)
{
	const uint8_t *payload;
	struct intel_ax211_scan_session_event reported;
	int result;
	int report_result;

	/* Nothing to accept without a scan session. */
	if (!controller->scan_initialized)
		return 0;
	kern_memset(&reported, 0, sizeof(reported));

	/* Logs a notification the session refuses. */
	result = drv_intel_ax211_scan_session_notification(
		&controller->scan_session, message, now, &reported);
	if (result == INTEL_AX211_SCAN_SESSION_FAILED &&
	    message->payload != NULL &&
	    message->payload_length >= 16U) {
		payload = message->payload;

		/* Reports a notification this driver does not recognize. */
		kern_logf(
			"intel-ax211: unexpected scan notification opcode=%02x "
			"flags=%02x "
			"length=%u uid=%02x%02x%02x%02x schedule=%u "
			"iteration=%u "
			"status=%u ebs=%u "
			"tail=%02x%02x%02x%02x%02x%02x%02x%02x\n",
			message->opcode, message->flags,
			(unsigned)message->payload_length, payload[3U],
			payload[2U], payload[1U], payload[0U], payload[4U],
			payload[5U], payload[6U], payload[7U], payload[8U],
			payload[9U], payload[10U], payload[11U], payload[12U],
			payload[13U], payload[14U], payload[15U]);
	}

	/* An aborted scan discards its staging set. */
	if (result == INTEL_AX211_SCAN_SESSION_ABORTED)
		ax211_pci_bss_staging_discard(controller);

	/* Advances the common channel only after a validated firmware event. */
	if (result == INTEL_AX211_SCAN_SESSION_COMPLETE) {
		/* A refused report is a scan failure. */
		report_result = wlan_station_report_scan_channel_complete(
			controller->station,
			reported.common_generation,
			controller->scan_step_index);
		if (report_result != 0 &&
		    report_result != ESTALE &&
		    report_result != ENODEV) {
			ax211_pci_scan_report_error(controller,
			    INTEL_AX211_SCAN_SESSION_FAILED);
		}
	}

	/* An expected, repeated or stale notification is not a failure. */
	if (result == INTEL_AX211_SCAN_SESSION_OK ||
	    result == INTEL_AX211_SCAN_SESSION_COMPLETE ||
	    result == INTEL_AX211_SCAN_SESSION_ABORTED ||
	    result == INTEL_AX211_SCAN_SESSION_DUPLICATE ||
	    result == INTEL_AX211_SCAN_SESSION_STALE) {
		return 0;
	}
	ax211_pci_scan_report_error(controller, result);

	/* The failure is reported through the scan error latch. */
	return 0;
}

/* Normalizes MPDU v5, reports scan frames, and dispatches connection frames. */
static int
ax211_pci_rx_dispatch(
	struct ax211_pci_controller *controller,
	const struct intel_ax211_protocol_message *message)
{
	struct intel_ax211_bss_entry bss_entry;
	struct intel_ax211_rx_delivery mpdu;
	uint16_t frame_control;
	uint16_t subtype;
	int decode_result;
	int dispatch_result;
	int result;

	/* Decodes the frame into cleared staging. */
	kern_memset(&bss_entry, 0, sizeof(bss_entry));
	kern_memset(&mpdu, 0, sizeof(mpdu));
	ax211_pci_scrub(controller->runtime_frame,
			sizeof(controller->runtime_frame));
	result = drv_intel_ax211_rx_mpdu_decode(
		message, controller->hardware_epoch, controller->runtime_frame,
		sizeof(controller->runtime_frame), &mpdu);
	if (result != INTEL_AX211_RX_OK) {
		/*
		 * A malformed or unsupported over-the-air frame is local to
		 * this receive slot.  Dropping it must not quarantine the
		 * firmware epoch.
		 */
		ax211_pci_scrub(controller->runtime_frame,
				sizeof(controller->runtime_frame));

		/* Succeeded: the frame is dropped. */
		return 0;
	}

	/* Reads the frame control field. */
	frame_control = (uint16_t)((uint16_t)mpdu.frame[0U] |
				   ((uint16_t)mpdu.frame[1U] << 8));
	subtype = frame_control & 0x00f0U;

	/* Hands a beacon or probe response of a running scan to the scan. */
	if (controller->scan_initialized &&
	    controller->scan_session.phase ==
		    INTEL_AX211_SCAN_SESSION_RUNNING &&
	    (frame_control & 0x000cU) == 0U &&
	    (subtype == 0x0080U || subtype == 0x0050U)) {
		/* Records the BSS in the staging set of the scan generation. */
		if (controller->bss_staging_initialized &&
		    controller->bss_staging_generation ==
			    controller->scan_session.common_generation) {
			/* Records a frame that decodes as a BSS. */
			decode_result = drv_intel_ax211_bss_decode(
				&mpdu, controller->scan_session.common_generation,
				controller->hardware_epoch, &bss_entry);
			if (decode_result == INTEL_AX211_BSS_OK) {
				/*
				 * Use the same monotonic observation point
				 * immediately before both private and common
				 * admission decisions.
				 */
				bss_entry.last_seen_ticks = clock_ticks();
				(void)drv_intel_ax211_bss_cache_observe(
					&controller->bss_staging_cache,
					&bss_entry);
			}
		}

		/*
		 * Scan observation copies/latches data without entering radio
		 * callbacks.
		 */
		(void)wlan_station_report_scan_frame(
			controller->station,
			controller->scan_session.common_generation, mpdu.frame,
			mpdu.length, mpdu.rssi_dbm, mpdu.channel);
	}

	/* Hands the frame to the connection, when there is one. */
	dispatch_result = 0;
	if (controller->connection_generation != 0U) {
		dispatch_result = ax211_pci_connection_rx_dispatch(
			controller, &mpdu, frame_control);
	}

	/* Clears the copies. */
	ax211_pci_scrub(&bss_entry, sizeof(bss_entry));
	ax211_pci_scrub(controller->runtime_frame,
			sizeof(controller->runtime_frame));
	if (dispatch_result != 0)
		return dispatch_result;

	/* Succeeded. */
	return 0;
}

/* Converts private coordinator outcomes to one common scan error latch. */
static void
ax211_pci_scan_report_error(
	struct ax211_pci_controller *controller,
	int result)
{
	int error;

	/* Nothing to report without a running scan generation. */
	if (controller == NULL ||
	    controller->station == NULL ||
	    !controller->scan_initialized ||
	    controller->scan_session.common_generation == 0U) {
		return;
	}

	/* Logs a failure, discards the staging set and latches the error. */
	error = ax211_pci_scan_result_errno(result);
	if (error != 0) {
		kern_logf("intel-ax211: scan generation=%u failed result=%d "
			   "error=%d phase=%u\n",
			   (unsigned)controller->scan_session.common_generation,
			   result, error, controller->scan_session.phase);
		ax211_pci_bss_staging_discard(controller);

		/*
		 * This latch only wakes common work; lifecycle_lock remains the
		 * owner.
		 */
		(void)wlan_station_report_scan_error(
			controller->station,
			controller->scan_session.common_generation, error);
	}
}

/* Maps the scan session's results to errno values. */
static int
ax211_pci_scan_result_errno(
	int result)
{
	/* Maps each result. */
	switch (result) {
	case INTEL_AX211_SCAN_SESSION_OK:
	case INTEL_AX211_SCAN_SESSION_COMPLETE:
	case INTEL_AX211_SCAN_SESSION_ABORTED:
		/* A success, a completed or an aborted scan. */
		return 0;
	case INTEL_AX211_SCAN_SESSION_INVALID:
		/* An invalid argument. */
		return EINVAL;
	case INTEL_AX211_SCAN_SESSION_UNSUPPORTED:
		/* An unsupported firmware. */
		return ENOTSUP;
	case INTEL_AX211_SCAN_SESSION_BUSY:
		/* A session still running. */
		return EBUSY;
	case INTEL_AX211_SCAN_SESSION_STALE:
	case INTEL_AX211_SCAN_SESSION_DUPLICATE:
	case INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER:
		/* An event that does not belong to the current scan. */
		return ESTALE;
	case INTEL_AX211_SCAN_SESSION_TIMEOUT:
		/* A timeout. */
		return ETIMEDOUT;
	case INTEL_AX211_SCAN_SESSION_COMMAND:
	case INTEL_AX211_SCAN_SESSION_FAILED:
	default:
		/* Any other failure is an I/O failure. */
		return EIO;
	}
}

/* Dispatches one recycled-safe copy; no DMA pointer crosses this boundary. */
static int
ax211_pci_runtime_event_dispatch(
	struct ax211_pci_controller *controller,
	const uint8_t *bytes,
	size_t length,
	const struct intel_ax211_boot_received_event *event)
{
	struct intel_ax211_event decoded;
	struct intel_ax211_protocol_message message;
	uint64_t now;
	int tx_event;
	int result;

	/* Refuses missing arguments and an event of another epoch or length. */
	if (controller == NULL ||
	    bytes == NULL ||
	    event == NULL ||
	    event->generation == 0U ||
	    event->generation != controller->hardware_epoch ||
	    event->length != length) {
		return EINVAL;
	}
	kern_memset(&decoded, 0, sizeof(decoded));
	kern_memset(&message, 0, sizeof(message));

	/* Decodes the event. */
	result = ax211_pci_event_message(bytes, length, event, &decoded,
					 &message);
	if (result != 0)
		return result;

	/* Reads the clock. */
	result = ax211_pci_clock_us(controller, &now);
	if (result != 0)
		return EIO;

	/* Hands a transmit event to the transmit ring. */
	tx_event = ax211_pci_tx_event(&decoded);
	if (tx_event) {
		/* Reports a failed dispatch. */
		result = ax211_pci_tx_dispatch(controller, &message);
		if (result != 0)
			return result;

		/* Succeeded. */
		return 0;
	}

	/* Hands a command answer to the scan session. */
	if ((decoded.queue & 0x80U) == 0U) {
		/* Reports a failed dispatch. */
		result = ax211_pci_scan_command_dispatch(
			controller, bytes, length, now);
		if (result != 0)
			return result;

		/* Succeeded. */
		return 0;
	}

	/* Hands a scan notification to the scan session. */
	if (message.group == INTEL_AX211_SCAN_FAMILY_CLASSIC &&
	    (message.opcode == INTEL_AX211_SCAN_FINISHED_OPCODE ||
	     message.opcode == INTEL_AX211_SCAN_PASS_FINISHED_OPCODE)) {
		/* Reports a failed dispatch. */
		result = ax211_pci_scan_notification_dispatch(
			controller, &message, now);
		if (result != 0)
			return result;

		/* Succeeded. */
		return 0;
	}

	/* Hands a received frame to the receive path. */
	if (message.group == INTEL_AX211_RX_FRAME_FAMILY &&
	    message.opcode == INTEL_AX211_RX_FRAME_OPCODE) {
		/* Reports a failed dispatch. */
		result = ax211_pci_rx_dispatch(controller, &message);
		if (result != 0)
			return result;

		/* Succeeded. */
		return 0;
	}

	/* Hands a time reservation notice to the association session. */
	if (message.group == INTEL_AX211_ASSOC_FAMILY_LINK_SETUP &&
	    message.opcode == INTEL_AX211_ASSOC_TIME_RESERVATION_NOTICE_OPCODE &&
	    controller->association_initialized) {
		/* An expired, ignored, repeated or stale notice is accepted. */
		result = drv_intel_ax211_assoc_session_event_accept(
			&controller->association, &message,
			controller->connection_generation,
			controller->hardware_epoch);
		if (result == INTEL_AX211_ASSOC_SESSION_EXPIRED ||
		    result == INTEL_AX211_ASSOC_EVENT_IGNORED ||
		    result == INTEL_AX211_ASSOC_DUPLICATE ||
		    result == INTEL_AX211_ASSOC_STALE) {
			return 0;
		}

		/* Failed. */
		return EIO;
	}

	/* Succeeded: any other event is ignored. */
	return 0;
}

/* Retains an exact post-recycle notification until the common gate unwinds. */
static int
ax211_pci_deferred_event_enqueue(
	struct ax211_pci_controller *controller,
	const uint8_t *bytes,
	const struct intel_ax211_boot_received_event *received)
{
	struct ax211_pci_deferred_event *slot;
	unsigned index;

	/* Refuses missing arguments and an event of another epoch or size. */
	if (controller == NULL ||
	    bytes == NULL ||
	    received == NULL ||
	    received->length == 0U ||
	    received->length > INTEL_AX211_BOOT_EVENT_CAPACITY ||
	    received->generation == 0U ||
	    received->generation != controller->hardware_epoch) {
		return EINVAL;
	}

	/* Refuses an event when the queue is full. */
	if (controller->deferred_event_count >= AX211_DEFERRED_EVENT_LIMIT)
		return ENOBUFS;

	/* Copies the event to the tail of the queue. */
	index = (controller->deferred_event_head +
		 controller->deferred_event_count) %
		AX211_DEFERRED_EVENT_LIMIT;
	slot = &controller->deferred_event[index];
	kern_memset(slot, 0, sizeof(*slot));
	slot->received = *received;
	kern_memcpy(slot->bytes, bytes, received->length);
	controller->deferred_event_count++;

	/* Asks for a poll to drain the queue. */
	if (controller->net_device != NULL)
		net_device_schedule_poll(controller->net_device);

	/* Succeeded. */
	return 0;
}

/* Dispatches one copied notification only from the outer poll safe point. */
static int
ax211_pci_deferred_event_drain_one(
	struct ax211_pci_controller *controller)
{
	struct ax211_pci_deferred_event *slot;
	struct intel_ax211_boot_received_event received;
	int result;

	/* Nothing to drain. */
	if (controller == NULL || controller->deferred_event_count == 0U)
		return ENOENT;

	/* A drain already runs. */
	if (controller->deferred_event_draining)
		return EBUSY;

	/* Refuses a slot of an invalid size. */
	slot = &controller->deferred_event[controller->deferred_event_head];
	if (slot->received.length == 0U ||
	    slot->received.length > sizeof(controller->deferred_dispatch_event)) {
		return EIO;
	}

	/* Takes the oldest event off the queue. */
	received = slot->received;
	kern_memcpy(controller->deferred_dispatch_event, slot->bytes,
	       received.length);
	ax211_pci_scrub(slot, sizeof(*slot));
	controller->deferred_event_head =
		(controller->deferred_event_head + 1U) %
		AX211_DEFERRED_EVENT_LIMIT;
	controller->deferred_event_count--;

	/* Dispatches the event and clears its copy. */
	controller->deferred_event_draining = 1U;
	result = ax211_pci_runtime_event_dispatch(
		controller, controller->deferred_dispatch_event,
		received.length, &received);
	ax211_pci_scrub(controller->deferred_dispatch_event, received.length);
	controller->deferred_event_draining = 0U;
	if (result != 0)
		return result;

	/* Succeeded. */
	return 0;
}

/* Scrubs every copied notification when its firmware epoch is retired. */
static void
ax211_pci_deferred_event_clear(
	struct ax211_pci_controller *controller)
{
	/* Nothing to clear without a controller. */
	if (controller == NULL)
		return;

	/* Empties the queue and clears every copy. */
	controller->deferred_event_head = 0U;
	controller->deferred_event_count = 0U;
	controller->deferred_event_draining = 0U;
	ax211_pci_scrub(controller->deferred_event,
			sizeof(controller->deferred_event));
	ax211_pci_scrub(controller->deferred_dispatch_event,
			sizeof(controller->deferred_dispatch_event));
}

/* Reads a little-endian 32-bit value. */
static uint32_t
ax211_pci_get_le32(
	const uint8_t bytes[4])
{
	/* Assembles the word from its four bytes, least significant first. */
	return (uint32_t)bytes[0U] | ((uint32_t)bytes[1U] << 8) |
	       ((uint32_t)bytes[2U] << 16) | ((uint32_t)bytes[3U] << 24);
}

/* Coherent TX allocations need only an ordered visibility boundary. */
static int
ax211_pci_tx_sync_for_device(
	void *argument,
	const struct drv_dma_buffer *buffer,
	size_t offset,
	size_t length)
{
	struct ax211_pci_controller *controller;
	int coherent;

	/*
	 * Refuses missing arguments, a range outside the buffer and a controller
	 * without a runtime.
	 */
	controller = argument;
	if (controller == NULL ||
	    buffer == NULL ||
	    buffer->address == NULL ||
	    buffer->device_address == 0U ||
	    offset > buffer->size ||
	    length > buffer->size - offset ||
	    !controller->runtime_initialized) {
		return -1;
	}

	/* Refuses DMA memory that is not coherent. */
	coherent = drv_dma_device_is_coherent(controller->frame_ring.dma_device);
	if (!coherent)
		return -1;

	/* Succeeded: orders the writes before the device reads them. */
	kern_io_write_barrier();
	return 0;
}

/* Rings only the API89 queue-1 write-pointer doorbell. */
static int
ax211_pci_tx_write32(
	void *argument,
	uint32_t offset,
	uint32_t value)
{
	struct ax211_pci_controller *controller;
	int result;

	/* Refuses any other register and a missing register write. */
	controller = argument;
	if (controller == NULL ||
	    offset != INTEL_AX211_TX_RING_WRITE_POINTER_REGISTER ||
	    controller->mmio.ops == NULL ||
	    controller->mmio.ops->csr_write32 == NULL) {
		return -1;
	}

	/* Orders the ring's writes before the doorbell, then rings it. */
	kern_io_write_barrier();
	result = controller->mmio.ops->csr_write32(
		controller->mmio.argument, offset, value);
	if (result != 0)
		return result;

	/* Succeeded. */
	return 0;
}

/* Reads the device clock for the association session; UINT64_MAX on failure. */
static uint64_t
ax211_pci_assoc_clock_us(
	void *argument)
{
	uint64_t now;
	int result;

	/* A failed read is the end of time, which expires every command. */
	result = ax211_pci_clock_us(argument, &now);
	if (result != 0)
		return UINT64_MAX;

	/* Reports the time. */
	return now;
}

/* Converts one common-clock deadline into a bounded device-clock interval. */
static int
ax211_pci_command_timeout(
	struct ax211_pci_controller *controller,
	uint64_t deadline_ticks,
	uint64_t now_us,
	uint64_t *timeout_us,
	uint64_t *deadline_us)
{
	uint64_t now_ticks;
	uint64_t remaining_ticks;
	uint64_t remaining_us;

	/* Refuses missing arguments. */
	if (controller == NULL || timeout_us == NULL || deadline_us == NULL)
		return EINVAL;
	remaining_us = AX211_DIRECT_TIMEOUT_US;

	/* Bounds the interval by a set deadline. */
	if (deadline_ticks != 0U && deadline_ticks != UINT64_MAX) {
		/* Refuses a deadline that has passed. */
		now_ticks = clock_ticks();
		if (now_ticks >= deadline_ticks)
			return ETIMEDOUT;

		/*
		 * Converts the remaining ticks, keeping the bound on overflow.
		 */
		remaining_ticks = deadline_ticks - now_ticks;
		if (remaining_ticks > UINT64_MAX / 1000000U) {
			remaining_us = AX211_DIRECT_TIMEOUT_US;
		} else {
			remaining_us =
				remaining_ticks * 1000000U / KERN_CLOCK_HZ;

			/* Rounds a remainder below 1 us up to 1 us. */
			if (remaining_us == 0U)
				remaining_us = 1U;

			/* Never exceeds the bound. */
			if (remaining_us > AX211_DIRECT_TIMEOUT_US)
				remaining_us = AX211_DIRECT_TIMEOUT_US;
		}
	}

	/* Refuses a deadline beyond the device clock. */
	if (now_us > UINT64_MAX - remaining_us)
		return EOVERFLOW;

	/* Succeeded: reports the interval and the deadline. */
	*timeout_us = remaining_us;
	*deadline_us = now_us + remaining_us;
	return 0;
}

/*
 * Reads a bounded AX210-family SRAM range while an outer command owns the NIC.
 * The HBUS data window advances by one dword after every successful read.
 */
static int
ax211_pci_sram_read_locked(
	struct ax211_pci_controller *controller,
	uint32_t address,
	uint32_t *words,
	size_t count)
{
	size_t index;
	int result;

	/*
	 * Refuses missing arguments, a misplaced or misaligned range, a range
	 * beyond the address space, a NIC not held awake, and missing register
	 * access.
	 */
	if (controller == NULL ||
	    words == NULL ||
	    count == 0U ||
	    address < AX211_ERROR_LOG_MIN_ADDRESS ||
	    (address & 3U) != 0U ||
	    count - 1U > (UINT32_MAX - address) / sizeof(uint32_t) ||
	    controller->mmio.wake_hold_depth == 0U ||
	    controller->mmio.ops == NULL ||
	    controller->mmio.ops->csr_read32 == NULL ||
	    controller->mmio.ops->csr_write32 == NULL) {
		return EINVAL;
	}

	/* Points the data window at the address. */
	result = controller->mmio.ops->csr_write32(controller->mmio.argument,
						   AX211_CSR_SRAM_READ_ADDRESS,
						   address);
	if (result != 0)
		return EIO;
	kern_io_barrier();

	/* Reads the words in turn. */
	for (index = 0U; index < count; index++) {
		/* A failed read clears what was read. */
		result = controller->mmio.ops->csr_read32(
			controller->mmio.argument, AX211_CSR_SRAM_READ_DATA,
			&words[index]);
		if (result != 0) {
			kern_memset(words, 0, count * sizeof(*words));

			/* Failed. */
			return EIO;
		}
	}

	/* Succeeded. */
	return 0;
}

/*
 * Captures only the stable LMAC/UMAC error-table fields needed to identify a
 * firmware assertion.  The ALIVE pointers are firmware-owned SRAM addresses;
 * cache-control tag bits are not part of the address.
 */
static void
ax211_pci_firmware_error_dump(
	struct ax211_pci_controller *controller)
{
	uint32_t lmac[AX211_LMAC_ERROR_WORD_COUNT];
	uint32_t umac[AX211_UMAC_ERROR_WORD_COUNT];
	uint32_t lmac_address;
	uint32_t umac_address;
	int lmac_result;
	int umac_result;

	/* Nothing to read without a controller. */
	if (controller == NULL)
		return;

	/* The tables are known only from a healthy ALIVE. */
	if (!controller->runtime_start.alive_accepted ||
	    controller->runtime_start.alive.status !=
		    INTEL_AX211_PROTOCOL_ALIVE_HEALTHY) {
		kern_logf("intel-ax211: firmware error table unavailable "
			   "alive=%u status=%04x\n",
			   (unsigned)controller->runtime_start.alive_accepted,
			   controller->runtime_start.alive.status);

		/* Nothing can be read. */
		return;
	}

	/* Reads both tables. */
	kern_memset(lmac, 0, sizeof(lmac));
	kern_memset(umac, 0, sizeof(umac));
	lmac_address =
		controller->runtime_start.alive.lmac[0].error_log_address;
	umac_address = controller->runtime_start.alive.umac.upper_error_log_address &
		       ~AX211_SRAM_POINTER_TAG_BITS;
	lmac_result = ax211_pci_sram_read_locked(controller, lmac_address, lmac,
						 AX211_LMAC_ERROR_WORD_COUNT);
	umac_result = ax211_pci_sram_read_locked(controller, umac_address, umac,
						 AX211_UMAC_ERROR_WORD_COUNT);

	/* Logs the LMAC table, or why it could not be read. */
	if (lmac_result == 0) {
		kern_logf(
			"intel-ax211: LMAC firmware error ptr=%08x valid=%08x "
			"id=%08x data=%08x/%08x/%08x hcmd=%08x last=%08x "
			"isr=%08x/%08x/%08x/%08x/%08x\n",
			lmac_address, lmac[0U], lmac[1U], lmac[7U], lmac[8U],
			lmac[9U], lmac[23U], lmac[29U], lmac[24U], lmac[25U],
			lmac[26U], lmac[27U], lmac[28U]);
	} else {
		kern_logf("intel-ax211: LMAC firmware error unavailable "
			   "ptr=%08x result=%d\n",
			   lmac_address, lmac_result);
	}

	/* Logs the UMAC table, or why it could not be read. */
	if (umac_result == 0) {
		kern_logf(
			"intel-ax211: UMAC firmware error ptr=%08x valid=%08x "
			"id=%08x data=%08x/%08x/%08x hcmd=%08x isr=%08x\n",
			umac_address, umac[0U], umac[1U], umac[6U], umac[7U],
			umac[8U], umac[13U], umac[14U]);
	} else {
		kern_logf("intel-ax211: UMAC firmware error unavailable "
			   "ptr=%08x result=%d\n",
			   umac_address, umac_result);
	}
}

/*
 * Performs one exact synchronous command while lifecycle_lock owns the
 * transport.  Asynchronous notifications are copied to a bounded queue and
 * dispatched only after the outer common callback has unwound; only the
 * matching oldest command response retires the slot.
 */
static int
ax211_pci_direct_command(
	struct ax211_pci_controller *controller,
	const struct intel_ax211_command_request *request,
	uint64_t deadline_ticks,
	uint8_t *response,
	size_t response_capacity,
	size_t *response_length,
	struct intel_ax211_protocol_message *completed_message,
	struct intel_ax211_protocol_pending_command *completed_pending)
{
	const uint8_t *error_payload;
	const char *reason;
	uint16_t bad_sequence;
	uint32_t error_type;
	uint32_t error_service;
	uint8_t event_group;
	uint64_t slot_address;
	struct intel_ax211_boot_received_event received;
	struct intel_ax211_command_handle handle;
	struct intel_ax211_command_entry *entry;
	struct intel_ax211_event decoded;
	struct intel_ax211_protocol_pending_command pending;
	uint64_t deadline_us;
	uint64_t now_us;
	uint64_t timeout_us;
	size_t pending_count;
	size_t length;
	unsigned index;
	int command_submitted;
	int decode_result;
	int tx_event;
	int lock_result;
	int result;
	int unlock_result;

	/*
	 * Refuses missing arguments and a controller whose operational
	 * firmware is not running.
	 */
	if (controller == NULL ||
	    request == NULL ||
	    response_length == NULL ||
	    (response_capacity != 0U && response == NULL) ||
	    !controller->runtime_initialized ||
	    !controller->runtime_active ||
	    controller->runtime_start.state !=
		    INTEL_AX211_RUNTIME_START_STATE_RUNNING ||
	    !controller->runtime_start.commands_initialized) {
		return EBUSY;
	}

	/* Refuses a second command while one is outstanding. */
	pending_count = drv_intel_ax211_command_pending_count(
		&controller->runtime_start.commands);
	if (pending_count != 0U)
		return EBUSY;
	*response_length = 0U;

	/* Clears the optional message output. */
	if (completed_message != NULL)
		kern_memset(completed_message, 0, sizeof(*completed_message));

	/* Clears the optional pending-command output and the cause records. */
	if (completed_pending != NULL)
		kern_memset(completed_pending, 0, sizeof(*completed_pending));
	kern_memset(&handle, 0, sizeof(handle));
	kern_memset(&pending, 0, sizeof(pending));
	controller->command_fh_causes = 0U;
	controller->command_hw_causes = 0U;
	controller->command_raw_fh_causes = 0U;
	controller->command_raw_hw_causes = 0U;

	/* Converts the deadline into a timeout. */
	result = ax211_pci_clock_us(controller, &now_us);
	if (result == 0) {
		result = ax211_pci_command_timeout(controller, deadline_ticks,
						   now_us, &timeout_us,
						   &deadline_us);
	}

	/* Reports a failed conversion. */
	if (result != 0)
		return result;

	/* Holds the NIC awake. */
	lock_result = ax211_pci_wake_hold(controller);
	if (lock_result != 0)
		return EIO;
	command_submitted = 0;

	/* Submits the command and keeps its pending record. */
	result = drv_intel_ax211_command_submit(
		&controller->runtime_start.commands, request, now_us,
		timeout_us, &handle);
	if (result == INTEL_AX211_COMMAND_OK) {
		command_submitted = 1;
		entry = &controller->runtime_start.commands
				 .entry[handle.token.index];
		pending = entry->pending;
		result = 0;
	} else {
		result = EIO;
	}

	index = 0U;

	/* Receives events until the answer arrives or the bounds run out. */
	while (result == 0 && index < AX211_DIRECT_EVENT_LIMIT) {
		kern_memset(&received, 0, sizeof(received));

		/* A timeout or a receive failure ends the wait. */
		result = ax211_pci_receive_event(
			controller, deadline_us, controller->runtime_event,
			sizeof(controller->runtime_event), &received);
		if (result == INTEL_AX211_BOOT_RECEIVE_TIMEOUT) {
			result = ETIMEDOUT;
			break;
		}
		if (result != INTEL_AX211_BOOT_RECEIVE_OK) {
			result = EIO;
			break;
		}

		/* Refuses an event of another epoch or of an invalid size. */
		if (received.generation != controller->hardware_epoch ||
		    received.length == 0U ||
		    received.length > sizeof(controller->runtime_event)) {
			result = EIO;
			break;
		}

		/* Refuses an event that does not decode. */
		decode_result = drv_intel_ax211_event_decode(
			controller->runtime_event, received.length, &decoded);
		if (decode_result != INTEL_AX211_SUCCESS) {
			result = EIO;
			break;
		}

		/* Defers a transmit event. */
		tx_event = ax211_pci_tx_event(&decoded);
		if (tx_event) {
			result = ax211_pci_deferred_event_enqueue(
				controller, controller->runtime_event,
				&received);
			ax211_pci_scrub(controller->runtime_event,
					received.length);
			index++;
			continue;
		}

		/* Defers a notification, logging a command rejection. */
		if ((decoded.queue & 0x80U) != 0U) {
			event_group =
				decoded.flags &
				(uint8_t)~INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK;

			/* Logs the firmware's rejection of a command. */
			if (event_group == INTEL_AX211_PROTOCOL_FAMILY_CLASSIC &&
			    decoded.command.opcode ==
				    AX211_COMMAND_REJECTION_OPCODE) {
				/* Logs a well-formed rejection's fields. */
				error_payload = controller->runtime_event +
						decoded.payload_offset;
				if (decoded.payload_length ==
				    AX211_COMMAND_REJECTION_SIZE) {
					bad_sequence =
						(uint16_t)error_payload[6U] |
						((uint16_t)error_payload[7U]
						 << 8);
					error_type =
						ax211_pci_get_le32(error_payload);
					error_service = ax211_pci_get_le32(
						error_payload + 8U);
					kern_logf("intel-ax211: firmware "
						   "command error "
						   "type=%08x command=%02x "
						   "sequence=%04x "
						   "service=%08x\n",
						   error_type,
						   error_payload[4U],
						   bad_sequence,
						   error_service);
				} else {
					kern_logf("intel-ax211: malformed "
						   "firmware "
						   "command error length=%u\n",
						   (unsigned)decoded
							   .payload_length);
				}
			}

			/* Defers the notification. */
			result = ax211_pci_deferred_event_enqueue(
				controller, controller->runtime_event,
				&received);
			ax211_pci_scrub(controller->runtime_event,
					received.length);
			index++;
			continue;
		}

		/* Completes the command with its answer. */
		length = 0U;
		result = drv_intel_ax211_command_complete(
			&controller->runtime_start.commands,
			controller->runtime_event, received.length,
			controller->hardware_epoch, response,
			response_capacity, &length);
		if (result == INTEL_AX211_COMMAND_OK) {
			*response_length = length;

			/* Reports the pending record when it is asked for. */
			if (completed_pending != NULL)
				*completed_pending = pending;

			/*
			 * Reports the answer as a message when it is asked for.
			 */
			if (completed_message != NULL) {
				completed_message->opcode =
					decoded.command.opcode;
				completed_message->group =
					decoded.flags &
					(uint8_t)~INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK;
				completed_message->version =
					pending.response_version;
				completed_message->flags =
					decoded.flags;
				completed_message->queue =
					decoded.queue;
				completed_message->index =
					decoded.index;
				completed_message->generation =
					controller->hardware_epoch;
				completed_message->payload = response;
				completed_message->payload_length =
					length;
			}

			result = 0;
			command_submitted = 0;
		} else {
			kern_logf(
				"intel-ax211: command completion "
				"rejected "
				"request=%02x/%02x response=%02x/%02x "
				"flags=%02x queue=%02x index=%02x "
				"payload=%u "
				"expected=%u..%u result=%d\n",
				(unsigned)request->command.group,
				(unsigned)request->command.opcode,
				(unsigned)(decoded.flags &
					   (uint8_t)~INTEL_AX211_PROTOCOL_COMMAND_FAILED_MASK),
				(unsigned)decoded.command.opcode,
				(unsigned)decoded.flags,
				(unsigned)decoded.queue,
				(unsigned)decoded.index,
				(unsigned)decoded.payload_length,
				(unsigned)request
					->minimum_response_length,
				(unsigned)request
					->maximum_response_length,
				result);
			result = EIO;
		}

		/* Clears the event and ends the wait. */
		ax211_pci_scrub(controller->runtime_event,
				received.length);
		break;
	}

	/* An exhausted event budget is a timeout. */
	if (result == 0 && index == AX211_DIRECT_EVENT_LIMIT)
		result = ETIMEDOUT;

	/* Dumps the firmware's error tables after a fatal cause. */
	if (result != 0 &&
	    command_submitted &&
	    ((controller->command_fh_causes &
	      INTEL_AX211_TRANSPORT_QUEUE_CAUSE_FAULT) != 0U ||
	     (controller->command_hw_causes &
	      INTEL_AX211_TRANSPORT_EVENT_FATAL_CAUSES) != 0U)) {
		ax211_pci_firmware_error_dump(controller);
	}

	/* Logs a timed-out command with the transport's state. */
	if (result == ETIMEDOUT && command_submitted) {
		/* Names the bound that ran out. */
		slot_address = controller->transport.memory
				       .command_slots_device_address +
			       (uint64_t)handle.token.index *
				       INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE;
		if (index == AX211_DIRECT_EVENT_LIMIT)
			reason = "event-limit";
		else
			reason = "deadline";
		kern_logf(
			"intel-ax211: command timeout opcode=%02x group=%02x "
			"token=%u events=%u reason=%s slot-page=%03x "
			"fh=%08x hw=%08x raw-fh=%08x raw-hw=%08x "
			"rx-head=%u rx-tail=%u ring-head=%u ring-tail=%u "
			"used=%u\n",
			(unsigned)request->command.opcode,
			(unsigned)request->command.group,
			(unsigned)handle.token.index, index,
			reason,
			(unsigned)(slot_address & UINT64_C(0xfff)),
			controller->command_fh_causes,
			controller->command_hw_causes,
			controller->command_raw_fh_causes,
			controller->command_raw_hw_causes,
			(unsigned)controller->transport.rx_head,
			(unsigned)controller->transport.rx_tail,
			(unsigned)controller->transport.command_ring.head,
			(unsigned)controller->transport.command_ring.tail,
			(unsigned)controller->transport.command_ring.used);
	}

	/* Cancels a command that did not complete. */
	if (result != 0 && command_submitted) {
		(void)drv_intel_ax211_command_cancel(
			&controller->runtime_start.commands, &handle);
	}

	/* Releases the NIC; a failed release fails a successful command. */
	unlock_result = ax211_pci_wake_release(controller);
	if (result == 0 && unlock_result != 0)
		result = EIO;

	/* Failed. */
	if (result != 0)
		return result;

	/* Succeeded. */
	return 0;
}

/*
 * Sends one association command for the association session and normalizes
 * its answer; the queue commands also set up or reset the transmit ring.
 */
static int
ax211_pci_assoc_exchange(
	void *argument,
	const struct intel_ax211_assoc_command *command,
	struct intel_ax211_assoc_reply *reply)
{
	uint16_t flags;
	uint16_t queue;
	uint16_t reserved;
	uint16_t write_pointer;
	struct ax211_pci_controller *controller;
	struct intel_ax211_command_request request;
	struct intel_ax211_protocol_message message;
	struct intel_ax211_protocol_pending_command pending;
	size_t minimum;
	size_t maximum;
	size_t response_length;
	int same;
	int result;

	/*
	 * Refuses missing arguments and a command of another connection or
	 * firmware boot.
	 */
	controller = argument;
	if (controller == NULL ||
	    command == NULL ||
	    reply == NULL ||
	    command->common_generation != controller->connection_generation ||
	    command->hardware_epoch != controller->hardware_epoch) {
		return INTEL_AX211_ASSOC_INVALID;
	}

	/* Bounds the answer by the kind the command expects. */
	minimum = 0U;
	maximum = 0U;
	if (command->response_kind == INTEL_AX211_ASSOC_RESPONSE_STATUS_ZERO) {
		minimum = 4U;
		maximum = 4U;
	} else if (command->response_kind == INTEL_AX211_ASSOC_RESPONSE_QUEUE) {
		minimum = INTEL_AX211_ASSOC_QUEUE_RESPONSE_SIZE;
		maximum = INTEL_AX211_ASSOC_QUEUE_RESPONSE_SIZE;
	} else if (command->response_kind ==
		   INTEL_AX211_ASSOC_RESPONSE_IGNORED) {
		maximum = INTEL_AX211_ASSOC_RESPONSE_MAX;
	}

	/* A queue addition must be the one the transmit ring builds. */
	if (command->step == INTEL_AX211_ASSOC_STEP_QUEUE_ENABLE) {
		/* Builds the ring's queue command. */
		result = drv_intel_ax211_tx_ring_queue_add_build(
			&controller->frame_ring, AX211_PEER_SLOT,
			INTEL_AX211_TX_RING_MANAGEMENT_TID,
			&controller->tx_queue_config);
		if (result != INTEL_AX211_TX_RING_OK)
			return INTEL_AX211_ASSOC_INVALID;

		/* Refuses a command of another length. */
		if (command->payload_length !=
		    sizeof(controller->tx_queue_config.command)) {
			return INTEL_AX211_ASSOC_INVALID;
		}

		/* Refuses other command bytes. */
		same = kern_memcmp(command->payload,
				   controller->tx_queue_config.command,
				   sizeof(controller->tx_queue_config.command));
		if (same != 0)
			return INTEL_AX211_ASSOC_INVALID;
	}

	/*
	 * Describes the command; a wide header carries the wire version, a
	 * narrow one the layout version.
	 */
	kern_memset(&request, 0, sizeof(request));
	request.command.group = command->group;
	request.command.opcode = command->opcode;
	if (command->header == INTEL_AX211_ASSOC_HEADER_WIDE)
		request.command.version = command->wire_version;
	else
		request.command.version = command->layout_version;
	request.payload = command->payload;
	request.payload_length = command->payload_length;
	request.response_version = command->response_version;
	request.minimum_response_length = minimum;
	request.maximum_response_length = maximum;
	kern_memset(&message, 0, sizeof(message));
	kern_memset(&pending, 0, sizeof(pending));
	kern_memset(reply, 0, sizeof(*reply));
	response_length = 0U;

	/* Sends the command; a failure is a timeout or an I/O failure. */
	result = ax211_pci_direct_command(
		controller, &request, controller->control_deadline_ticks,
		reply->payload, sizeof(reply->payload), &response_length,
		&message, &pending);
	if (result != 0) {
		kern_logf("intel-ax211: association exchange failed step=%u "
			   "opcode=%02x group=%02x result=%d\n",
			   command->step, command->opcode, command->group,
			   result);

		/* Reports a timeout as a timeout. */
		if (result == ETIMEDOUT)
			return INTEL_AX211_ASSOC_TIMEOUT;

		/* Reports any other failure as an I/O failure. */
		return INTEL_AX211_ASSOC_IO;
	}

	/* Describes the answer. */
	reply->step = command->step;
	reply->response_version = command->response_version;
	reply->sequence = command->sequence;
	reply->common_generation = command->common_generation;
	reply->hardware_epoch = command->hardware_epoch;
	reply->payload_length = response_length;

	/*
	 * Completes the ring's queue for an addition, resets it for a removal.
	 */
	if (command->step == INTEL_AX211_ASSOC_STEP_QUEUE_ENABLE) {
		/* Logs and refuses an answer the ring does not accept. */
		result = drv_intel_ax211_tx_ring_queue_add_complete(
			&controller->frame_ring, &controller->tx_queue_config,
			controller->hardware_epoch,
			controller->connection_generation, &message, &pending);
		if (result != INTEL_AX211_TX_RING_OK) {
			/* Decodes the queue word, when it arrived. */
			queue = 0;
			flags = 0;
			write_pointer = 0;
			reserved = 0;
			if (response_length >= 2U) {
				queue = (uint16_t)reply->payload[0U] |
					((uint16_t)reply->payload[1U] << 8);
			}

			/* Decodes the flags word, when it arrived. */
			if (response_length >= 4U) {
				flags = (uint16_t)reply->payload[2U] |
					((uint16_t)reply->payload[3U] << 8);
			}

			/* Decodes the write pointer, when it arrived. */
			if (response_length >= 6U) {
				write_pointer =
					(uint16_t)reply->payload[4U] |
					((uint16_t)reply->payload[5U] << 8);
			}

			/* Decodes the reserved word, when it arrived. */
			if (response_length >= 8U) {
				reserved = (uint16_t)reply->payload[6U] |
					   ((uint16_t)reply->payload[7U] << 8);
			}

			/* Logs the answer. */
			kern_logf("intel-ax211: queue add response rejected "
				   "result=%d "
				   "length=%u qid=%u flags=%04x wp=%u "
				   "reserved=%04x\n",
				   result, (unsigned)response_length,
				   (unsigned)queue, (unsigned)flags,
				   (unsigned)write_pointer, (unsigned)reserved);

			/* Reports the refused answer. */
			return INTEL_AX211_ASSOC_FIRMWARE;
		}
	} else if (command->step == INTEL_AX211_ASSOC_STEP_QUEUE_REMOVE) {
		/* A failed reset is an I/O failure. */
		result = drv_intel_ax211_tx_ring_reset(&controller->frame_ring, 1);
		if (result != INTEL_AX211_TX_RING_OK)
			return INTEL_AX211_ASSOC_IO;
	}

	/* Succeeded: *reply holds the answer. */
	return INTEL_AX211_ASSOC_OK;
}

/* Maps the association session's results to errno values. */
static int
ax211_pci_assoc_result_errno(
	int result)
{
	/* Maps each result. */
	switch (result) {
	case INTEL_AX211_ASSOC_OK:
	case INTEL_AX211_ASSOC_AUTH_READY:
	case INTEL_AX211_ASSOC_COMPLETE:
	case INTEL_AX211_ASSOC_ROLLED_BACK:
		/* A success, the end of a stage, or a finished rollback. */
		return 0;
	case INTEL_AX211_ASSOC_INVALID:
		/* An invalid argument. */
		return EINVAL;
	case INTEL_AX211_ASSOC_UNSUPPORTED:
		/* An unsupported firmware. */
		return ENOTSUP;
	case INTEL_AX211_ASSOC_PENDING:
		/* A session still running. */
		return EBUSY;
	case INTEL_AX211_ASSOC_STALE:
	case INTEL_AX211_ASSOC_DUPLICATE:
	case INTEL_AX211_ASSOC_OUT_OF_ORDER:
		/* An answer that does not belong to the current command. */
		return ESTALE;
	case INTEL_AX211_ASSOC_TIMEOUT:
		/* A timeout. */
		return ETIMEDOUT;
	case INTEL_AX211_ASSOC_FIRMWARE:
	case INTEL_AX211_ASSOC_IO:
	case INTEL_AX211_ASSOC_ROLLBACK_FAILED:
	default:
		/* Any other failure is an I/O failure. */
		return EIO;
	}
}

/* Maps the transmit ring's results to errno values. */
static int
ax211_pci_tx_ring_result_errno(
	int result)
{
	/* Maps each result. */
	switch (result) {
	case INTEL_AX211_TX_RING_OK:
		/* A success. */
		return 0;
	case INTEL_AX211_TX_RING_INVALID:
		/* An invalid argument. */
		return EINVAL;
	case INTEL_AX211_TX_RING_UNSUPPORTED:
		/* An unsupported firmware. */
		return ENOTSUP;
	case INTEL_AX211_TX_RING_NOT_READY:
		/* A ring that is not running. */
		return ENETDOWN;
	case INTEL_AX211_TX_RING_FULL:
	case INTEL_AX211_TX_RING_PENDING:
		/* A ring with no room or a pending operation. */
		return EBUSY;
	case INTEL_AX211_TX_RING_STALE:
	case INTEL_AX211_TX_RING_OUT_OF_ORDER:
		/* An answer that does not belong to the ring. */
		return ESTALE;
	case INTEL_AX211_TX_RING_DUPLICATE:
		/* A cookie already in use. */
		return EEXIST;
	case INTEL_AX211_TX_RING_TIMEOUT:
		/* A timeout. */
		return ETIMEDOUT;
	case INTEL_AX211_TX_RING_NO_MEMORY:
		/* No memory. */
		return ENOMEM;
	case INTEL_AX211_TX_RING_TX_FAILED:
	case INTEL_AX211_TX_RING_IO_ERROR:
	case INTEL_AX211_TX_RING_POISONED:
	case INTEL_AX211_TX_RING_BARRIER_REQUIRED:
	case INTEL_AX211_TX_RING_KICK_FAILED:
	case INTEL_AX211_TX_RING_MALFORMED:
	default:
		/* Any other failure is an I/O failure. */
		return EIO;
	}
}

/* Maps the key state's results to errno values. */
static int
ax211_pci_key_result_errno(
	int result)
{
	/* Maps each result. */
	switch (result) {
	case INTEL_AX211_KEY_OK:
	case INTEL_AX211_KEY_DUPLICATE:
		/* A success or a repeat of one. */
		return 0;
	case INTEL_AX211_KEY_INVALID:
		/* An invalid argument. */
		return EINVAL;
	case INTEL_AX211_KEY_UNSUPPORTED:
		/* An unsupported firmware. */
		return ENOTSUP;
	case INTEL_AX211_KEY_STALE:
		/* A key of another connection or generation. */
		return ESTALE;
	case INTEL_AX211_KEY_MISSING:
	default:
		/* A missing key, and any other failure. */
		return ENOENT;
	}
}

/* Copies only scan-derived metadata which belongs to this firmware epoch. */
static int
ax211_pci_assoc_profile(
	struct ax211_pci_controller *controller,
	const struct wlan_bss_record *bss,
	uint64_t connection_generation,
	struct intel_ax211_assoc_profile *profile)
{
	struct drv_intel_ax211_bss_assoc_metadata metadata;
	struct intel_ax211_bss_entry entry;
	struct intel_ax211_tx_queue_config queue;
	int present;
	int result;

	/*
	 * Refuses missing arguments, an unpublished scan, a missing ring and an
	 * unset generation.
	 */
	if (controller == NULL ||
	    bss == NULL ||
	    profile == NULL ||
	    !controller->bss_published_initialized ||
	    !controller->tx_ring_allocated ||
	    connection_generation == 0U) {
		return EINVAL;
	}

	/* Refuses a channel the scan does not cover. */
	present = ax211_pci_scan_channel_present(controller, bss->channel);
	if (!present)
		return EINVAL;
	kern_memset(&entry, 0, sizeof(entry));

	/* Finds the BSS in the published scan. */
	result = drv_intel_ax211_bss_cache_lookup(
		&controller->bss_published_cache, bss->bssid, bss->channel,
		controller->hardware_epoch, &entry);
	if (result != INTEL_AX211_BSS_OK) {
		/* A BSS the scan did not see is missing. */
		if (result == INTEL_AX211_BSS_NOT_FOUND)
			return ENOENT;

		/* Any other failure is a stale scan. */
		return ESTALE;
	}
	kern_memset(&metadata, 0, sizeof(metadata));

	/* Takes the BSS's association metadata. */
	result = drv_intel_ax211_bss_assoc_metadata(
		&entry, connection_generation, controller->hardware_epoch,
		&metadata);
	if (result != INTEL_AX211_BSS_OK)
		return EIO;
	kern_memset(&queue, 0, sizeof(queue));

	/* Builds the management queue's addresses. */
	result = drv_intel_ax211_tx_ring_queue_add_build(
		&controller->frame_ring, AX211_PEER_SLOT,
		INTEL_AX211_TX_RING_MANAGEMENT_TID, &queue);
	if (result != INTEL_AX211_TX_RING_OK) {
		result = ax211_pci_tx_ring_result_errno(result);
		return result;
	}

	/* Describes the addresses, the channel and the receive chains. */
	kern_memset(profile, 0, sizeof(*profile));
	kern_memcpy(profile->station_address, controller->net_device->hwaddr, 6U);
	kern_memcpy(profile->bssid, metadata.bssid, 6U);
	profile->channel = metadata.channel;
	profile->channel_width_mhz = INTEL_AX211_ASSOC_CHANNEL_WIDTH_MHZ;
	profile->rx_chain_mask = controller->runtime_start.nvm.rx_chain_mask;

	/*
	 * Preserve the mandatory 1 Mbps fallback bitmap used by the reference
	 * command ABI even on 5 GHz, where the firmware does not use CCK.
	 */
	if (profile->channel <= 14U)
		profile->cck_ack_rates = AX211_ASSOC_CCK_ACK_RATES;
	else
		profile->cck_ack_rates = 1U;

	/*
	 * Mandatory 5 GHz basic OFDM rates: 6, 12, and 24 Mbit/s.  Do not
	 * advertise every optional OFDM rate as an ACK/basic rate.
	 */
	profile->ofdm_ack_rates = AX211_ASSOC_OFDM_ACK_RATES;

	/* A short preamble is used on 2.4 GHz when the BSS allows it. */
	profile->short_preamble = 0U;
	if (profile->channel <= 14U &&
	    (metadata.capability & AX211_CAPABILITY_SHORT_PREAMBLE) != 0U) {
		profile->short_preamble = 1U;
	}

	/*
	 * Short-slot timing is mandatory in the 5 GHz OFDM-only band even when
	 * the AP omits the 2.4-GHz capability bit from its beacon.
	 */
	profile->short_slot = 0U;
	if (profile->channel > 14U ||
	    (metadata.capability & AX211_CAPABILITY_SHORT_SLOT) != 0U) {
		profile->short_slot = 1U;
	}

	/*
	 * The first p038 common association profile does not negotiate WMM.
	 * Keep data frames non-QoS even when the scanned BSS advertises WMM;
	 * enabling QoS here would make the common 802.11 layer reject its own
	 * data frames before a matching WMM association contract exists.
	 */
	profile->qos = 0U;
	profile->beacon_interval_tu = metadata.beacon_interval_tu;
	if (metadata.tim_valid != 0U)
		profile->dtim_period = metadata.dtim_period;
	else
		profile->dtim_period = 0U;
	profile->queue_byte_count_address = queue.byte_count_address;
	profile->queue_descriptor_address = queue.tfd_address;

	/* Safe legacy EDCA defaults: BE, BK, VI, VO. */
	profile->edca[0U].ecw_min = 4U;
	profile->edca[0U].ecw_max = 10U;
	profile->edca[0U].aifsn = 3U;
	profile->edca[1U].ecw_min = 4U;
	profile->edca[1U].ecw_max = 10U;
	profile->edca[1U].aifsn = 7U;
	profile->edca[2U].ecw_min = 3U;
	profile->edca[2U].ecw_max = 4U;
	profile->edca[2U].aifsn = 2U;
	profile->edca[2U].txop_32us = 94U;
	profile->edca[3U].ecw_min = 2U;
	profile->edca[3U].ecw_max = 3U;
	profile->edca[3U].aifsn = 2U;
	profile->edca[3U].txop_32us = 47U;

	/* Succeeded: selects the BSS and clears the copies. */
	controller->selected_bss = entry;
	controller->selected_metadata = metadata;
	controller->selected_bss_valid = 1U;
	ax211_pci_scrub(&queue, sizeof(queue));
	ax211_pci_scrub(&metadata, sizeof(metadata));
	ax211_pci_scrub(&entry, sizeof(entry));
	return 0;
}

/* Forgets the connection: its BSS, session, keys and transmit state. */
static void
ax211_pci_connection_clear(
	struct ax211_pci_controller *controller)
{
	uint8_t key_index;

	/* Nothing to clear without a controller. */
	if (controller == NULL)
		return;

	/* Clears the connection's state, its session and its keys. */
	controller->connection_generation = 0U;
	controller->control_deadline_ticks = 0U;
	controller->selected_bss_valid = 0U;
	controller->association_initialized = 0U;
	controller->keys_initialized = 0U;
	ax211_pci_scrub(&controller->selected_bss,
			sizeof(controller->selected_bss));
	ax211_pci_scrub(&controller->selected_metadata,
			sizeof(controller->selected_metadata));
	ax211_pci_scrub(&controller->association,
			sizeof(controller->association));
	ax211_pci_scrub(&controller->keys, sizeof(controller->keys));
	ax211_pci_staged_key_clear(&controller->staged_pairwise_key);

	/* Clears each staged group key. */
	for (key_index = 0U; key_index < INTEL_AX211_KEY_INDEX_LIMIT;
	     key_index++) {
		ax211_pci_staged_key_clear(
			&controller->staged_group_key[key_index]);
	}

	/* Clears the retired keys and the transmit state. */
	controller->retired_pairwise_key_generation = 0U;
	ax211_pci_scrub(controller->retired_group_key_generation,
			sizeof(controller->retired_group_key_generation));
	ax211_pci_scrub(controller->retired_group_key_remove,
			sizeof(controller->retired_group_key_remove));
	ax211_pci_scrub(&controller->tx_queue_config,
			sizeof(controller->tx_queue_config));
	ax211_pci_scrub(controller->tx_report_completion,
			sizeof(controller->tx_report_completion));
}

/* Performs the association state machine's finite reverse-order teardown. */
static int
ax211_pci_assoc_rollback(
	struct ax211_pci_controller *controller,
	uint64_t generation)
{
	uint64_t now;
	int result;

	/* Refuses a missing controller. */
	if (controller == NULL)
		return EINVAL;

	/* A connection without a session is only forgotten. */
	if (!controller->association_initialized) {
		/* Refuses another connection's generation. */
		if (controller->connection_generation != 0U &&
		    generation != 0U &&
		    controller->connection_generation != generation) {
			return ESTALE;
		}
		ax211_pci_connection_clear(controller);

		/* Succeeded. */
		return 0;
	}

	/* Refuses another connection's generation. */
	if (generation != 0U && generation != controller->connection_generation)
		return ESTALE;

	/* Removes every key the firmware may hold. */
	if (controller->keys_initialized) {
		/* Reports a failed removal. */
		result = ax211_pci_keys_remove_all(
			controller, controller->control_deadline_ticks);
		if (result != 0)
			return result;
	}

	/* Reads the clock. */
	result = ax211_pci_clock_us(controller, &now);
	if (result != 0)
		return EIO;

	/* Cancels the session and drives its rollback. */
	result = drv_intel_ax211_assoc_cancel(&controller->association,
					      controller->connection_generation,
					      controller->hardware_epoch, now);
	if (result == INTEL_AX211_ASSOC_PENDING ||
	    result == INTEL_AX211_ASSOC_ROLLED_BACK) {
		result = drv_intel_ax211_assoc_drive(
			&controller->association, &ax211_assoc_ops, controller);
	}

	/* Reports a rollback that did not finish. */
	if (result != INTEL_AX211_ASSOC_ROLLED_BACK &&
	    result != INTEL_AX211_ASSOC_OK) {
		result = ax211_pci_assoc_result_errno(result);
		return result;
	}

	/* Refuses a transmit ring that still runs or holds frames. */
	if (controller->frame_ring.enabled ||
	    controller->frame_ring.pending_count != 0U) {
		return EIO;
	}
	ax211_pci_connection_clear(controller);

	/* Succeeded. */
	return 0;
}

/* Enables own and broadcast/multicast reception after MAC association. */
static int
ax211_pci_mcast_filter_configure(
	struct ax211_pci_controller *controller,
	uint64_t deadline_ticks)
{
	struct intel_ax211_command_request request;
	uint8_t payload[INTEL_AX211_ASSOC_MULTICAST_LIST_SIZE];
	size_t response_length;
	int result;

	/* Refuses a missing controller and a connection without a BSS. */
	if (controller == NULL || !controller->selected_bss_valid)
		return EINVAL;
	kern_memset(payload, 0, sizeof(payload));

	/* Encodes the filter for the BSSID. */
	result = drv_intel_ax211_assoc_mcast_filter_encode(
		controller->selected_metadata.bssid, payload, sizeof(payload));
	if (result != INTEL_AX211_ASSOC_OK) {
		ax211_pci_scrub(payload, sizeof(payload));
		result = ax211_pci_assoc_result_errno(result);
		return result;
	}

	/* Describes the command, which has an empty answer. */
	kern_memset(&request, 0, sizeof(request));
	request.command.group = INTEL_AX211_ASSOC_FAMILY_CLASSIC;
	request.command.opcode = INTEL_AX211_ASSOC_MULTICAST_LIST_OPCODE;
	request.command.version = INTEL_AX211_ASSOC_MULTICAST_LIST_VERSION;
	request.payload = payload;
	request.payload_length = sizeof(payload);
	request.response_version = 0U;
	request.minimum_response_length = 0U;
	request.maximum_response_length = 0U;
	response_length = 0U;

	/* Sends the command; an answer with a payload is an I/O failure. */
	result = ax211_pci_direct_command(controller, &request, deadline_ticks,
					  NULL, 0U, &response_length, NULL,
					  NULL);
	if (result == 0 && response_length != 0U)
		result = EIO;
	ax211_pci_scrub(payload, sizeof(payload));
	if (result != 0)
		return result;

	/* Succeeded: the filter is set. */
	return 0;
}

/* Programs the frozen API89 PM-off client table after multicast admission. */
static int
ax211_pci_mac_power_configure(
	struct ax211_pci_controller *controller,
	uint64_t deadline_ticks)
{
	struct intel_ax211_command_request request;
	uint8_t payload[INTEL_AX211_ASSOC_POWER_SAVE_SIZE];
	uint8_t response[INTEL_AX211_ASSOC_POWER_SAVE_RESPONSE_SIZE];
	uint32_t beacon_interval_ms;
	size_t response_length;
	int validate_result;
	int result;

	/* Refuses a connection without a BSS or a beacon interval. */
	if (controller == NULL ||
	    !controller->selected_bss_valid ||
	    controller->selected_metadata.beacon_interval_tu == 0U) {
		return EINVAL;
	}

	/*
	 * One TU is 1.024 ms; round up so keep-alive never undershoots the
	 * advertised beacon interval when converting to the codec's ms input.
	 */
	beacon_interval_ms =
		((uint32_t)controller->selected_metadata.beacon_interval_tu *
			 1024U +
		 999U) /
		1000U;
	kern_memset(payload, 0, sizeof(payload));
	kern_memset(response, 0, sizeof(response));

	/* Encodes the table. */
	result = drv_intel_ax211_assoc_mac_power_encode(
		controller->selected_metadata.dtim_period, beacon_interval_ms,
		payload, sizeof(payload));
	if (result != INTEL_AX211_ASSOC_OK) {
		ax211_pci_scrub(payload, sizeof(payload));
		result = ax211_pci_assoc_result_errno(result);
		return result;
	}

	/* Describes the command and its optional status answer. */
	kern_memset(&request, 0, sizeof(request));
	request.command.group = INTEL_AX211_ASSOC_FAMILY_CLASSIC;
	request.command.opcode = INTEL_AX211_ASSOC_POWER_SAVE_OPCODE;
	request.command.version = INTEL_AX211_ASSOC_POWER_SAVE_VERSION;
	request.payload = payload;
	request.payload_length = sizeof(payload);
	request.response_version = 0U;
	request.minimum_response_length = 0U;
	request.maximum_response_length = sizeof(response);
	response_length = 0U;

	/* Sends the command and checks its answer. */
	result = ax211_pci_direct_command(controller, &request, deadline_ticks,
					  response, sizeof(response),
					  &response_length, NULL, NULL);
	if (result == 0) {
		validate_result =
			drv_intel_ax211_assoc_mac_power_response_validate(
				response, response_length);
		result = ax211_pci_assoc_result_errno(validate_result);
	}

	/* Clears the answer and the table. */
	ax211_pci_scrub(response, sizeof(response));
	ax211_pci_scrub(payload, sizeof(payload));
	if (result != 0)
		return result;

	/* Succeeded: power save is off. */
	return 0;
}

/* Removes every tuple still reachable by firmware before station teardown. */
static int
ax211_pci_keys_remove_all(
	struct ax211_pci_controller *controller,
	uint64_t deadline_ticks)
{
	uint8_t command[INTEL_AX211_KEY_COMMAND_SIZE];
	uint8_t index;
	uint64_t generation;
	int encode_result;
	int result;

	/* Nothing to remove without a key state. */
	if (controller == NULL || !controller->keys_initialized)
		return 0;
	result = 0;

	/* Removes each group key the firmware may hold. */
	for (index = 0U; index < INTEL_AX211_KEY_INDEX_LIMIT && result == 0;
	     index++) {
		/* Takes the active key, else a programmed staged one. */
		generation = controller->keys.active_group[index];
		if (generation == 0U &&
		    controller->staged_group_key[index].valid &&
		    controller->staged_group_key[index].programmed) {
			generation = controller->staged_group_key[index]
					     .request.key_generation;
		}

		/* Else takes a retired key still to be removed. */
		if (generation == 0U &&
		    controller->retired_group_key_remove[index]) {
			generation =
				controller->retired_group_key_generation[index];
		}

		/* Encodes and sends the removal. */
		if (generation != 0U) {
			kern_memset(command, 0, sizeof(command));

			/* Sends a removal that encoded. */
			encode_result = drv_intel_ax211_key_remove_encode(
				controller->connection_generation,
				generation, INTEL_AX211_KEY_GROUP_KEY,
				index, command);
			result = ax211_pci_key_result_errno(encode_result);
			if (result == 0) {
				result = ax211_pci_key_command(
					controller, command, deadline_ticks);
			}

			drv_intel_ax211_key_command_scrub(command);
		}
	}

	/* Removes the pairwise key the firmware may hold. */
	if (result == 0) {
		/* Takes the active key, else a programmed staged one. */
		generation = controller->keys.active_pairwise;
		if (generation == 0U &&
		    controller->staged_pairwise_key.valid &&
		    controller->staged_pairwise_key.programmed) {
			generation = controller->staged_pairwise_key.request
					     .key_generation;
		}

		/* Encodes and sends the removal. */
		if (generation != 0U) {
			kern_memset(command, 0, sizeof(command));

			/* Sends a removal that encoded. */
			encode_result = drv_intel_ax211_key_remove_encode(
				controller->connection_generation,
				generation, INTEL_AX211_KEY_PAIRWISE,
				0U, command);
			result = ax211_pci_key_result_errno(encode_result);
			if (result == 0) {
				result = ax211_pci_key_command(
					controller, command, deadline_ticks);
			}

			drv_intel_ax211_key_command_scrub(command);
		}
	}

	/* Reports the failure. */
	if (result != 0)
		return result;

	/* Succeeded: the firmware holds no key. */
	return 0;
}

/* Issues one exact API89 key command; caller scrubs its payload. */
static int
ax211_pci_key_command(
	struct ax211_pci_controller *controller,
	const uint8_t *payload,
	uint64_t deadline_ticks)
{
	struct intel_ax211_command_request request;
	uint8_t response[4U];
	size_t response_length;
	int result;

	/* Refuses missing arguments. */
	if (controller == NULL || payload == NULL)
		return EINVAL;

	/* Describes the key command. */
	kern_memset(&request, 0, sizeof(request));
	request.command.group = INTEL_AX211_KEY_FAMILY;
	request.command.opcode = INTEL_AX211_KEY_OPCODE;

	/*
	 * SEC_KEY layout is v1 in the command table; API89 wide headers use v0.
	 */
	request.command.version = INTEL_AX211_KEY_WIRE_VERSION;
	request.payload = payload;
	request.payload_length = INTEL_AX211_KEY_COMMAND_SIZE;
	request.response_version = INTEL_AX211_KEY_RESPONSE_VERSION;
	request.minimum_response_length = 0U;
	request.maximum_response_length = sizeof(response);

	/* Sends the command and clears its answer. */
	kern_memset(response, 0, sizeof(response));
	response_length = 0U;
	result = ax211_pci_direct_command(controller, &request, deadline_ticks,
					  response, sizeof(response),
					  &response_length, NULL, NULL);
	ax211_pci_scrub(response, sizeof(response));
	if (result != 0)
		return result;

	/* Succeeded: the firmware accepted the command. */
	return 0;
}

/* Retains one replacement secret only until its checked activation barrier. */
static int
ax211_pci_staged_key_store(
	struct ax211_pci_staged_key *staged,
	const struct intel_ax211_key_request *request)
{
	int same;

	/* Refuses missing arguments. */
	if (staged == NULL || request == NULL)
		return EINVAL;

	/* A staged slot accepts only the same key again. */
	if (staged->valid) {
		/* Refuses another key. */
		if (staged->request.connection_generation !=
			    request->connection_generation ||
		    staged->request.key_generation != request->key_generation ||
		    staged->request.receive_packet_number !=
			    request->receive_packet_number ||
		    staged->request.kind != request->kind ||
		    staged->request.key_index != request->key_index) {
			return EBUSY;
		}

		/* A programmed key's bytes are erased; its fields match. */
		if (staged->programmed)
			return 0;

		/* Refuses other key bytes. */
		same = kern_memcmp(staged->request.key, request->key,
				   INTEL_AX211_KEY_BYTES);
		if (same != 0)
			return EBUSY;

		/* Succeeded: the same key is already staged. */
		return 0;
	}

	/* Succeeded: stages the key. */
	staged->request = *request;
	staged->valid = 1U;
	staged->programmed = 0U;
	return 0;
}

/* Marks the key hardware-owned before crossing the uncertain command edge. */
static int
ax211_pci_staged_key_program(
	struct ax211_pci_controller *controller,
	struct ax211_pci_staged_key *staged,
	uint64_t deadline_ticks)
{
	uint8_t command[INTEL_AX211_KEY_COMMAND_SIZE];
	int encode_result;
	int result;

	/* Refuses missing arguments and an empty slot. */
	if (controller == NULL || staged == NULL || !staged->valid)
		return EINVAL;

	/* A programmed key needs nothing more. */
	if (staged->programmed)
		return 0;
	kern_memset(command, 0, sizeof(command));

	/* Encodes the addition and sends it. */
	encode_result = drv_intel_ax211_key_add_encode(&staged->request,
						       command);
	result = ax211_pci_key_result_errno(encode_result);
	if (result == 0) {
		staged->programmed = 1U;
		result = ax211_pci_key_command(controller, command,
					       deadline_ticks);

		/*
		 * Once ADD crossed the command transport boundary it is not
		 * retryable: a timeout cannot prove whether firmware installed
		 * it. Erase the controller-owned plaintext even if the
		 * subsequent global fail-closed reset also fails and leaves
		 * this object quarantined.
		 */
		ax211_pci_scrub(staged->request.key,
				sizeof(staged->request.key));
	}

	/* Clears the encoded command. */
	drv_intel_ax211_key_command_scrub(command);
	if (result != 0)
		return result;

	/* Succeeded: the firmware holds the key. */
	return 0;
}

/* Clears one staging slot, key bytes included. */
static void
ax211_pci_staged_key_clear(
	struct ax211_pci_staged_key *staged)
{
	/* Clears a slot that exists. */
	if (staged != NULL)
		ax211_pci_scrub(staged, sizeof(*staged));
}

/* Reports whether any key is active: 1 when one is, 0 when none is. */
static int
ax211_pci_keys_have_active(
	const struct ax211_pci_controller *controller)
{
	uint8_t index;

	/* No key is active without a key state. */
	if (controller == NULL || !controller->keys_initialized)
		return 0;

	/* The pairwise key is active. */
	if (controller->keys.active_pairwise != 0U)
		return 1;

	/* Looks for an active group key. */
	for (index = 0U; index < INTEL_AX211_KEY_INDEX_LIMIT; index++) {
		/* A group key is active. */
		if (controller->keys.active_group[index] != 0U)
			return 1;
	}

	/* No key is active. */
	return 0;
}

/*
 * Reports whether a key request names the connection's addresses: the BSSID
 * for a pairwise key, the broadcast address for a group key.  1 when it does,
 * 0 when not.
 */
static int
ax211_pci_key_request_address_valid(
	const struct ax211_pci_controller *controller,
	const struct wlan_radio_key_request *request)
{
	static const uint8_t broadcast[6] = {0xffU, 0xffU, 0xffU,
					     0xffU, 0xffU, 0xffU};
	int same;

	/* Rejects missing arguments and a connection without a BSS. */
	if (controller == NULL ||
	    request == NULL ||
	    !controller->selected_bss_valid) {
		return 0;
	}

	/* A pairwise key is at index 0 and names the BSSID. */
	if (request->kind == WLAN_RADIO_KEY_PAIRWISE) {
		/* Rejects any other index. */
		if (request->key_index != 0U)
			return 0;

		/* Rejects any other address. */
		same = kern_memcmp(request->address,
				   controller->selected_metadata.bssid, 6U);
		if (same != 0)
			return 0;

		/* The addresses match. */
		return 1;
	}

	/* Rejects any other kind. */
	if (request->kind != WLAN_RADIO_KEY_GROUP)
		return 0;

	/* A group key names the broadcast address. */
	same = kern_memcmp(request->address, broadcast, sizeof(broadcast));
	if (same != 0)
		return 0;

	/* The addresses match. */
	return 1;
}

/* An uncertain key command is recoverable only after a global DMA reset. */
static int
ax211_pci_key_fail_closed(
	struct ax211_pci_controller *controller,
	int error)
{
	int stop_error;

	/* Without a controller, reports the error, or EIO for none. */
	if (controller == NULL) {
		/* Reports the error. */
		if (error != 0)
			return error;

		/* Reports a failure without a reason. */
		return EIO;
	}

	/* Logs the failure and stops the session. */
	kern_logf("intel-ax211: fail-closed key/association path error=%d "
		   "phase=%u step=%u resources=%08x\n",
		   error, controller->association.phase,
		   controller->association.step,
		   controller->association.resources);
	stop_error = ax211_pci_session_stop(controller);

	/* The independent owner can retry after this radio callback returns. */
	if (stop_error != 0) {
		controller->quarantined = 1U;
		ax211_pci_stop_defer_locked(controller, stop_error);

		/* Reports the unproven stop; no resource absence is claimed. */
		return stop_error;
	}

	/* Reports the error, or EIO for none, after a proven stop. */
	controller->quarantined = 0U;
	if (error != 0)
		return error;

	/* Reports a failure without a reason. */
	return EIO;
}

/* Queues one frame on the transmit ring under the caller's deadline. */
static int
ax211_pci_tx_submit(
	struct ax211_pci_controller *controller,
	const struct intel_ax211_tx_request *request,
	uint64_t deadline_ticks,
	int report_completion)
{
	struct intel_ax211_tx_ring_handle handle;
	uint64_t deadline_us;
	uint64_t now_us;
	uint64_t timeout_us;
	int result;

	/* Refuses missing arguments, a flag other than 0 or 1, and no ring. */
	if (controller == NULL ||
	    request == NULL ||
	    (report_completion != 0 && report_completion != 1) ||
	    !controller->tx_ring_allocated) {
		return EINVAL;
	}

	/* Converts the deadline into a timeout. */
	result = ax211_pci_clock_us(controller, &now_us);
	if (result == 0) {
		result = ax211_pci_command_timeout(controller, deadline_ticks,
						   now_us, &timeout_us,
						   &deadline_us);
	}

	/* Reports a failed conversion. */
	if (result != 0)
		return result;
	(void)deadline_us;
	kern_memset(&handle, 0, sizeof(handle));

	/* Queues the frame; a failure that poisons the ring starts recovery. */
	result = drv_intel_ax211_tx_ring_submit(&controller->frame_ring, request,
						now_us, timeout_us, &handle);
	if (result != INTEL_AX211_TX_RING_OK) {
		/* Starts recovery for a ring that needs a reset. */
		if (result == INTEL_AX211_TX_RING_KICK_FAILED ||
		    result == INTEL_AX211_TX_RING_POISONED ||
		    result == INTEL_AX211_TX_RING_BARRIER_REQUIRED) {
			ax211_pci_recovery_latch_locked(controller, EIO);
		}

		/* Reports the failure. */
		result = ax211_pci_tx_ring_result_errno(result);
		return result;
	}

	/* Succeeded: records whether the completion is reported. */
	if (report_completion != 0)
		controller->tx_report_completion[handle.index] = 1U;
	else
		controller->tx_report_completion[handle.index] = 0U;
	return 0;
}

/* Retires an exact TX response before publishing its common completion. */
static int
ax211_pci_tx_dispatch(
	struct ax211_pci_controller *controller,
	const struct intel_ax211_protocol_message *message)
{
	struct intel_ax211_tx_ring_retired retired;
	struct intel_ax211_protocol_message tx_message;
	struct wlan_station *station;
	unsigned report_completion;
	int acknowledged;
	int status;
	int report_result;
	int result;

	/* Refuses missing arguments. */
	if (controller == NULL || message == NULL)
		return EINVAL;

	/* Nothing to retire without a ring. */
	if (!controller->tx_ring_allocated)
		return 0;
	kern_memset(&retired, 0, sizeof(retired));
	tx_message = *message;
	tx_message.queue &= 0x7fU;

	/* Ignores a repeated, stale or early response. */
	result = drv_intel_ax211_tx_ring_complete(&controller->frame_ring,
						  &tx_message, &retired);
	if (result == INTEL_AX211_TX_RING_DUPLICATE ||
	    result == INTEL_AX211_TX_RING_STALE ||
	    result == INTEL_AX211_TX_RING_NOT_READY) {
		return 0;
	}

	/* Refuses any other failure than a failed transmission. */
	if (result != INTEL_AX211_TX_RING_OK &&
	    result != INTEL_AX211_TX_RING_TX_FAILED) {
		return EIO;
	}

	/* Takes the frame's reporting flag. */
	report_completion =
		controller->tx_report_completion[retired.handle.index];
	controller->tx_report_completion[retired.handle.index] = 0U;
	station = NULL;

	/* A frame without reporting is retired. */
	if (!report_completion)
		return 0;

	/* Logs a failed transmission. */
	if (result == INTEL_AX211_TX_RING_TX_FAILED) {
		kern_logf("intel-ax211: TX failed generation=%u cookie=%u "
			   "acknowledged=%u failure=%u/%u\n",
			   (unsigned)retired.handle.connection_generation,
			   (unsigned)retired.handle.cookie,
			   retired.acknowledged, retired.rts_retries,
			   retired.frame_retries);
	}

	/* A station that is gone takes no report. */
	report_result = ax211_pci_operation_enter_locked(controller, &station);
	if (report_result == ENODEV)
		return 0;

	/* Refuses an operation that could not be entered. */
	if (report_result != 0)
		return EIO;

	/* Reports a sent frame as acknowledged, a failed one as EIO. */
	if (result == INTEL_AX211_TX_RING_OK) {
		acknowledged = 1;
		status = 0;
	} else {
		acknowledged = 0;
		status = EIO;
	}

	/*
	 * Completion can advance WPA and synchronously enter another radio op.
	 */
	mutex_unlock(&controller->lifecycle_lock);

	report_result = wlan_station_report_tx_complete(
		station, retired.handle.connection_generation,
		retired.handle.cookie, acknowledged, status);
	mutex_lock(&controller->lifecycle_lock);

	ax211_pci_operation_leave_locked(controller);

	/* A stale report or a station that left is not a failure. */
	if (report_result != 0 &&
	    report_result != ESTALE &&
	    report_result != ENODEV) {
		return EIO;
	}

	/* Succeeded: the completion is reported. */
	return 0;
}

/* Fails one uncertain oldest TX and resets the complete hardware epoch. */
static int
ax211_pci_tx_timeout_check(
	struct ax211_pci_controller *controller,
	uint64_t now_us)
{
	struct intel_ax211_tx_ring_handle handle;
	struct wlan_station *station;
	unsigned report_completion;
	int enter_result;
	int report_result;
	int result;

	/* Nothing can time out without pending frames. */
	if (controller == NULL ||
	    !controller->tx_ring_allocated ||
	    !controller->frame_ring.enabled ||
	    controller->frame_ring.pending_count == 0U) {
		return 0;
	}
	kern_memset(&handle, 0, sizeof(handle));

	/* The oldest frame is still within its time. */
	result = drv_intel_ax211_tx_ring_timeout_oldest(&controller->frame_ring,
							now_us, &handle);
	if (result == INTEL_AX211_TX_RING_PENDING ||
	    result == INTEL_AX211_TX_RING_NOT_READY) {
		return 0;
	}

	/* Refuses any outcome other than a timeout. */
	if (result != INTEL_AX211_TX_RING_TIMEOUT)
		return EIO;

	/* Logs the timeout and takes the frame's reporting flag. */
	kern_logf(
		"intel-ax211: TX completion timeout generation=%u cookie=%u\n",
		(unsigned)handle.connection_generation,
		(unsigned)handle.cookie);
	report_completion = controller->tx_report_completion[handle.index];
	controller->tx_report_completion[handle.index] = 0U;
	station = NULL;

	/* Reports the timed-out frame to a station that takes reports. */
	report_result = 0;
	if (report_completion) {
		/* Reports through an entered operation. */
		enter_result = ax211_pci_operation_enter_locked(controller,
								&station);
		if (enter_result == 0) {
			mutex_unlock(&controller->lifecycle_lock);
			report_result = wlan_station_report_tx_complete(
				station, handle.connection_generation,
				handle.cookie, 0, ETIMEDOUT);
			mutex_lock(&controller->lifecycle_lock);
			ax211_pci_operation_leave_locked(controller);
		}
	}

	/* A failed report is an I/O failure. */
	if (report_result != 0 &&
	    report_result != ESTALE &&
	    report_result != ENODEV) {
		return EIO;
	}

	/* Reports the timeout, which resets the epoch. */
	return ETIMEDOUT;
}

/* Classifies one copied MPDU against the current connection and key epoch. */
static int
ax211_pci_connection_rx_dispatch(
	struct ax211_pci_controller *controller,
	const struct intel_ax211_rx_delivery *mpdu,
	uint16_t frame_control)
{
	struct wlan_radio_rx_frame report;
	struct wlan_station *station;
	enum intel_ax211_key_kind kind;
	uint64_t key_generation;
	uint8_t key_index;
	unsigned type;
	int result;

	/* Refuses missing arguments and a missing connection. */
	if (controller == NULL ||
	    mpdu == NULL ||
	    mpdu->frame == NULL ||
	    controller->connection_generation == 0U) {
		return EINVAL;
	}

	/* Passes only management and data frames. */
	type = frame_control & 0x000cU;
	if (type != 0U && type != 0x0008U)
		return 0;

	/* Describes the frame. */
	kern_memset(&report, 0, sizeof(report));
	report.generation = controller->connection_generation;
	report.frame = mpdu->frame;
	report.length = mpdu->length;
	report.rssi_dbm = mpdu->rssi_dbm;
	report.channel = mpdu->channel;

	/*
	 * A CCMP frame carries its key; any other protected frame is dropped.
	 */
	if (mpdu->cipher == INTEL_AX211_RX_PROTECTION_CCMP) {
		/*
		 * Drops a frame without a key state, too short or undecrypted.
		 */
		if (!controller->keys_initialized ||
		    mpdu->length < 10U ||
		    !mpdu->decrypted) {
			return 0;
		}

		/* A group-addressed frame uses a group key at its index. */
		if ((mpdu->frame[4U] & 1U) != 0U)
			kind = INTEL_AX211_KEY_GROUP_KEY;
		else
			kind = INTEL_AX211_KEY_PAIRWISE;
		if (kind == INTEL_AX211_KEY_PAIRWISE)
			key_index = 0U;
		else
			key_index = mpdu->key_index;

		/* Drops a frame whose key is not known. */
		result = drv_intel_ax211_key_state_rx_generation(
			&controller->keys, controller->connection_generation,
			kind, key_index, controller->hardware_epoch,
			&key_generation);
		if (result != INTEL_AX211_KEY_OK)
			return 0;

		/* Describes the key. */
		report.key_generation = key_generation;
		report.packet_number = mpdu->packet_number;
		report.cipher = WLAN_RADIO_CIPHER_CCMP;
		report.decrypted = 1U;
		report.key_index = key_index;
	} else if (mpdu->cipher != INTEL_AX211_RX_PROTECTION_NONE) {
		/* Drops a frame of another protection. */
		return 0;
	}
	station = NULL;

	/* A station that is gone takes no frame. */
	result = ax211_pci_operation_enter_locked(controller, &station);
	if (result == ENODEV)
		return 0;
	if (result != 0)
		return EIO;

	/*
	 * Frame ingestion may advance WPA and synchronously enter a radio op.
	 */
	mutex_unlock(&controller->lifecycle_lock);

	(void)wlan_station_report_frame(station, &report);
	mutex_lock(&controller->lifecycle_lock);

	ax211_pci_operation_leave_locked(controller);

	/*
	 * Once the private descriptor and crypto envelope have validated, every
	 * common result is a per-frame policy/drop/control outcome.  It must
	 * not quarantine otherwise healthy DMA or firmware state.
	 */
	return 0;
}

/* Performs a stopped NVM pass followed by one retained runtime pass. */
static int
ax211_net_open(
	struct net_device *device)
{
	struct ax211_pci_controller *controller;
	int busy;
	int error;

	/* Refuses a missing device. */
	if (device == NULL)
		return ENODEV;

	/* Refuses a device without a controller. */
	controller = device->driver_data;
	if (controller == NULL)
		return ENODEV;
	mutex_lock(&controller->lifecycle_lock);

	/*
	 * Independent retirement must release its epoch before open can reuse
	 * it.
	 */
	busy = wlan_station_stop_busy(controller->station);
	if (busy) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return EBUSY;
	}

	/*
	 * A controller suspended for the sleep is not opened until its resume.
	 */
	if (controller->suspended) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed: the controller is suspended. */
		return EBUSY;
	}

	/* Starts the epoch. */
	error = ax211_pci_open_locked(controller, 0);

	/*
	 * An open by the user starts a fresh count of restarts, and replaces any
	 * restart a recovery asked for.
	 */
	if (error == 0) {
		controller->net_opened = 1U;
		controller->reopen_pending = 0U;
		controller->reopen_attempts = 0U;
	}

	/* Lets the poll, the timers, and the retirement thread in again. */
	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Suspends the controller for S0 idle (ws052-p005).
 *
 * Only a radio that is off goes to sleep this way: no runtime session runs
 * on the controller, so D3hot loses nothing the next open does not start
 * afresh.  A radio that is on refuses with EBUSY: the network daemon turns
 * it off before the sleep (Wi-Fi's own suspend is later work).  Opens are
 * refused until the resume.
 */
static int
ax211_pci_suspend(
	struct drv_pci_device *device)
{
	struct ax211_pci_controller *controller;
	int busy;

	/* A controller that is not there has nothing to suspend. */
	controller = drv_pci_device_driver_data(device);
	if (controller == NULL)
		return 0;

	/*
	 * Refuses a radio that is on; otherwise refuses opens until the resume.
	 */
	mutex_lock(&controller->lifecycle_lock);

	busy = 0;
	if (controller->net_opened ||
	    controller->reopen_pending ||
	    controller->active_dma != NULL) {
		busy = 1;
	}
	if (!busy)
		controller->suspended = 1U;

	mutex_unlock(&controller->lifecycle_lock);

	/* Says why a radio that is on stops the sleep. */
	if (busy) {
		kern_logf("intel-ax211: the radio is on; it is turned off "
			   "before the sleep\n");
		return EBUSY;
	}

	/* Succeeded: the controller may go to D3hot. */
	return 0;
}

/*
 * Resumes the controller after S0 idle: opens are taken again; the next
 * open boots the firmware as it always does.
 */
static int
ax211_pci_resume(
	struct drv_pci_device *device)
{
	struct ax211_pci_controller *controller;

	/* A controller that is not there has nothing to resume. */
	controller = drv_pci_device_driver_data(device);
	if (controller == NULL)
		return 0;

	/* Takes opens again. */
	mutex_lock(&controller->lifecycle_lock);

	controller->suspended = 0U;

	mutex_unlock(&controller->lifecycle_lock);

	/* Succeeded: the radio can be turned on again. */
	return 0;
}

/*
 * Starts a new epoch: a stopped NVM pass followed by one retained runtime
 * pass, after retiring an earlier stopped epoch.  The lifecycle mutex is held
 * by the caller, the device open or the recovery's restart (BUG-158).  The
 * retirement of the earlier epoch drops the mutex for a while; a restart
 * whose device was closed in that window starts nothing (ECANCELED).
 */
static int
ax211_pci_open_locked(
	struct ax211_pci_controller *controller,
	int restarting)
{
	struct intel_ax211_protocol_command_table table;
	struct intel_ax211_protocol_nvm nvm;
	struct drv_dma_device *dma_device;
	struct wlan_station *station;
	const char *stage;
	unsigned long enabled;
	int boot_result;
	int runtime_result;
	int step_result;
	int coherent;
	int cleanup_error;
	int error;
	int ltr_enabled;

	/*
	 * Finishes an earlier stopped epoch before publishing a fresh scan
	 * profile.
	 */
	if (!controller->detaching &&
	    controller->ready &&
	    controller->station_attached &&
	    (controller->close_pending || controller->session_stopped)) {
		/* Reports a close that did not finish. */
		error = ax211_pci_close_locked(controller);
		if (error != 0)
			return error;
	}

	/* The user closed the device while the earlier epoch was retired. */
	if (restarting && !controller->net_opened)
		return ECANCELED;

	/*
	 * Refuses a detaching, unready, quarantined or unattached controller;
	 * a running one opens again only while it admits operations.
	 */
	if (controller->detaching ||
	    !controller->ready ||
	    controller->quarantined ||
	    !controller->station_attached) {
		error = ENODEV;
	} else if (controller->runtime_active) {
		/* A running controller is busy while it does not admit. */
		if (controller->operation_admission_open &&
		    !controller->recovery_pending &&
		    !controller->recovery_running) {
			error = 0;
		} else {
			error = EBUSY;
		}
	} else if (controller->operations_active != 0U) {
		error = EBUSY;
	} else {
		/* Starts from a closed admission and a fresh session. */
		controller->operation_admission_open = 0U;
		controller->session_stopped = 0U;
		dma_device = drv_pci_device_dma(controller->device);
		kern_memset(&table, 0, sizeof(table));
		kern_memset(&nvm, 0, sizeof(nvm));
		boot_result = INTEL_AX211_BOOT_OK;
		runtime_result = INTEL_AX211_RUNTIME_START_OK;
		stage = "dma-coherency";

		/* Requires coherent DMA. */
		error = 0;
		if (dma_device == NULL) {
			error = EIO;
		} else {
			coherent = drv_dma_device_is_coherent(dma_device);
			if (!coherent)
				error = EIO;
		}

		/* Initializes the first-boot coordinator. */
		if (error == 0) {
			stage = "boot-init";
			step_result = drv_intel_ax211_boot_init(
				&controller->boot,
				&ax211_runtime_start_ops.boot,
				controller, dma_device,
				&controller->mmio,
				&controller->transport,
				(uint16_t)controller->hardware_revision,
				AX211_RADIO_KIND_GF,
				controller->hardware_epoch);
			if (step_result != INTEL_AX211_BOOT_OK)
				error = EIO;
		}

		/* Runs the first boot, which reads the NVM and stops. */
		if (error == 0) {
			stage = "firmware-boot";
			controller->boot_initialized = 1U;
			boot_result = drv_intel_ax211_boot_run(
				&controller->boot, &nvm);
			if (boot_result != INTEL_AX211_BOOT_OK)
				error = EIO;
			controller->hardware_epoch =
				controller->boot.generation;

			/* Notes a reset that went on without the indication. */
			if (boot_result == INTEL_AX211_BOOT_OK &&
			    controller->mmio.master_disable_timed_out) {
				kern_logf("intel-ax211: master-disable "
					   "indication "
					   "timed out; PCI bus master disabled "
					   "and reset "
					   "completed\n");
			}

			/* Logs the coordinator's state after a failure. */
			if (error != 0) {
				kern_logf(
					"intel-ax211: first boot failed "
					"result=%d "
					"last=%u state=%u generation=%u "
					"files=%u dma=%u "
					"exposed=%u hardware=%u transport=%u "
					"alive=%u "
					"pnvm=%u init=%u commands=%u nvm=%u\n",
					boot_result,
					controller->boot.last_error,
					controller->boot.state,
					controller->boot.generation,
					controller->boot.files_loaded,
					controller->boot.dma_prepared,
					controller->boot.dma_exposed,
					controller->boot.hardware_touched,
					controller->boot.transport_bound,
					controller->boot.alive_accepted,
					controller->boot.pnvm_accepted,
					controller->boot.init_accepted,
					controller->boot.command_table_valid,
					controller->boot.nvm_valid);
			}

			/* Logs the last event received before a failure. */
			if (error != 0 &&
			    controller->last_receive_length != 0U) {
				kern_logf("intel-ax211: last rx length=%u "
					   "opcode=%02x "
					   "group=%02x index=%02x queue=%02x "
					   "version=%u\n",
					   (unsigned)controller
						   ->last_receive_length,
					   controller->last_receive_header[4U],
					   controller->last_receive_header[5U],
					   controller->last_receive_header[6U],
					   controller->last_receive_header[7U],
					   controller->last_receive_version);
			}
		}

		/* Borrows the command-version table of the first boot. */
		if (error == 0) {
			stage = "command-table";
			step_result = drv_intel_ax211_boot_command_table(
				&controller->boot, &table);
			if (step_result != INTEL_AX211_BOOT_OK)
				error = EIO;
		}

		/* Reads the LTR choice. */
		ltr_enabled = 0;
		if (error == 0) {
			stage = "ltr";
			error = ax211_pci_ltr_enabled(controller, &ltr_enabled);
		}

		/* Initializes the operational start coordinator. */
		if (error == 0) {
			stage = "runtime-init";
			step_result = drv_intel_ax211_runtime_start_init(
				&controller->runtime_start,
				&ax211_runtime_start_ops, controller,
				dma_device, &controller->mmio,
				&controller->transport,
				(uint16_t)controller->hardware_revision,
				AX211_RADIO_KIND_GF, &table, &nvm,
				ltr_enabled,
				controller->hardware_epoch);
			if (step_result != INTEL_AX211_RUNTIME_START_OK)
				error = EIO;
		}

		/* Starts the operational firmware. */
		if (error == 0) {
			/* Logs the coordinator's state after a failure. */
			stage = "runtime-start";
			controller->runtime_initialized = 1U;
			runtime_result = drv_intel_ax211_runtime_start_run(
				&controller->runtime_start);
			if (runtime_result != INTEL_AX211_RUNTIME_START_OK)
				error = EIO;
			controller->hardware_epoch =
				controller->runtime_start.generation;
			if (error != 0) {
				kern_logf(
					"intel-ax211: runtime start failed "
					"result=%d "
					"last=%u state=%u generation=%u "
					"files=%u dma=%u "
					"exposed=%u hardware=%u transport=%u "
					"alive=%u "
					"pnvm=%u init=%u profile=%u mcc=%u "
					"nic=%u\n",
					runtime_result,
					controller->runtime_start.last_error,
					controller->runtime_start.state,
					controller->runtime_start.generation,
					controller->runtime_start.files_loaded,
					controller->runtime_start.dma_prepared,
					controller->runtime_start.dma_exposed,
					controller->runtime_start
						.hardware_touched,
					controller->runtime_start
						.transport_bound,
					controller->runtime_start
						.alive_accepted,
					controller->runtime_start.pnvm_accepted,
					controller->runtime_start.init_accepted,
					controller->runtime_start.profile_valid,
					controller->runtime_start.mcc_valid,
					controller->runtime_start.wake_held);
			}

			/* Logs the last event received before a failure. */
			if (error != 0 &&
			    controller->last_receive_length != 0U) {
				kern_logf("intel-ax211: last runtime rx "
					   "length=%u "
					   "opcode=%02x group=%02x index=%02x "
					   "queue=%02x "
					   "version=%u\n",
					   (unsigned)controller
						   ->last_receive_length,
					   controller->last_receive_header[4U],
					   controller->last_receive_header[5U],
					   controller->last_receive_header[6U],
					   controller->last_receive_header[7U],
					   controller->last_receive_version);
			}
		}

		/* Requires the association command versions. */
		if (error == 0) {
			stage = "api89-validation";
			step_result = drv_intel_ax211_assoc_api89_validate(&table);
			if (step_result != INTEL_AX211_ASSOC_OK)
				error = ENOTSUP;
		}

		/* Requires the key command versions. */
		if (error == 0) {
			step_result = drv_intel_ax211_key_api89_validate(&table);
			if (step_result != INTEL_AX211_KEY_OK)
				error = ENOTSUP;
		}

		/* Requires the transmit command versions. */
		if (error == 0) {
			step_result =
				drv_intel_ax211_tx_ring_api89_validate(&table);
			if (step_result != INTEL_AX211_TX_RING_OK)
				error = ENOTSUP;
		}

		/* Allocates the transmit ring. */
		if (error == 0) {
			stage = "tx-ring";

			/* Counts the management cookies down from the top. */
			step_result = drv_intel_ax211_tx_ring_allocate(
				dma_device, &ax211_tx_ring_ops,
				controller, &controller->frame_ring);
			if (step_result != INTEL_AX211_TX_RING_OK)
				error = ENOMEM;
			if (error == 0) {
				controller->tx_ring_allocated = 1U;
				controller->next_management_cookie = UINT64_MAX;
			}
		}

		/* Initializes scanning. */
		if (error == 0) {
			stage = "scan-init";
			error = ax211_pci_scan_initialize(controller);
		}

		/* Marks the runtime active for the interrupt handler. */
		if (error == 0) {
			enabled =
				spin_lock_irqsave(&controller->interrupt_lock);
			controller->runtime_active = 1U;
			spin_unlock_irqrestore(&controller->interrupt_lock,
					       enabled);
		}

		/* Opens the common station. */
		station = controller->station;
		if (error == 0) {
			stage = "station-open";
			if (station == NULL)
				error = ENODEV;
			else
				error = wlan_station_open(station);
		}

		/*
		 * Opens admission, or stops the session of a failed open and
		 * quarantines a controller whose stop failed.
		 */
		if (error == 0) {
			controller->operation_admission_open = 1U;
		} else {
			kern_logf(
				"intel-ax211: open failed stage=%s error=%d\n",
				stage, error);

			/* Stops the session; a failed stop quarantines. */
			cleanup_error = ax211_pci_session_stop(controller);
			if (cleanup_error != 0) {
				controller->quarantined = 1U;
				error = cleanup_error;
			}
		}

		/* Clears the table description and the NVM copy. */
		ax211_pci_scrub(&table, sizeof(table));
		ax211_pci_scrub(&nvm, sizeof(nvm));
	}

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded: the epoch runs and admission is open. */
	return 0;
}

/*
 * Reconciles common retirement only after all driver operations can be joined.
 */
static int
ax211_pci_close_locked(
	struct ax211_pci_controller *controller)
{
	struct wlan_station *station;
	uint64_t deadline;
	int log;
	int error;
	int stop_error;

	/*
	 * Denies new forwards while preserving every existing lease until it
	 * returns.
	 */
	controller->close_pending = 1U;
	controller->operation_admission_open = 0U;

	/* Joins the operations in flight. */
	deadline = ax211_pci_lifecycle_deadline();
	error = ax211_pci_operations_join_locked(controller, deadline);
	if (error != 0)
		return error;

	/* Takes the attached station. */
	if (controller->station_attached)
		station = controller->station;
	else
		station = NULL;

	/*
	 * Gives common retirement its own bounded window outside the callback
	 * mutex.
	 */
	mutex_unlock(&controller->lifecycle_lock);

	if (station == NULL) {
		error = ENODEV;
	} else {
		deadline = ax211_pci_lifecycle_deadline();
		error = ax211_pci_station_close_wait(station, deadline);
	}
	mutex_lock(&controller->lifecycle_lock);

	/* Logs a close that needs the checked stop, at most once a second. */
	if (error != 0) {
		/* Logs the error. */
		log = ax211_pci_log_rejection(controller);
		if (log) {
			kern_logf("intel-ax211: common close requires checked "
				   "stop (%d)\n",
				   error);
		}
	}

	/*
	 * A failed inverse still requires a checked stop, never a reset under a
	 * live pin.
	 */
	if (controller->operations_active != 0U)
		return EBUSY;

	/* Stops the session while the station is quiesced. */
	stop_error = wlan_station_quiesce_begin(station);
	if (stop_error != 0)
		return stop_error;
	stop_error = ax211_pci_session_stop(controller);
	wlan_station_quiesce_end(station);

	/* A failed stop quarantines the controller. */
	if (stop_error != 0) {
		controller->quarantined = 1U;
		return stop_error;
	}

	/*
	 * Successful global stop lets outstanding key/association inverses
	 * prove absence.
	 */
	controller->quarantined = 0U;
	mutex_unlock(&controller->lifecycle_lock);

	deadline = ax211_pci_lifecycle_deadline();
	error = ax211_pci_station_close_wait(station, deadline);
	mutex_lock(&controller->lifecycle_lock);
	if (error == 0)
		controller->close_pending = 0U;

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Closes the epoch without losing the ability to retry an incomplete close. */
static void
ax211_net_close(
	struct net_device *device)
{
	struct ax211_pci_controller *controller;
	int log;
	int error;

	/* Retires carrier visibility before closing driver admission. */
	if (device == NULL)
		return;
	(void)net_device_set_carrier(device, 0);

	/* Nothing to close without a controller. */
	controller = device->driver_data;
	if (controller == NULL)
		return;
	mutex_lock(&controller->lifecycle_lock);

	/* A closed device is not restarted behind the user's back. */
	controller->net_opened = 0U;
	controller->reopen_pending = 0U;

	/* Closes the epoch between the station's stop request and its end. */
	wlan_station_stop_request(controller->station);
	error = ax211_pci_close_locked(controller);
	wlan_station_stop_complete(controller->station, error);

	/* Logs a close left pending, at most once a second. */
	if (error != 0) {
		/* Logs the error. */
		log = ax211_pci_log_rejection(controller);
		if (log) {
			kern_logf("intel-ax211: checked close pending (%d)\n",
				   error);
		}
	}

	mutex_unlock(&controller->lifecycle_lock);
}

/*
 * Bounds repeated rejection diagnostics while the lifecycle mutex is held:
 * reports 1 when a diagnostic may be logged now, 0 when not.
 */
static int
ax211_pci_log_rejection(
	struct ax211_pci_controller *controller)
{
	uint64_t now;

	/*
	 * Keeps the first diagnostic and emits at most one retry diagnostic per
	 * second.
	 */
	now = clock_ticks();
	if (now < controller->reject_log_deadline)
		return 0;

	/* Sets the next deadline one second ahead, saturating the clock. */
	if (UINT64_MAX - now < KERN_CLOCK_HZ)
		controller->reject_log_deadline = UINT64_MAX;
	else
		controller->reject_log_deadline = now + KERN_CLOCK_HZ;

	/* A diagnostic may be logged. */
	return 1;
}

/* Keeps Ethernet conversion and CCMP framing in the common WLAN station. */
static int
ax211_net_transmit(
	struct net_device *device,
	struct packet_buf *packet)
{
	struct ax211_pci_controller *controller;
	struct wlan_station *station;
	int error;

	/* Refuses a missing packet. */
	if (packet == NULL)
		return EINVAL;

	/* Drops the packet of a missing device. */
	if (device == NULL) {
		packet_buf_free(packet);

		/* Failed. */
		return ENODEV;
	}

	/* Drops the packet of a device without a controller. */
	controller = device->driver_data;
	if (controller == NULL) {
		packet_buf_free(packet);

		/* Failed. */
		return ENODEV;
	}

	mutex_lock(&controller->lifecycle_lock);

	/* Enters an operation of a running, admitting controller. */
	if (!controller->runtime_active ||
	    controller->quarantined ||
	    !controller->operation_admission_open ||
	    !controller->station_attached) {
		/* A ready controller is down; any other has no device. */
		station = NULL;
		if (controller->ready)
			error = ENETDOWN;
		else
			error = ENODEV;
	} else {
		error = ax211_pci_operation_enter_locked(controller, &station);
	}

	mutex_unlock(&controller->lifecycle_lock);

	/* Drops the packet when no operation was entered. */
	if (station == NULL) {
		packet_buf_free(packet);

		/* Failed. */
		return error;
	}

	/* Hands the packet to the station and leaves the operation. */
	error = wlan_station_transmit(station, packet);
	mutex_lock(&controller->lifecycle_lock);

	ax211_pci_operation_leave_locked(controller);

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Drains up to budget runtime events, checks transmit timeouts, starts a
 * recovery on a fatal error, and reports the number of events handled.
 */
static unsigned
ax211_net_poll_receive(
	struct net_device *device,
	unsigned budget)
{
	struct ax211_pci_controller *controller;
	struct intel_ax211_boot_received_event event;
	uint64_t now;
	unsigned count;
	unsigned reschedule;
	unsigned pending;
	unsigned long enabled;
	int dispatch_result;
	int fatal_error;
	int result;

	/* Nothing to poll without a device or a budget. */
	if (device == NULL || budget == 0U)
		return 0U;

	/* Nothing to poll without a controller. */
	controller = device->driver_data;
	if (controller == NULL)
		return 0U;
	count = 0U;
	mutex_lock(&controller->lifecycle_lock);

	/* A poll already running is asked to poll once more. */
	if (controller->poll_active) {
		controller->poll_reschedule = 1U;
		mutex_unlock(&controller->lifecycle_lock);

		/* No event was handled here. */
		return 0U;
	}

	/* Nothing to poll on a stopped or quarantined controller. */
	if (!controller->runtime_active || controller->quarantined) {
		mutex_unlock(&controller->lifecycle_lock);

		/* No event was handled. */
		return 0U;
	}

	/* A pending recovery's error stops the loop before it starts. */
	controller->poll_active = 1U;
	if (controller->recovery_pending)
		fatal_error = controller->recovery_error;
	else
		fatal_error = 0;

	/* Handles events until the budget runs out or nothing is left. */
	while (fatal_error == 0 &&
	       count < budget &&
	       controller->runtime_active &&
	       !controller->quarantined) {
		/* Drains deferred events first. */
		if (controller->deferred_event_count != 0U) {
			/* A busy drain waits for the next poll. */
			result = ax211_pci_deferred_event_drain_one(controller);
			if (result == EBUSY)
				break;
			if (result != 0) {
				fatal_error = result;
				break;
			}

			count++;
			continue;
		}

		/* Reads the clock. */
		result = ax211_pci_clock_us(controller, &now);
		if (result != 0) {
			fatal_error = EIO;
			break;
		}

		kern_memset(&event, 0, sizeof(event));

		/* Receives the next event; none arriving ends the loop. */
		result = ax211_pci_receive_event(
			controller, now, controller->runtime_event,
			sizeof(controller->runtime_event), &event);
		if (result == INTEL_AX211_BOOT_RECEIVE_TIMEOUT)
			break;
		if (result != INTEL_AX211_BOOT_RECEIVE_OK) {
			pending = (unsigned)drv_intel_ax211_command_pending_count(
				&controller->runtime_start.commands);
			kern_logf(
				"intel-ax211: runtime receive failed result=%d "
				"scan-phase=%u pending=%u\n",
				result, controller->scan_session.phase,
				pending);
			fatal_error = EIO;
			break;
		}

		/* Dispatches the event and clears its bytes. */
		dispatch_result = ax211_pci_runtime_event_dispatch(
			controller, controller->runtime_event, event.length,
			&event);
		ax211_pci_scrub(controller->runtime_event, event.length);

		/* A failed dispatch is fatal. */
		if (dispatch_result != 0) {
			kern_logf(
				"intel-ax211: runtime dispatch failed error=%d "
				"length=%u opcode=%02x group=%02x index=%02x "
				"queue=%02x "
				"version=%u scan-phase=%u\n",
				dispatch_result, (unsigned)event.length,
				controller->last_receive_header[4U],
				controller->last_receive_header[5U],
				controller->last_receive_header[6U],
				controller->last_receive_header[7U],
				controller->last_receive_version,
				controller->scan_session.phase);
			fatal_error = dispatch_result;
			break;
		}

		count++;
	}

	/* Checks the transmit timeouts of a healthy controller. */
	if (fatal_error == 0 &&
	    controller->runtime_active &&
	    !controller->quarantined) {
		/* A failed check is fatal. */
		result = ax211_pci_clock_us(controller, &now);
		if (result == 0)
			result = ax211_pci_tx_timeout_check(controller, now);
		if (result != 0)
			fatal_error = result;
	}

	/* Runs the recovery of a fatal error. */
	if (fatal_error != 0) {
		/* Latches the error unless a recovery is already pending. */
		if (!controller->recovery_pending) {
			ax211_pci_recovery_latch_locked(controller,
							fatal_error);
		}

		(void)ax211_pci_recovery_run_locked(controller);
	}

	/*
	 * Polls again for a request, a recovery, deferred events, a full budget
	 * or a latched interrupt, while receiving is enabled.
	 */
	enabled = spin_lock_irqsave(&controller->interrupt_lock);

	reschedule = 0U;
	if ((controller->poll_reschedule ||
	     controller->recovery_pending ||
	     controller->deferred_event_count != 0U ||
	     count >= budget ||
	     controller->irq_latched) &&
	    controller->receive_enabled &&
	    controller->runtime_active &&
	    !controller->quarantined) {
		reschedule = 1U;
	}

	spin_unlock_irqrestore(&controller->interrupt_lock, enabled);

	controller->poll_active = 0U;
	controller->poll_reschedule = 0U;

	mutex_unlock(&controller->lifecycle_lock);

	/* Schedules the next poll. */
	if (reschedule)
		net_device_schedule_poll(device);

	/* Reports the number of events handled. */
	return count;
}

/*
 * Admits a WLAN ioctl by the controller's state and hands it to the common
 * station.
 */
static int
ax211_net_ioctl(
	struct net_device *device,
	unsigned long request,
	void *argument)
{
	struct ax211_pci_controller *controller;
	int log;
	int result;

	/* Refuses a missing device. */
	if (device == NULL)
		return ENODEV;

	/* Refuses a device without a controller. */
	controller = device->driver_data;
	if (controller == NULL)
		return ENODEV;
	mutex_lock(&controller->lifecycle_lock);

	/*
	 * Admits the queries, and a disconnect of a stopped session, always;
	 * any other request needs a running controller.
	 */
	if (!controller->station_attached || controller->detaching) {
		result = ENODEV;
	} else if (request == SIOCGWLANSTATUS ||
		   request == SIOCGWLANSCAN ||
		   request == SIOCGWLANBSS ||
		   (request == SIOCSWLANDISCONNECT &&
		    controller->session_stopped)) {
		result = 0;
	} else if (request == SIOCSWLANDISCONNECT &&
		   !controller->quarantined &&
		   (controller->recovery_pending ||
		    controller->recovery_running)) {
		/*
		 * A disconnect that arrives while recovery is stopping the
		 * session waits for it: the session ends stopped and the retry
		 * is admitted then.  EBUSY is the retryable answer; ENETDOWN
		 * made networkd's cleanup after a refused key fail, and the
		 * user read "Network is down" instead of the refusal (BUG-157).
		 */
		result = EBUSY;
	} else if (!controller->runtime_active ||
		   controller->quarantined ||
		   controller->recovery_pending ||
		   controller->recovery_running) {
		result = ENETDOWN;
	} else {
		result = 0;
	}

	/* Logs a refused connect, at most once a second. */
	if (request == SIOCSWLANCONNECT && result != 0) {
		/* Logs the controller's state. */
		log = ax211_pci_log_rejection(controller);
		if (log) {
			kern_logf("intel-ax211: connect ioctl rejected "
				   "result=%d runtime=%u "
				   "quarantined=%u recovery=%u/%u attached=%u "
				   "detaching=%u\n",
				   result, controller->runtime_active,
				   controller->quarantined,
				   controller->recovery_pending,
				   controller->recovery_running,
				   controller->station_attached,
				   controller->detaching);
		}
	}

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the refusal. */
	if (result != 0)
		return result;

	/* Hands the request to the station. */
	result = wlan_station_ioctl(device, request, argument);
	if (result != 0)
		return result;

	/* Succeeded. */
	return 0;
}

/* Frees the controller only after the last removed net-device reference. */
static void
ax211_net_release(
	void *driver_data)
{
	struct ax211_pci_controller *controller;

	/* Nothing to free without a controller. */
	controller = driver_data;
	if (controller == NULL)
		return;

	/* Clears and frees the controller. */
	ax211_pci_scrub(controller, sizeof(*controller));
	kern_free(controller);
}

/* Starts one finite asynchronous firmware scan on the selected channel. */
static int
ax211_radio_scan_channel_start(
	void *context,
	uint64_t generation,
	uint32_t step_index,
	uint32_t channel,
	uint64_t deadline)
{
	struct ax211_pci_controller *controller;
	uint64_t now;
	int present;
	int cache_result;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/*
	 * Refuses an unset generation, a channel out of range and no deadline.
	 */
	if (generation == 0U || channel > UINT8_MAX || deadline == 0U)
		return EINVAL;

	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a controller that cannot scan now. */
	if (!controller->ready ||
	    controller->detaching ||
	    controller->quarantined ||
	    controller->close_pending ||
	    controller->recovery_pending ||
	    controller->recovery_running ||
	    !controller->runtime_active ||
	    !controller->scan_initialized ||
	    !controller->station_attached ||
	    controller->station == NULL) {
		/* A ready controller is down; any other has no device. */
		if (controller->ready)
			result = ENETDOWN;
		else
			result = ENODEV;
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return result;
	}

	/* Refuses a step beyond the scan's channels. */
	if (step_index >= controller->scan_session.full_profile.channel_count) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return EINVAL;
	}

	/* Refuses a channel the scan does not cover. */
	present = ax211_pci_scan_channel_present(controller, (uint8_t)channel);
	if (!present) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return EINVAL;
	}

	result = ax211_pci_clock_us(controller, &now);

	/*
	 * Common starts a fresh staging set for each scan generation.  Preserve
	 * the published set until this entire generation completes
	 * successfully.
	 */
	if (result == 0 && (!controller->bss_staging_initialized ||
			    controller->bss_staging_generation != generation)) {
		/* Starts an empty staging cache for the generation. */
		ax211_pci_bss_staging_discard(controller);
		cache_result = drv_intel_ax211_bss_cache_init(
			&controller->bss_staging_cache,
			controller->hardware_epoch);
		if (cache_result == INTEL_AX211_BSS_OK)
			result = 0;
		else
			result = EIO;
		if (result == 0) {
			controller->bss_staging_initialized = 1U;
			controller->bss_staging_generation = generation;
		}
	}

	/* Starts the channel's scan. */
	if (result == 0) {
		result = drv_intel_ax211_scan_session_begin_channel(
			&controller->scan_session, generation, (uint8_t)channel,
			now);
	} else {
		result = INTEL_AX211_SCAN_SESSION_FAILED;
	}

	/* Records the step, or discards the staging set of a failed start. */
	if (result == INTEL_AX211_SCAN_SESSION_OK)
		controller->scan_step_index = step_index;
	else
		ax211_pci_bss_staging_discard(controller);

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	result = ax211_pci_scan_result_errno(result);
	if (result != 0)
		return result;

	/* Succeeded: the channel's scan runs. */
	return 0;
}

/* Retires the matching asynchronous scan or requests a bounded retry. */
static int
ax211_radio_scan_stop(
	void *context,
	uint64_t generation)
{
	struct ax211_pci_controller *controller;
	uint64_t now;
	uint8_t phase;
	int error;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/* Refuses an unset generation. */
	if (generation == 0U)
		return EINVAL;

	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a recovery that has not started. */
	if (controller->recovery_pending && !controller->recovery_running) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return ENETDOWN;
	}

	/* Nothing runs on a stopped runtime or without a scan session. */
	if (!controller->runtime_active || !controller->scan_initialized) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Succeeded. */
		return 0;
	}

	/* Nothing runs without a scan generation. */
	if (controller->scan_session.common_generation == 0U) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Succeeded. */
		return 0;
	}

	/* Refuses another generation's scan. */
	if (controller->scan_session.common_generation != generation) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return ESTALE;
	}

	/* An idle session has nothing to stop. */
	phase = controller->scan_session.phase;
	if (phase == INTEL_AX211_SCAN_SESSION_IDLE) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Succeeded. */
		return 0;
	}

	/*
	 * Takes a finished scan's result, or expires and aborts a running one.
	 */
	if (phase == INTEL_AX211_SCAN_SESSION_TERMINAL) {
		result = controller->scan_session.terminal_result;
	} else {
		/* Expires the session at the current time. */
		error = ax211_pci_clock_us(controller, &now);
		if (error != 0) {
			result = INTEL_AX211_SCAN_SESSION_FAILED;
		} else {
			result = drv_intel_ax211_scan_session_expire(
				&controller->scan_session, now);
		}

		/* Aborts a session that is still running. */
		phase = controller->scan_session.phase;
		if (result == INTEL_AX211_SCAN_SESSION_OK &&
		    phase == INTEL_AX211_SCAN_SESSION_RUNNING) {
			result = drv_intel_ax211_scan_session_abort(
				&controller->scan_session, generation, now);
		}
	}

	/* A scan still stopping is retried. */
	if (result == INTEL_AX211_SCAN_SESSION_OK ||
	    result == INTEL_AX211_SCAN_SESSION_BUSY) {
		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		return EBUSY;
	}

	/* A completed scan publishes the set after its last channel. */
	if (result == INTEL_AX211_SCAN_SESSION_COMPLETE) {
		/* Publishes after the final step. */
		if (controller->scan_step_index + 1U ==
		    controller->scan_session.full_profile.channel_count) {
			/*
			 * A set already published needs no second publication.
			 */
			if (!controller->bss_staging_initialized &&
			    controller->bss_published_generation == generation) {
				error = 0;
			} else {
				error = ax211_pci_bss_staging_publish(
					controller, generation);
			}

			/*
			 * A scan that completed proves the epoch works:
			 * restarts count from zero again.
			 */
			if (error == 0)
				controller->reopen_attempts = 0U;
		} else {
			/*
			 * One common generation spans every channel.  Keep
			 * private metadata from earlier channels until the
			 * final step publishes the complete set; association
			 * may select any of those BSSes.
			 */
			error = 0;
		}

		mutex_unlock(&controller->lifecycle_lock);

		/* Failed. */
		if (error != 0)
			return error;

		/* Succeeded: the channel's scan is retired. */
		return 0;
	}

	/* An aborted or repeated scan is retired. */
	if (result == INTEL_AX211_SCAN_SESSION_ABORTED ||
	    result == INTEL_AX211_SCAN_SESSION_DUPLICATE) {
		ax211_pci_bss_staging_discard(controller);
		mutex_unlock(&controller->lifecycle_lock);

		/* Succeeded. */
		return 0;
	}

	/*
	 * An expired or failed command leaves firmware ownership ambiguous, and
	 * a timed-out command poisons the command transaction until a device
	 * reset.  The whole epoch is stopped by the poll's recovery owner, not
	 * here inside the common scan timer, and the recovery then has the
	 * retirement thread start a new epoch (BUG-158).  Until the stop, this
	 * call answers ENETDOWN to the common retry; after it, the runtime is
	 * inactive and the retry succeeds, so no producer is stranded behind the
	 * common WLAN barrier.
	 */
	error = ax211_pci_scan_result_errno(result);
	kern_logf("intel-ax211: scan stop generation=%u phase=%u result=%d "
		   "error=%d; recovering\n",
		   (unsigned)generation, phase, result, error);
	ax211_pci_bss_staging_discard(controller);
	ax211_pci_recovery_latch_locked(controller, error);

	mutex_unlock(&controller->lifecycle_lock);

	/*
	 * Failed: the scan is over and the stop is retried after the recovery.
	 */
	if (error != 0)
		return error;

	/*
	 * Failed without a mapped reason: still retryable after the recovery.
	 */
	return EIO;
}

/*
 * Starts a connection to bss: builds the association profile and runs the
 * authentication stage of the firmware's association session.
 */
static int
ax211_radio_connect_start(
	void *context,
	uint64_t generation,
	const struct wlan_bss_record *bss,
	uint64_t deadline)
{
	struct ax211_pci_controller *controller;
	struct intel_ax211_assoc_profile profile;
	struct intel_ax211_protocol_command_table table;
	uint64_t now;
	uint64_t ticks;
	int log;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/* Refuses an unset generation, a missing BSS and an unset deadline. */
	if (generation == 0U || bss == NULL || deadline == 0U)
		return EINVAL;

	/* Refuses a deadline that has passed. */
	ticks = clock_ticks();
	if (ticks >= deadline)
		return EINVAL;
	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a stopped runtime and a connection already in progress. */
	if (!controller->runtime_active ||
	    controller->close_pending ||
	    controller->quarantined ||
	    controller->recovery_pending ||
	    controller->recovery_running ||
	    !controller->tx_ring_allocated ||
	    controller->association_initialized ||
	    controller->connection_generation != 0U) {
		/* Logs the refusal, at most once a second. */
		log = ax211_pci_log_rejection(controller);
		if (log) {
			kern_logf("intel-ax211: connect admission rejected "
				   "runtime=%u "
				   "quarantined=%u recovery=%u/%u tx-ring=%u "
				   "association=%u "
				   "generation=%u\n",
				   controller->runtime_active,
				   controller->quarantined,
				   controller->recovery_pending,
				   controller->recovery_running,
				   controller->tx_ring_allocated,
				   controller->association_initialized,
				   (unsigned)controller->connection_generation);
		}

		/* A running controller is busy; any other is down. */
		if (controller->runtime_active)
			result = EBUSY;
		else
			result = ENETDOWN;
		mutex_unlock(&controller->lifecycle_lock);

		/* Reports the refusal. */
		return result;
	}

	/* Builds the profile and describes the pinned command table. */
	kern_memset(&profile, 0, sizeof(profile));
	result = ax211_pci_assoc_profile(controller, bss, generation, &profile);
	kern_memset(&table, 0, sizeof(table));
	if (result == 0) {
		table.bytes = controller->runtime_start.command_version_bytes;
		table.count = INTEL_AX211_PROTOCOL_API89_COMMAND_COUNT;
		result = ax211_pci_clock_us(controller, &now);
	}

	/* Records the connection and starts its key state. */
	if (result == 0) {
		controller->connection_generation = generation;
		controller->control_deadline_ticks = deadline;

		/* Starts the key state; a failure is an I/O failure. */
		result = drv_intel_ax211_key_state_init(
			&controller->keys, controller->hardware_epoch,
			generation);
		if (result == INTEL_AX211_KEY_OK)
			controller->keys_initialized = 1U;
		else
			result = EIO;
	}

	/* Runs the authentication stage of the association session. */
	if (result == 0) {
		/* Begins the session and drives it. */
		result = drv_intel_ax211_assoc_begin(
			&controller->association, &table, &profile, generation,
			controller->hardware_epoch, now);
		if (result == INTEL_AX211_ASSOC_OK) {
			controller->association_initialized = 1U;
			result = drv_intel_ax211_assoc_drive(
				&controller->association, &ax211_assoc_ops,
				controller);
		}

		/* Logs a session that stopped before the stage ended. */
		if (result != INTEL_AX211_ASSOC_AUTH_READY) {
			kern_logf(
				"intel-ax211: association drive stopped "
				"result=%d "
				"phase=%u step=%u failure=%d resources=%08x\n",
				result, controller->association.phase,
				controller->association.step,
				controller->association.failure,
				controller->association.resources);
		}

		result = ax211_pci_assoc_result_errno(result);
	}

	/*
	 * Undoes a failed start: a rolled-back session clears the connection,
	 * and any other one fails the keys closed.
	 */
	if (result != 0 && controller->association_initialized) {
		/* Clears a connection whose session rolled back cleanly. */
		if (controller->association.phase ==
			    INTEL_AX211_ASSOC_PHASE_IDLE &&
		    !controller->frame_ring.enabled &&
		    controller->frame_ring.pending_count == 0U) {
			ax211_pci_connection_clear(controller);
		} else {
			result = ax211_pci_key_fail_closed(controller, result);
		}
	} else if (result != 0 && controller->connection_generation != 0U) {
		ax211_pci_connection_clear(controller);
	}

	/* Logs the outcome. */
	if (result != 0) {
		kern_logf("intel-ax211: association start failed result=%d "
			   "phase=%u step=%u failure=%d resources=%08x\n",
			   result, controller->association.phase,
			   controller->association.step,
			   controller->association.failure,
			   controller->association.resources);
	} else {
		kern_logf("intel-ax211: association hardware ready "
			   "generation=%u\n",
			   (unsigned)generation);
	}

	/* Clears the profile and the table description. */
	ax211_pci_scrub(&profile, sizeof(profile));
	ax211_pci_scrub(&table, sizeof(table));

	mutex_unlock(&controller->lifecycle_lock);

	/* Failed. */
	if (result != 0)
		return result;

	/* Succeeded: the station may authenticate. */
	return 0;
}

/* Rolls back the connection of generation. */
static int
ax211_radio_disconnect(
	void *context,
	uint64_t generation)
{
	struct ax211_pci_controller *controller;
	int log;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/* Refuses an unset generation. */
	if (generation == 0U)
		return EINVAL;
	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a recovery that has not started; otherwise rolls back. */
	if (controller->recovery_pending && !controller->recovery_running) {
		result = ENETDOWN;
	} else {
		/* Rolls the session back under the lifecycle deadline. */
		controller->control_deadline_ticks =
			ax211_pci_lifecycle_deadline();
		result = ax211_pci_assoc_rollback(controller, generation);

		/* Logs a failed rollback, at most once a second. */
		if (result != 0) {
			log = ax211_pci_log_rejection(controller);
			if (log) {
				kern_logf("intel-ax211: association rollback "
					   "failed generation=%u result=%d "
					   "phase=%u step=%u failure=%d\n",
					   (unsigned)generation, result,
					   controller->association.phase,
					   controller->association.step,
					   controller->association.failure);
			}
		}

		/* A failure of a live connection fails the keys closed. */
		if (result != 0 &&
		    result != ESTALE &&
		    controller->runtime_active) {
			result = ax211_pci_key_fail_closed(controller, result);
		}
	}

	mutex_unlock(&controller->lifecycle_lock);

	/* Failed. */
	if (result != 0)
		return result;

	/* Succeeded. */
	return 0;
}

/* Sends one management frame of the connection of generation. */
static int
ax211_radio_management_transmit(
	void *context,
	uint64_t generation,
	const uint8_t *frame,
	size_t length,
	uint64_t deadline)
{
	struct ax211_pci_controller *controller;
	struct intel_ax211_tx_request request;
	unsigned attempt;
	int log;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/* Refuses an unset generation, an empty frame and an unset deadline. */
	if (generation == 0U || frame == NULL || length == 0U || deadline == 0U)
		return EINVAL;
	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a stopped runtime, a disabled ring and another connection. */
	if (!controller->runtime_active ||
	    controller->close_pending ||
	    controller->recovery_pending ||
	    controller->recovery_running ||
	    !controller->frame_ring.enabled ||
	    controller->connection_generation != generation) {
		/* Logs the refusal, at most once a second. */
		log = ax211_pci_log_rejection(controller);
		if (log) {
			kern_logf("intel-ax211: management TX rejected "
				   "runtime=%u "
				   "recovery=%u/%u ring=%u "
				   "expected-generation=%u generation=%u\n",
				   controller->runtime_active,
				   controller->recovery_pending,
				   controller->recovery_running,
				   controller->frame_ring.enabled,
				   (unsigned)controller->connection_generation,
				   (unsigned)generation);
		}

		result = ENETDOWN;
	} else {
		/* Describes the frame. */
		kern_memset(&request, 0, sizeof(request));
		request.connection_generation = generation;
		request.frame = frame;
		request.length = length;
		request.frame_class = INTEL_AX211_TX_FRAME_MANAGEMENT;
		if (controller->selected_metadata.channel > 14U)
			request.band_5ghz = 1U;
		else
			request.band_5ghz = 0U;

		/* Retries with a fresh cookie while the cookie is in use. */
		result = EEXIST;
		attempt = 0U;
		while (attempt < INTEL_AX211_TX_RING_DESCRIPTOR_SLOTS &&
		       result == EEXIST) {
			/*
			 * Takes the next cookie, counting down and skipping 0.
			 */
			if (controller->next_management_cookie == 0U)
				controller->next_management_cookie = UINT64_MAX;
			request.cookie = controller->next_management_cookie--;
			result = ax211_pci_tx_submit(controller, &request,
						     deadline, 0);
			attempt++;
		}
	}

	mutex_unlock(&controller->lifecycle_lock);

	/* Failed. */
	if (result != 0)
		return result;

	/* Succeeded: the frame is queued. */
	return 0;
}

/*
 * Runs the association stage of the session with the access point's answer,
 * then sets the multicast filter and the power configuration.
 */
static int
ax211_radio_association_set(
	void *context,
	uint64_t generation,
	const uint8_t bssid[6],
	uint16_t aid,
	uint64_t deadline)
{
	struct ax211_pci_controller *controller;
	struct intel_ax211_assoc_update update;
	uint64_t now;
	uint64_t ticks;
	int same;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/*
	 * Refuses an unset generation, a missing BSSID, an association id out
	 * of range and an unset deadline.
	 */
	if (generation == 0U ||
	    bssid == NULL ||
	    aid == 0U ||
	    aid > 2007U ||
	    deadline == 0U) {
		return EINVAL;
	}

	/* Refuses a deadline that has passed. */
	ticks = clock_ticks();
	if (ticks >= deadline)
		return EINVAL;
	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a stopped runtime, a missing session and another network. */
	result = 0;
	if (!controller->runtime_active ||
	    controller->close_pending ||
	    controller->recovery_pending ||
	    controller->recovery_running ||
	    !controller->association_initialized ||
	    !controller->selected_bss_valid) {
		result = ENETDOWN;
	} else if (controller->connection_generation != generation) {
		result = ESTALE;
	} else {
		/* Refuses another BSSID. */
		same = kern_memcmp(bssid, controller->selected_metadata.bssid,
				   6U);
		if (same != 0)
			result = ESTALE;
	}

	/* Runs the association stage and configures the association. */
	if (result == 0) {
		/* Describes the access point's answer. */
		kern_memset(&update, 0, sizeof(update));
		update.association_id = aid;
		update.dtim_period = controller->selected_metadata.dtim_period;
		update.dtim_count = controller->selected_metadata.dtim_count;
		update.beacon_arrive_time =
			controller->selected_metadata.beacon_arrive_time;
		update.beacon_timestamp = controller->selected_metadata.beacon_timestamp;

		/* Runs the stage under the caller's deadline. */
		result = ax211_pci_clock_us(controller, &now);
		if (result == 0) {
			/* Begins the stage and drives it. */
			controller->control_deadline_ticks = deadline;
			result = drv_intel_ax211_assoc_begin_update(
				&controller->association, &update, generation,
				controller->hardware_epoch, now);
			if (result == INTEL_AX211_ASSOC_OK) {
				result = drv_intel_ax211_assoc_drive(
					&controller->association,
					&ax211_assoc_ops, controller);
			}

			/* Requires the session to end associated. */
			result = ax211_pci_assoc_result_errno(result);
			if (result == 0 &&
			    controller->association.phase !=
				    INTEL_AX211_ASSOC_PHASE_ASSOCIATED) {
				result = EIO;
			}

			/* Sets the multicast filter. */
			if (result == 0) {
				/* Configures it and logs a failure. */
				result = ax211_pci_mcast_filter_configure(
					controller, deadline);
				if (result != 0) {
					kern_logf(
						"intel-ax211: post-association "
						"multicast configuration "
						"failed (%d)\n",
						result);
				}
			}

			/* Sets the power configuration. */
			if (result == 0) {
				/* Configures it and logs a failure. */
				result = ax211_pci_mac_power_configure(
					controller, deadline);
				if (result != 0) {
					kern_logf(
						"intel-ax211: post-association "
						"power configuration failed "
						"(%d)\n",
						result);
				}
			}

			/* A failure fails the keys closed. */
			if (result != 0) {
				result = ax211_pci_key_fail_closed(controller,
								   result);
			}
		}

		/* Clears the answer. */
		ax211_pci_scrub(&update, sizeof(update));
	}

	mutex_unlock(&controller->lifecycle_lock);

	/* Failed. */
	if (result != 0)
		return result;

	/* Succeeded: the station is associated. */
	return 0;
}

/* Rolls back the association of generation. */
static int
ax211_radio_association_clear(
	void *context,
	uint64_t generation,
	uint64_t deadline)
{
	struct ax211_pci_controller *controller;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/* Refuses an unset generation and an unset deadline. */
	if (generation == 0U || deadline == 0U)
		return EINVAL;
	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a recovery that has not started; otherwise rolls back. */
	if (controller->recovery_pending && !controller->recovery_running) {
		result = ENETDOWN;
	} else {
		/* A failure of a live connection fails the keys closed. */
		controller->control_deadline_ticks = deadline;
		result = ax211_pci_assoc_rollback(controller, generation);
		if (result != 0 &&
		    result != ESTALE &&
		    controller->runtime_active) {
			result = ax211_pci_key_fail_closed(controller, result);
		}
	}

	mutex_unlock(&controller->lifecycle_lock);

	/* Failed. */
	if (result != 0)
		return result;

	/* Succeeded: the association is rolled back. */
	return 0;
}

/* Sends one frame of the connection, checking its key when it is encrypted. */
static int
ax211_radio_frame_transmit(
	void *context,
	const struct wlan_radio_tx_request *request)
{
	struct ax211_pci_controller *controller;
	struct intel_ax211_tx_request tx;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/*
	 * Refuses a missing request, unset generation and cookie, an empty
	 * frame and an unset deadline.
	 */
	if (request == NULL ||
	    request->generation == 0U ||
	    request->cookie == 0U ||
	    request->frame == NULL ||
	    request->length == 0U ||
	    request->deadline_ticks == 0U) {
		return EINVAL;
	}
	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a stopped runtime, a disabled ring and another connection. */
	if (!controller->runtime_active ||
	    controller->close_pending ||
	    controller->recovery_pending ||
	    controller->recovery_running ||
	    !controller->frame_ring.enabled) {
		result = ENETDOWN;
	} else if (controller->connection_generation != request->generation) {
		result = ESTALE;
	} else {
		/* Describes the frame. */
		kern_memset(&tx, 0, sizeof(tx));
		tx.connection_generation = request->generation;
		tx.cookie = request->cookie;
		tx.key_generation = request->key_generation;
		tx.packet_number = request->packet_number;
		tx.frame = request->frame;
		tx.length = request->length;
		tx.encrypted = request->encrypted;
		tx.key_index = request->key_index;
		if (controller->selected_metadata.channel > 14U)
			tx.band_5ghz = 1U;
		else
			tx.band_5ghz = 0U;

		/* Translates the frame class; refuses any other. */
		if (request->frame_class == WLAN_RADIO_FRAME_MANAGEMENT)
			tx.frame_class = INTEL_AX211_TX_FRAME_MANAGEMENT;
		else if (request->frame_class == WLAN_RADIO_FRAME_EAPOL)
			tx.frame_class = INTEL_AX211_TX_FRAME_EAPOL;
		else if (request->frame_class == WLAN_RADIO_FRAME_DATA)
			tx.frame_class = INTEL_AX211_TX_FRAME_DATA;
		else
			result = EINVAL;

		/* Checks the key of an encrypted frame and queues the frame. */
		if (request->frame_class == WLAN_RADIO_FRAME_MANAGEMENT ||
		    request->frame_class == WLAN_RADIO_FRAME_EAPOL ||
		    request->frame_class == WLAN_RADIO_FRAME_DATA) {
			/* Refuses a key that does not match the frame. */
			result = 0;
			if (request->encrypted) {
				/* A frame without a key state has no key. */
				if (controller->keys_initialized) {
					result = drv_intel_ax211_key_state_tx_validate(
						&controller->keys,
						request->generation,
						request->key_generation,
						request->key_index,
						request->packet_number,
						controller->hardware_epoch);
				} else {
					result = INTEL_AX211_KEY_MISSING;
				}
				result = ax211_pci_key_result_errno(result);
			}

			/* Queues the frame. */
			if (result == 0) {
				result = ax211_pci_tx_submit(
					controller, &tx,
					request->deadline_ticks, 1);
			}
		}

		/* Clears the description. */
		ax211_pci_scrub(&tx, sizeof(tx));
	}

	mutex_unlock(&controller->lifecycle_lock);

	/* Failed. */
	if (result != 0)
		return result;

	/* Succeeded: the frame is queued. */
	return 0;
}

/*
 * Installs one pairwise or group key, and activates the first complete pair
 * of the initial handshake.
 */
static int
ax211_radio_key_install(
	void *context,
	const struct wlan_radio_key_request *request)
{
	struct ax211_pci_controller *controller;
	struct ax211_pci_staged_key *staged;
	struct intel_ax211_key_request key;
	uint8_t index;
	uint64_t group_generation;
	uint64_t now;
	int active_before;
	int active;
	int valid;
	int state_result;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/*
	 * Refuses a missing request, unset generations and an unset deadline.
	 */
	if (request == NULL ||
	    request->generation == 0U ||
	    request->key_generation == 0U ||
	    request->deadline_ticks == 0U) {
		return EINVAL;
	}

	/* Refuses a deadline that has passed. */
	now = clock_ticks();
	if (now >= request->deadline_ticks)
		return EINVAL;

	/*
	 * Refuses another kind, an index out of range, and a pairwise key at any
	 * index but 0.
	 */
	if ((request->kind != WLAN_RADIO_KEY_PAIRWISE &&
	     request->kind != WLAN_RADIO_KEY_GROUP) ||
	    request->key_index >= INTEL_AX211_KEY_INDEX_LIMIT ||
	    (request->kind == WLAN_RADIO_KEY_PAIRWISE &&
	     request->key_index != 0U)) {
		return EINVAL;
	}
	mutex_lock(&controller->lifecycle_lock);

	/*
	 * Refuses a stopped runtime, a stale connection and a foreign address.
	 */
	kern_memset(&key, 0, sizeof(key));
	result = 0;
	if (!controller->runtime_active ||
	    controller->close_pending ||
	    controller->recovery_pending ||
	    controller->recovery_running ||
	    !controller->keys_initialized) {
		result = ENETDOWN;
	} else if (controller->connection_generation != request->generation) {
		result = ESTALE;
	} else {
		/*
		 * Refuses a request whose addresses are not the connection's.
		 */
		valid = ax211_pci_key_request_address_valid(controller, request);
		if (!valid)
			result = EINVAL;
	}

	/* Stages, programs and activates the key. */
	if (result == 0) {
		/* Copies the request into the private key. */
		key.connection_generation = request->generation;
		key.key_generation = request->key_generation;
		key.receive_packet_number = request->receive_packet_number;
		key.key_index = request->key_index;
		if (request->kind == WLAN_RADIO_KEY_PAIRWISE)
			key.kind = INTEL_AX211_KEY_PAIRWISE;
		else
			key.kind = INTEL_AX211_KEY_GROUP_KEY;
		kern_memcpy(key.key, request->key, sizeof(key.key));

		/* Selects the key's staging slot. */
		if (key.kind == INTEL_AX211_KEY_PAIRWISE)
			staged = &controller->staged_pairwise_key;
		else
			staged = &controller->staged_group_key[key.key_index];
		active_before = ax211_pci_keys_have_active(controller);

		/* A key that is already active needs nothing more. */
		if ((key.kind == INTEL_AX211_KEY_PAIRWISE &&
		     controller->keys.active_pairwise == key.key_generation) ||
		    (key.kind == INTEL_AX211_KEY_GROUP_KEY &&
		     controller->keys.active_group[key.key_index] ==
			     key.key_generation)) {
			result = 0;
		} else {
			/* Stages the key and records it as installed. */
			result = ax211_pci_staged_key_store(staged, &key);
			if (result == 0) {
				state_result = drv_intel_ax211_key_state_installed(
					&controller->keys, &key,
					controller->hardware_epoch);
			} else {
				state_result = INTEL_AX211_KEY_INVALID;
			}

			/* A repeat of an installed key is not a failure. */
			if (result == 0 &&
			    state_result != INTEL_AX211_KEY_OK &&
			    state_result != INTEL_AX211_KEY_DUPLICATE) {
				result = ax211_pci_key_result_errno(
					state_result);
			}

			/*
			 * Initial keys must be live before common sends its
			 * first M4. Replacement keys remain private until
			 * keys_activate().
			 */
			if (result == 0 && !active_before) {
				result = ax211_pci_staged_key_program(
					controller, staged,
					request->deadline_ticks);
			}
		}

		/*
		 * The initial common handshake installs pairwise then group
		 * keys but has no separate activation callback.  Activate that
		 * first complete pair only after both firmware ACKs.
		 */
		active = 1;
		if (result == 0)
			active = ax211_pci_keys_have_active(controller);
		if (result == 0 &&
		    !active &&
		    controller->staged_pairwise_key.valid &&
		    controller->staged_pairwise_key.programmed) {
			/* Finds the first programmed group key. */
			group_generation = 0U;
			index = 0U;
			while (index < INTEL_AX211_KEY_INDEX_LIMIT &&
			       group_generation == 0U) {
				/* Takes a programmed group key's generation. */
				if (controller->staged_group_key[index].valid &&
				    controller->staged_group_key[index]
					    .programmed) {
					group_generation =
						controller
							->staged_group_key
								[index]
							.request.key_generation;
				}

				index++;
			}

			/* Activates the pair once a group key is programmed. */
			if (group_generation != 0U) {
				/* Records the pair as the active keys. */
				state_result = drv_intel_ax211_key_state_activate(
					&controller->keys,
					request->generation,
					controller->staged_pairwise_key
						.request.key_generation,
					group_generation,
					controller->hardware_epoch);
				result = ax211_pci_key_result_errno(
					state_result);

				/* Forgets the staged keys of an active pair. */
				if (result == 0) {
					ax211_pci_staged_key_clear(
						&controller
							 ->staged_pairwise_key);
					for (index = 0U;
					     index <
					     INTEL_AX211_KEY_INDEX_LIMIT;
					     index++) {
						ax211_pci_staged_key_clear(
							&controller->staged_group_key
								 [index]);
					}
				}
			}
		}

		/* A failure after programming fails the keys closed. */
		if (result != 0 && staged != NULL && staged->programmed)
			result = ax211_pci_key_fail_closed(controller, result);
	}

	/* Clears the private copy of the key. */
	ax211_pci_scrub(&key, sizeof(key));

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	if (result != 0)
		return result;

	/* Succeeded: the key is installed. */
	return 0;
}

/* Removes one pairwise or group key from the firmware and the key state. */
static int
ax211_radio_key_delete(
	void *context,
	uint64_t generation,
	enum wlan_radio_key_kind kind,
	uint8_t key_index,
	uint64_t key_generation,
	uint64_t deadline)
{
	struct ax211_pci_controller *controller;
	struct ax211_pci_staged_key *staged;
	enum intel_ax211_key_kind private_kind;
	uint8_t command[INTEL_AX211_KEY_COMMAND_SIZE];
	int active_match;
	int programmed;
	int retired_match;
	int remove;
	int state_result;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/*
	 * Refuses unset generations and deadline, another kind, an index out of
	 * range, and a pairwise key at any index but 0.
	 */
	if (generation == 0U ||
	    key_generation == 0U ||
	    deadline == 0U ||
	    (kind != WLAN_RADIO_KEY_PAIRWISE && kind != WLAN_RADIO_KEY_GROUP) ||
	    key_index >= INTEL_AX211_KEY_INDEX_LIMIT ||
	    (kind == WLAN_RADIO_KEY_PAIRWISE && key_index != 0U)) {
		return EINVAL;
	}

	/* Translates the kind. */
	if (kind == WLAN_RADIO_KEY_PAIRWISE)
		private_kind = INTEL_AX211_KEY_PAIRWISE;
	else
		private_kind = INTEL_AX211_KEY_GROUP_KEY;
	kern_memset(command, 0, sizeof(command));
	mutex_lock(&controller->lifecycle_lock);

	/*
	 * A proven global stop retired every key, including the core's pending
	 * inverse.
	 */
	if (controller->session_stopped) {
		result = 0;
	} else if (!controller->runtime_active ||
		   (controller->recovery_pending &&
		    !controller->recovery_running) ||
		   !controller->keys_initialized) {
		result = ENETDOWN;
	} else if (controller->connection_generation != generation) {
		result = ESTALE;
	} else {
		/* Selects the key's staging slot. */
		if (private_kind == INTEL_AX211_KEY_PAIRWISE)
			staged = &controller->staged_pairwise_key;
		else
			staged = &controller->staged_group_key[key_index];

		/* Notes whether the firmware holds the key as staged. */
		programmed = 0;
		if (staged->valid &&
		    staged->request.key_generation == key_generation &&
		    staged->programmed) {
			programmed = 1;
		}

		/* Notes whether the key is the active one. */
		active_match = 0;
		if (private_kind == INTEL_AX211_KEY_PAIRWISE) {
			if (controller->keys.active_pairwise == key_generation)
				active_match = 1;
		} else if (controller->keys.active_group[key_index] ==
			   key_generation) {
			active_match = 1;
		}

		/* Notes whether the key is the one an activation retired. */
		retired_match = 0;
		if (private_kind == INTEL_AX211_KEY_PAIRWISE) {
			if (controller->retired_pairwise_key_generation ==
			    key_generation) {
				retired_match = 1;
			}
		} else if (controller->retired_group_key_generation
				   [key_index] == key_generation) {
			retired_match = 1;
		}

		/*
		 * The firmware holds the key when it is programmed, active, or
		 * a retired group key still to be removed.
		 */
		remove = 0;
		if (programmed ||
		    active_match ||
		    (retired_match &&
		     private_kind == INTEL_AX211_KEY_GROUP_KEY &&
		     controller->retired_group_key_remove[key_index])) {
			remove = 1;
		}

		/* Encodes the removal of a key the firmware holds. */
		result = 0;
		if (remove) {
			state_result = drv_intel_ax211_key_remove_encode(
				generation, key_generation, private_kind,
				key_index, command);
			result = ax211_pci_key_result_errno(state_result);
		}

		/* Sends the removal. */
		if (result == 0 && remove) {
			result = ax211_pci_key_command(controller, command,
						       deadline);
		}

		/* Records the removal of a staged or active key. */
		if (result == 0 &&
		    ((staged->valid &&
		      staged->request.key_generation == key_generation) ||
		     active_match)) {
			state_result = drv_intel_ax211_key_state_removed(
				&controller->keys, generation, private_kind,
				key_index, key_generation,
				controller->hardware_epoch);
			result = ax211_pci_key_result_errno(state_result);
		}

		/* Forgets the staged key. */
		if (result == 0 &&
		    staged->valid &&
		    staged->request.key_generation == key_generation) {
			ax211_pci_staged_key_clear(staged);
		}

		/* Forgets the retired key. */
		if (result == 0 && retired_match) {
			/* Clears the retired generation of the key's kind. */
			if (private_kind == INTEL_AX211_KEY_PAIRWISE) {
				controller->retired_pairwise_key_generation =
					0U;
			} else {
				controller->retired_group_key_generation
					[key_index] = 0U;
				controller
					->retired_group_key_remove[key_index] =
					0U;
			}
		}

		/* A failure on a key the firmware may hold fails closed. */
		if (result != 0 &&
		    (programmed || active_match || retired_match)) {
			result = ax211_pci_key_fail_closed(controller, result);
		}
	}

	/* Clears the encoded command. */
	drv_intel_ax211_key_command_scrub(command);

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	if (result != 0)
		return result;

	/* Succeeded: the key is removed. */
	return 0;
}

/*
 * Activates a staged pairwise and group key pair, retiring the keys it
 * replaces.
 */
static int
ax211_radio_keys_activate(
	void *context,
	uint64_t generation,
	uint64_t pairwise_key_generation,
	uint64_t group_key_generation,
	uint64_t deadline)
{
	struct ax211_pci_controller *controller;
	struct ax211_pci_staged_key *group;
	uint64_t old_group[INTEL_AX211_KEY_INDEX_LIMIT];
	uint64_t old_pairwise;
	uint64_t now;
	uint8_t group_index;
	uint8_t index;
	int command_crossed;
	int state_result;
	int result;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;

	/* Refuses unset generations and an unset deadline. */
	if (generation == 0U ||
	    pairwise_key_generation == 0U ||
	    group_key_generation == 0U ||
	    deadline == 0U) {
		return EINVAL;
	}

	/* Refuses a deadline that has passed. */
	now = clock_ticks();
	if (now >= deadline)
		return EINVAL;
	mutex_lock(&controller->lifecycle_lock);

	/* Refuses a stopped runtime and a stale connection. */
	if (!controller->runtime_active ||
	    controller->close_pending ||
	    controller->recovery_pending ||
	    controller->recovery_running ||
	    !controller->keys_initialized) {
		result = ENETDOWN;
	} else if (controller->connection_generation != generation) {
		result = ESTALE;
	} else {
		/* Finds the staged group key of the generation. */
		group = NULL;
		group_index = 0U;
		while (group_index < INTEL_AX211_KEY_INDEX_LIMIT &&
		       group == NULL) {
			/* Takes the slot that holds the generation. */
			if (controller->staged_group_key[group_index].valid &&
			    controller->staged_group_key[group_index]
					    .request.key_generation ==
				    group_key_generation) {
				group = &controller->staged_group_key
						 [group_index];
			} else {
				group_index++;
			}
		}

		/* Remembers the keys the activation replaces. */
		old_pairwise = controller->keys.active_pairwise;
		kern_memcpy(old_group, controller->keys.active_group,
		       sizeof(old_group));
		command_crossed = 0;

		/* Programs a group key the firmware does not hold yet. */
		result = 0;
		if (group != NULL && !group->programmed) {
			command_crossed = 1;
			result = ax211_pci_staged_key_program(controller, group,
							      deadline);
		}

		/* Programs a pairwise key the firmware does not hold yet. */
		if (result == 0 &&
		    controller->staged_pairwise_key.valid &&
		    controller->staged_pairwise_key.request.key_generation ==
			    pairwise_key_generation &&
		    !controller->staged_pairwise_key.programmed) {
			command_crossed = 1;
			result = ax211_pci_staged_key_program(
				controller, &controller->staged_pairwise_key,
				deadline);
		}

		/* Records the pair as the active keys. */
		if (result == 0) {
			state_result = drv_intel_ax211_key_state_activate(
				&controller->keys, generation,
				pairwise_key_generation,
				group_key_generation,
				controller->hardware_epoch);
			result = ax211_pci_key_result_errno(state_result);
		}

		/* Retires the replaced keys and forgets the staged ones. */
		if (result == 0) {
			/* Retires a replaced pairwise key. */
			if (old_pairwise != 0U &&
			    old_pairwise != pairwise_key_generation) {
				controller->retired_pairwise_key_generation =
					old_pairwise;
			}

			/* Retires each replaced group key. */
			for (index = 0U; index < INTEL_AX211_KEY_INDEX_LIMIT;
			     index++) {
				/*
				 * A key in another slot must still be removed.
				 */
				if (old_group[index] != 0U &&
				    old_group[index] != group_key_generation) {
					controller->retired_group_key_generation
						[index] = old_group[index];
					if (index != group_index) {
						controller->retired_group_key_remove
							[index] = 1U;
					} else {
						controller->retired_group_key_remove
							[index] = 0U;
					}
				}
			}

			/* Forgets the staged keys. */
			ax211_pci_staged_key_clear(
				&controller->staged_pairwise_key);
			for (index = 0U; index < INTEL_AX211_KEY_INDEX_LIMIT;
			     index++) {
				ax211_pci_staged_key_clear(
					&controller->staged_group_key[index]);
			}
		} else if (command_crossed) {
			/*
			 * A failure after a key command fails the keys closed.
			 */
			result = ax211_pci_key_fail_closed(controller, result);
		}

		/* Clears the copy of the replaced keys. */
		ax211_pci_scrub(old_group, sizeof(old_group));
	}

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	if (result != 0)
		return result;

	/* Succeeded: the pair is active. */
	return 0;
}

/*
 * The common work pin retains this context; hardware access stays serialized.
 */
static int
ax211_radio_stop_retry(
	void *context)
{
	struct ax211_pci_controller *controller;
	int error;

	/* Retries the close under the lifecycle lock. */
	controller = context;
	mutex_lock(&controller->lifecycle_lock);

	error = ax211_pci_close_locked(controller);

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Starts a new epoch a recovery asked for (BUG-158).
 *
 * Runs on the WLAN retirement thread with the station's work pin held and no
 * common lease, so the close inside the open can join common retirement.
 */
static int
ax211_radio_restart(
	void *context)
{
	struct ax211_pci_controller *controller;
	unsigned attempt;
	int error;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;
	mutex_lock(&controller->lifecycle_lock);

	/*
	 * A request the user's close or open replaced, or a detach, needs
	 * nothing.
	 */
	if (!controller->reopen_pending ||
	    !controller->net_opened ||
	    controller->detaching) {
		controller->reopen_pending = 0U;
		mutex_unlock(&controller->lifecycle_lock);

		/* Succeeded: nothing was owed. */
		return 0;
	}

	/* Counts this restart against the bound before it runs. */
	controller->reopen_pending = 0U;
	controller->reopen_attempts++;
	attempt = controller->reopen_attempts;

	/* Retires the stopped epoch and starts a new one, as an open does. */
	error = ax211_pci_open_locked(controller, 1);
	kern_logf("intel-ax211: restarted after recovery attempt=%u error=%d\n",
		   attempt, error);

	/*
	 * A failed start is tried again on a later pass, within the same bound.
	 */
	if (error != 0 && error != ECANCELED)
		ax211_pci_reopen_request_locked(controller);

	/* Lets the poll and the timers in again. */
	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure; a later pass or the user's cycle tries again. */
	if (error != 0)
		return error;

	/* Succeeded: scans and joins are admitted again. */
	return 0;
}

/* Stops the session and quarantines a controller whose stop failed. */
static int
ax211_radio_quiesce(
	void *context)
{
	struct ax211_pci_controller *controller;
	int error;

	/* Refuses a missing controller. */
	controller = context;
	if (controller == NULL)
		return ENODEV;
	mutex_lock(&controller->lifecycle_lock);

	/* Stops the session; a failed stop quarantines the controller. */
	error = ax211_pci_session_stop(controller);
	if (error != 0)
		controller->quarantined = 1U;

	mutex_unlock(&controller->lifecycle_lock);

	/* Reports the failure. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Erases temporary and terminal controller-owned identity/state bytes. */
static void
ax211_pci_scrub(
	void *memory,
	size_t length)
{
	volatile uint8_t *byte;

	/* Clears every byte through a volatile pointer. */
	byte = memory;
	while (length != 0U) {
		*byte++ = 0U;
		length--;
	}
}
/* End consolidated pci-intel-ax211.c. */
