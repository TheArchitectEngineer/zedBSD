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
