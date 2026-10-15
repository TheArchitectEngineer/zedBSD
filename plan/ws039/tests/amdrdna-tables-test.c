/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amdrdna driver's table readers: the GPU's block table,
 * the boot database, the ACPI table of video BIOS images and the image's
 * tables, the VRAM layout, the firmware picture's shape, the device-ID
 * table and the display register addresses.
 *
 *   amdrdna-tables-test                     the synthetic cases
 *   amdrdna-tables-test --dump DIRECTORY    also the files a hardware dump
 *                                           (hw-dump.py) left there:
 *                                           discovery.bin, runtime-db.bin,
 *                                           vfct.bin and rom.bin
 *   amdrdna-tables-test --dcn               the display register indices for
 *                                           Navi10's windows, one line per
 *                                           register and instance
 *
 * Each case prints "PASS name" or "FAIL name: why"; the exit status is 1
 * when any case failed.  The ACPI copy of the video BIOS is the firmware's
 * running copy, which differs from the flash in a few marked bytes (the
 * posted bit among them), so the two are compared by their tables' places.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/* The case results. */
static int failures;

static void check(int condition, const char *name, const char *why);
static void put16(uint8_t *bytes, size_t at, uint16_t value);
static void put32(uint8_t *bytes, size_t at, uint32_t value);
static uint16_t sum_bytes(const uint8_t *bytes, size_t length);
static size_t read_file(const char *directory, const char *name, uint8_t **bytes);
static void test_block_table(void);
static void test_boot_database(void);
static void test_vbios(void);
static void test_layout(void);
static void test_picture(void);
static void picture_scaled_case(struct amdrdna_picture_input *input);
static void test_products(void);
static void test_dump(const char *directory);
static void print_dcn(void);
static void navi10_display(struct amdrdna_ip *display);

int
main(
	int argc,
	char **argv)
{
	/* Prints the display register table when asked. */
	if (argc == 2 && strcmp(argv[1], "--dcn") == 0) {
		print_dcn();
		return 0;
	}

	/* Runs the synthetic cases. */
	test_block_table();
	test_boot_database();
	test_vbios();
	test_layout();
	test_picture();
	test_products();

	/* Runs the cases of a hardware dump when one is named. */
	if (argc == 3 && strcmp(argv[1], "--dump") == 0)
		test_dump(argv[2]);

	/* Reports the whole result. */
	if (failures != 0) {
		printf("FAIL amdrdna-tables-test: %d case(s)\n", failures);
		return 1;
	}

	printf("PASS amdrdna-tables-test\n");
	return 0;
}

/* Records one case. */
static void
check(
	int condition,
	const char *name,
	const char *why)
{
	/* Prints the case's result and counts a failure. */
	if (condition) {
		printf("PASS %s\n", name);
	} else {
		printf("FAIL %s: %s\n", name, why);
		failures++;
	}
}

/* Writes a little-endian 16-bit word. */
static void
put16(
	uint8_t *bytes,
	size_t at,
	uint16_t value)
{
	bytes[at] = (uint8_t)value;
	bytes[at + 1U] = (uint8_t)(value >> 8);
}

/* Writes a little-endian 32-bit word. */
static void
put32(
	uint8_t *bytes,
	size_t at,
	uint32_t value)
{
	put16(bytes, at, (uint16_t)value);
	put16(bytes, at + 2U, (uint16_t)(value >> 16));
}

/* Adds bytes into a 16-bit sum. */
static uint16_t
sum_bytes(
	const uint8_t *bytes,
	size_t length)
{
	uint16_t sum;
	size_t index;

	sum = 0U;
	for (index = 0U; index < length; index++)
		sum = (uint16_t)(sum + bytes[index]);
	return sum;
}

/*
 * Builds a block table with one die, the graphics, display and NBIO blocks
 * and a graphics shape, then checks the reader on it and on damaged copies.
 */
static void
test_block_table(void)
{
	static uint8_t table[2048];
	static uint8_t copy[2048];
	struct amdrdna_block_table parsed;
	size_t list;
	size_t die;
	size_t entry;
	size_t shape;
	size_t length;
	int error;

	memset(table, 0, sizeof(table));
	list = 0x40U;
	die = list + 80U;

	/* The block list: signature, layout 2, one die at die. */
	put32(table, list, 0x53445049U);
	put16(table, list + 4U, 2U);
	put16(table, list + 12U, 1U);
	put16(table, list + 14U, 0U);
	put16(table, list + 16U, (uint16_t)die);

	/* The die: three blocks. */
	put16(table, die, 0U);
	put16(table, die + 2U, 3U);
	entry = die + 4U;

	/* Graphics 10.1.10, two windows. */
	put16(table, entry, 11U);
	table[entry + 2U] = 0U;
	table[entry + 3U] = 2U;
	table[entry + 4U] = 10U;
	table[entry + 5U] = 1U;
	table[entry + 6U] = 10U;
	put32(table, entry + 8U, 0x1260U);
	put32(table, entry + 12U, 0xa000U);
	entry += 16U;

	/* Display 2.0.2, four windows. */
	put16(table, entry, 271U);
	table[entry + 3U] = 4U;
	table[entry + 4U] = 2U;
	table[entry + 5U] = 0U;
	table[entry + 6U] = 2U;
	put32(table, entry + 8U, 0x12U);
	put32(table, entry + 12U, 0xc0U);
	put32(table, entry + 16U, 0x34c0U);
	put32(table, entry + 20U, 0x9000U);
	entry += 24U;

	/* A second graphics instance, which the reader must not take. */
	put16(table, entry, 11U);
	table[entry + 2U] = 1U;
	table[entry + 3U] = 1U;
	table[entry + 4U] = 9U;
	put32(table, entry + 8U, 0x7777U);
	entry += 12U;

	/* The list's length and the top list entry with its sum. */
	length = entry - list;
	put16(table, list + 6U, (uint16_t)length);
	put16(table, 12U, (uint16_t)list);
	put16(table, 16U, (uint16_t)length);
	put16(table, 14U, sum_bytes(table + list, length));

	/* The graphics shape after the list. */
	shape = entry;
	put32(table, shape, 0x4347U);
	put16(table, shape + 4U, 1U);
	put32(table, shape + 8U, 88U);
	put32(table, shape + 12U, 2U);
	put32(table, shape + 16U, 3U);
	put32(table, shape + 20U, 2U);
	put32(table, shape + 24U, 8U);
	put32(table, shape + 28U, 16U);
	put32(table, shape + 12U + 11U * 4U, 32U);
	put32(table, shape + 12U + 12U * 4U, 20U);
	put32(table, shape + 12U + 16U * 4U, 2U);
	put16(table, 20U, (uint16_t)shape);
	put16(table, 24U, 88U);
	put16(table, 22U, sum_bytes(table + shape, 88U));

	/* The top header: signature, version, length and sum. */
	put32(table, 0U, 0x28211407U);
	put16(table, 4U, 1U);
	put16(table, 6U, 1U);
	length = shape + 88U;
	put16(table, 10U, (uint16_t)length);
	put16(table, 8U, sum_bytes(table + 10U, length - 10U));

	/* The reader takes the blocks and the shape. */
	error = amdrdna_block_table_parse(table, sizeof(table), &parsed);
	check(error == 0, "block-table.parse", "the synthetic table was refused");
	check(parsed.blocks[AMDRDNA_BLOCK_GC].found == 1U &&
	    parsed.blocks[AMDRDNA_BLOCK_GC].major == 10U &&
	    parsed.blocks[AMDRDNA_BLOCK_GC].minor == 1U &&
	    parsed.blocks[AMDRDNA_BLOCK_GC].step == 10U &&
	    parsed.blocks[AMDRDNA_BLOCK_GC].window[0] == 0x1260U,
	    "block-table.graphics", "the first graphics instance was not taken");
	check(parsed.blocks[AMDRDNA_BLOCK_DCN].window_count == 4U &&
	    parsed.blocks[AMDRDNA_BLOCK_DCN].window[2] == 0x34c0U,
	    "block-table.display", "the display windows are wrong");
	check(parsed.listed == 3U, "block-table.listed", "the entry count is wrong");
	check(parsed.has_shape &&
	    parsed.shape.engines == 2U &&
	    parsed.shape.pairs_per_array == 5U &&
	    parsed.shape.arrays_per_engine == 2U &&
	    parsed.shape.wave_lanes == 32U,
	    "block-table.shape", "the graphics shape is wrong");
	check(parsed.blocks[AMDRDNA_BLOCK_NBIO].found == 0U, "block-table.absent", "an unlisted block was found");

	/* A damaged sum, a damaged list sum and a short buffer are refused. */
	memcpy(copy, table, sizeof(copy));
	copy[length - 1U] ^= 0x01U;
	error = amdrdna_block_table_parse(copy, sizeof(copy), &parsed);
	check(error == EINVAL, "block-table.sum", "a damaged table was taken");

	memcpy(copy, table, sizeof(copy));
	copy[list + 20U] ^= 0x40U;
	put16(copy, 8U, sum_bytes(copy + 10U, length - 10U));
	error = amdrdna_block_table_parse(copy, sizeof(copy), &parsed);
	check(error == EINVAL, "block-table.list-sum", "a damaged block list was taken");

	error = amdrdna_block_table_parse(table, length - 4U, &parsed);
	check(error == EINVAL, "block-table.short", "a cut table was taken");

	error = amdrdna_block_table_parse(table, 8U, &parsed);
	check(error == EINVAL, "block-table.tiny", "eight bytes were taken");
}

/* Checks the boot database reader on absent, present and damaged databases. */
static void
test_boot_database(void)
{
	static uint8_t bytes[4096];
	struct amdrdna_boot_database database;
	int error;

	/* Random bytes without the mark are no database. */
	memset(bytes, 0xd7, sizeof(bytes));
	error = amdrdna_boot_database_parse(bytes, sizeof(bytes), &database);
	check(error == 0 && !database.present, "boot-database.absent", "bytes without the mark were taken");

	/* A database with two entries, the second the boot configuration at 0x100. */
	memset(bytes, 0, sizeof(bytes));
	put16(bytes, 0U, 0x0ed5U);
	put16(bytes, 2U, 0x0100U);
	put16(bytes, 4U, 2U);
	put32(bytes, 8U, 1U);
	put16(bytes, 12U, 0x80U);
	put16(bytes, 14U, 4U);
	put32(bytes, 16U, 5U);
	put16(bytes, 20U, 0x100U);
	put16(bytes, 22U, 8U);
	put32(bytes, 0x100U, 0x2U);
	error = amdrdna_boot_database_parse(bytes, sizeof(bytes), &database);
	check(error == 0 &&
	    database.present &&
	    database.version == 0x0100U &&
	    database.entries == 2U &&
	    database.has_config &&
	    database.config_read &&
	    database.config_bits == 0x2U,
	    "boot-database.config", "the boot configuration was not read");

	/* An entry past the bytes given is found but left for the caller. */
	put16(bytes, 20U, 0x2000U);
	error = amdrdna_boot_database_parse(bytes, 0x1000U, &database);
	check(error == 0 && database.has_config && !database.config_read && database.config_offset == 0x2000U,
	    "boot-database.far-entry", "an entry past the bytes was mishandled");

	/* Too many entries, or a short configuration entry, are damage. */
	put16(bytes, 4U, 0x40U);
	error = amdrdna_boot_database_parse(bytes, sizeof(bytes), &database);
	check(error == EINVAL, "boot-database.count", "a directory of 64 entries was taken");

	put16(bytes, 4U, 2U);
	put16(bytes, 22U, 4U);
	error = amdrdna_boot_database_parse(bytes, sizeof(bytes), &database);
	check(error == EINVAL, "boot-database.entry-length", "a 4-byte configuration was taken");
}

/*
 * Builds a video BIOS image with a firmware table, a VRAM table, a power
 * table and two command routines, wraps it in an ACPI table with a record
 * of another function before it, and checks both readers.
 */
static void
test_vbios(void)
{
	static uint8_t image[4096];
	static uint8_t acpi[8192];
	struct amdrdna_vbios_info info;
	struct amdrdna_vbios_owner owner;
	size_t header;
	size_t data;
	size_t commands;
	size_t at;
	size_t offset;
	size_t length;
	int error;

	memset(image, 0, sizeof(image));
	image[0] = 0x55U;
	image[1] = 0xaaU;
	image[2] = 8U;
	header = 0x100U;
	put16(image, 0x48U, (uint16_t)header);
	memcpy(image + header + 4U, "ATOM", 4U);
	commands = 0x200U;
	data = 0x300U;
	put16(image, header + 0x1eU, (uint16_t)commands);
	put16(image, header + 0x20U, (uint16_t)data);

	/* The data list: 35 entries. */
	put16(image, data, 4U + 35U * 2U);
	image[data + 2U] = 2U;
	image[data + 3U] = 1U;

	/* The firmware table, content 3.3: capability, clocks, scratch, power table ID. */
	at = 0x400U;
	put16(image, data + 4U + 4U * 2U, (uint16_t)at);
	put16(image, at, 76U);
	image[at + 2U] = 3U;
	image[at + 3U] = 3U;
	put32(image, at + 8U, 30000U);
	put32(image, at + 12U, 87500U);
	put32(image, at + 16U, 0x401U);
	put32(image, at + 24U, 0x4cU);
	put32(image, at + 60U, 7U);

	/* The VRAM table, layout 2.2: firmware 64 KiB at 6000 MiB. */
	at = 0x500U;
	put16(image, data + 4U + 11U * 2U, (uint16_t)at);
	put16(image, at, 48U);
	image[at + 2U] = 2U;
	image[at + 3U] = 2U;
	put32(image, at + 4U, 6000U * 1024U);
	put16(image, at + 8U, 64U);

	/* The power table: revision 1, 482 bytes, shutdown at 118 C. */
	at = 0x600U;
	put16(image, data + 4U + 15U * 2U, (uint16_t)at);
	put16(image, at, 64U);
	image[at + 2U] = 12U;
	image[at + 4U] = 1U;
	put16(image, at + 5U, 482U);
	put16(image, at + 32U, 118U);

	/* The command list: post at 0x700, pixel clock at 0x720. */
	put16(image, commands, 4U + 81U * 2U);
	put16(image, commands + 4U, 0x700U);
	put16(image, commands + 4U + 12U * 2U, 0x720U);
	put16(image, 0x700U + 4U, 0x0810U);
	put16(image, 0x720U + 4U, 0x0410U);

	/* The reader takes the tables. */
	error = amdrdna_vbios_parse(image, sizeof(image), &info);
	check(error == 0, "vbios.parse", "the synthetic image was refused");
	check(info.firmware_format == 3U &&
	    info.firmware_content == 3U &&
	    info.capability_bits == 0x401U &&
	    info.boot_engine_clock_10khz == 30000U &&
	    info.scratch_first == 0x4cU &&
	    info.power_table_id == 7U,
	    "vbios.firmware", "the firmware table is wrong");
	check(info.firmware_start_kib == 6000U * 1024U && info.firmware_kib == 64U,
	    "vbios.reserve", "the VRAM table is wrong");
	check(info.power_revision == 1U && info.power_bytes == 482U && info.shutdown_temperature == 118U,
	    "vbios.power", "the power table is wrong");
	check(info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset == 0x700U &&
	    info.commands[AMDRDNA_VBIOS_COMMAND_POST].parameter_bytes == 8U &&
	    info.commands[AMDRDNA_VBIOS_COMMAND_POST].work_bytes == 0x10U &&
	    info.commands[AMDRDNA_VBIOS_COMMAND_PIXEL_CLOCK].offset == 0x720U &&
	    info.commands[AMDRDNA_VBIOS_COMMAND_ENCODER].offset == 0U,
	    "vbios.commands", "the command routines are wrong");
	check(amdrdna_vbios_declared_length(image) == 4096U, "vbios.length", "the declared length is wrong");

	/* A wrong signature, and a header past the end, are refused. */
	image[header + 4U] = 'B';
	error = amdrdna_vbios_check(image, sizeof(image));
	check(error == EINVAL, "vbios.signature", "a wrong signature was taken");
	memcpy(image + header + 4U, "MOTA", 4U);
	error = amdrdna_vbios_check(image, sizeof(image));
	check(error == 0, "vbios.reversed", "the reversed signature was refused");
	error = amdrdna_vbios_check(image, header + 8U);
	check(error == EINVAL, "vbios.cut", "a cut header was taken");

	/* The ACPI table: a 16-byte image of 4:0.0 first, then the image of 3:0.0. */
	memset(acpi, 0, sizeof(acpi));
	memcpy(acpi, "VFCT", 4U);
	put32(acpi, 0x34U, 0x4cU);
	at = 0x4cU;
	put32(acpi, at, 4U);
	put16(acpi, at + 0x0cU, 0x1002U);
	put16(acpi, at + 0x0eU, 0x731fU);
	put32(acpi, at + 0x18U, 16U);
	at += 0x1cU + 16U;
	put32(acpi, at, 3U);
	put16(acpi, at + 0x0cU, 0x1002U);
	put16(acpi, at + 0x0eU, 0x731fU);
	put32(acpi, at + 0x18U, sizeof(image));
	memcpy(acpi + at + 0x1cU, image, sizeof(image));
	put32(acpi, 4U, (uint32_t)(at + 0x1cU + sizeof(image)));

	owner.bus = 3U;
	owner.device = 0U;
	owner.function = 0U;
	owner.vendor = 0x1002U;
	owner.product = 0x731fU;
	error = amdrdna_vbios_find_in_acpi(acpi, at + 0x1cU + sizeof(image), &owner, &offset, &length);
	check(error == 0 && offset == at + 0x1cU && length == sizeof(image), "vbios.acpi", "the function's record was not found");

	owner.bus = 5U;
	error = amdrdna_vbios_find_in_acpi(acpi, at + 0x1cU + sizeof(image), &owner, &offset, &length);
	check(error == ENOENT, "vbios.acpi-other", "another function's image was found");

	owner.bus = 3U;
	error = amdrdna_vbios_find_in_acpi(acpi, at + 0x1cU + 100U, &owner, &offset, &length);
	check(error == EINVAL, "vbios.acpi-cut", "a cut image was taken");
}

/* Checks the layout of a 6128 MiB board with a 256 MiB window and the firmware's picture at 0. */
static void
test_layout(void)
{
	struct amdrdna_layout_input input;
	struct amdrdna_layout layout;
	int error;

	memset(&input, 0, sizeof(input));
	input.vram_bytes = 6128ULL << 20;
	input.window_bytes = 256ULL << 20;
	input.gop_present = true;
	input.gop_offset = 0U;
	input.gop_bytes = 1921ULL * 1080U * 4U;
	error = amdrdna_layout_plan(&input, &layout);
	check(error == 0, "layout.plan", "the plan was refused");
	check(layout.reserved_count == 3U &&
	    layout.reserved[0].reason == AMDRDNA_RANGE_TOP &&
	    layout.reserved[0].start == (6127ULL << 20) &&
	    layout.reserved[0].end == (6128ULL << 20) &&
	    layout.reserved[1].reason == AMDRDNA_RANGE_GOP &&
	    layout.reserved[1].end == 0x7eb000U &&
	    layout.reserved[2].reason == AMDRDNA_RANGE_TRAINING &&
	    layout.reserved[2].end == (32ULL << 20),
	    "layout.ranges", "the kept ranges are wrong");
	check(layout.cpu_pool_end == (256ULL << 20) &&
	    layout.gpu_pool_start == (256ULL << 20) &&
	    layout.gpu_pool_end == (6127ULL << 20),
	    "layout.pools", "the pools are wrong");

	/* A firmware range past the VRAM, and a tiny VRAM, are refused. */
	input.firmware_start = 6128ULL << 20;
	input.firmware_bytes = 4096U;
	error = amdrdna_layout_plan(&input, &layout);
	check(error == EINVAL, "layout.outside", "a range past the VRAM was taken");
	input.firmware_bytes = 0U;
	input.vram_bytes = 16ULL << 20;
	error = amdrdna_layout_plan(&input, &layout);
	check(error == EINVAL, "layout.small", "16 MiB of VRAM was planned");
}

/*
 * Fills the picture the first board run showed: the loader asked for
 * 640x480, and the firmware kept the monitor's 1920x1080 timing and
 * stretched a 640x480 surface over it, with the ratios Linux would write
 * (the 3.19 ratio moved up by five bits).
 */
static void
picture_scaled_case(
	struct amdrdna_picture_input *input)
{
	memset(input, 0, sizeof(*input));
	input->active_width = 1920U;
	input->active_height = 1080U;
	input->viewport_width = 640U;
	input->viewport_height = 480U;
	input->scaler_mode = 1U;
	input->scaler_width = 1920U;
	input->scaler_height = 1080U;
	input->horizontal_ratio = (uint32_t)(((640ULL << 19) / 1920U) << 5);
	input->vertical_ratio = (uint32_t)(((480ULL << 19) / 1080U) << 5);
	input->loader_present = true;
	input->loader_width = 640U;
	input->loader_height = 480U;
}

/* Checks the comparison of the loader's picture with the plane's viewport, and the shapes. */
static void
test_picture(void)
{
	struct amdrdna_picture_input input;
	struct amdrdna_picture picture;
	int error;

	/* The cold dump's picture: 1920x1080 fetched and shown pixel for pixel. */
	memset(&input, 0, sizeof(input));
	input.active_width = 1920U;
	input.active_height = 1080U;
	input.viewport_width = 1920U;
	input.viewport_height = 1080U;
	input.scaler_width = 1920U;
	input.scaler_height = 1080U;
	input.horizontal_ratio = 1U << 24;
	input.vertical_ratio = 1U << 24;
	input.loader_present = true;
	input.loader_width = 1920U;
	input.loader_height = 1080U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 &&
	    picture.shape == AMDRDNA_PICTURE_WHOLE &&
	    picture.rectangle_inside &&
	    picture.ratios_agree,
	    "picture.whole", "a 1920x1080 picture on a 1920x1080 timing was not whole");

	/* The same with the scaler's registers left zero: still whole, the rectangle only noted. */
	input.scaler_width = 0U;
	input.scaler_height = 0U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 &&
	    picture.shape == AMDRDNA_PICTURE_WHOLE &&
	    !picture.rectangle_inside &&
	    !picture.ratios_agree,
	    "picture.whole-unset", "an unset scaler changed the verdict");

	/* The first board run: a 640x480 surface stretched over 1920x1080. */
	picture_scaled_case(&input);
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 &&
	    picture.shape == AMDRDNA_PICTURE_SCALED &&
	    picture.rectangle_inside &&
	    picture.ratios_agree,
	    "picture.scaled", "a 640x480 surface stretched over 1920x1080 was refused");
	check(strcmp(amdrdna_picture_shape_name(picture.shape), "scaled") == 0,
	    "picture.name", "the shape's name is wrong");

	/* Stretched with its aspect kept: 1440x1080 at 240,0. */
	picture_scaled_case(&input);
	input.scaler_x = 240U;
	input.scaler_width = 1440U;
	input.horizontal_ratio = (uint32_t)(((640ULL << 19) / 1440U) << 5);
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 &&
	    picture.shape == AMDRDNA_PICTURE_SCALED &&
	    picture.rectangle_inside &&
	    picture.ratios_agree,
	    "picture.scaled-aspect", "a pillar-boxed stretch was refused");

	/* Unscaled in the middle of the screen. */
	picture_scaled_case(&input);
	input.scaler_mode = 0U;
	input.scaler_x = 640U;
	input.scaler_y = 300U;
	input.scaler_width = 640U;
	input.scaler_height = 480U;
	input.horizontal_ratio = 1U << 24;
	input.vertical_ratio = 1U << 24;
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 &&
	    picture.shape == AMDRDNA_PICTURE_PLACED &&
	    picture.rectangle_inside &&
	    picture.ratios_agree,
	    "picture.placed", "a centred 640x480 picture was not placed");

	/*
	 * The second board run: scaling on, the ratios left at one and the
	 * scaler working them out by itself; agrees, and turned off it is noted.
	 */
	picture_scaled_case(&input);
	input.horizontal_ratio = 1U << 24;
	input.vertical_ratio = 1U << 24;
	input.scaler_automatic = 1U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 &&
	    picture.shape == AMDRDNA_PICTURE_SCALED &&
	    picture.rectangle_inside &&
	    picture.ratios_agree,
	    "picture.automatic-scale", "a stretch with ratios the scaler works out was not taken");
	input.scaler_automatic = 0U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 && !picture.ratios_agree,
	    "picture.manual-one", "ratios of one for a 640x480 to 1920x1080 stretch were not noted");

	/* The scaler centring the viewport by itself, its rectangle the whole screen. */
	picture_scaled_case(&input);
	input.scaler_mode = 0U;
	input.scaler_automatic = 2U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 &&
	    picture.shape == AMDRDNA_PICTURE_PLACED &&
	    picture.ratios_agree,
	    "picture.automatic-centre", "a picture the scaler centres was not placed");

	/* A ratio that does not fit, and a rectangle past the screen, are only noted. */
	picture_scaled_case(&input);
	input.horizontal_ratio = 1U << 24;
	input.scaler_height = 1200U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 &&
	    picture.shape == AMDRDNA_PICTURE_SCALED &&
	    !picture.rectangle_inside &&
	    !picture.ratios_agree,
	    "picture.notes", "a wrong ratio or rectangle was not noted, or was refused");

	/* The loader's picture of another size than the viewport is refused. */
	picture_scaled_case(&input);
	input.viewport_width = 1920U;
	input.viewport_height = 1080U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == EINVAL, "picture.other-size", "a 640x480 loader picture matched a 1920x1080 viewport");

	/* The same viewport without a loader picture on this GPU is accepted. */
	input.loader_present = false;
	error = amdrdna_picture_check(&input, &picture);
	check(error == 0 && picture.shape == AMDRDNA_PICTURE_WHOLE,
	    "picture.no-loader", "a picture without a loader's was refused");

	/* A viewport moved into the surface, and an empty one, are refused. */
	picture_scaled_case(&input);
	input.viewport_y = 8U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == ENOTSUP, "picture.moved", "a moved viewport was taken");
	picture_scaled_case(&input);
	input.viewport_width = 0U;
	error = amdrdna_picture_check(&input, &picture);
	check(error == EINVAL, "picture.empty", "an empty viewport was taken");
}

/* Checks the device-ID table and the chip names. */
static void
test_products(void)
{
	const struct amdrdna_product *product;
	struct amdrdna_ip graphics;

	product = amdrdna_product_find(0x731fU);
	check(product != NULL && product->generation == AMDRDNA_GENERATION_RDNA1 && strcmp(product->chip, "navi10") == 0,
	    "products.rx5600xt", "0x731f is not Navi10");
	product = amdrdna_product_find(0x73bfU);
	check(product != NULL && product->generation == AMDRDNA_GENERATION_RDNA2, "products.rdna2", "0x73bf is not RDNA2");
	product = amdrdna_product_find(0x744cU);
	check(product != NULL && product->generation == AMDRDNA_GENERATION_RDNA3, "products.rdna3", "0x744c is not RDNA3");
	product = amdrdna_product_find(0x67dfU);
	check(product == NULL, "products.polaris", "a Polaris ID was recognized");

	memset(&graphics, 0, sizeof(graphics));
	graphics.found = 1U;
	graphics.major = 10U;
	graphics.minor = 1U;
	graphics.step = 10U;
	check(strcmp(amdrdna_graphics_chip_name(&graphics), "navi10") == 0, "products.gc", "10.1.10 is not navi10");
	graphics.step = 9U;
	check(amdrdna_graphics_chip_name(&graphics) == NULL, "products.gc-unknown", "10.1.9 was named");
}

/* Reads one file of a dump; 0 bytes when it is absent. */
static size_t
read_file(
	const char *directory,
	const char *name,
	uint8_t **bytes)
{
	char path[1024];
	FILE *stream;
	long length;

	*bytes = NULL;
	snprintf(path, sizeof(path), "%s/%s", directory, name);
	stream = fopen(path, "rb");
	if (stream == NULL)
		return 0U;
	fseek(stream, 0L, SEEK_END);
	length = ftell(stream);
	fseek(stream, 0L, SEEK_SET);
	*bytes = malloc((size_t)length);
	if (*bytes == NULL || fread(*bytes, 1U, (size_t)length, stream) != (size_t)length) {
		fclose(stream);
		free(*bytes);
		*bytes = NULL;
		return 0U;
	}
	fclose(stream);
	return (size_t)length;
}

/* Checks the readers on a hardware dump of the RX 5600 XT. */
static void
test_dump(
	const char *directory)
{
	struct amdrdna_block_table table;
	struct amdrdna_boot_database database;
	struct amdrdna_vbios_owner owner;
	struct amdrdna_vbios_info info;
	struct amdrdna_vbios_info rom_info;
	uint8_t *bytes;
	size_t length;
	size_t offset;
	size_t image_length;
	int error;

	/* The block table. */
	length = read_file(directory, "discovery.bin", &bytes);
	if (length != 0U) {
		error = amdrdna_block_table_parse(bytes, length, &table);
		check(error == 0, "dump.block-table", "the dumped table was refused");
		printf("dump: layout %u.%u list %u listed %u gc %u.%u.%u dcn %u.%u.%u psp %u.%u.%u smu %u.%u.%u sdma %u.%u.%u ih %u.%u.%u nbio %u.%u.%u windows gc 0x%x/0x%x dcn 0x%x/0x%x/0x%x/0x%x shape %u se %u sa %u pairs %u rb %u l2 %u wave %u\n",
		    table.layout_major, table.layout_minor, table.list_layout, table.listed,
		    table.blocks[AMDRDNA_BLOCK_GC].major, table.blocks[AMDRDNA_BLOCK_GC].minor, table.blocks[AMDRDNA_BLOCK_GC].step,
		    table.blocks[AMDRDNA_BLOCK_DCN].major, table.blocks[AMDRDNA_BLOCK_DCN].minor, table.blocks[AMDRDNA_BLOCK_DCN].step,
		    table.blocks[AMDRDNA_BLOCK_PSP].major, table.blocks[AMDRDNA_BLOCK_PSP].minor, table.blocks[AMDRDNA_BLOCK_PSP].step,
		    table.blocks[AMDRDNA_BLOCK_SMU].major, table.blocks[AMDRDNA_BLOCK_SMU].minor, table.blocks[AMDRDNA_BLOCK_SMU].step,
		    table.blocks[AMDRDNA_BLOCK_SDMA0].major, table.blocks[AMDRDNA_BLOCK_SDMA0].minor, table.blocks[AMDRDNA_BLOCK_SDMA0].step,
		    table.blocks[AMDRDNA_BLOCK_IH].major, table.blocks[AMDRDNA_BLOCK_IH].minor, table.blocks[AMDRDNA_BLOCK_IH].step,
		    table.blocks[AMDRDNA_BLOCK_NBIO].major, table.blocks[AMDRDNA_BLOCK_NBIO].minor, table.blocks[AMDRDNA_BLOCK_NBIO].step,
		    (unsigned)table.blocks[AMDRDNA_BLOCK_GC].window[0], (unsigned)table.blocks[AMDRDNA_BLOCK_GC].window[1],
		    (unsigned)table.blocks[AMDRDNA_BLOCK_DCN].window[0], (unsigned)table.blocks[AMDRDNA_BLOCK_DCN].window[1],
		    (unsigned)table.blocks[AMDRDNA_BLOCK_DCN].window[2], (unsigned)table.blocks[AMDRDNA_BLOCK_DCN].window[3],
		    table.has_shape ? 1U : 0U, (unsigned)table.shape.engines, (unsigned)table.shape.arrays_per_engine,
		    (unsigned)table.shape.pairs_per_array, (unsigned)table.shape.backends_per_engine,
		    (unsigned)table.shape.cache_channels, (unsigned)table.shape.wave_lanes);
		check(table.blocks[AMDRDNA_BLOCK_GC].major == 10U &&
		    table.blocks[AMDRDNA_BLOCK_GC].minor == 1U &&
		    table.blocks[AMDRDNA_BLOCK_GC].step == 10U &&
		    table.blocks[AMDRDNA_BLOCK_GC].window[0] == 0x1260U &&
		    table.blocks[AMDRDNA_BLOCK_DCN].major == 2U &&
		    table.blocks[AMDRDNA_BLOCK_DCN].step == 2U &&
		    table.blocks[AMDRDNA_BLOCK_DCN].window[2] == 0x34c0U &&
		    table.blocks[AMDRDNA_BLOCK_PSP].window[0] == 0x16000U &&
		    table.blocks[AMDRDNA_BLOCK_NBIO].window[2] == 0xd20U &&
		    table.blocks[AMDRDNA_BLOCK_SMUIO].window[0] == 0x16800U &&
		    table.listed == 39U,
		    "dump.block-table.values", "the dumped table does not match the Linux sysfs view");
		free(bytes);
	}

	/* The boot database's place holds none on this board. */
	length = read_file(directory, "runtime-db.bin", &bytes);
	if (length != 0U) {
		error = amdrdna_boot_database_parse(bytes, length, &database);
		check(error == 0 && !database.present, "dump.boot-database", "a database was found where Linux found none");
		free(bytes);
	}

	/* The ACPI image of 3:0.0 and its tables. */
	length = read_file(directory, "vfct.bin", &bytes);
	if (length != 0U) {
		owner.bus = 3U;
		owner.device = 0U;
		owner.function = 0U;
		owner.vendor = 0x1002U;
		owner.product = 0x731fU;
		error = amdrdna_vbios_find_in_acpi(bytes, length, &owner, &offset, &image_length);
		check(error == 0 && offset == 0x68U && image_length == 0xe200U, "dump.vfct", "the image record was not found");
		error = amdrdna_vbios_parse(bytes + offset, image_length, &info);
		check(error == 0, "dump.vbios", "the image was refused");
		printf("dump: vbios header 0x%x data 0x%x commands 0x%x fw %u.%u caps 0x%x sclk %u mclk %u scratch 0x%x pptid %u top %uK reserve %u.%u fw %uK@%uK pp %u.%u rev %u %uB shutdown %u dpm %u.%u post 0x%x/%u/%u enc 0x%x pclk 0x%x pg 0x%x crtc 0x%x dceclk 0x%x tx 0x%x aux 0x%x\n",
		    info.header_offset, info.data_list, info.command_list, info.firmware_format, info.firmware_content,
		    (unsigned)info.capability_bits, (unsigned)info.boot_engine_clock_10khz, (unsigned)info.boot_memory_clock_10khz,
		    (unsigned)info.scratch_first, (unsigned)info.power_table_id, (unsigned)info.reserved_top_kib,
		    info.reserve_format, info.reserve_content, (unsigned)info.firmware_kib, (unsigned)info.firmware_start_kib,
		    info.power_format, info.power_content, info.power_revision, info.power_bytes, info.shutdown_temperature,
		    info.board_format, info.board_content,
		    info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset, info.commands[AMDRDNA_VBIOS_COMMAND_POST].work_bytes,
		    info.commands[AMDRDNA_VBIOS_COMMAND_POST].parameter_bytes,
		    info.commands[AMDRDNA_VBIOS_COMMAND_ENCODER].offset, info.commands[AMDRDNA_VBIOS_COMMAND_PIXEL_CLOCK].offset,
		    info.commands[AMDRDNA_VBIOS_COMMAND_PIPE_POWER].offset, info.commands[AMDRDNA_VBIOS_COMMAND_TIMING_ENABLE].offset,
		    info.commands[AMDRDNA_VBIOS_COMMAND_DISPLAY_CLOCK].offset, info.commands[AMDRDNA_VBIOS_COMMAND_TRANSMITTER].offset,
		    info.commands[AMDRDNA_VBIOS_COMMAND_AUX].offset);
		check(info.header_offset == 0x2c4U &&
		    info.data_list == 0x92c2U &&
		    info.command_list == 0x921cU &&
		    info.firmware_format == 3U &&
		    info.firmware_content == 3U &&
		    info.capability_bits == 0x1U &&
		    info.scratch_first == 0x4cU &&
		    info.power_table_id == 0U &&
		    info.power_format == 12U &&
		    info.shutdown_temperature == 118U &&
		    info.board_format == 4U &&
		    info.board_content == 5U &&
		    info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset == 0x930cU &&
		    info.commands[AMDRDNA_VBIOS_COMMAND_TRANSMITTER].offset == 0xae78U,
		    "dump.vbios.values", "the image's tables do not match the Python read of the same image");

		/* The flash copy declares the same length and reads the same tables. */
		free(bytes);
		length = read_file(directory, "rom.bin", &bytes);
		if (length != 0U) {
			check(amdrdna_vbios_declared_length(bytes) == 0xe200U, "dump.rom.length", "the flash image declares another length");
			error = amdrdna_vbios_parse(bytes, amdrdna_vbios_declared_length(bytes), &rom_info);
			printf("dump: rom caps 0x%x (the firmware marks its running copy, which the ACPI table holds)\n", (unsigned)rom_info.capability_bits);
			check(error == 0 &&
			    rom_info.firmware_table == info.firmware_table &&
			    rom_info.data_list == info.data_list &&
			    rom_info.power_table == info.power_table &&
			    rom_info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset == info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset,
			    "dump.rom.tables", "the flash image has its tables elsewhere than the ACPI image");
		}
	}
	free(bytes);
}

/* Prints every display register index for Navi10's display windows. */
static void
print_dcn(void)
{
	struct amdrdna_ip display;
	unsigned which;
	unsigned instance;

	navi10_display(&display);
	for (which = 0U; which < AMDRDNA_DCN_REGISTER_COUNT; which++) {
		for (instance = 0U; instance < AMDRDNA_DISPLAY_PIPES; instance++)
			printf("%u %u 0x%05x\n", which, instance, (unsigned)amdrdna_dcn_index(&display, (enum amdrdna_dcn_register)which, instance));
	}
}

/* Fills the display block with Navi10's windows. */
static void
navi10_display(
	struct amdrdna_ip *display)
{
	memset(display, 0, sizeof(*display));
	display->found = 1U;
	display->major = 2U;
	display->step = 2U;
	display->window_count = 4U;
	display->window[0] = 0x12U;
	display->window[1] = 0xc0U;
	display->window[2] = 0x34c0U;
	display->window[3] = 0x9000U;
}
