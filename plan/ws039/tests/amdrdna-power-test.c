/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amdrdna driver's power tables of stage A4 (the full
 * tier): the board's power table put together from the video BIOS's power
 * table and board table, the power table a firmware file of the unit
 * carries, the readings the unit's firmware hands back, and the feature
 * words.
 *
 *   amdrdna-power-test [--linux DIRECTORY] [--dump DIRECTORY] [--firmware DIRECTORY]
 *
 * --linux names the directory amdrdna-power-linux wrote: tables laid out
 * by the Linux v6.19 structures, which the driver's reading must match byte
 * for byte.  --dump names the board's dump (rom.bin, the video BIOS of the
 * RX 5600 XT of 2026-10-16), --firmware the navi10_*.bin files of
 * linux-firmware 20260410.  Without one of them its cases are skipped and
 * say so.  Each case prints "PASS name" or "FAIL name: why"; the exit
 * status is 1 when any case failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/*
 * The places in the synthetic tables, written out here from the Linux
 * structures (the --linux cases check the same places against the
 * structures themselves): the power table's length, its controller kind,
 * shutdown temperature and the start of the firmware's part; the
 * firmware's features, limits and board part; the board table's length and
 * board part; the readings.
 */
#define POWER_BYTES		1674U
#define POWER_CONTROLLER_AT	21U
#define POWER_SHUTDOWN_AT	32U
#define POWER_PART_AT		482U
#define PART_FEATURES_AT	4U
#define PART_EDGE_AT		52U
#define PART_HOTSPOT_AT		54U
#define PART_MEMORY_AT		56U
#define PART_BOARD_AT		964U
#define PART_BOARD_BYTES	196U
#define BOARD_BYTES		200U
#define BOARD_PART_AT		4U
#define READING_EDGE_AT		34U
#define READING_HOTSPOT_AT	36U
#define READING_MEMORY_AT	38U
#define READING_FAN_AT		62U

/* The case results. */
static int failures;

static void check(int condition, const char *name, const char *why);
static void test_build(void);
static void test_refusals(void);
static void test_firmware_file(void);
static void test_readings(void);
static void test_features(void);
static void test_linux(const char *directory);
static void test_dump(const char *directory);
static void test_real_firmware(const char *directory);
static void fill(uint8_t *bytes, size_t length, unsigned step, unsigned start);
static uint8_t *read_file(const char *path, size_t *length);
static uint8_t *read_named(const char *directory, const char *name, size_t *length);
static void put16(uint8_t *bytes, size_t at, uint16_t value);
static void put32(uint8_t *bytes, size_t at, uint32_t value);

int
main(
	int argc,
	char **argv)
{
	const char *linux_directory;
	const char *dump_directory;
	const char *firmware_directory;
	int index;

	/* Reads the options. */
	linux_directory = NULL;
	dump_directory = NULL;
	firmware_directory = NULL;
	for (index = 1; index + 1 < argc; index += 2) {
		if (strcmp(argv[index], "--linux") == 0) {
			linux_directory = argv[index + 1];
		} else if (strcmp(argv[index], "--dump") == 0) {
			dump_directory = argv[index + 1];
		} else if (strcmp(argv[index], "--firmware") == 0) {
			firmware_directory = argv[index + 1];
		} else {
			fprintf(stderr, "usage: amdrdna-power-test [--linux DIR] [--dump DIR] [--firmware DIR]\n");
			return 2;
		}
	}

	/* Runs the synthetic cases. */
	test_build();
	test_refusals();
	test_firmware_file();
	test_readings();
	test_features();

	/* Runs the cases of each directory given. */
	if (linux_directory != NULL) {
		test_linux(linux_directory);
	} else {
		printf("SKIP the Linux layout: no directory\n");
	}
	if (dump_directory != NULL) {
		test_dump(dump_directory);
	} else {
		printf("SKIP the board's video BIOS: no dump\n");
	}
	if (firmware_directory != NULL) {
		test_real_firmware(firmware_directory);
	} else {
		printf("SKIP the firmware files: no directory\n");
	}

	/* Reports the whole result. */
	if (failures != 0) {
		printf("FAIL amdrdna-power-test: %d case(s)\n", failures);
		return 1;
	}

	printf("PASS amdrdna-power-test\n");
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

/* A synthetic power table and board table put together. */
static void
test_build(void)
{
	struct amdrdna_power_limits limits;
	uint8_t power[POWER_BYTES];
	uint8_t board[BOARD_BYTES];
	uint8_t table[AMDRDNA_SMU_POWER_TABLE_BYTES];
	int same_part;
	int same_board;
	int same_tail;
	size_t at;
	int error;

	/* Two tables whose every byte says where it came from. */
	fill(power, sizeof(power), 7U, 3U);
	fill(board, sizeof(board), 13U, 5U);
	put16(power, POWER_SHUTDOWN_AT, 118U);
	power[POWER_CONTROLLER_AT] = 27U;
	put32(power, POWER_PART_AT + PART_FEATURES_AT, 0xa3ddaffbU);
	put32(power, POWER_PART_AT + PART_FEATURES_AT + 4U, 0x00000623U);
	put16(power, POWER_PART_AT + PART_EDGE_AT, 100U);
	put16(power, POWER_PART_AT + PART_HOTSPOT_AT, 106U);
	put16(power, POWER_PART_AT + PART_MEMORY_AT, 105U);

	/* Puts them together. */
	memset(table, 0, sizeof(table));
	error = amdrdna_power_table_build(power, sizeof(power), board, sizeof(board), 4U, 5U, table, &limits);
	check(error == 0, "build.ok", "a sound pair is refused");

	/* The firmware's part up to the board part, the board part, and the rest. */
	same_part = memcmp(table, power + POWER_PART_AT, PART_BOARD_AT) == 0;
	same_board = memcmp(table + PART_BOARD_AT, board + BOARD_PART_AT, PART_BOARD_BYTES) == 0;
	at = PART_BOARD_AT + PART_BOARD_BYTES;
	same_tail = memcmp(table + at, power + POWER_PART_AT + at, AMDRDNA_SMU_POWER_TABLE_BYTES - at) == 0;
	check(same_part, "build.part", "the firmware's part is not copied");
	check(same_board, "build.board", "the board part is not the board table's");
	check(same_tail, "build.tail", "the part after the board part is not the power table's");

	/* The limits. */
	check(limits.edge == 100U, "build.edge", "the edge limit is not 100");
	check(limits.hotspot == 106U, "build.hotspot", "the hotspot limit is not 106");
	check(limits.memory == 105U, "build.memory", "the memory limit is not 105");
	check(limits.shutdown == 118U, "build.shutdown", "the shutdown temperature is not 118");
	check(limits.controller == 27U, "build.controller", "the controller kind is not 27");
	check(limits.features_low == 0xa3ddaffbU && limits.features_high == 0x00000623U, "build.features",
	    "the features to run are not 0x00000623a3ddaffb");
}

/* Tables the driver must not hand over. */
static void
test_refusals(void)
{
	struct amdrdna_power_limits limits;
	uint8_t power[POWER_BYTES];
	uint8_t board[BOARD_BYTES];
	uint8_t table[AMDRDNA_SMU_POWER_TABLE_BYTES];
	int error;

	/* Some tables. */
	fill(power, sizeof(power), 3U, 1U);
	fill(board, sizeof(board), 5U, 2U);

	/* Each refusal. */
	error = amdrdna_power_table_build(power, POWER_BYTES - 1U, board, sizeof(board), 4U, 5U, table, &limits);
	check(error == EINVAL, "refuse.power-short", "a short power table is taken");
	error = amdrdna_power_table_build(power, sizeof(power), NULL, 0U, 4U, 5U, table, &limits);
	check(error == EINVAL, "refuse.no-board", "a missing board table is taken");
	error = amdrdna_power_table_build(power, sizeof(power), board, sizeof(board), 3U, 5U, table, &limits);
	check(error == EINVAL, "refuse.board-format", "board table format 3 is taken");
	error = amdrdna_power_table_build(power, sizeof(power), board, sizeof(board), 4U, 7U, table, &limits);
	check(error == EINVAL, "refuse.board-content", "board table content 7 (Navi 12) is taken");
	error = amdrdna_power_table_build(power, sizeof(power), board, BOARD_BYTES - 1U, 4U, 5U, table, &limits);
	check(error == EINVAL, "refuse.board-short", "a short board table is taken");
}

/* Synthetic firmware files of the unit: layout 2.1 with a list, 2.0 with one table, and the refusals. */
static void
test_firmware_file(void)
{
	uint8_t file[512];
	size_t offset;
	size_t bytes;
	int error;

	/* Layout 2.1: three entries at 100, tables at 200, 260 and 320. */
	memset(file, 0, sizeof(file));
	put16(file, 8U, 2U);
	put16(file, 10U, 1U);
	put32(file, 36U, 3U);
	put32(file, 40U, 100U);
	put32(file, 100U, 11U);
	put32(file, 104U, 200U);
	put32(file, 108U, 60U);
	put32(file, 112U, 22U);
	put32(file, 116U, 260U);
	put32(file, 120U, 60U);
	put32(file, 124U, 33U);
	put32(file, 128U, 320U);
	put32(file, 132U, 50U);
	error = amdrdna_power_table_in_firmware(file, sizeof(file), 22U, &offset, &bytes);
	check(error == 0 && offset == 260U && bytes == 60U, "file.list", "ID 22 is not the table at 260");
	error = amdrdna_power_table_in_firmware(file, sizeof(file), 44U, &offset, &bytes);
	check(error == ENOENT, "file.list-missing", "a missing ID is found");

	/* A list past the file's end, and a table past it. */
	put32(file, 36U, 40U);
	error = amdrdna_power_table_in_firmware(file, sizeof(file), 22U, &offset, &bytes);
	check(error == EINVAL, "file.list-past-end", "a list past the end is read");
	put32(file, 36U, 3U);
	put32(file, 120U, 400U);
	error = amdrdna_power_table_in_firmware(file, sizeof(file), 22U, &offset, &bytes);
	check(error == EINVAL, "file.table-past-end", "a table past the end is taken");

	/* Layout 2.0: one table, whatever the ID. */
	put16(file, 10U, 0U);
	put32(file, 36U, 300U);
	put32(file, 40U, 100U);
	error = amdrdna_power_table_in_firmware(file, sizeof(file), 99U, &offset, &bytes);
	check(error == 0 && offset == 300U && bytes == 100U, "file.one", "layout 2.0's table is not taken");

	/* Layout 1 carries no table; layout 2.2 is not known; a short file is refused. */
	put16(file, 8U, 1U);
	error = amdrdna_power_table_in_firmware(file, sizeof(file), 99U, &offset, &bytes);
	check(error == ENOENT, "file.no-tables", "layout 1 has a table");
	put16(file, 8U, 2U);
	put16(file, 10U, 2U);
	error = amdrdna_power_table_in_firmware(file, sizeof(file), 99U, &offset, &bytes);
	check(error == EINVAL, "file.unknown", "layout 2.2 is read");
	error = amdrdna_power_table_in_firmware(file, 43U, 99U, &offset, &bytes);
	check(error == EINVAL, "file.short", "a 43-byte file is read");
}

/* Synthetic readings. */
static void
test_readings(void)
{
	struct amdrdna_power_readings readings;
	uint8_t bytes[AMDRDNA_SMU_READINGS_BYTES];

	/* A table with the four fields set among other bytes. */
	fill(bytes, sizeof(bytes), 11U, 9U);
	put16(bytes, READING_EDGE_AT, 47U);
	put16(bytes, READING_HOTSPOT_AT, 52U);
	put16(bytes, READING_MEMORY_AT, 50U);
	put16(bytes, READING_FAN_AT, 1234U);
	amdrdna_power_readings_read(bytes, &readings);
	check(readings.edge == 47U, "readings.edge", "the edge is not 47");
	check(readings.hotspot == 52U, "readings.hotspot", "the hotspot is not 52");
	check(readings.memory == 50U, "readings.memory", "the memory is not 50");
	check(readings.fan_speed == 1234U, "readings.fan", "the fan is not 1234 rpm");
}

/* The feature words. */
static void
test_features(void)
{
	/* All three, with others. */
	check(amdrdna_power_features_on(0xffffffffU, 0x00000623U), "features.all", "all three are not seen");
	check(amdrdna_power_features_on(0x80000000U, 0x00000003U), "features.three", "exactly three are not seen");

	/* One missing each. */
	check(!amdrdna_power_features_on(0x7fffffffU, 0x00000003U), "features.no-stop", "a missing stop is taken");
	check(!amdrdna_power_features_on(0x80000000U, 0x00000002U), "features.no-fan", "a missing fan control is taken");
	check(!amdrdna_power_features_on(0x80000000U, 0x00000001U), "features.no-thermal", "a missing thermal control is taken");
}

/* The tables amdrdna-power-linux laid out with the Linux structures. */
static void
test_linux(
	const char *directory)
{
	struct amdrdna_power_limits limits;
	struct amdrdna_power_readings readings;
	uint8_t table[AMDRDNA_SMU_POWER_TABLE_BYTES];
	uint8_t *power;
	uint8_t *board;
	uint8_t *expected;
	uint8_t *metrics;
	uint8_t *legacy;
	size_t power_bytes;
	size_t board_bytes;
	size_t expected_bytes;
	size_t metrics_bytes;
	size_t legacy_bytes;
	int error;

	/* Reads the five files. */
	power = read_named(directory, "power.bin", &power_bytes);
	board = read_named(directory, "board.bin", &board_bytes);
	expected = read_named(directory, "expected.bin", &expected_bytes);
	metrics = read_named(directory, "metrics.bin", &metrics_bytes);
	legacy = read_named(directory, "legacy.bin", &legacy_bytes);
	if (power == NULL || board == NULL || expected == NULL || metrics == NULL || legacy == NULL) {
		check(0, "linux.files", "a file of the Linux layout is missing");
		free(power);
		free(board);
		free(expected);
		free(metrics);
		free(legacy);
		return;
	}

	/* The sizes the driver uses are the structures'. */
	check(power_bytes == POWER_BYTES, "linux.power-size", "the power table is not 1674 bytes");
	check(board_bytes == BOARD_BYTES, "linux.board-size", "the board table is not 200 bytes");
	check(expected_bytes == AMDRDNA_SMU_POWER_TABLE_BYTES, "linux.table-size", "the firmware's table is not the driver's size");
	check(metrics_bytes == AMDRDNA_SMU_READINGS_BYTES, "linux.readings-size", "the readings are not the driver's size");
	check(legacy_bytes <= AMDRDNA_SMU_READINGS_BYTES, "linux.legacy-size", "the older readings are larger than the driver's");

	/* The table the driver puts together is the one Linux does, byte for byte. */
	error = amdrdna_power_table_build(power, power_bytes, board, board_bytes, 4U, 5U, table, &limits);
	check(error == 0, "linux.build", "the Linux pair is refused");
	check(error == 0 && memcmp(table, expected, AMDRDNA_SMU_POWER_TABLE_BYTES) == 0, "linux.table",
	    "the table differs from the one Linux puts together");
	check(limits.edge == 100U && limits.hotspot == 106U && limits.memory == 105U, "linux.limits",
	    "the limits are not 100, 106 and 105");
	check(limits.shutdown == 118U && limits.controller == 27U, "linux.shutdown",
	    "the shutdown temperature and the controller are not 118 and 27");
	check(limits.features_low == 0xa3ddaffbU && limits.features_high == 0x00000623U, "linux.features",
	    "the features to run are not the Linux table's");

	/* The readings of both layouts. */
	amdrdna_power_readings_read(metrics, &readings);
	check(readings.edge == 47U && readings.hotspot == 52U && readings.memory == 50U && readings.fan_speed == 1234U,
	    "linux.readings", "the readings are not 47, 52, 50 and 1234");
	amdrdna_power_readings_read(legacy, &readings);
	check(readings.edge == 41U && readings.hotspot == 43U && readings.memory == 42U && readings.fan_speed == 987U,
	    "linux.readings-legacy", "the older readings are not 41, 43, 42 and 987");

	free(power);
	free(board);
	free(expected);
	free(metrics);
	free(legacy);
}

/* The board's video BIOS: the table the driver would hand over and its limits. */
static void
test_dump(
	const char *directory)
{
	struct amdrdna_vbios_info info;
	struct amdrdna_power_limits limits;
	uint8_t table[AMDRDNA_SMU_POWER_TABLE_BYTES];
	uint8_t *image;
	size_t length;
	uint16_t power_bytes;
	uint16_t board_bytes;
	int error;

	/* Reads the image. */
	image = read_named(directory, "rom.bin", &length);
	if (image == NULL) {
		printf("SKIP the board's video BIOS: no rom.bin in %s\n", directory);
		return;
	}

	/* Reads its tables as stage A1v does. */
	error = amdrdna_vbios_parse(image, length, &info);
	check(error == 0, "dump.parse", "the image is not read");
	check(info.power_table_id == 0U, "dump.table-id", "the image names a firmware file's power table");
	check(info.power_table != 0U && info.board_table != 0U, "dump.tables", "the power or board table is missing");
	check(info.board_format == 4U && info.board_content == 5U, "dump.board-version", "the board table is not 4.5");
	if (error != 0 || info.power_table == 0U || info.board_table == 0U) {
		free(image);
		return;
	}

	/* Puts the table together. */
	power_bytes = amdrdna_le16(image, info.power_table);
	board_bytes = amdrdna_le16(image, info.board_table);
	error = amdrdna_power_table_build(image + info.power_table, power_bytes, image + info.board_table, board_bytes,
	    info.board_format, info.board_content, table, &limits);
	check(error == 0, "dump.build", "the board's tables are refused");

	/*
	 * The limits Linux showed on the board (hwmon: edge critical 100 C,
	 * junction critical 106 C) and the shutdown temperature stage A1v
	 * read (118 C).
	 */
	check(limits.edge == 100U, "dump.edge", "the edge limit is not 100");
	check(limits.hotspot == 106U, "dump.hotspot", "the hotspot limit is not 106");
	check(limits.memory == 105U, "dump.memory", "the memory limit is not 105");
	check(limits.shutdown == 118U, "dump.shutdown", "the shutdown temperature is not 118");
	check(limits.controller != 0U, "dump.controller", "the board has no thermal controller");
	check(memcmp(table + PART_BOARD_AT, image + info.board_table + BOARD_PART_AT, PART_BOARD_BYTES) == 0,
	    "dump.board", "the board part is not the board table's");
	printf("NOTE dump: power %uB board %uB controller %u features 0x%08x%08x\n",
	    (unsigned)power_bytes,
	    (unsigned)board_bytes,
	    limits.controller,
	    (unsigned)limits.features_high,
	    (unsigned)limits.features_low);
	free(image);
}

/* The real firmware file of the unit: its list of power tables. */
static void
test_real_firmware(
	const char *directory)
{
	static const uint32_t identities[3] = { 2187U, 2207U, 2204U };
	struct amdrdna_power_limits limits;
	uint8_t board[BOARD_BYTES];
	uint8_t table[AMDRDNA_SMU_POWER_TABLE_BYTES];
	uint8_t *file;
	size_t length;
	size_t offset;
	size_t bytes;
	unsigned index;
	char name[64];
	int error;

	/* Reads the file. */
	file = read_named(directory, "navi10_smc.bin", &length);
	if (file == NULL) {
		printf("SKIP the firmware files: no navi10_smc.bin in %s\n", directory);
		return;
	}

	/* Each of its three tables is a whole power table that can be put together. */
	fill(board, sizeof(board), 13U, 5U);
	for (index = 0U; index < 3U; index++) {
		snprintf(name, sizeof(name), "firmware.table-%u", (unsigned)identities[index]);
		error = amdrdna_power_table_in_firmware(file, length, identities[index], &offset, &bytes);
		check(error == 0 && bytes == POWER_BYTES, name, "the table is missing or not 1674 bytes");
		if (error != 0)
			continue;
		error = amdrdna_power_table_build(file + offset, bytes, board, sizeof(board), 4U, 5U, table, &limits);
		snprintf(name, sizeof(name), "firmware.build-%u", (unsigned)identities[index]);
		check(error == 0 && limits.edge != 0U && limits.shutdown != 0U, name, "the table is refused or has no limits");
		printf("NOTE firmware table %u: edge %uC hotspot %uC mem %uC shutdown %uC\n",
		    (unsigned)identities[index], limits.edge, limits.hotspot, limits.memory, limits.shutdown);
	}

	/* An ID the file does not carry. */
	error = amdrdna_power_table_in_firmware(file, length, 1U, &offset, &bytes);
	check(error == ENOENT, "firmware.missing", "ID 1 is found");
	free(file);
}

/* Fills a buffer with bytes that differ from one place to the next. */
static void
fill(
	uint8_t *bytes,
	size_t length,
	unsigned step,
	unsigned start)
{
	size_t at;

	for (at = 0U; at < length; at++)
		bytes[at] = (uint8_t)(start + step * at);
}

/* Reads a whole file into memory; NULL when it cannot be read. */
static uint8_t *
read_file(
	const char *path,
	size_t *length)
{
	FILE *stream;
	uint8_t *bytes;
	long size;

	stream = fopen(path, "rb");
	if (stream == NULL)
		return NULL;
	if (fseek(stream, 0L, SEEK_END) != 0 || (size = ftell(stream)) <= 0 || fseek(stream, 0L, SEEK_SET) != 0) {
		fclose(stream);
		return NULL;
	}
	bytes = malloc((size_t)size);
	if (bytes == NULL || fread(bytes, 1U, (size_t)size, stream) != (size_t)size) {
		free(bytes);
		fclose(stream);
		return NULL;
	}
	fclose(stream);
	*length = (size_t)size;
	return bytes;
}

/* Reads one file of a directory. */
static uint8_t *
read_named(
	const char *directory,
	const char *name,
	size_t *length)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", directory, name);
	return read_file(path, length);
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
	bytes[at] = (uint8_t)value;
	bytes[at + 1U] = (uint8_t)(value >> 8);
	bytes[at + 2U] = (uint8_t)(value >> 16);
	bytes[at + 3U] = (uint8_t)(value >> 24);
}
