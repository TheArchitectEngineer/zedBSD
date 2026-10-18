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
#include "bootloader/uefi/elf64.h"
#include "bootloader/uefi/framebuffer.h"
#include "bootloader/uefi/logo.h"
#include "bootloader/uefi/memory-map.h"
#include "bootloader/uefi/volume-discovery.h"
#include "bootloader/uefi/zedbsd-config.h"

static uint32_t random_state;

static uint32_t next_random(void);
static void test_memory(unsigned round);
static void test_e820(unsigned round);
static void test_fat(unsigned round);
static void test_logo(unsigned round);
static void test_logo_path(unsigned round);
static void test_override(unsigned round);
static void test_config(unsigned round);
static void test_elf(unsigned round);
static void test_framebuffer(unsigned round);
static void test_uefi_map(unsigned round);
static void test_uefi_logo(unsigned round);
static void test_device_path(unsigned round);
static void test_bpb(unsigned round);
static void test_handles(unsigned round);

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
		test_config(round);
		test_elf(round);
		test_framebuffer(round);
		test_uefi_map(round);
		test_uefi_logo(round);
		test_device_path(round);
		test_bpb(round);
		test_handles(round);
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

/* Parses a random zedbsd.cfg. */
static void
test_config(
	unsigned round)
{
	static const char *const lines[] = {
		"kernel=vmunix", "kernel=/boot/vmunix", "kernel=../x", "kernel=dev/sda", "kernel=a:b", "kernel=./k",
		"kernel=UUID=1", "boot0=UUID=ABCD-1234", "overlay-root=root.img", "overlay-data=/data.img",
		"overlay-root=boot1:r.img", "swap0=swap.img", "swap1=/dev/sd0b", "swap4=x", "swap2=UUID=1",
		"overlay-data=a//b", "overlay-root=a/../b", "kmsg=quiet", "login=graphical", "x", "=y", "z=",
		"a#b=c", "quote=\"x\"", "", "logo=kei.ppm", "kernel=vmunix",
		"verylong=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		"tab=a\tb", "cr=a\rb",
	};
	static struct zbl_uefi_kern_config configuration;
	char text[4200];
	size_t length;
	size_t line_length;
	unsigned count;
	unsigned index;
	const char *line;
	const char *uuid;
	enum zbl_uefi_kern_config_result result;

	/* Joins random lines with LF or CRLF. */
	length = 0;
	count = next_random() % 10U;
	if (next_random() % 50U == 0)
		count = 70U;
	for (index = 0; index < count; index++) {
		line = lines[next_random() % 30U];
		line_length = strlen(line);
		if (length + line_length + 2U >= sizeof(text))
			break;
		memcpy(text + length, line, line_length);
		length += line_length;
		if (index + 1U < count || next_random() % 2U == 0) {
			if (next_random() % 4U == 0) {
				text[length] = '\r';
				length++;
			}
			text[length] = '\n';
			length++;
		}
	}
	if (next_random() % 30U == 0 && length != 0)
		text[next_random() % length] = (char)(next_random() % 256U);

	/* Parses it with a selected serial that is sometimes malformed. */
	uuid = "ABCD-1234";
	if (next_random() % 15U == 0)
		uuid = next_random() % 2U == 0 ? "abcd-1234" : "ABCD1234X";
	result = zbl_uefi_kern_config_parse(next_random() % 200U == 0 ? NULL : &configuration,
					    next_random() % 200U == 0 ? NULL : text,
					    length,
					    uuid,
					    next_random() % 20U == 0 ? 9U : 16U);
	printf("C %u %d %s", round, (int)result, zbl_uefi_kern_config_result_name(result));
	if (result == ZBL_UEFI_KERN_CONFIG_OK) {
		printf(" [%s] %u %u %u %u [%s]",
		       configuration.kernel_path,
		       configuration.parameter_record.magic,
		       configuration.parameter_record.version,
		       configuration.parameter_record.size,
		       configuration.parameter_record.length,
		       configuration.parameter_record.text);
	}
	printf("\n");
}

/* Plans and places a random kernel image. */
static void
test_elf(
	unsigned round)
{
	static unsigned char buffer[512];
	static struct zbl_elf64_plan plan;
	unsigned char *ph;
	uint64_t paddr;
	uint64_t memsz;
	uint64_t value;
	unsigned count;
	unsigned index;
	int planned;
	int placed;

	/* Writes an ELF64 header that is valid most of the time. */
	memset(buffer, 0, sizeof(buffer));
	buffer[0] = 0x7f;
	buffer[1] = 'E';
	buffer[2] = 'L';
	buffer[3] = 'F';
	buffer[4] = 2;
	buffer[5] = 1;
	buffer[6] = 1;
	buffer[16] = 2;
	buffer[18] = 62;
	buffer[20] = 1;
	count = 1U + next_random() % 4U;
	value = 0xffffffff80200000ULL + (next_random() % 4U) * 0x1000U;
	memcpy(buffer + 24, &value, 8);
	value = 64U;
	memcpy(buffer + 32, &value, 8);
	buffer[52] = 64;
	buffer[54] = 56;
	buffer[56] = (unsigned char)count;
	if (next_random() % 20U == 0)
		buffer[next_random() % 64U] ^= (unsigned char)(1U << (next_random() % 8U));

	/* Writes ascending segments, sometimes broken. */
	paddr = 0x200000U;
	for (index = 0; index < count; index++) {
		ph = buffer + 64U + index * 56U;
		memsz = (uint64_t)(1U + next_random() % 64U) * 0x1000U;
		value = next_random() % 6U == 0 ? 2U : 1U;
		memcpy(ph, &value, 4);
		value = next_random() % 2U == 0 ? 5U : 6U;
		memcpy(ph + 4, &value, 4);
		value = 0x1000U * (index + 1U);
		memcpy(ph + 8, &value, 8);
		value = 0xffffffff80000000ULL + paddr + (next_random() % 30U == 0 ? 0x1000U : 0U);
		memcpy(ph + 16, &value, 8);
		value = paddr;
		memcpy(ph + 24, &value, 8);
		value = memsz - (next_random() % 2U == 0 ? 0x800U : 0U) + (next_random() % 30U == 0 ? 0x2000U : 0U);
		memcpy(ph + 32, &value, 8);
		memcpy(ph + 40, &memsz, 8);
		value = next_random() % 25U == 0 ? 0x800U : 0x1000U;
		memcpy(ph + 48, &value, 8);
		paddr += memsz;
		if (next_random() % 25U == 0)
			paddr -= 0x1000U;
	}

	/* Plans the image against a file limit, then places it at a random base. */
	memset(&plan, 0, sizeof(plan));
	planned = zbl_elf64_plan(buffer, next_random() % 10U == 0 ? 0x1000U : 0x1000000U, &plan);
	printf("X %u %d %d %llx %llx %llx", round, planned, plan.segment_count,
	       (unsigned long long)plan.entry,
	       (unsigned long long)plan.link_physical_start,
	       (unsigned long long)plan.link_physical_end);
	placed = zbl_elf64_place(&plan, (uint64_t)(next_random() % 0x240U) * 0x200000ULL + (next_random() % 30U == 0 ? 0x1000U : 0U));
	printf(" %d %llx %llx", placed, (unsigned long long)plan.physical_start, (unsigned long long)plan.physical_end);
	for (index = 0; index < plan.segment_count && index < 8U; index++)
		printf(" %llx", (unsigned long long)plan.segment[index].physical);
	printf("\n");
}

/* Plans the mapping of a random framebuffer. */
static void
test_framebuffer(
	unsigned round)
{
	struct zbl_uefi_framebuffer_mapping mapping;
	uint64_t base;
	uint64_t size;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	int planned;

	/* Draws a geometry near the limits. */
	base = (uint64_t)(next_random() % 0x10000U) * 0x10000U;
	if (next_random() % 20U == 0)
		base = UINT64_MAX - (next_random() % 0x100000U);
	width = next_random() % 4000U;
	height = next_random() % 3000U;
	stride = width + (next_random() % 3U == 0 ? next_random() % 300U : 0U);
	if (next_random() % 20U == 0)
		stride = width - 1U;
	size = (uint64_t)stride * height * 4U + (next_random() % 3U == 0 ? next_random() % 0x400000U : 0U);
	if (next_random() % 20U == 0)
		size = next_random() % 0x1000U;

	/* Plans the mapping. */
	memset(&mapping, 0, sizeof(mapping));
	planned = zbl_uefi_framebuffer_mapping_plan(base, size, width, height, stride, next_random() % 30U == 0 ? 0U : 4U, round % 97U == 0 ? NULL : &mapping);
	printf("B %u %d %llx %llx %u\n", round, planned,
	       (unsigned long long)mapping.required_bytes,
	       (unsigned long long)mapping.aligned_base,
	       mapping.large_page_count);
}

/* Normalizes a random UEFI memory map in both forms. */
static void
test_uefi_map(
	unsigned round)
{
	static uint64_t storage[16 * 6];
	struct zbl6_memory_range ranges[12];
	struct zbl6_memory_range_v6 ranges6[12];
	EFI_MEMORY_DESCRIPTOR *descriptor;
	uint32_t count;
	uint32_t index;
	uint32_t range_count;
	UINTN stride;
	enum zbl_uefi_map_result result;
	enum zbl_memory_result result6;

	/* Writes descriptors of a random stride. */
	stride = next_random() % 4U == 0 ? 48U : 40U;
	if (next_random() % 30U == 0)
		stride = 36U;
	count = next_random() % 14U;
	memset(storage, 0, sizeof(storage));
	for (index = 0; index < count; index++) {
		descriptor = (EFI_MEMORY_DESCRIPTOR *)((unsigned char *)storage + index * stride);
		descriptor->Type = next_random() % 16U;
		descriptor->PhysicalStart = (uint64_t)(next_random() % 40U) * 0x1000U + (next_random() % 40U == 0 ? 0x10U : 0U);
		descriptor->NumberOfPages = next_random() % 8U;
		if (next_random() % 50U == 0)
			descriptor->NumberOfPages = UINT64_MAX / 0x800U;
		descriptor->Attribute = next_random() % 10U == 0 ? (1ULL << 63) : 0xfU;
	}

	/* Normalizes them into the earlier form. */
	range_count = 0xdead;
	result = zbl_uefi_normalize_memory_map(storage, count * stride, stride, ranges, 1U + next_random() % 12U, &range_count);
	printf("U %u %d %s %u", round, (int)result, zbl_uefi_map_result_name(result), range_count);
	for (index = 0; result == ZBL_UEFI_MAP_OK && index < range_count; index++)
		printf(" %llx/%llx/%u", (unsigned long long)ranges[index].base, (unsigned long long)ranges[index].size, ranges[index].type);
	printf("\n");

	/* Normalizes them into the v6 form. */
	range_count = 0xdead;
	result6 = zbl_uefi_normalize_memory_map_v6(storage, count * stride, stride, ranges6, 1U + next_random() % 12U, &range_count);
	printf("V %u %d %u", round, (int)result6, range_count);
	for (index = 0; result6 == ZBL_MEMORY_OK && index < range_count; index++)
		printf(" %llx/%llx/%u/%llx", (unsigned long long)ranges6[index].base, (unsigned long long)ranges6[index].size, ranges6[index].type, (unsigned long long)ranges6[index].attributes);
	printf("\n");
}

/* Draws a random PPM onto a small host framebuffer through the UEFI drawer. */
static void
test_uefi_logo(
	unsigned round)
{
	static uint32_t screen[40 * 32];
	static unsigned char file[4096];
	struct zbl6_framebuffer framebuffer;
	uint32_t width;
	uint32_t height;
	uint32_t index;
	uint32_t hash;
	size_t size;
	int drawn;

	/* Writes a header, sometimes with fit=contain or a broken field. */
	width = 1U + next_random() % 60U;
	height = 1U + next_random() % 40U;
	if (next_random() % 3U == 0)
		size = (size_t)snprintf((char *)file, sizeof(file), "P6\n# fit=contain \r\n%u %u\n255\n", width, height);
	else if (next_random() % 8U == 0)
		size = (size_t)snprintf((char *)file, sizeof(file), "P6 %u\n%u 25%u\n", width, height, next_random() % 10U);
	else
		size = (size_t)snprintf((char *)file, sizeof(file), "P6\n#c\n%u\t%u 255\n", width, height);
	for (index = 0; index < width * height * 3U && size < sizeof(file); index++) {
		file[size] = (unsigned char)next_random();
		size++;
	}
	if (next_random() % 10U == 0)
		size -= next_random() % size;

	/* Draws it on a random screen size within the buffer. */
	memset(screen, 0x55, sizeof(screen));
	framebuffer.physical_base = (uint64_t)(uintptr_t)screen;
	framebuffer.size = sizeof(screen);
	framebuffer.width = 1U + next_random() % 32U;
	framebuffer.height = 1U + next_random() % 32U;
	framebuffer.stride = 40U;
	framebuffer.format = next_random() % 2U == 0 ? ZBL6_FRAMEBUFFER_RGBX8888 : ZBL6_FRAMEBUFFER_BGRX8888;
	drawn = zbl_uefi_logo_draw(file, size, &framebuffer);

	/* Hashes the screen. */
	hash = 2166136261U;
	for (index = 0; index < 40U * 32U; index++)
		hash = (hash ^ screen[index]) * 16777619U;
	printf("G %u %d %08x\n", round, drawn, hash);
}

/* Appends one device path node. */
static size_t
put_node(
	unsigned char *path,
	size_t at,
	unsigned type,
	unsigned subtype,
	unsigned size)
{
	unsigned index;

	/* Writes the header and random contents. */
	path[at] = (unsigned char)type;
	path[at + 1U] = (unsigned char)subtype;
	path[at + 2U] = (unsigned char)(size & 0xffU);
	path[at + 3U] = (unsigned char)(size >> 8);
	for (index = 4U; index < size; index++)
		path[at + index] = (unsigned char)next_random();

	/* Reports where the next node goes. */
	return at + size;
}

/* Parses random partition device paths and compares pairs of them. */
static void
test_device_path(
	unsigned round)
{
	static unsigned char paths[2][600];
	struct zbl_uefi_partition_path views[2];
	struct kern_boot_partition_identity identity;
	enum zbl_uefi_device_path_result results[2];
	size_t at;
	size_t available;
	unsigned which;
	unsigned count;
	unsigned index;
	unsigned char *hd;
	int same;
	int copied;

	/* Builds two paths, the second often on the same disk as the first. */
	for (which = 0; which < 2U; which++) {
		at = 0;
		if (which == 1U && next_random() % 2U == 0) {
			memcpy(paths[1], paths[0], 40U);
			at = 40U;
		} else {
			count = 1U + next_random() % 3U;
			for (index = 0; index < count && at < 200U; index++)
				at = put_node(paths[which], at, 1U + next_random() % (next_random() % 10U == 0 ? 6U : 3U), next_random() % 4U, 4U + next_random() % 12U);
			while (at < 40U)
				at = put_node(paths[which], at, 2U, 1U, 4U);
			at = 40U;
		}
		if (next_random() % 8U != 0) {
			hd = paths[which] + at;
			at = put_node(paths[which], at, 4U, 1U, next_random() % 20U == 0 ? 40U : 42U);
			hd[40] = (unsigned char)(next_random() % 3U == 0 ? 2U : 1U);
			hd[41] = (unsigned char)(hd[40] == 2U ? 2U : 1U);
			if (next_random() % 20U == 0)
				hd[41] = 3U;
			if (next_random() % 30U == 0)
				memset(hd + 4, 0, 4U);
		}
		if (next_random() % 4U == 0)
			at = put_node(paths[which], at, next_random() % 5U == 0 ? 1U : 4U, 4U, 6U);
		at = put_node(paths[which], at, 0x7fU, next_random() % 15U == 0 ? 1U : 0xffU, next_random() % 30U == 0 ? 5U : 4U);
		available = at;
		if (next_random() % 10U == 0)
			available = at - 1U - next_random() % 4U;
		results[which] = zbl_uefi_partition_path_parse(paths[which], available, &views[which]);
	}

	/* Reports both parses, whether they share a disk and the identity of the first. */
	same = zbl_uefi_partition_paths_same_disk(&views[0], &views[1]);
	memset(&identity, 0, sizeof(identity));
	copied = zbl_uefi_partition_identity_copy(&views[0], &identity);
	printf("D %u %d %s %d %d %zu %zu %u %llx %d %d %u %u %llx %02x%02x\n", round,
	       (int)results[0], zbl_uefi_device_path_result_name(results[0]), (int)results[1],
	       (int)views[0].style, views[0].path_size, views[0].disk_prefix_size,
	       views[0].partition_number, (unsigned long long)views[0].partition_size,
	       same, copied, identity.scheme, identity.index,
	       (unsigned long long)identity.first_lba, identity.signature[0], identity.signature[15]);
}

/* Parses a random FAT boot sector near the valid shapes. */
static void
test_bpb(
	unsigned round)
{
	static unsigned char sector[1024];
	struct zbl_uefi_fat_info info;
	char uuid[ZBL_UEFI_FAT_UUID_SIZE];
	uint32_t total;
	uint32_t fat_size;
	unsigned kind;
	enum zbl_uefi_fat_result result;

	/* Fills a FAT12, FAT16 or FAT32 BPB with random flaws. */
	memset(sector, 0, sizeof(sector));
	kind = next_random() % 3U;
	sector[0] = next_random() % 20U == 0 ? 0x00U : 0xebU;
	sector[2] = 0x90U;
	if (next_random() % 10U == 0)
		sector[0] = 0xe9U;
	sector[11] = 0x00;
	sector[12] = next_random() % 15U == 0 ? 0x04U : 0x02U;
	sector[13] = (unsigned char)(1U << (next_random() % 6U));
	sector[14] = (unsigned char)(1U + next_random() % 32U);
	sector[16] = (unsigned char)(1U + next_random() % 2U);
	sector[21] = next_random() % 15U == 0 ? 0x12U : 0xf8U;
	if (kind == 0U) {
		total = 2000U + next_random() % 4000U;
		fat_size = 1U + next_random() % 12U;
		sector[17] = 0x00;
		sector[18] = 0x02;
		sector[19] = (unsigned char)total;
		sector[20] = (unsigned char)(total >> 8);
		sector[22] = (unsigned char)fat_size;
		sector[38] = 0x29U;
	} else if (kind == 1U) {
		total = 70000U + next_random() % 2000000U;
		fat_size = 100U + next_random() % 300U;
		sector[17] = 0x00;
		sector[18] = 0x02;
		memcpy(sector + 32, &total, 4);
		sector[22] = (unsigned char)fat_size;
		sector[23] = (unsigned char)(fat_size >> 8);
		sector[38] = next_random() % 10U == 0 ? 0x28U : 0x29U;
	} else {
		total = 600000U + next_random() % 40000000U;
		fat_size = 500U + next_random() % 40000U;
		memcpy(sector + 32, &total, 4);
		memcpy(sector + 36, &fat_size, 4);
		sector[44] = (unsigned char)(next_random() % 4U);
		sector[66] = 0x29U;
		if (next_random() % 20U == 0)
			sector[42] = 1;
	}
	sector[39] = 0x12;
	sector[67] = 0x34;
	sector[510] = 0x55;
	sector[511] = next_random() % 20U == 0 ? 0x00U : 0xaaU;
	if (next_random() % 25U == 0)
		memcpy(sector + 3, "EXFAT   ", 8);
	if (next_random() % 10U == 0)
		sector[next_random() % 64U] = (unsigned char)next_random();

	/* Parses it on a medium of a random size and block size. */
	result = zbl_uefi_fat_bpb_parse(sector, next_random() % 30U == 0 ? 256U : 512U,
					next_random() % 20U == 0 ? 4096U : 512U,
					(uint64_t)total * (next_random() % 5U == 0 ? 1U : 2U), &info);
	memset(uuid, 0, sizeof(uuid));
	zbl_uefi_fat_uuid(info.volume_serial ^ next_random(), uuid);
	printf("T %u %d %s %d %u %u %u %u %u %08x %s\n", round, (int)result, zbl_uefi_fat_result_name(result),
	       (int)info.type, info.bytes_per_sector, info.sectors_per_cluster, info.total_sectors,
	       info.fat_sectors, info.cluster_count, info.volume_serial, uuid);
}

/* Orders random handle lists and runs a selection over them. */
static void
test_handles(
	unsigned round)
{
	static int objects[8];
	void *firmware[10];
	void *ordered[10];
	struct zbl_uefi_volume_selection selection;
	struct zbl_uefi_volume_match match;
	struct zbl_uefi_fat_info fat;
	size_t count;
	size_t ordered_count;
	unsigned index;
	enum zbl_uefi_handle_order_result result;
	enum zbl_uefi_volume_match_action action;
	int taken;

	/* Orders a list with duplicates and sometimes a null. */
	count = next_random() % 10U;
	for (index = 0; index < count; index++)
		firmware[index] = next_random() % 25U == 0 ? NULL : &objects[next_random() % 8U];
	ordered_count = 99;
	result = zbl_uefi_volume_order_handles(next_random() % 30U == 0 ? NULL : &objects[next_random() % 8U],
					       firmware, count, ordered, 1U + next_random() % 9U, &ordered_count);
	printf("H %u %d %zu", round, (int)result, ordered_count);
	for (index = 0; result == ZBL_UEFI_HANDLE_ORDER_OK && index < ordered_count; index++)
		printf(" %d", (int)((int *)ordered[index] - objects));

	/* Records matches in an order that sometimes does not ascend. */
	zbl_uefi_volume_selection_init(&selection);
	memset(&fat, 0, sizeof(fat));
	fat.volume_serial = next_random();
	for (index = 0; index < next_random() % 4U; index++) {
		fat.type = next_random() % 8U == 0 ? ZBL_UEFI_FAT12 : ZBL_UEFI_FAT16;
		action = zbl_uefi_volume_selection_record_match(&selection, &objects[index], index * 2U + (next_random() % 6U == 0 ? 0U : 1U) - (index == 0 ? 0U : 0U), &fat, &objects[1], &objects[2]);
		printf(" a%d", (int)action);
	}
	printf(" f%d", (int)zbl_uefi_volume_selection_finish(&selection));
	memset(&match, 0, sizeof(match));
	taken = zbl_uefi_volume_selection_take(&selection, &match);
	printf(" t%d %zu %s\n", taken, match.order, match.uuid);
}
