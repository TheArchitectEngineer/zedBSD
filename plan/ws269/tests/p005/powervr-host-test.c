/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PowerVR firmware file parser, the MIPS page table and
 * the bounded EBUSY retry.
 *
 * A synthetic firmware file exercises every refusal; the real file, when
 * its path is given, must parse to the values recorded for it.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/firmware-image.h"
#include "drivers/gpu/powervr/mips-table.h"
#include "drivers/gpu/powervr/retry.h"

#define IMAGE_BYTES	0x4000U
#define TAIL		(IMAGE_BYTES - 0x1000U)

static int failures;
static int passes;

static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL: %s\n", what);
}

static void
put16(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void
put32(uint8_t *bytes, uint32_t value)
{
	put16(bytes, value);
	put16(bytes + 2, value >> 16);
}

static void
put64(uint8_t *bytes, uint64_t value)
{
	put32(bytes, (uint32_t)value);
	put32(bytes + 4, (uint32_t)(value >> 32));
}

static uint32_t
get32(const volatile uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t
get64(const volatile uint8_t *bytes)
{
	return (uint64_t)get32(bytes) | ((uint64_t)get32(bytes + 4) << 32);
}

static void
put_section(uint8_t *entry, uint32_t id, uint32_t memory, uint32_t base, uint32_t size, uint32_t offset)
{
	put32(entry + 0, id);
	put32(entry + 4, memory);
	put32(entry + 8, base);
	put32(entry + 12, size);
	put32(entry + 16, size);
	put32(entry + 20, offset);
}

/*
 * Builds a small firmware file: an ELF program of three loadable segments
 * (code, data with a cleared tail, and one not loaded), three sections,
 * BRN 63553 and 71242, and the features MIPS, PHYS_BUS_WIDTH (36),
 * NUM_CLUSTERS (1), SLC_SIZE_IN_KILOBYTES (16) and XE_TPU2.
 */
static void
build_image(uint8_t *image)
{
	uint8_t *header;
	uint8_t *info;
	uint8_t *words;
	uint8_t *segment;
	uint64_t features_low;
	uint64_t features_high;

	memset(image, 0, IMAGE_BYTES);

	/* ELF header: 32-bit little endian, program headers at 52, three of 32 bytes. */
	image[0] = 0x7f;
	image[1] = 'E';
	image[2] = 'L';
	image[3] = 'F';
	image[4] = 1;
	image[5] = 1;
	put32(image + 28, 52);
	put16(image + 42, 32);
	put16(image + 44, 3);

	/* Code: 16 bytes of 0xc1 at 0xc0000000 + 0x10. */
	segment = image + 52;
	put32(segment + 0, 1);
	put32(segment + 4, 0x400);
	put32(segment + 8, 0xc0000010U);
	put32(segment + 16, 16);
	put32(segment + 20, 16);
	memset(image + 0x400, 0xc1, 16);

	/* Data: 8 bytes of 0xd1 at the private data base, 24 bytes in memory. */
	segment += 32;
	put32(segment + 0, 1);
	put32(segment + 4, 0x500);
	put32(segment + 8, 0xc0032000U);
	put32(segment + 16, 8);
	put32(segment + 20, 24);
	memset(image + 0x500, 0xd1, 8);

	/* A segment of another type, aimed nowhere, which is skipped. */
	segment += 32;
	put32(segment + 0, 4);
	put32(segment + 8, 0x12345678U);
	put32(segment + 16, 4);
	put32(segment + 20, 4);

	/* Header. */
	header = image + TAIL;
	put32(header + 0, 3);
	put32(header + 4, 48);
	put32(header + 8, 3);
	put32(header + 12, 24);
	put64(header + 16, powervr_bvnc_pack(36, 50, 54, 182));
	put32(header + 24, 4096);
	put32(header + 28, 1);
	put16(header + 32, 1);
	put16(header + 34, 1);
	put32(header + 36, 6976702);
	put32(header + 40, 0x100);

	/* Sections: code at offset 0, boot data in data at 0x1000, data (private) at 0. */
	put_section(header + 48, POWERVR_SECTION_MIPS_CODE, POWERVR_MEMORY_CODE, 0xc0000000U, 0x2000, 0);
	put_section(header + 72, POWERVR_SECTION_MIPS_BOOT_DATA, POWERVR_MEMORY_DATA, 0xbfc01000U, 0x1000, 0x1000);
	put_section(header + 96, POWERVR_SECTION_MIPS_PRIVATE, POWERVR_MEMORY_DATA, 0xc0032000U, 0x1000, 0);

	/* Device information: one word each of BRN, ERN, two of features, values. */
	info = image + TAIL - 0x100;
	put64(info + 0, 1);
	put64(info + 8, 1);
	put64(info + 16, 2);
	put64(info + 24, 3);
	words = info + 32;
	put64(words + 0, (1ULL << 9) | (1ULL << 11));
	put64(words + 8, 0xf5);
	features_low = (1ULL << POWERVR_FEATURE_MIPS) | (1ULL << POWERVR_FEATURE_PHYS_BUS_WIDTH) |
		       (1ULL << POWERVR_FEATURE_NUM_CLUSTERS) | (1ULL << POWERVR_FEATURE_SLC_KILOBYTES);
	features_high = 1ULL << (POWERVR_FEATURE_XE_TPU2 - 64U);
	put64(words + 16, features_low);
	put64(words + 24, features_high);

	/* Values in feature order: NUM_CLUSTERS (28), PHYS_BUS_WIDTH (36), SLC_SIZE_IN_KILOBYTES (46). */
	put64(words + 32, 1);
	put64(words + 40, 36);
	put64(words + 48, 16);
}

static void
test_synthetic(void)
{
	static uint8_t image[IMAGE_BYTES];
	static uint8_t broken[IMAGE_BYTES];
	struct powervr_fw_image parsed;
	const struct powervr_fw_section *section;
	uint8_t code[0x2000];
	uint8_t data[0x2000];
	uint64_t bvnc;
	uint64_t value;
	int error;
	int index;

	build_image(image);
	bvnc = powervr_bvnc_pack(36, 50, 54, 182);
	check(bvnc == 0x00240032003600b6ULL, "bvnc packs 16 bits per field, branch highest");

	/* Parses. */
	error = powervr_fw_image_parse(image, IMAGE_BYTES, bvnc, &parsed);
	check(error == 0, "synthetic image parses");
	check(parsed.version_major == 1 && parsed.version_minor == 1 && parsed.version_build == 6976702, "version read");
	check(parsed.section_count == 3, "three sections");
	check(parsed.code_size == 0x2000 && parsed.data_size == 0x2000, "allocation sizes summed per kind");
	check(parsed.core_code_size == 0 && parsed.core_data_size == 0, "no core allocations");
	check(powervr_fw_image_has_quirk(&parsed, POWERVR_QUIRK_63553), "BRN 63553");
	check(powervr_fw_image_has_quirk(&parsed, POWERVR_QUIRK_71242), "BRN 71242");
	check(!powervr_fw_image_has_quirk(&parsed, 0), "no BRN 44079");
	check(parsed.host_quirks == POWERVR_HOST_QUIRK_SMALL_PAGES, "host BRN 71317 for 36.50.54.182");
	check(powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_MIPS), "MIPS feature");
	check(powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_XE_TPU2), "XE_TPU2 in the second word");
	check(!powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_META), "no META");
	check(!powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_COUNT), "position past the features");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_PHYS_BUS_WIDTH, &value);
	check(error == 0 && value == 36, "bus width value");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_NUM_CLUSTERS, &value);
	check(error == 0 && value == 1, "cluster count value");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_SLC_KILOBYTES, &value);
	check(error == 0 && value == 16, "SLC size value");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_SLC_LINE_BITS, &value);
	check(error == ENOENT, "absent feature has no value");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_MIPS, &value);
	check(error == EINVAL, "flag feature has no value");
	section = powervr_fw_image_section(&parsed, POWERVR_SECTION_MIPS_BOOT_DATA);
	check(section != NULL && section->alloc_offset == 0x1000 && section->base == 0xbfc01000U, "boot data section found");
	check(powervr_fw_image_section(&parsed, POWERVR_SECTION_MIPS_STACK) == NULL, "absent section is NULL");

	/* Loads. */
	memset(code, 0xee, sizeof(code));
	memset(data, 0xee, sizeof(data));
	memset(code, 0, sizeof(code));
	memset(data, 0, sizeof(data));
	data[8] = 0x55;
	error = powervr_fw_image_load(&parsed, code, data);
	check(error == 0, "program loads");
	for (index = 0; index < 16; index++) {
		if (code[0x10 + index] != 0xc1)
			break;
	}
	check(index == 16 && code[0x0f] == 0 && code[0x20] == 0, "code segment placed at its offset");
	check(data[0] == 0xd1 && data[7] == 0xd1 && data[8] == 0 && data[23] == 0, "data placed, tail cleared");

	/* Refusals. */
	error = powervr_fw_image_parse(image, IMAGE_BYTES, powervr_bvnc_pack(36, 52, 104, 182), &parsed);
	check(error == ENOEXEC, "other core refused");
	error = powervr_fw_image_parse(image, IMAGE_BYTES - 1, bvnc, &parsed);
	check(error == EINVAL, "partial block refused");
	error = powervr_fw_image_parse(image, 0x800, bvnc, &parsed);
	check(error == EINVAL, "file under one block refused");

	memcpy(broken, image, IMAGE_BYTES);
	put32(broken + TAIL + 28, 0);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == ENOEXEC, "closed build refused");

	memcpy(broken, image, IMAGE_BYTES);
	put16(broken + TAIL + 32, 2);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == ENOEXEC, "major version 2 refused");

	memcpy(broken, image, IMAGE_BYTES);
	put32(broken + TAIL + 0, 2);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == ENOEXEC, "other description version refused");

	memcpy(broken, image, IMAGE_BYTES);
	put32(broken + TAIL + 8, 9);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == EINVAL, "too many sections refused");

	memcpy(broken, image, IMAGE_BYTES);
	put32(broken + TAIL + 72 + 20, 0x1800);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == EINVAL, "section past its allocation refused");

	memcpy(broken, image, IMAGE_BYTES);
	put32(broken + TAIL + 40, 0x10);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == EINVAL, "device information smaller than its header refused");

	memcpy(broken, image, IMAGE_BYTES);
	put64(broken + TAIL - 0x100 + 24, 2);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == EINVAL, "feature values running out refused");

	memcpy(broken, image, IMAGE_BYTES);
	put64(broken + TAIL - 0x100 + 0, 0x100);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == EINVAL, "mask longer than the device information refused");

	memcpy(broken, image, IMAGE_BYTES);
	put32(broken + 52 + 32 + 8, 0xc0032ff0U);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == 0, "image with a straddling segment still parses");
	error = powervr_fw_image_load(&parsed, code, data);
	check(error == EINVAL, "segment crossing its section's end refused");

	memcpy(broken, image, IMAGE_BYTES);
	put32(broken + 52 + 4, IMAGE_BYTES - 8);
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == 0, "image with a segment past the file still parses");
	error = powervr_fw_image_load(&parsed, code, data);
	check(error == EINVAL, "segment reading past the file refused");

	memcpy(broken, image, IMAGE_BYTES);
	broken[4] = 2;
	error = powervr_fw_image_parse(broken, IMAGE_BYTES, bvnc, &parsed);
	check(error == 0, "image with a 64-bit program still parses");
	error = powervr_fw_image_load(&parsed, code, data);
	check(error == EINVAL, "64-bit program refused");
}

static void
test_real(const char *path)
{
	struct powervr_fw_image parsed;
	const struct powervr_fw_section *section;
	uint8_t *bytes;
	uint8_t *code;
	uint8_t *data;
	uint64_t value;
	long size;
	FILE *file;
	int error;

	file = fopen(path, "rb");
	check(file != NULL, "real firmware opens");
	if (file == NULL)
		return;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes = malloc((size_t)size);
	check(fread(bytes, 1, (size_t)size, file) == (size_t)size, "real firmware reads");
	fclose(file);

	error = powervr_fw_image_parse(bytes, (size_t)size, powervr_bvnc_pack(36, 50, 54, 182), &parsed);
	check(error == 0, "real firmware parses for 36.50.54.182");
	printf("real: %ld bytes, v%u.%u build %u, %u sections, code %u, data %u, BRN mask 0x%llx\n",
	       size,
	       parsed.version_major,
	       parsed.version_minor,
	       parsed.version_build,
	       parsed.section_count,
	       parsed.code_size,
	       parsed.data_size,
	       (unsigned long long)parsed.quirks);
	check(parsed.version_major == 1 && parsed.version_build == 6976702, "real version 1.x build 6976702");
	check(parsed.section_count == 6, "real: six sections");
	check(parsed.code_size == 102400 && parsed.data_size == 20480, "real: code 100 KiB, data 20 KiB");
	check(parsed.quirks == ((1ULL << 9) | (1ULL << 11)), "real: BRN 63553 and 71242 only");
	check(parsed.host_quirks == POWERVR_HOST_QUIRK_SMALL_PAGES, "real: host BRN 71317");
	check(powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_MIPS), "real: MIPS");
	check(!powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_META), "real: not META");
	check(!powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_RISCV), "real: not RISC-V");
	check(powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_XE_TPU2), "real: XE_TPU2");
	check(powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_PBE2_IN_XE), "real: PBE2_IN_XE");
	check(powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_SECURE_RESET), "real: SYS_BUS_SECURE_RESET");
	check(powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_GPU_MULTICORE), "real: GPU_MULTICORE_SUPPORT");
	check(!powervr_fw_image_has_feature(&parsed, POWERVR_FEATURE_XT_TOP), "real: not XT_TOP_INFRASTRUCTURE");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_PHYS_BUS_WIDTH, &value);
	check(error == 0 && value == 36, "real: bus width 36");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_LAYOUT_MARS, &value);
	check(error == 0 && value == 1, "real: LAYOUT_MARS 1");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_SLC_LINE_BITS, &value);
	check(error == 0 && value == 512, "real: SLC line 512 bits");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_SLC_KILOBYTES, &value);
	check(error == 0 && value == 16, "real: SLC 16 KiB");
	error = powervr_fw_image_feature_value(&parsed, POWERVR_FEATURE_NUM_CLUSTERS, &value);
	check(error == 0 && value == 1, "real: one cluster");
	section = powervr_fw_image_section(&parsed, POWERVR_SECTION_MIPS_PRIVATE);
	check(section != NULL && (section->base & (POWERVR_HEAP_BYTES - 1U)) == 0x32000U, "real: data at heap offset 0x32000");
	section = powervr_fw_image_section(&parsed, POWERVR_SECTION_MIPS_BOOT_CODE);
	check(section != NULL && section->alloc_offset == 98304, "real: boot code at code offset 96 KiB");

	code = calloc(1, parsed.code_size);
	data = calloc(1, parsed.data_size);
	error = powervr_fw_image_load(&parsed, code, data);
	check(error == 0, "real program loads into its sections");
	free(data);
	free(code);
	free(bytes);
}

static void
test_mips(void)
{
	static uint32_t table[POWERVR_MIPS_TABLE_ENTRIES];
	static uint8_t boot[64];
	uint64_t pages[POWERVR_MIPS_TABLE_PAGES];
	uint32_t entry;
	int error;
	int index;

	check(POWERVR_MIPS_TABLE_PAGES == 4, "the table is four pages");
	check(POWERVR_HEAP_CONFIG_OFFSET == 0xfd0000U, "configuration area at 16 MiB - 192 KiB");
	check(POWERVR_HEAP_OS_INIT_OFFSET == 0xfe0000U && POWERVR_HEAP_SYSTEM_INIT_OFFSET == 0xff0000U, "init records 64 KiB apart");
	check(POWERVR_HEAP_GENERAL_BYTES == 0xed0000U, "general part excludes the reserved 1 MiB and the configuration area");
	check(powervr_mips_address(0) == 0xc0000000U, "heap offset 0 at 0xc0000000");
	check(powervr_mips_address(0x32000) == 0xc0032000U, "data offset");
	check(powervr_mips_address(0x1fe0000) == 0xc0fe0000U, "offset wraps within the window");

	entry = powervr_mips_entry(0x80123000ULL, POWERVR_MIPS_UNCACHED, 36);
	check(entry == ((((0x80123000ULL >> 12) << 6) & 0x3fffffc0U) | (2U << 3) | 7U), "uncached entry on a 36-bit bus");
	entry = powervr_mips_entry(0x80123000ULL, POWERVR_MIPS_CACHED, 36);
	check(((entry >> 3) & 7U) == 1U, "cached on a wide bus writes through");
	entry = powervr_mips_entry(0x80123000ULL, POWERVR_MIPS_CACHED, 32);
	check(((entry >> 3) & 7U) == 3U, "cached on a 32-bit bus writes back");
	entry = powervr_mips_entry(0xf00000000ULL, POWERVR_MIPS_UNCACHED, 36);
	check((entry & 0x3fffffc0U) == ((0xf00000000ULL >> 12) << 6), "36-bit frame kept on a wide bus");
	entry = powervr_mips_entry(0xf00000000ULL, POWERVR_MIPS_UNCACHED, 32);
	check((entry & 0x3fffffc0U) == (((0xf00000000ULL >> 12) << 6) & 0x03ffffc0U), "narrow bus masks the frame");

	memset(table, 0, sizeof(table));
	error = powervr_mips_map(table, 0x32000, 0x80200000ULL, 0x3000, POWERVR_MIPS_UNCACHED, 36);
	check(error == 0, "map three pages");
	check(table[0x31] == 0 && table[0x35] == 0, "neighbours untouched");
	for (index = 0; index < 3; index++) {
		if (table[0x32 + index] != powervr_mips_entry(0x80200000ULL + (uint64_t)index * 0x1000U, POWERVR_MIPS_UNCACHED, 36))
			break;
	}
	check(index == 3, "consecutive frames");
	powervr_mips_unmap(table, 0x32000, 0x3000);
	check(table[0x32] == 0 && table[0x34] == 0, "unmap clears");
	check(powervr_mips_map(table, 0x32800, 0x80200000ULL, 0x1000, POWERVR_MIPS_UNCACHED, 36) == EINVAL, "unaligned offset refused");
	check(powervr_mips_map(table, 0x32000, 0x80200800ULL, 0x1000, POWERVR_MIPS_UNCACHED, 36) == EINVAL, "unaligned run refused");
	check(powervr_mips_map(table, 0x32000, 0x80200000ULL, 0x800, POWERVR_MIPS_UNCACHED, 36) == EINVAL, "partial page refused");
	check(powervr_mips_map(table, 0xfff000, 0x80200000ULL, 0x2000, POWERVR_MIPS_UNCACHED, 36) == EINVAL, "range past the heap refused");
	check(powervr_mips_map(table, 0, 0xfffff000ULL, 0x2000, POWERVR_MIPS_UNCACHED, 32) == EINVAL, "run past a 32-bit bus refused");
	check(powervr_mips_map(table, 0xfff000, 0x80200000ULL, 0x1000, POWERVR_MIPS_UNCACHED, 36) == 0, "last page maps");
	check(table[POWERVR_MIPS_TABLE_ENTRIES - 1] != 0, "last entry written");

	for (index = 0; index < 4; index++)
		pages[index] = 0x80400000ULL + (uint64_t)index * 0x1000U;
	memset(boot, 0xff, sizeof(boot));
	powervr_mips_boot_parameters(boot, 0x80310000ULL, 0x18000000ULL, pages);
	check(get64(boot + 0) == 0x80310000ULL, "boot: stack at 0");
	check(get64(boot + 8) == 0x18000000ULL, "boot: registers at 8");
	check(get64(boot + 16) == 0x80400000ULL && get64(boot + 40) == 0x80403000ULL, "boot: table pages from 16");
	check(get32(boot + 48) == 12 && get32(boot + 52) == 4, "boot: 4 KiB pages, four of them");
	check(get32(boot + 56) == 0 && get32(boot + 60) == 0, "boot: reserved words cleared");

	check(powervr_mips_remap_target(0x80318000ULL) == (0x80318000ULL | 12U), "remap: page and window size");
	check(powervr_mips_remap_target(0xf80318abcULL) == (0xf80318000ULL | 12U), "remap: offset bits dropped");
}

struct retry_script {
	const int *answers;
	unsigned count;
	unsigned calls;
	unsigned pauses;
};

static int
scripted_attempt(void *context)
{
	struct retry_script *script = context;
	int answer;

	answer = script->answers[script->calls < script->count ? script->calls : script->count - 1U];
	script->calls++;
	return answer;
}

static void
scripted_pause(void *context)
{
	struct retry_script *script = context;

	script->pauses++;
}

static void
test_retry(void)
{
	static const int busy_then_done[] = { EBUSY, EBUSY, 0 };
	static const int always_busy[] = { EBUSY };
	static const int other_error[] = { EBUSY, ENOMEM };
	struct retry_script script;
	struct powervr_retry retry;
	unsigned tries;
	int answer;

	retry.attempt = scripted_attempt;
	retry.pause = scripted_pause;
	retry.context = &script;

	memset(&script, 0, sizeof(script));
	script.answers = busy_then_done;
	script.count = 3;
	answer = powervr_retry_busy(&retry, 50, &tries);
	check(answer == 0 && tries == 3 && script.pauses == 2, "EBUSY twice, then registered");

	memset(&script, 0, sizeof(script));
	script.answers = always_busy;
	script.count = 1;
	answer = powervr_retry_busy(&retry, 50, &tries);
	check(answer == EBUSY && tries == 50 && script.pauses == 49, "EBUSY every time: bounded at 50");

	memset(&script, 0, sizeof(script));
	script.answers = other_error;
	script.count = 2;
	answer = powervr_retry_busy(&retry, 50, &tries);
	check(answer == ENOMEM && tries == 2, "another error ends the retry at once");

	memset(&script, 0, sizeof(script));
	script.answers = always_busy;
	script.count = 1;
	answer = powervr_retry_busy(&retry, 0, &tries);
	check(answer == EBUSY && tries == 0 && script.calls == 0, "no attempts allowed is a refusal");
}

int
main(int argc, char **argv)
{
	test_synthetic();
	test_mips();
	test_retry();
	if (argc > 1)
		test_real(argv[1]);
	printf("PASS %d FAIL %d\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
