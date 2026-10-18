/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host driver that feeds the loaders' pure C helpers seeded random input
 * and prints every result, so that two builds of the helpers from two source
 * trees can be compared by their output.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bootloader/bios/fat-directory.h"
#include "bootloader/bios/logo.h"
#include "bootloader/bios/memory-map.h"
#include "bootloader/common/boot-override.h"
#include "bootloader/common/logo-path.h"

static uint32_t random_state;

static uint32_t next_random(void);
static void test_memory(unsigned round);
static void test_e820(unsigned round);
static void test_fat(unsigned round);
static void test_logo(unsigned round);
static void test_logo_path(unsigned round);
static void test_override(unsigned round);

/* Runs every helper over the same seeded inputs. */
int
main(
	void)
{
	unsigned round;

	/* Runs a fixed number of rounds of each helper. */
	random_state = 12345U;
	for (round = 0; round < 20000U; round++) {
		test_memory(round);
		test_e820(round);
		test_fat(round);
		test_logo(round);
		test_logo_path(round);
		test_override(round);
	}

	/* Succeeded: every result was printed. */
	return 0;
}

/* Draws the next value of a fixed linear congruential sequence. */
static uint32_t
next_random(
	void)
{
	/* Advances the sequence. */
	random_state = random_state * 1103515245U + 12345U;

	/* Reports the upper bits, which vary the most. */
	return random_state >> 8;
}

/* Normalizes a random E820 map. */
static void
test_memory(
	unsigned round)
{
	struct zbl_e820_entry raw[12];
	struct zbl6_memory_range_v6 ranges[16];
	uint32_t count;
	uint32_t range_count;
	uint32_t capacity;
	uint32_t index;
	enum zbl_memory_result result;

	/* Builds a map of page-ish ranges, some overlapping, some unaligned. */
	count = next_random() % 12U;
	for (index = 0; index < count; index++) {
		raw[index].base = (uint64_t)(next_random() % 64U) * 0x800U + (next_random() % 4U == 0 ? next_random() % 0x1000U : 0U);
		raw[index].size = (uint64_t)(next_random() % 16U) * 0x800U + (next_random() % 4U == 0 ? next_random() % 0x1000U : 0U);
		if (next_random() % 50U == 0)
			raw[index].base = UINT64_MAX - (next_random() % 0x3000U);
		raw[index].type = next_random() % 7U;
		raw[index].attributes = next_random() % 8U == 0 ? next_random() % 4U : 1U;
	}

	/* Normalizes it into a buffer that is sometimes too small. */
	capacity = 1U + next_random() % 16U;
	range_count = 0xdeadU;
	result = zbl_bios_normalize_memory_map(raw, count, ranges, capacity, &range_count);
	printf("M %u %d %u", round, (int)result, range_count);
	for (index = 0; index < range_count && index < 16U; index++) {
		printf(" %llx/%llx/%u/%u/%llx",
		       (unsigned long long)ranges[index].base,
		       (unsigned long long)ranges[index].size,
		       ranges[index].type,
		       ranges[index].flags,
		       (unsigned long long)ranges[index].attributes);
	}
	printf("\n");

	/* Normalizes it again refusing overlap, through the common entry. */
	if (round % 4U == 0) {
		range_count = 0xdeadU;
		result = zbl_memory_normalize(raw, count, 0, 0, ranges, capacity, &range_count);
		printf("MN %u %d %u\n", round, (int)result, range_count);
	}
}

/* Feeds random E820 responses to the collector's check. */
static void
test_e820(
	unsigned round)
{
	struct zbl_e820_entry raw[6];
	struct zbl_e820_state state;
	enum zbl_e820_result result;
	unsigned step;

	/* Runs a short enumeration of random responses. */
	memset(raw, 0, sizeof(raw));
	state.count = next_random() % 3U == 0 ? next_random() % 8U : 0U;
	state.token = next_random() % 4U;
	for (step = 0; step < 8U; step++) {
		result = zbl_bios_e820_accept(next_random() % 20U == 0 ? 0 : &state,
					      raw,
					      next_random() % 10U == 0 ? 0U : 6U,
					      next_random() % 6U == 0,
					      next_random() % 10U == 0 ? 0x1234U : 0x534d4150U,
					      next_random() % 3U == 0 ? 20U : (next_random() % 8U == 0 ? 16U : 24U),
					      next_random() % 5U);
		printf("E %u %u %d %u %u %u\n", round, step, (int)result, state.count, state.token, raw[step % 6U].attributes);
		if (result != ZBL_E820_CONTINUE)
			break;
	}
}

/* Writes one 8.3 entry into a directory sector. */
static void
put_short(
	unsigned char *entry,
	const char *name)
{
	unsigned index;

	/* Copies the space-padded name and marks a plain file. */
	memset(entry, 0, 32U);
	for (index = 0; index < 11U; index++)
		entry[index] = (unsigned char)name[index];
	entry[11] = (unsigned char)(next_random() % 8U == 0 ? 0x08U : 0x20U);
}

/* Computes the LFN checksum of an 8.3 name. */
static unsigned char
lfn_sum(
	const unsigned char *name)
{
	unsigned index;
	unsigned char sum;

	/* Rotates and adds each name byte. */
	sum = 0;
	for (index = 0; index < 11U; index++)
		sum = (unsigned char)(((sum & 1U) << 7) + (sum >> 1) + name[index]);

	/* Reports the checksum. */
	return sum;
}

/* Searches random directory sectors that hold long and short names. */
static void
test_fat(
	unsigned round)
{
	static const char *const shorts[] = {"KERNEL     ", "VMUNIX  ELF", "LOGO    PPM", "A       B  "};
	static const char *const longs[] = {"kernel", "vmunix.elf", "Logo.ppm", "averyveryverylongfilename.with.dots", "a.b"};
	struct zbl_bios_fat_directory_state state;
	unsigned char sector[512];
	unsigned char *entry;
	const char *name;
	const char *component;
	unsigned slot;
	unsigned parts;
	unsigned part;
	unsigned character;
	unsigned position;
	unsigned char sum;
	int found;

	/* Fills a sector with LFN runs, short entries and noise. */
	memset(sector, 0, sizeof(sector));
	slot = 0;
	while (slot < 16U) {
		entry = sector + slot * 32U;
		if (next_random() % 7U == 0) {
			/* A random entry. */
			for (position = 0; position < 32U; position++)
				entry[position] = (unsigned char)next_random();
			slot++;
			continue;
		}

		/* A long name and its short entry. */
		name = longs[next_random() % 5U];
		put_short(sector + 15U * 32U, shorts[next_random() % 4U]);
		sum = lfn_sum(sector + 15U * 32U);
		parts = (unsigned)((strlen(name) + 12U) / 13U);
		for (part = parts; part >= 1U && slot < 15U; part--) {
			entry = sector + slot * 32U;
			memset(entry, 0, 32U);
			entry[0] = (unsigned char)(part | (part == parts ? 0x40U : 0U));
			if (next_random() % 30U == 0)
				entry[0] = (unsigned char)(entry[0] ^ 1U);
			entry[11] = 0x0f;
			entry[13] = next_random() % 30U == 0 ? (unsigned char)(sum + 1U) : sum;
			for (character = 0; character < 13U; character++) {
				static const unsigned offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
				position = (part - 1U) * 13U + character;
				if (position < strlen(name)) {
					entry[offsets[character]] = (unsigned char)name[position];
					entry[offsets[character] + 1U] = 0;
				} else if (position == strlen(name)) {
					entry[offsets[character]] = 0;
					entry[offsets[character] + 1U] = 0;
				} else {
					entry[offsets[character]] = 0xff;
					entry[offsets[character] + 1U] = 0xff;
				}
			}
			slot++;
		}
		if (slot < 16U) {
			memcpy(sector + slot * 32U, sector + 15U * 32U, 32U);
			if (next_random() % 20U == 0)
				sector[slot * 32U] = 0xe5;
			slot++;
		}
		if (next_random() % 10U == 0 && slot < 16U) {
			sector[slot * 32U] = 0;
			break;
		}
	}

	/* Searches the sector for several components, carrying the state across. */
	memset(&state, 0x5a, sizeof(state));
	state.active = 0;
	state.invalid = 0;
	for (part = 0; part < 4U; part++) {
		if (next_random() % 2U == 0)
			component = longs[next_random() % 5U];
		else
			component = next_random() % 2U == 0 ? "kernel" : "VMUNIX.ELF";
		found = zbl_bios_fat_directory_search(&state, sector, next_random() % 40U == 0 ? 500U : sizeof(sector), component, strlen(component));
		printf("F %u %u %d %u %u %u %u %u\n", round, part, found, state.long_name_length, state.expected_ordinal, state.checksum, state.active, state.invalid);
	}
}

/* Decodes a random small PPM onto a random screen. */
static void
test_logo(
	unsigned round)
{
	static struct zbl_bios_logo logo;
	unsigned char file[2048];
	uint32_t size;
	uint32_t width;
	uint32_t height;
	uint32_t index;
	uint32_t offset;
	uint32_t chunk;
	uint32_t hash;
	int state;

	/* Writes a header, sometimes with fit=contain or a broken field. */
	width = 1U + next_random() % 20U;
	height = 1U + next_random() % 20U;
	if (next_random() % 3U == 0)
		size = (uint32_t)snprintf((char *)file, sizeof(file), "P6\n# fit=contain \n%u %u\n255\n", width, height);
	else if (next_random() % 8U == 0)
		size = (uint32_t)snprintf((char *)file, sizeof(file), "P6 %u\n%u 25%u\n", width, height, next_random() % 10U);
	else
		size = (uint32_t)snprintf((char *)file, sizeof(file), "P6\n#c\n%u %u 255\n", width, height);
	if (next_random() % 20U == 0)
		file[next_random() % size] = (unsigned char)next_random();

	/* Appends the pixels. */
	for (index = 0; index < width * height * 3U && size < sizeof(file); index++) {
		file[size] = (unsigned char)next_random();
		size++;
	}

	/* Feeds the file in chunks and hashes the writes that come back. */
	zbl_bios_logo_begin(&logo, 1U + next_random() % 30U, 1U + next_random() % 30U, 32U, next_random() % 2U);
	hash = 2166136261U;
	state = 0;
	for (offset = 0; offset < size && state == 0; offset += chunk) {
		chunk = 1U + next_random() % 512U;
		if (chunk > size - offset)
			chunk = size - offset;
		state = zbl_bios_logo_feed(&logo, file + offset, chunk);
		hash = (hash ^ logo.count ^ (logo.fill_now << 16) ^ logo.background) * 16777619U;
		for (index = 0; index < logo.count; index++)
			hash = (hash ^ logo.offsets[index] ^ (logo.pixels[index] * 31U)) * 16777619U;
	}
	printf("L %u %d %08x %u %u %u %u\n", round, state, hash, logo.origin_x, logo.origin_y, logo.scaled_width, logo.scaled_height);
}

/* Builds a random parameter text. */
static size_t
random_parameters(
	char *text,
	size_t capacity)
{
	static const char *const tokens[] = {"logo=a.ppm", "logo=/x", "logo=a/../b", "logo=", "logo=ok_1-2.ppm", "kmsg=quiet", "kmsg=console", "login=graphical", "x=y", "logox=1", "", "kmsg", "logo=bad*char", "login=console", "logo=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
	size_t length;
	size_t token_length;
	unsigned count;
	unsigned index;
	const char *token;

	/* Joins random tokens with single spaces. */
	length = 0;
	count = next_random() % 6U;
	for (index = 0; index < count; index++) {
		token = tokens[next_random() % 15U];
		token_length = strlen(token);
		if (length + token_length + 2U >= capacity)
			break;
		if (index != 0) {
			text[length] = ' ';
			length++;
		}
		memcpy(text + length, token, token_length);
		length += token_length;
	}
	text[length] = '\0';

	/* Reports the text's length. */
	return length;
}

/* Looks for the logo path and whole tokens in random parameters. */
static void
test_logo_path(
	unsigned round)
{
	char text[256];
	char path[80];
	size_t length;
	int found;
	int quiet;

	/* Finds the logo path with a capacity that is sometimes too small. */
	length = random_parameters(text, sizeof(text));
	memset(path, 0, sizeof(path));
	found = zbl_logo_path(text, length, path, next_random() % 4U == 0 ? 5U : sizeof(path));
	quiet = zbl_parameter_present(text, length, next_random() % 2U == 0 ? "kmsg=quiet" : "kmsg");
	printf("P %u %d %d %s\n", round, found, quiet, found == 1 ? path : "-");
}

/* Rewrites random parameter records for random keys. */
static void
test_override(
	unsigned round)
{
	static struct kern_boot_parameter_record record;
	size_t length;
	int result;

	/* Applies random keys to a random record. */
	memset(&record, 0x77, sizeof(record));
	length = random_parameters(record.text, 200U);
	record.length = (uint16_t)length;
	if (next_random() % 30U == 0)
		record.text[length] = 'x';
	result = zbl_boot_override_apply(&record, next_random() % 5U);
	if (result != 0) {
		printf("O %u %d\n", round, result);
		return;
	}
	printf("O %u %d %u %s %d\n", round, result, record.length, record.text, record.text[record.length + 1U]);
}
