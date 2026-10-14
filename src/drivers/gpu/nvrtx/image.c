/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The containers of the GSP firmware files.
 *
 * Only the container headers are read: the six-word file header, the
 * booter's high-security and load headers, the bootloader's descriptor, and
 * the ELF section table of the GSP firmware.  The images themselves are
 * copied as they are and never decoded (the NVIDIA licence forbids reverse
 * engineering).  Every offset a header names is checked against the file
 * size before it is used.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gsp.h"

/* The ELF identification bytes and the fields of the 64-bit file header the driver reads. */
#define IMAGE_ELF_HEADER_BYTES		64U
#define IMAGE_ELF_CLASS_AT		4U
#define IMAGE_ELF_CLASS_64		2U
#define IMAGE_ELF_DATA_AT		5U
#define IMAGE_ELF_DATA_LITTLE		1U
#define IMAGE_ELF_SECTIONS_AT		0x28U
#define IMAGE_ELF_SECTION_SIZE_AT	0x3aU
#define IMAGE_ELF_SECTION_COUNT_AT	0x3cU
#define IMAGE_ELF_NAMES_INDEX_AT	0x3eU

/* The fields of one 64-bit ELF section header the driver reads. */
#define IMAGE_SECTION_HEADER_BYTES	64U
#define IMAGE_SECTION_NAME_AT		0U
#define IMAGE_SECTION_OFFSET_AT		0x18U
#define IMAGE_SECTION_SIZE_AT		0x20U

/* The longest section name compared, the terminating zero included. */
#define IMAGE_SECTION_NAME_MAX		32U

static int image_range(size_t size, uint64_t at, uint64_t length);
static int image_bin_header(const uint8_t *file, size_t size, size_t *inner, size_t *payload_at, size_t *payload_size);
static uint32_t image_get16(const uint8_t *bytes, size_t at);
static int image_name_equal(const uint8_t *file, size_t size, uint64_t at, const char *name);

/*
 * Reads the containers of a booter image (booter_load or booter_unload).
 *
 * Returns 0 with the payload, signature and load ranges filled, or EINVAL
 * when a header or a range it names does not fit the file.
 */
int
nvrtx_booter_parse(
	const uint8_t *file,
	size_t size,
	struct nvrtx_booter *booter)
{
	size_t inner;
	size_t load;
	uint32_t patch_location_at;
	uint32_t patch_signature_at;
	uint32_t signature_count_at;
	uint32_t signature_bytes;
	uint32_t app_count;
	int error;

	/* Reads the file header: where the high-security header and the payload are. */
	error = image_bin_header(file, size, &inner, &booter->payload_at, &booter->payload_size);
	if (error != 0)
		return error;

	booter->file = file;
	booter->file_size = size;

	/* The high-security header must fit the file. */
	error = image_range(size, inner, NVRTX_HS_HEADER_BYTES);
	if (error != 0)
		return error;

	/* The three words the header points at: the patch location, the signature index and the count. */
	patch_location_at = nvrtx_get32(file, inner + NVRTX_HS_PATCH_LOC_AT);
	patch_signature_at = nvrtx_get32(file, inner + NVRTX_HS_PATCH_SIG_AT);
	signature_count_at = nvrtx_get32(file, inner + NVRTX_HS_SIG_COUNT_AT);

	/* Each of the three words must lie inside the file. */
	error = image_range(size, patch_location_at, 4U);
	if (error != 0)
		return error;

	error = image_range(size, patch_signature_at, 4U);
	if (error != 0)
		return error;

	error = image_range(size, signature_count_at, 4U);
	if (error != 0)
		return error;

	/* Reads the three words. */
	booter->patch_location = nvrtx_get32(file, patch_location_at);
	booter->patch_signature = nvrtx_get32(file, patch_signature_at);
	booter->signature_count = nvrtx_get32(file, signature_count_at);

	/* A booter without a signature cannot be started on the secure processor. */
	if (booter->signature_count == 0U)
		return EINVAL;

	/* The production signatures: equal parts of one range, starting at the chosen index. */
	signature_bytes = nvrtx_get32(file, inner + NVRTX_HS_SIG_PROD_SIZE_AT);
	booter->signature_size = signature_bytes / booter->signature_count;
	booter->signatures_at = (size_t)nvrtx_get32(file, inner + NVRTX_HS_SIG_PROD_AT) + booter->patch_signature;
	error = image_range(size, booter->signatures_at, (uint64_t)booter->signature_size * booter->signature_count);
	if (error != 0)
		return error;

	/* The signature is written into the payload, so its place must lie inside it. */
	error = image_range(booter->payload_size, booter->patch_location, booter->signature_size);
	if (error != 0)
		return error;

	/* The load header must fit the file. */
	load = nvrtx_get32(file, inner + NVRTX_HS_LOAD_AT);
	error = image_range(size, load, NVRTX_HS_LOAD_BYTES);
	if (error != 0)
		return error;

	/* Reads the code and data ranges and the first application's range. */
	booter->os_code_at = nvrtx_get32(file, load + NVRTX_HS_LOAD_OS_CODE_AT);
	booter->os_code_size = nvrtx_get32(file, load + NVRTX_HS_LOAD_OS_CODE_SIZE_AT);
	booter->os_data_at = nvrtx_get32(file, load + NVRTX_HS_LOAD_OS_DATA_AT);
	booter->os_data_size = nvrtx_get32(file, load + NVRTX_HS_LOAD_OS_DATA_SIZE_AT);
	booter->app_at = nvrtx_get32(file, load + NVRTX_HS_LOAD_APP0_AT);
	booter->app_size = nvrtx_get32(file, load + NVRTX_HS_LOAD_APP0_SIZE_AT);

	/* A booter has at least the one application its secure code is. */
	app_count = nvrtx_get32(file, load + NVRTX_HS_LOAD_APP_COUNT_AT);
	if (app_count == 0U)
		return EINVAL;

	/* The non-secure code, the secure code that follows it, and the data lie in the payload. */
	error = image_range(booter->payload_size, 0U, (uint64_t)booter->os_code_size + booter->app_size);
	if (error != 0)
		return error;

	error = image_range(booter->payload_size, booter->os_data_at, booter->os_data_size);
	if (error != 0)
		return error;

	/* Succeeded: the booter's ranges are all inside the file. */
	return 0;
}

/*
 * Finds the file offset of one of a booter's signatures.
 *
 * Index 0 is the production signature; index 1 is the one a debug-fused GPU
 * takes.  Returns EINVAL for an index the booter does not have.
 */
int
nvrtx_booter_signature(
	const struct nvrtx_booter *booter,
	unsigned index,
	size_t *at)
{
	/* Refuses an index beyond the booter's signatures. */
	if (index >= booter->signature_count)
		return EINVAL;

	/* Succeeded: the signature's place in the file. */
	*at = booter->signatures_at + (size_t)index * booter->signature_size;
	return 0;
}

/*
 * Reads the containers of the GSP's RISC-V bootloader file.
 */
int
nvrtx_bootloader_parse(
	const uint8_t *file,
	size_t size,
	struct nvrtx_bootloader *bootloader)
{
	size_t inner;
	int error;

	/* Reads the file header: where the descriptor and the payload are. */
	error = image_bin_header(file, size, &inner, &bootloader->payload_at, &bootloader->payload_size);
	if (error != 0)
		return error;

	/* The descriptor must fit the file. */
	error = image_range(size, inner, NVRTX_RV_DESC_BYTES);
	if (error != 0)
		return error;

	/* Reads the version written to the GSP and the offsets the placement record names. */
	bootloader->app_version = nvrtx_get32(file, inner + NVRTX_RV_DESC_APP_VERSION_AT);
	bootloader->manifest_at = nvrtx_get32(file, inner + NVRTX_RV_DESC_MANIFEST_AT);
	bootloader->monitor_data_at = nvrtx_get32(file, inner + NVRTX_RV_DESC_MONITOR_DATA_AT);
	bootloader->monitor_code_at = nvrtx_get32(file, inner + NVRTX_RV_DESC_MONITOR_CODE_AT);

	/* Succeeded: the bootloader's payload is inside the file. */
	return 0;
}

/*
 * Reads the ELF file header: where the section table is and how it is laid out.
 *
 * The header is the first 64 bytes of a file of file_size bytes.  Returns
 * EINVAL unless it is a 64-bit little-endian ELF header whose section table
 * fits the file.
 */
int
nvrtx_elf_header(
	const uint8_t *header,
	size_t file_size,
	struct nvrtx_elf *elf)
{
	uint64_t table_bytes;
	int error;

	/* The identification must say 64-bit little-endian ELF. */
	if (header[0] != 0x7fU ||
	    header[1] != 'E' ||
	    header[2] != 'L' ||
	    header[3] != 'F')
		return EINVAL;
	if (header[IMAGE_ELF_CLASS_AT] != IMAGE_ELF_CLASS_64)
		return EINVAL;
	if (header[IMAGE_ELF_DATA_AT] != IMAGE_ELF_DATA_LITTLE)
		return EINVAL;

	/* Reads the section table's place, its entry size and count, and the names section's index. */
	elf->file_size = file_size;
	elf->table_at = nvrtx_get64(header, IMAGE_ELF_SECTIONS_AT);
	elf->entry_bytes = image_get16(header, IMAGE_ELF_SECTION_SIZE_AT);
	elf->count = image_get16(header, IMAGE_ELF_SECTION_COUNT_AT);
	elf->names_index = image_get16(header, IMAGE_ELF_NAMES_INDEX_AT);

	/* The entries must be whole headers, and the names section one of them. */
	if (elf->entry_bytes < IMAGE_SECTION_HEADER_BYTES || elf->names_index >= elf->count)
		return EINVAL;

	/* The whole table must lie inside the file. */
	table_bytes = (uint64_t)elf->entry_bytes * elf->count;
	error = image_range(file_size, elf->table_at, table_bytes);
	if (error != 0)
		return error;

	/* Succeeded: the table can be read. */
	elf->table_bytes = (size_t)table_bytes;
	return 0;
}

/*
 * Finds the file range of one section from the section table.
 */
int
nvrtx_elf_entry(
	const struct nvrtx_elf *elf,
	const uint8_t *table,
	uint32_t index,
	struct nvrtx_section *section)
{
	size_t header;
	uint64_t at;
	uint64_t length;
	int error;

	/* Refuses an index beyond the table. */
	if (index >= elf->count)
		return EINVAL;

	/* Reads the section's offset and size. */
	header = (size_t)elf->entry_bytes * index;
	at = nvrtx_get64(table, header + IMAGE_SECTION_OFFSET_AT);
	length = nvrtx_get64(table, header + IMAGE_SECTION_SIZE_AT);

	/* The section's contents must lie inside the file. */
	error = image_range(elf->file_size, at, length);
	if (error != 0)
		return error;

	/* Succeeded: the section's range. */
	section->at = (size_t)at;
	section->size = (size_t)length;
	return 0;
}

/*
 * Finds a section by name, given the section table and the names section's bytes.
 *
 * Returns ENOENT when no section has the name.
 */
int
nvrtx_elf_find(
	const struct nvrtx_elf *elf,
	const uint8_t *table,
	const uint8_t *names,
	size_t names_size,
	const char *name,
	struct nvrtx_section *section)
{
	uint32_t index;
	uint32_t name_at;
	int equal;
	int error;

	/* Compares each section's name with the one asked for. */
	for (index = 0U; index < elf->count; index++) {
		/* Reads where the section's name is in the names section. */
		name_at = nvrtx_get32(table, (size_t)elf->entry_bytes * index + IMAGE_SECTION_NAME_AT);
		equal = image_name_equal(names, names_size, name_at, name);

		/* A section of another name is passed over. */
		if (equal == 0)
			continue;

		/* Reads the named section's range. */
		error = nvrtx_elf_entry(elf, table, index, section);
		if (error != 0)
			return error;

		/* Succeeded: the section is found. */
		return 0;
	}

	/* No section has the name. */
	return ENOENT;
}

/*
 * Finds a section of a 64-bit little-endian ELF file held whole in memory.
 *
 * Returns 0 with the section's file range, ENOENT when no section has the
 * name, or EINVAL when the file or its section table does not fit.
 */
int
nvrtx_elf_section(
	const uint8_t *file,
	size_t size,
	const char *name,
	struct nvrtx_section *section)
{
	struct nvrtx_elf elf;
	struct nvrtx_section names;
	int error;

	/* The file header must be there. */
	error = image_range(size, 0U, IMAGE_ELF_HEADER_BYTES);
	if (error != 0)
		return error;

	/* Reads the header and finds the names section. */
	error = nvrtx_elf_header(file, size, &elf);
	if (error != 0)
		return error;

	error = nvrtx_elf_entry(&elf, file + elf.table_at, elf.names_index, &names);
	if (error != 0)
		return error;

	/* Looks the name up. */
	error = nvrtx_elf_find(&elf, file + elf.table_at, file + names.at, names.size, name, section);
	if (error != 0)
		return error;

	/* Succeeded: the section is found. */
	return 0;
}

/* Reports EINVAL unless the range [at, at + length) lies inside size bytes. */
static int
image_range(
	size_t size,
	uint64_t at,
	uint64_t length)
{
	/* A start beyond the end, or a length past it, does not fit. */
	if (at > size)
		return EINVAL;
	if (length > size - at)
		return EINVAL;

	/* Succeeded: the range fits. */
	return 0;
}

/* Reads the six-word file header: the inner header's place and the payload's range. */
static int
image_bin_header(
	const uint8_t *file,
	size_t size,
	size_t *inner,
	size_t *payload_at,
	size_t *payload_size)
{
	uint32_t magic;
	uint32_t version;
	int error;

	/* The header must be there with NVIDIA's magic and the version this driver reads. */
	error = image_range(size, 0U, NVRTX_BIN_HEADER_BYTES);
	if (error != 0)
		return error;

	magic = nvrtx_get32(file, NVRTX_BIN_MAGIC_AT);
	version = nvrtx_get32(file, NVRTX_BIN_VERSION_AT);
	if (magic != NVRTX_BIN_MAGIC)
		return EINVAL;
	if (version != NVRTX_BIN_VERSION)
		return EINVAL;

	/* Reads the inner header's place and the payload's range. */
	*inner = nvrtx_get32(file, NVRTX_BIN_INNER_AT);
	*payload_at = nvrtx_get32(file, NVRTX_BIN_PAYLOAD_AT);
	*payload_size = nvrtx_get32(file, NVRTX_BIN_PAYLOAD_SIZE_AT);

	/* The payload must fit the file. */
	error = image_range(size, *payload_at, *payload_size);
	if (error != 0)
		return error;

	/* Succeeded: the header is valid. */
	return 0;
}

/* Reads a little-endian 16-bit field. */
static uint32_t
image_get16(
	const uint8_t *bytes,
	size_t at)
{
	/* Succeeded: the two bytes, lowest first. */
	return (uint32_t)bytes[at] | ((uint32_t)bytes[at + 1U] << 8);
}

/* Reports nonzero when the zero-terminated name at a file offset equals the given name. */
static int
image_name_equal(
	const uint8_t *file,
	size_t size,
	uint64_t at,
	const char *name)
{
	size_t index;

	/* Compares byte by byte, never reading past the file or a short name. */
	for (index = 0U; index < IMAGE_SECTION_NAME_MAX; index++) {
		/* A name running off the file, or differing here, is not the one. */
		if (at + index >= size)
			return 0;
		if (file[at + index] != (uint8_t)name[index])
			return 0;

		/* Both names ended together. */
		if (name[index] == '\0')
			return 1;
	}

	/* A name as long as the limit is not one this driver asks for. */
	return 0;
}
