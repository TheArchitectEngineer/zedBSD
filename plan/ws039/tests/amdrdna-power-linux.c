/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Writes the tables amdrdna-power-test --linux compares with: a power
 * table and a board table laid out by the Linux v6.19 structures, the
 * table the unit's firmware takes put together the way Linux puts it
 * together (navi10_store_powerplay_table and the copy of
 * navi10_append_powerplay_table, through BoardReserved), and the readings
 * in both of the unit's layouts.
 *
 *   amdrdna-power-linux DIRECTORY
 *
 * Built with the Linux tree's headers on the include path
 * (drivers/gpu/drm/amd/include, pm/swsmu/inc and pm/swsmu/inc/pmfw_if).
 * The Linux names stay in this test; the driver has its own.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* What the Linux headers expect of the kernel's own headers. */
#define __counted_by(member)
#define __packed __attribute__((packed))
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#include "atomfirmware.h"
#include "smu11_driver_if_navi10.h"
#include "smu_v11_0_pptable.h"

static int write_file(const char *directory, const char *name, const void *bytes, size_t length);
static void fill(void *buffer, size_t length, unsigned step, unsigned start);

int
main(
	int argc,
	char **argv)
{
	static struct smu_11_0_powerplay_table power;
	static struct atom_smc_dpm_info_v4_5 board;
	static PPTable_t expected;
	static SmuMetrics_t metrics;
	static SmuMetrics_legacy_t legacy;
	size_t from;
	size_t to;
	size_t bytes;

	if (argc != 2) {
		fprintf(stderr, "usage: amdrdna-power-linux DIRECTORY\n");
		return 2;
	}

	/* The power table: every byte says where it came from, then the fields the driver reads. */
	fill(&power, sizeof(power), 7U, 3U);
	power.header.structuresize = sizeof(power);
	power.software_shutdown_temp = 118U;
	power.thermal_controller_type = 27U;
	power.smc_pptable.FeaturesToRun[0] = 0xa3ddaffbU;
	power.smc_pptable.FeaturesToRun[1] = 0x00000623U;
	power.smc_pptable.TedgeLimit = 100U;
	power.smc_pptable.ThotspotLimit = 106U;
	power.smc_pptable.TmemLimit = 105U;

	/* The board table. */
	fill(&board, sizeof(board), 13U, 5U);
	board.table_header.structuresize = sizeof(board);
	board.table_header.format_revision = 4U;
	board.table_header.content_revision = 5U;

	/* The table Linux hands over: the firmware's part, then the board part through BoardReserved. */
	memcpy(&expected, &power.smc_pptable, sizeof(expected));
	from = offsetof(struct atom_smc_dpm_info_v4_5, I2cControllers);
	to = offsetof(PPTable_t, I2cControllers);
	bytes = offsetof(PPTable_t, BoardReserved) + sizeof(expected.BoardReserved) - to;
	if (bytes != sizeof(board) - from) {
		fprintf(stderr, "amdrdna-power-linux: the board part is %zu bytes, the board table gives %zu\n", bytes, sizeof(board) - from);
		return 1;
	}
	memcpy((uint8_t *)&expected + to, (const uint8_t *)&board + from, bytes);

	/* The readings of both layouts. */
	fill(&metrics, sizeof(metrics), 11U, 9U);
	metrics.TemperatureEdge = 47U;
	metrics.TemperatureHotspot = 52U;
	metrics.TemperatureMem = 50U;
	metrics.CurrFanSpeed = 1234U;
	metrics.CurrClock[PPCLK_GFXCLK] = 300U;
	metrics.CurrClock[PPCLK_SOCCLK] = 600U;
	metrics.CurrClock[PPCLK_UCLK] = 875U;
	metrics.CurrClock[PPCLK_DCEFCLK] = 506U;
	metrics.CurrClock[PPCLK_DISPCLK] = 164U;
	metrics.AverageGfxclkFrequencyPostDs = 299U;
	metrics.AverageGfxActivity = 37U;
	fill(&legacy, sizeof(legacy), 17U, 1U);
	legacy.TemperatureEdge = 41U;
	legacy.TemperatureHotspot = 43U;
	legacy.TemperatureMem = 42U;
	legacy.CurrFanSpeed = 987U;
	legacy.CurrClock[PPCLK_GFXCLK] = 1750U;
	legacy.CurrClock[PPCLK_SOCCLK] = 1100U;
	legacy.CurrClock[PPCLK_UCLK] = 100U;
	legacy.CurrClock[PPCLK_DCEFCLK] = 810U;
	legacy.CurrClock[PPCLK_DISPCLK] = 600U;
	legacy.AverageGfxclkFrequency = 1600U;
	legacy.AverageGfxActivity = 99U;

	/* Writes them. */
	if (write_file(argv[1], "power.bin", &power, sizeof(power)) != 0)
		return 1;
	if (write_file(argv[1], "board.bin", &board, sizeof(board)) != 0)
		return 1;
	if (write_file(argv[1], "expected.bin", &expected, sizeof(expected)) != 0)
		return 1;
	if (write_file(argv[1], "metrics.bin", &metrics, sizeof(metrics)) != 0)
		return 1;
	if (write_file(argv[1], "legacy.bin", &legacy, sizeof(legacy)) != 0)
		return 1;
	return 0;
}

/* Writes one file of the directory. */
static int
write_file(
	const char *directory,
	const char *name,
	const void *bytes,
	size_t length)
{
	char path[1024];
	FILE *stream;

	snprintf(path, sizeof(path), "%s/%s", directory, name);
	stream = fopen(path, "wb");
	if (stream == NULL) {
		perror(path);
		return 1;
	}
	if (fwrite(bytes, 1U, length, stream) != length) {
		perror(path);
		fclose(stream);
		return 1;
	}
	fclose(stream);
	return 0;
}

/* Fills a buffer with bytes that differ from one place to the next. */
static void
fill(
	void *buffer,
	size_t length,
	unsigned step,
	unsigned start)
{
	uint8_t *bytes;
	size_t at;

	bytes = buffer;
	for (at = 0U; at < length; at++)
		bytes[at] = (uint8_t)(start + step * at);
}
