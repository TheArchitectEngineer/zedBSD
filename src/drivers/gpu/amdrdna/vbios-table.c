/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Reading the video BIOS image of an RDNA board.
 *
 * The image is found in the ACPI table the firmware fills with the images
 * of its display devices, or read from the board's flash.  Its header
 * leads to two lists: the data tables (the firmware description, the VRAM
 * the firmware keeps, the power table and the board's power-delivery
 * table) and the command routines that later set the display up.  Only
 * the headers and the fields the driver uses are read; every offset is
 * checked against the image's length before it is followed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/*
 * The ACPI table of video BIOS images: where the offset of its first image
 * record is and the size of the table's fixed part, then the layout of one
 * image record (bus, device, function, vendor, device ID, image length)
 * and its size, after which the image follows.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/atombios.h (UEFI_ACPI_VFCT,
 * VFCT_IMAGE_HEADER, GOP_VBIOS_CONTENT).
 */
#define ACPI_IMAGE_OFFSET_AT		0x34U
#define ACPI_FIXED_BYTES		0x4cU
#define ACPI_RECORD_BUS_AT		0x00U
#define ACPI_RECORD_DEVICE_AT		0x04U
#define ACPI_RECORD_FUNCTION_AT		0x08U
#define ACPI_RECORD_VENDOR_AT		0x0cU
#define ACPI_RECORD_PRODUCT_AT		0x0eU
#define ACPI_RECORD_LENGTH_AT		0x18U
#define ACPI_RECORD_BYTES		0x1cU

/*
 * The start of an image: the two option-ROM bytes, the byte that gives the
 * image's length in 512-byte units, and the word that points at the image's
 * header, whose signature lies 4 bytes in.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_bios.c (AMD_IS_VALID_VBIOS,
 * AMD_VBIOS_LENGTH, the header pointer at 0x48 in check_atom_bios) and
 * drivers/gpu/drm/amd/include/atomfirmware.h
 * (struct atom_rom_header_v2_2).
 */
#define IMAGE_MARK_FIRST		0x55U
#define IMAGE_MARK_SECOND		0xaaU
#define IMAGE_BLOCKS_AT			2U
#define IMAGE_BLOCK_BYTES		512U
#define IMAGE_HEADER_POINTER_AT		0x48U
#define HEADER_SIGNATURE_AT		4U
#define HEADER_COMMAND_LIST_AT		0x1eU
#define HEADER_DATA_LIST_AT		0x20U
#define HEADER_BYTES			0x22U

/*
 * The common header every table and routine starts with (its length word,
 * its format byte and its content byte), the list entries of the data
 * tables the driver reads (board power delivery 2, firmware 4, VRAM kept
 * by firmware 11, power 15), and the list entries of the command routines
 * it names (post 0, encoder 4, pixel clock 12, pipe power 13, timing
 * enable 35, display clock 46, transmitter 76, AUX 78).  A routine's
 * attribute word follows its common header: workspace bytes in bits 7:0,
 * parameter bytes in bits 14:8.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/atomfirmware.h
 * (struct atom_common_table_header,
 * struct atom_master_list_of_data_tables_v2_1,
 * struct atom_master_list_of_command_functions_v2_1,
 * struct atom_function_attribute).
 */
#define COMMON_LENGTH_AT		0U
#define COMMON_FORMAT_AT		2U
#define COMMON_CONTENT_AT		3U
#define COMMON_BYTES			4U
#define DATA_BOARD_POWER		2U
#define DATA_FIRMWARE			4U
#define DATA_FIRMWARE_RESERVE		11U
#define DATA_POWER			15U
#define ROUTINE_ATTRIBUTE_AT		4U
#define ROUTINE_WORK_MASK		0x00ffU
#define ROUTINE_PARAMETER_MASK		0x7f00U
#define ROUTINE_PARAMETER_SHIFT		8U

/*
 * The firmware description: the capability word, the boot engine and
 * memory clocks (content before 5), the first scratch register, the power
 * table ID (content 3 on), and the extra VRAM the firmware keeps at the top
 * (content 4 on).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/atomfirmware.h
 * (struct atom_firmware_info_v3_1, v3_3, v3_4, v3_5) and
 * drivers/gpu/drm/amd/amdgpu/amdgpu_atomfirmware.c
 * (amdgpu_atomfirmware_get_fw_reserved_fb_size).
 */
#define FIRMWARE_ENGINE_CLOCK_AT	8U
#define FIRMWARE_MEMORY_CLOCK_AT	12U
#define FIRMWARE_CAPABILITY_AT		16U
#define FIRMWARE_SCRATCH_AT		24U
#define FIRMWARE_POWER_TABLE_ID_AT	60U
#define FIRMWARE_RESERVED_TOP_AT	84U
#define FIRMWARE_CLOCKS_BEFORE		5U
#define FIRMWARE_POWER_TABLE_ID_FROM	3U
#define FIRMWARE_RESERVED_TOP_FROM	4U

/*
 * The table of VRAM the firmware keeps: layout 2.1 (a start word whose top
 * two bits are flags, the firmware's KiB and the driver's KiB, where only
 * the flag value 2 means a VRAM reservation) and layout 2.2 (the
 * firmware's start and KiB, the driver's start and KiB).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/atomfirmware.h
 * (struct vram_usagebyfirmware_v2_1, struct vram_usagebyfirmware_v2_2) and
 * drivers/gpu/drm/amd/include/atombios.h
 * (ATOM_VRAM_OPERATION_FLAGS_SHIFT,
 * ATOM_VRAM_BLOCK_SRIOV_MSG_SHARE_RESERVATION).
 */
#define RESERVE_START_AT		4U
#define RESERVE_FIRMWARE_AT		8U
#define RESERVE_DRIVER_AT		10U
#define RESERVE_DRIVER_START_AT		12U
#define RESERVE_DRIVER_KIB_AT		16U
#define RESERVE_FLAGS_SHIFT		30U
#define RESERVE_FLAG_RESERVATION	2U
#define RESERVE_ADDRESS_MASK		0x3fffffffU

/*
 * The power table's header: the table revision byte, its length word and
 * the software shutdown temperature in degrees Celsius.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/pm/swsmu/inc/smu_v11_0_pptable.h
 * (struct smu_11_0_powerplay_table, packed).
 */
#define POWER_REVISION_AT		4U
#define POWER_LENGTH_AT			5U
#define POWER_SHUTDOWN_AT		32U

/*
 * The list entries of the command routines named by
 * enum amdrdna_vbios_command, in its order.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/atomfirmware.h
 * (struct atom_master_list_of_command_functions_v2_1: asic_init,
 * digxencodercontrol, setpixelclock, enabledisppowergating, enablecrtc,
 * setdceclock, dig1transmittercontrol, processauxchanneltransaction).
 */
static const uint16_t command_entries[AMDRDNA_VBIOS_COMMAND_COUNT] = {
	0x00U,
	0x04U,
	0x0cU,
	0x0dU,
	0x23U,
	0x2eU,
	0x4cU,
	0x4eU
};

static int find_table(const uint8_t *image, size_t length, uint16_t list, unsigned entry, uint16_t *offset, uint8_t *format, uint8_t *content);
static void read_firmware(const uint8_t *image, struct amdrdna_vbios_info *info);
static void read_reserve(const uint8_t *image, struct amdrdna_vbios_info *info);
static void read_power(const uint8_t *image, struct amdrdna_vbios_info *info);
static void read_commands(const uint8_t *image, size_t length, struct amdrdna_vbios_info *info);

/*
 * Finds the video BIOS image of one PCI function in the ACPI table of images.
 *
 * Returns 0 with the image's offset and length inside the table, ENOENT
 * when no record names the function, or EINVAL for a table whose records
 * run past its end.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_bios.c (amdgpu_acpi_vfct_bios).
 */
int
amdrdna_vbios_find_in_acpi(
	const uint8_t *table,
	size_t length,
	const struct amdrdna_vbios_owner *owner,
	size_t *image_offset,
	size_t *image_length)
{
	size_t record;
	size_t image;
	uint32_t record_length;
	uint32_t bus;
	uint32_t device;
	uint32_t function;
	uint16_t vendor;
	uint16_t product;
	bool owned;

	/* A table shorter than its fixed part has no records. */
	if (length < ACPI_FIXED_BYTES)
		return EINVAL;

	/* Walks the records from the first one the table points at. */
	record = amdrdna_le32(table, ACPI_IMAGE_OFFSET_AT);
	while (record < length) {
		/* The record's header must lie inside the table. */
		if (record + ACPI_RECORD_BYTES > length)
			return EINVAL;

		/* So must the image that follows it. */
		image = record + ACPI_RECORD_BYTES;
		record_length = amdrdna_le32(table, record + ACPI_RECORD_LENGTH_AT);
		if (record_length > length - image)
			return EINVAL;

		/* Reads the place and the identity the record names. */
		bus = amdrdna_le32(table, record + ACPI_RECORD_BUS_AT);
		device = amdrdna_le32(table, record + ACPI_RECORD_DEVICE_AT);
		function = amdrdna_le32(table, record + ACPI_RECORD_FUNCTION_AT);
		vendor = amdrdna_le16(table, record + ACPI_RECORD_VENDOR_AT);
		product = amdrdna_le16(table, record + ACPI_RECORD_PRODUCT_AT);

		/* The record is the function's when it holds an image and its place and identity match. */
		owned = true;
		if (record_length == 0U)
			owned = false;
		if (bus != owner->bus)
			owned = false;
		if (device != owner->device)
			owned = false;
		if (function != owner->function)
			owned = false;
		if (vendor != owner->vendor)
			owned = false;
		if (product != owner->product)
			owned = false;

		/* Hands back the matching image. */
		if (owned) {
			*image_offset = image;
			*image_length = record_length;
			return 0;
		}

		/* Steps to the next record. */
		record = image + record_length;
	}

	/* No record names the function. */
	return ENOENT;
}

/*
 * Checks that bytes are a video BIOS image with the header the driver reads.
 *
 * Returns 0, or EINVAL when the option-ROM mark, the header pointer or the
 * header's signature is wrong.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_bios.c (check_atom_bios).
 */
int
amdrdna_vbios_check(
	const uint8_t *image,
	size_t length)
{
	uint16_t header;
	const uint8_t *signature;

	/* An image shorter than the header pointer has no header. */
	if (length < IMAGE_HEADER_POINTER_AT + 2U)
		return EINVAL;

	/* The option-ROM mark opens every image. */
	if (image[0] != IMAGE_MARK_FIRST)
		return EINVAL;
	if (image[1] != IMAGE_MARK_SECOND)
		return EINVAL;

	/* The header pointer must lead to a header inside the image. */
	header = amdrdna_le16(image, IMAGE_HEADER_POINTER_AT);
	if (header == 0U)
		return EINVAL;
	if ((size_t)header + HEADER_BYTES > length)
		return EINVAL;

	/* The header's signature reads "ATOM" (or the same bytes reversed). */
	signature = image + header + HEADER_SIGNATURE_AT;
	if (signature[0] == 'A' &&
	    signature[1] == 'T' &&
	    signature[2] == 'O' &&
	    signature[3] == 'M')
		return 0;
	if (signature[0] == 'M' &&
	    signature[1] == 'O' &&
	    signature[2] == 'T' &&
	    signature[3] == 'A')
		return 0;

	/* Any other signature is not an image the driver reads. */
	return EINVAL;
}

/*
 * Reports the length an image declares for itself, in bytes; 0 when the
 * bytes do not open with the option-ROM mark.  The caller passes at least
 * the first 3 bytes.
 */
uint32_t
amdrdna_vbios_declared_length(
	const uint8_t *image)
{
	uint32_t blocks;

	/* Bytes without the option-ROM mark declare nothing. */
	if (image[0] != IMAGE_MARK_FIRST)
		return 0U;
	if (image[1] != IMAGE_MARK_SECOND)
		return 0U;

	/* The third byte counts 512-byte blocks. */
	blocks = image[IMAGE_BLOCKS_AT];

	/* Succeeded: the declared length. */
	return blocks * IMAGE_BLOCK_BYTES;
}

/*
 * Reads what the driver uses from a checked video BIOS image.
 *
 * Returns 0 with info filled (a table the image lacks leaves its fields
 * zero), or EINVAL when the image or its two lists are damaged.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/atom.c (amdgpu_atom_parse_data_header,
 * amdgpu_atom_parse_cmd_header).
 */
int
amdrdna_vbios_parse(
	const uint8_t *image,
	size_t length,
	struct amdrdna_vbios_info *info)
{
	uint16_t list_length;
	unsigned index;
	int error;

	/* Starts with nothing read. */
	info->length = (uint32_t)length;
	info->header_offset = 0U;
	info->data_list = 0U;
	info->command_list = 0U;
	info->firmware_table = 0U;
	info->firmware_format = 0U;
	info->firmware_content = 0U;
	info->capability_bits = 0U;
	info->boot_engine_clock_10khz = 0U;
	info->boot_memory_clock_10khz = 0U;
	info->scratch_first = 0U;
	info->power_table_id = 0U;
	info->reserved_top_kib = 0U;
	info->reserve_table = 0U;
	info->reserve_format = 0U;
	info->reserve_content = 0U;
	info->firmware_start_kib = 0U;
	info->firmware_kib = 0U;
	info->driver_start_kib = 0U;
	info->driver_kib = 0U;
	info->power_table = 0U;
	info->power_format = 0U;
	info->power_content = 0U;
	info->power_revision = 0U;
	info->power_bytes = 0U;
	info->shutdown_temperature = 0U;
	info->board_table = 0U;
	info->board_format = 0U;
	info->board_content = 0U;
	for (index = 0U; index < AMDRDNA_VBIOS_COMMAND_COUNT; index++) {
		info->commands[index].offset = 0U;
		info->commands[index].work_bytes = 0U;
		info->commands[index].parameter_bytes = 0U;
	}

	/* The image must carry the header the lists hang off. */
	error = amdrdna_vbios_check(image, length);
	if (error != 0)
		return error;

	/* The header names the two lists. */
	info->header_offset = amdrdna_le16(image, IMAGE_HEADER_POINTER_AT);
	info->command_list = amdrdna_le16(image, info->header_offset + HEADER_COMMAND_LIST_AT);
	info->data_list = amdrdna_le16(image, info->header_offset + HEADER_DATA_LIST_AT);

	/* The data list's header must lie inside the image, and so must the list. */
	if ((size_t)info->data_list + COMMON_BYTES > length)
		return EINVAL;

	/* The list the data list header declares must lie inside the image too. */
	list_length = amdrdna_le16(image, info->data_list + COMMON_LENGTH_AT);
	if ((size_t)info->data_list + list_length > length)
		return EINVAL;

	/* Reads the firmware description when the image has one. */
	error = find_table(image, length, info->data_list, DATA_FIRMWARE, &info->firmware_table, &info->firmware_format, &info->firmware_content);
	if (error == 0)
		read_firmware(image, info);

	/* Reads the VRAM the firmware keeps. */
	error = find_table(image, length, info->data_list, DATA_FIRMWARE_RESERVE, &info->reserve_table, &info->reserve_format, &info->reserve_content);
	if (error == 0)
		read_reserve(image, info);

	/* Reads the power table's header. */
	error = find_table(image, length, info->data_list, DATA_POWER, &info->power_table, &info->power_format, &info->power_content);
	if (error == 0)
		read_power(image, info);

	/* Notes the board's power-delivery table, which only its version is read of now. */
	(void)find_table(image, length, info->data_list, DATA_BOARD_POWER, &info->board_table, &info->board_format, &info->board_content);

	/* Notes the command routines the display needs later. */
	read_commands(image, length, info);

	/* Succeeded: what the image carries is described. */
	return 0;
}

/* Finds one data table from the data list and checks that it lies inside the image. */
static int
find_table(
	const uint8_t *image,
	size_t length,
	uint16_t list,
	unsigned entry,
	uint16_t *offset,
	uint8_t *format,
	uint8_t *content)
{
	uint16_t list_length;
	uint16_t table_length;
	size_t at;

	/* The entry must lie inside the list. */
	list_length = amdrdna_le16(image, list + COMMON_LENGTH_AT);
	at = (size_t)list + COMMON_BYTES + (size_t)entry * 2U;
	if (at + 2U > (size_t)list + list_length)
		return ENOENT;

	/* An entry of zero says the image has no such table. */
	*offset = amdrdna_le16(image, at);
	if (*offset == 0U)
		return ENOENT;

	/* The table's header must lie inside the image. */
	if ((size_t)*offset + COMMON_BYTES > length) {
		*offset = 0U;
		return EINVAL;
	}

	/* So must the table its header declares. */
	table_length = amdrdna_le16(image, *offset + COMMON_LENGTH_AT);
	if ((size_t)*offset + table_length > length) {
		*offset = 0U;
		return EINVAL;
	}

	/* Records the table's format and content versions. */
	*format = image[*offset + COMMON_FORMAT_AT];
	*content = image[*offset + COMMON_CONTENT_AT];

	/* Succeeded: the table lies at *offset. */
	return 0;
}

/* Reads the fields of the firmware description its version carries. */
static void
read_firmware(
	const uint8_t *image,
	struct amdrdna_vbios_info *info)
{
	uint16_t table;
	uint16_t table_length;

	/* The table and how many bytes it declares. */
	table = info->firmware_table;
	table_length = amdrdna_le16(image, table + COMMON_LENGTH_AT);

	/* Every version carries the capability word and the first scratch register. */
	if (table_length >= FIRMWARE_SCRATCH_AT + 4U) {
		info->capability_bits = amdrdna_le32(image, table + FIRMWARE_CAPABILITY_AT);
		info->scratch_first = amdrdna_le32(image, table + FIRMWARE_SCRATCH_AT);
	}

	/* The boot clocks are named until content 5, which keeps their place reserved. */
	if (info->firmware_content < FIRMWARE_CLOCKS_BEFORE) {
		info->boot_engine_clock_10khz = amdrdna_le32(image, table + FIRMWARE_ENGINE_CLOCK_AT);
		info->boot_memory_clock_10khz = amdrdna_le32(image, table + FIRMWARE_MEMORY_CLOCK_AT);
	}

	/* The power table ID comes with content 3. */
	if (info->firmware_content >= FIRMWARE_POWER_TABLE_ID_FROM && table_length >= FIRMWARE_POWER_TABLE_ID_AT + 4U)
		info->power_table_id = amdrdna_le32(image, table + FIRMWARE_POWER_TABLE_ID_AT);

	/* The extra VRAM kept at the top comes with content 4. */
	if (info->firmware_content >= FIRMWARE_RESERVED_TOP_FROM && table_length >= FIRMWARE_RESERVED_TOP_AT + 4U)
		info->reserved_top_kib = amdrdna_le32(image, table + FIRMWARE_RESERVED_TOP_AT);
}

/* Reads the ranges the table of firmware VRAM keeps, by its layout. */
static void
read_reserve(
	const uint8_t *image,
	struct amdrdna_vbios_info *info)
{
	uint16_t table;
	uint16_t table_length;
	uint32_t start;

	/* The table and how many bytes it declares. */
	table = info->reserve_table;
	table_length = amdrdna_le16(image, table + COMMON_LENGTH_AT);

	/* Layout 2.1 keeps VRAM only when its start word's flags ask for a reservation. */
	if (info->reserve_content == 1U) {
		/* A table too short for its fields keeps nothing. */
		if (table_length < RESERVE_DRIVER_AT + 2U)
			return;

		/* Takes the firmware's range only for the reservation flag. */
		start = amdrdna_le32(image, table + RESERVE_START_AT);
		if ((start >> RESERVE_FLAGS_SHIFT) == RESERVE_FLAG_RESERVATION) {
			info->firmware_start_kib = start & RESERVE_ADDRESS_MASK;
			info->firmware_kib = amdrdna_le16(image, table + RESERVE_FIRMWARE_AT);
		}

		/* Layout 2.1 names no driver range. */
		return;
	}

	/* Layout 2.2 and later name the firmware's range and the driver's. */
	if (table_length < RESERVE_DRIVER_KIB_AT + 4U)
		return;

	/* The firmware's range and the driver's range. */
	info->firmware_start_kib = amdrdna_le32(image, table + RESERVE_START_AT);
	info->firmware_kib = amdrdna_le16(image, table + RESERVE_FIRMWARE_AT);
	info->driver_start_kib = amdrdna_le32(image, table + RESERVE_DRIVER_START_AT);
	info->driver_kib = amdrdna_le32(image, table + RESERVE_DRIVER_KIB_AT);
}

/* Reads the power table's header fields. */
static void
read_power(
	const uint8_t *image,
	struct amdrdna_vbios_info *info)
{
	uint16_t table;
	uint16_t table_length;

	/* A table too short for its header fields is left unread. */
	table = info->power_table;
	table_length = amdrdna_le16(image, table + COMMON_LENGTH_AT);
	if (table_length < POWER_SHUTDOWN_AT + 2U)
		return;

	/* The revision, the driver part's length and the shutdown temperature. */
	info->power_revision = image[table + POWER_REVISION_AT];
	info->power_bytes = amdrdna_le16(image, table + POWER_LENGTH_AT);
	info->shutdown_temperature = amdrdna_le16(image, table + POWER_SHUTDOWN_AT);
}

/* Notes where the named command routines start and what they take. */
static void
read_commands(
	const uint8_t *image,
	size_t length,
	struct amdrdna_vbios_info *info)
{
	struct amdrdna_vbios_routine *routine;
	uint16_t list_length;
	uint16_t attribute;
	uint16_t offset;
	unsigned index;
	size_t at;

	/* The command list's header must lie inside the image. */
	if (info->command_list == 0U)
		return;
	if ((size_t)info->command_list + COMMON_BYTES > length)
		return;

	/* Notes every named routine the list holds. */
	list_length = amdrdna_le16(image, info->command_list + COMMON_LENGTH_AT);
	for (index = 0U; index < AMDRDNA_VBIOS_COMMAND_COUNT; index++) {
		/* An entry past the list's end is a routine the image lacks. */
		at = (size_t)info->command_list + COMMON_BYTES + (size_t)command_entries[index] * 2U;
		if (at + 2U > (size_t)info->command_list + list_length)
			continue;
		if (at + 2U > length)
			continue;

		/* An entry of zero, or one whose header runs past the image, is skipped. */
		offset = amdrdna_le16(image, at);
		if (offset == 0U)
			continue;
		if ((size_t)offset + ROUTINE_ATTRIBUTE_AT + 2U > length)
			continue;

		/* Records the routine's place and the bytes its attribute word names. */
		routine = &info->commands[index];
		attribute = amdrdna_le16(image, offset + ROUTINE_ATTRIBUTE_AT);
		routine->offset = offset;
		routine->work_bytes = (uint16_t)(attribute & ROUTINE_WORK_MASK);
		routine->parameter_bytes = (uint16_t)((attribute & ROUTINE_PARAMETER_MASK) >> ROUTINE_PARAMETER_SHIFT);
	}
}
