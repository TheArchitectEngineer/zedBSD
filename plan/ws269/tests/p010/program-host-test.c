/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of ws269-p010n: the data sequencer words the render
 * context's switch programs need, the shared register and persistent
 * temporaries programs against words worked out by hand from Mesa
 * 26.2.5 pvr_pds.c and pvr_rogue_pds_defs.h, the task words of the
 * static state against vdm.xml, ppp.xml and cr.xml, and the shading
 * cluster programs that store and load the shared registers.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/pds-encode.h"
#include "drivers/gpu/powervr/render-sr-program.h"
#include "drivers/gpu/powervr/usc-encode.h"

static int failures;
static int passes;

/* One expectation. */
static void
expect(
	int good,
	const char *what)
{
	if (good) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s\n", what);
}

/* Compares a run of words. */
static void
expect_words(
	const uint32_t *got,
	const uint32_t *want,
	unsigned count,
	const char *what)
{
	unsigned index;

	for (index = 0; index < count; index++) {
		if (got[index] != want[index]) {
			printf("FAIL %s word %u: 0x%08x, want 0x%08x\n", what, index, got[index], want[index]);
			failures++;
			return;
		}
	}
	passes++;
}

static void
words(void)
{
	uint32_t word;
	uint64_t wide;

	/* DOUTW src1: bsize all64 2; the second at store address 2 (<<13) and last (bit 31). */
	expect(powervr_pds_write_control_word(0, 0, 0, 0, 2, &word) == 0 && word == 0x00000002U, "write control first");
	expect(powervr_pds_write_control_word(1, 0, 0, 2, 2, &word) == 0 && word == 0x80004002U, "write control last");
	expect(powervr_pds_write_control_word(0, 1, 1, 0x1fff, 3, &word) == 0 && word == 0x17ffe003U, "write control common, bypass, fields full");
	expect(powervr_pds_write_control_word(2, 0, 0, 0, 0, &word) == EINVAL, "write control refuses a flag of 2");
	expect(powervr_pds_write_control_word(0, 0, 0, 0x2000, 0, &word) == EINVAL, "write control refuses a wide address");

	/* stmc: 0xd0000000, op 7 << 23, mask 0x10. */
	expect(powervr_pds_stream_clear(0, 0x10, &word) == 0 && word == 0xd3800010U, "stream clear");
	expect(powervr_pds_stream_clear(0, 0x20, &word) == EINVAL, "stream clear refuses a wide mask");

	/* ld src0: address 2.., count8 41, cmode 44, dest 47. */
	expect(powervr_pds_load_word(0x1000, 6, 0, 16, &wide) == 0 && wide == 0x00080c0000001000ULL, "load word");
	expect(powervr_pds_load_word(0x1002, 6, 0, 16, &wide) == EINVAL, "load word refuses a misaligned address");
	expect(powervr_pds_load_word(0x1000, 8, 0, 16, &wide) == EINVAL, "load word refuses a count of 8");
	expect(powervr_pds_load_word(0x1000, 1, 0, 32, &wide) == EINVAL, "load word refuses a destination of 32");

	/* st src0: address 2.., count4 40, cmode 44, src 46. */
	expect(powervr_pds_store_word(0x1000, 15, 0, 32, &wide) == 0 && wide == 0x00080f0000001000ULL, "store word");
	expect(powervr_pds_store_word(0x1000, 16, 0, 32, &wide) == EINVAL, "store word refuses a count of 16");
	expect(powervr_pds_store_word(0x1000, 1, 0, 64, &wide) == EINVAL, "store word refuses a source of 64");
}

static void
programs(void)
{
	static const uint32_t shared_data[POWERVR_SHARED_PDS_DATA_WORDS] = {
		0xda, 0x00012340, 0x00000002, 0,
		0xda, 0x00012370, 0x80004002, 0,
		0x00020000, 0x20, 0, 0, 0, 0, 0, 0,
	};
	static const uint32_t shared_code[POWERVR_SHARED_PDS_CODE_WORDS] = {
		0xf0020001, 0xf0060201, 0xf4000402,
	};
	static const uint32_t store_data[POWERVR_PERSISTENT_DATA_WORDS] = {
		0x00020000, 0x00080fda, 0x0002003c, 0x000bcfda, 0x00020078, 0x000f82da, 0, 0,
	};
	static const uint32_t store_code[POWERVR_PERSISTENT_STORE_CODE_WORDS] = {
		0xd0800000, 0xd0800001, 0xd0800002, 0xd1000000, 0xd3000000,
	};
	static const uint32_t resume_data[POWERVR_PERSISTENT_DATA_WORDS] = {
		0x00020000, 0x00080cda, 0x00020030, 0x000b0cda, 0x00020060, 0x000e08da, 0, 0,
	};
	static const uint32_t resume_code[POWERVR_PERSISTENT_RESUME_CODE_WORDS] = {
		0xd3800010, 0xd0000000, 0xd0000001, 0xd0000002, 0xd1000000, 0xd3000000,
	};
	uint32_t data[POWERVR_SHARED_PDS_DATA_WORDS];
	uint32_t code[POWERVR_SHARED_PDS_CODE_WORDS + 3];

	/*
	 * The shared register program: two 64-bit constants high half first
	 * with their control words at 2 and 6, the task word (address 0x20000,
	 * 13 temporaries in 4 blocks at bit 35) at 8; doutw src1 2 src0 0,
	 * src1 6 src0 2; doutu end from 64-bit constant 4.
	 */
	memset(data, 0xff, sizeof(data));
	expect(powervr_shared_pds_program(0xda00012340ULL, 0x20000, 13, data, code) == 0, "shared pds builds");
	expect_words(data, shared_data, POWERVR_SHARED_PDS_DATA_WORDS, "shared pds data");
	expect_words(code, shared_code, POWERVR_SHARED_PDS_CODE_WORDS, "shared pds code");

	/* The persistent store: 15, 15 and 2 words from ptemps 32, 47, 62, at +0, +60, +120. */
	memset(data, 0xff, sizeof(data));
	expect(powervr_persistent_store_program(0xda00020000ULL, data, code) == 0, "persistent store builds");
	expect_words(data, store_data, POWERVR_PERSISTENT_DATA_WORDS, "persistent store data");
	expect_words(code, store_code, POWERVR_PERSISTENT_STORE_CODE_WORDS, "persistent store code");

	/* The resume: stmc 0x10, then 6, 6 and 4 64-bit words into ptemps 16, 22, 28 from +0, +48, +96. */
	memset(data, 0xff, sizeof(data));
	expect(powervr_persistent_resume_program(0xda00020000ULL, data, code) == 0, "persistent resume builds");
	expect_words(data, resume_data, POWERVR_PERSISTENT_DATA_WORDS, "persistent resume data");
	expect_words(code, resume_code, POWERVR_PERSISTENT_RESUME_CODE_WORDS, "persistent resume code");
	expect(powervr_persistent_resume_program(0xda00020008ULL, data, code) == EINVAL, "persistent resume refuses a misaligned buffer");
}

static void
tasks(void)
{
	struct powervr_switch_programs places;
	struct powervr_switch_tasks words_out;

	/*
	 * PDS_STATE0: block 1 << 29, usc_target any 1 << 25, unified 4 << 10,
	 * data 64 bytes = 4; PDS_STATE1: the data place | sd_type pds 1 << 2 |
	 * sd_next_type pds 1; STREAM_OUT1: sync 1 << 10, data 32 bytes = 2 << 4.
	 */
	places.shared_store_data = 0x10100;
	places.shared_store_code = 0x10140;
	places.shared_load_data = 0x10180;
	places.shared_load_code = 0x101c0;
	places.persistent_store_data = 0x10200;
	places.persistent_resume_data = 0x10240;
	powervr_switch_task_words(&places, &words_out);
	expect(words_out.store[0] == 0x0001010522001004ULL, "store task 0");
	expect(words_out.store[1] == 0x10140ULL, "store task 1");
	expect(words_out.store[2] == 0x0001020000000420ULL, "store task 2");
	expect(words_out.resume[0] == 0x0001018520001004ULL, "resume task 0, all clusters");
	expect(words_out.resume[1] == 0x101c0ULL, "resume task 1");
	expect(words_out.resume[2] == 0x0001024000000420ULL, "resume task 2");
	expect(powervr_switch_base_word(0xda12345678ULL) == 0xda12345670ULL, "state base keeps bits 4..39");
	expect(powervr_switch_base_word(0x1da12345678ULL) == 0xda12345670ULL, "state base drops bit 40");
}

static void
usc(void)
{
	uint8_t store[POWERVR_SHARED_USC_BYTES_MAX];
	uint8_t load[POWERVR_SHARED_USC_BYTES_MAX];
	uint8_t again[POWERVR_SHARED_USC_BYTES_MAX];
	size_t store_length;
	size_t load_length;
	size_t again_length;

	/* Both programs build, aligned, within the room; the store builds the same twice. */
	expect(powervr_shared_usc_program(0, store, sizeof(store), &store_length) == 0, "shared store usc builds");
	expect(powervr_shared_usc_program(1, load, sizeof(load), &load_length) == 0, "shared load usc builds");
	expect(store_length % POWERVR_USC_PROGRAM_ALIGN == 0 && load_length % POWERVR_USC_PROGRAM_ALIGN == 0, "shared usc aligned");
	expect(powervr_shared_usc_program(0, again, sizeof(again), &again_length) == 0 && again_length == store_length && memcmp(again, store, store_length) == 0, "shared store usc repeatable");
	expect(store_length != load_length || memcmp(store, load, store_length) != 0, "store and load differ");
	expect(powervr_shared_usc_program(2, store, sizeof(store), &store_length) == EINVAL, "shared usc refuses a flag of 2");
	printf("shared usc: store %zu bytes, load %zu bytes\n", store_length, load_length);
}

int
main(void)
{
	words();
	programs();
	tasks();
	usc();
	printf("program-host-test: %d passed, %d failed\n", passes, failures);
	return failures != 0;
}
