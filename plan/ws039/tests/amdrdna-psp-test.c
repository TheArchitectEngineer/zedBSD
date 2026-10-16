/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amdrdna driver's words of stages A3t, A3 and A4: the
 * firmware files' headers cut into the pieces the security processor
 * loads, the security processor's ring frame, commands and reply, the
 * place of the memory-training data, and the thermal block's temperature,
 * fan and alert words.  The thermal words are checked against the board's
 * registers (the dump of 2026-10-16: "cold" is the firmware's state,
 * "loaded" Linux's).
 *
 *   amdrdna-psp-test [FIRMWARE-DIRECTORY]
 *
 * FIRMWARE-DIRECTORY holds the navi10_*.bin files of linux-firmware
 * 20260410; without it the real-file cases are skipped and say so.  Each
 * case prints "PASS name" or "FAIL name: why"; the exit status is 1 when
 * any case failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/* One real file's expected pieces: its name, kind, count and (offset, size) pairs. */
struct expected_file {
	const char *name;
	enum amdrdna_firmware_kind kind;
	unsigned pieces;
	uint32_t offsets[AMDRDNA_FIRMWARE_PIECES];
	uint32_t sizes[AMDRDNA_FIRMWARE_PIECES];
};

/*
 * The navi10 files of linux-firmware 20260410 and their pieces, read from
 * the headers by hand (plan/ws039/design.md 4.2 and the header dump of
 * ws039-p004).
 */
static const struct expected_file expected_files[] = {
	{ "navi10_sos.bin", AMDRDNA_FIRMWARE_SECURE, 4U,
	  { 256U, 100464U, 179104U, 180640U }, { 100208U, 78640U, 1536U, 3872U } },
	{ "navi10_asd.bin", AMDRDNA_FIRMWARE_WHOLE, 1U, { 256U, 0U, 0U, 0U }, { 205056U, 0U, 0U, 0U } },
	{ "navi10_smc.bin", AMDRDNA_FIRMWARE_WHOLE, 1U, { 256U, 0U, 0U, 0U }, { 262656U, 0U, 0U, 0U } },
	{ "navi10_pfp.bin", AMDRDNA_FIRMWARE_WHOLE, 1U, { 256U, 0U, 0U, 0U }, { 263168U, 0U, 0U, 0U } },
	{ "navi10_me.bin", AMDRDNA_FIRMWARE_WHOLE, 1U, { 256U, 0U, 0U, 0U }, { 263168U, 0U, 0U, 0U } },
	{ "navi10_ce.bin", AMDRDNA_FIRMWARE_WHOLE, 1U, { 256U, 0U, 0U, 0U }, { 263040U, 0U, 0U, 0U } },
	{ "navi10_mec.bin", AMDRDNA_FIRMWARE_COMPUTE, 2U, { 256U, 267264U, 0U, 0U }, { 267008U, 896U, 0U, 0U } },
	{ "navi10_mec2.bin", AMDRDNA_FIRMWARE_COMPUTE, 2U, { 256U, 267264U, 0U, 0U }, { 267008U, 896U, 0U, 0U } },
	{ "navi10_rlc.bin", AMDRDNA_FIRMWARE_RUN_LIST, 4U,
	  { 256U, 25344U, 25936U, 27472U }, { 25088U, 592U, 1536U, 16480U } },
	{ "navi10_sdma.bin", AMDRDNA_FIRMWARE_WHOLE, 1U, { 256U, 0U, 0U, 0U }, { 33536U, 0U, 0U, 0U } },
	{ "navi10_sdma1.bin", AMDRDNA_FIRMWARE_WHOLE, 1U, { 256U, 0U, 0U, 0U }, { 33536U, 0U, 0U, 0U } }
};

/* The case results. */
static int failures;

static void check(int condition, const char *name, const char *why);
static void test_files(const char *directory);
static void test_synthetic(void);
static void test_frames(void);
static void test_commands(void);
static void test_training(void);
static void test_thermal(void);
static uint8_t *read_file(const char *path, size_t *length);
static void put32(uint8_t *bytes, size_t at, uint32_t value);
static uint32_t get32(const uint8_t *bytes, size_t at);

int
main(
	int argc,
	char **argv)
{
	/* Runs the real-file cases when a directory is given. */
	if (argc == 2) {
		test_files(argv[1]);
	} else {
		printf("SKIP firmware files: no directory\n");
	}

	/* Runs the other cases. */
	test_synthetic();
	test_frames();
	test_commands();
	test_training();
	test_thermal();

	/* Reports the whole result. */
	if (failures != 0) {
		printf("FAIL amdrdna-psp-test: %d case(s)\n", failures);
		return 1;
	}

	printf("PASS amdrdna-psp-test\n");
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

/* The real firmware files: each one's pieces must be the ones read by hand. */
static void
test_files(
	const char *directory)
{
	struct amdrdna_firmware_layout layout;
	const struct expected_file *file;
	char path[512];
	char name[96];
	uint8_t *bytes;
	size_t length;
	unsigned index;
	unsigned piece;
	int same;
	int error;

	/* Checks every file. */
	for (index = 0U; index < sizeof(expected_files) / sizeof(expected_files[0]); index++) {
		file = &expected_files[index];
		snprintf(path, sizeof(path), "%s/%s", directory, file->name);
		snprintf(name, sizeof(name), "file.%s", file->name);
		bytes = read_file(path, &length);
		if (bytes == NULL) {
			check(0, name, "not readable");
			continue;
		}

		/* Cuts it and compares the pieces. */
		error = amdrdna_firmware_layout(file->kind, bytes, length, &layout);
		same = error == 0 && layout.piece_count == file->pieces;
		for (piece = 0U; same && piece < file->pieces; piece++) {
			if (layout.pieces[piece].offset != file->offsets[piece] ||
			    layout.pieces[piece].bytes != file->sizes[piece])
				same = 0;
		}
		check(same, name, "the pieces differ from the header read by hand");

		/* A file one byte short no longer matches its own header. */
		snprintf(name, sizeof(name), "file.%s.short", file->name);
		error = amdrdna_firmware_layout(file->kind, bytes, length - 1U, &layout);
		check(error == EINVAL, name, "a truncated file is taken");
		free(bytes);
	}
}

/* Synthetic headers: pieces past the end, wrong layouts and wrong kinds are refused. */
static void
test_synthetic(void)
{
	struct amdrdna_firmware_layout layout;
	uint8_t bytes[512];
	int error;

	/* A whole file: 512 bytes, a 32-byte header, a 256-byte program at 256. */
	memset(bytes, 0, sizeof(bytes));
	put32(bytes, 0x00U, sizeof(bytes));
	put32(bytes, 0x04U, 32U);
	put32(bytes, 0x08U, 0x00000001U);
	put32(bytes, 0x14U, 256U);
	put32(bytes, 0x18U, 256U);
	error = amdrdna_firmware_layout(AMDRDNA_FIRMWARE_WHOLE, bytes, sizeof(bytes), &layout);
	check(error == 0 && layout.piece_count == 1U && layout.pieces[0].offset == 256U && layout.pieces[0].bytes == 256U,
	    "synthetic.whole", "the program is not the one piece");

	/* The program one byte past the end. */
	put32(bytes, 0x14U, 257U);
	error = amdrdna_firmware_layout(AMDRDNA_FIRMWARE_WHOLE, bytes, sizeof(bytes), &layout);
	check(error == EINVAL, "synthetic.past-end", "a program past the end is taken");

	/* A size that wraps a 32-bit sum. */
	put32(bytes, 0x14U, 0xffffff80U);
	error = amdrdna_firmware_layout(AMDRDNA_FIRMWARE_WHOLE, bytes, sizeof(bytes), &layout);
	check(error == EINVAL, "synthetic.wrap", "a wrapping size is taken");

	/* A header larger than the file. */
	put32(bytes, 0x14U, 256U);
	put32(bytes, 0x04U, 1024U);
	error = amdrdna_firmware_layout(AMDRDNA_FIRMWARE_WHOLE, bytes, sizeof(bytes), &layout);
	check(error == EINVAL, "synthetic.header", "a header past the end is taken");

	/* A security file of layout 1.0 does not list its table and keys. */
	put32(bytes, 0x04U, 0x44U);
	put32(bytes, 0x08U, 0x00000001U);
	error = amdrdna_firmware_layout(AMDRDNA_FIRMWARE_SECURE, bytes, sizeof(bytes), &layout);
	check(error == EINVAL, "synthetic.secure-1.0", "a 1.0 security file is taken");

	/* A compute file whose jump table is the whole program. */
	put32(bytes, 0x04U, 0x2cU);
	put32(bytes, 0x24U, 0U);
	put32(bytes, 0x28U, 64U);
	error = amdrdna_firmware_layout(AMDRDNA_FIRMWARE_COMPUTE, bytes, sizeof(bytes), &layout);
	check(error == EINVAL, "synthetic.jumps-all", "a jump table with no program is taken");

	/* A run-list file of layout 2.0. */
	put32(bytes, 0x04U, 0x9cU);
	put32(bytes, 0x08U, 0x00000002U);
	error = amdrdna_firmware_layout(AMDRDNA_FIRMWARE_RUN_LIST, bytes, sizeof(bytes), &layout);
	check(error == EINVAL, "synthetic.run-list-2.0", "a 2.0 run-list file is taken");

	/* A file shorter than any header. */
	error = amdrdna_firmware_layout(AMDRDNA_FIRMWARE_WHOLE, bytes, 16U, &layout);
	check(error == EINVAL, "synthetic.tiny", "a 16-byte file is taken");
}

/* The ring frame and the write pointer's steps. */
static void
test_frames(void)
{
	uint8_t frame[AMDRDNA_PSP_FRAME_BYTES];
	uint32_t index;
	uint32_t next;
	int error;

	/* A frame's words: command 0x12_3456_7000, fence 0x8_0001_2000, value 7; the rest zero. */
	memset(frame, 0xa5, sizeof(frame));
	amdrdna_psp_frame(frame, 0x1234567000ULL, 0x800012000ULL, 7U);
	check(get32(frame, 0U) == 0x34567000U && get32(frame, 4U) == 0x12U && get32(frame, 8U) == 0U &&
	    get32(frame, 12U) == 0x00012000U && get32(frame, 16U) == 0x8U && get32(frame, 20U) == 7U &&
	    get32(frame, 24U) == 0U && get32(frame, 60U) == 0U,
	    "frame.words", "the frame's words are not in their places");

	/* The first frame, the last one wrapping, and a pointer between frames. */
	error = amdrdna_psp_ring_step(0U, &index, &next);
	check(error == 0 && index == 0U && next == 16U, "ring.first", "pointer 0 is not frame 0 then 16");
	error = amdrdna_psp_ring_step(1008U, &index, &next);
	check(error == 0 && index == 63U && next == 0U, "ring.wrap", "pointer 1008 is not frame 63 then 0");
	error = amdrdna_psp_ring_step(1024U + 32U, &index, &next);
	check(error == 0 && index == 2U && next == 48U, "ring.modulo", "a pointer past the ring is not taken modulo");
	error = amdrdna_psp_ring_step(8U, &index, &next);
	check(error == EINVAL, "ring.between", "a pointer between frames is taken");
}

/* The commands' words and the reply. */
static void
test_commands(void)
{
	uint8_t command[AMDRDNA_PSP_COMMAND_BYTES];
	struct amdrdna_psp_reply reply;

	/* Load a program: ID 6, address, size and type from byte 28. */
	memset(command, 0xa5, sizeof(command));
	amdrdna_psp_command_program(command, 0x1234567000ULL, 33536U, AMDRDNA_PROGRAM_COPY0);
	check(get32(command, 0U) == 0U && get32(command, 4U) == 0U && get32(command, 8U) == 6U &&
	    get32(command, 28U) == 0x34567000U && get32(command, 32U) == 0x12U &&
	    get32(command, 36U) == 33536U && get32(command, 40U) == 9U && get32(command, 1020U) == 0U,
	    "command.program", "the load command's words are not in their places");

	/* The table of contents: ID 0x20, address and size. */
	amdrdna_psp_command_contents(command, 0x100000ULL, 1536U);
	check(get32(command, 8U) == 0x20U && get32(command, 28U) == 0x100000U && get32(command, 32U) == 0U &&
	    get32(command, 36U) == 1536U && get32(command, 40U) == 0U,
	    "command.contents", "the table command's words are not in their places");

	/* The trusted region: ID 5, MC address, size, flags 2, physical address. */
	amdrdna_psp_command_region(command, 0x817d000000ULL, 0x900000U, 0x17d000000ULL);
	check(get32(command, 8U) == 5U && get32(command, 28U) == 0x7d000000U && get32(command, 32U) == 0x81U &&
	    get32(command, 36U) == 0x900000U && get32(command, 40U) == 2U &&
	    get32(command, 44U) == 0x7d000000U && get32(command, 48U) == 1U,
	    "command.region", "the region command's words are not in their places");

	/* The display application: ID 4, address, size, no shared buffer. */
	amdrdna_psp_command_application(command, 0x200000ULL, 205056U);
	check(get32(command, 8U) == 4U && get32(command, 28U) == 0x200000U && get32(command, 36U) == 205056U &&
	    get32(command, 40U) == 0U && get32(command, 44U) == 0U && get32(command, 48U) == 0U,
	    "command.application", "the application command's words are not in their places");

	/* Unload, start the run list, drop the region. */
	amdrdna_psp_command_unload(command, 0x55U);
	check(get32(command, 8U) == 2U && get32(command, 28U) == 0x55U, "command.unload", "the unload words differ");
	amdrdna_psp_command_plain(command, false);
	check(get32(command, 8U) == 0x21U && get32(command, 28U) == 0U, "command.run-list", "the start word differs");
	amdrdna_psp_command_plain(command, true);
	check(get32(command, 8U) == 7U, "command.drop", "the drop word differs");

	/* The reply from byte 864. */
	memset(command, 0, sizeof(command));
	put32(command, 864U, 0x0000000aU);
	put32(command, 868U, 0x00000003U);
	put32(command, 872U, 0x00400000U);
	put32(command, 876U, 0x00000081U);
	put32(command, 880U, 0x00900000U);
	amdrdna_psp_reply_read(command, &reply);
	check(reply.status == 0xaU && reply.session == 3U && reply.place == 0x8100400000ULL && reply.region_bytes == 0x900000U,
	    "reply.words", "the reply's words are not read from their places");
}

/* The memory-training data's places on the board's 6128 MiB. */
static void
test_training(void)
{
	uint64_t driver_copy;
	uint64_t processor_copy;

	/* No reserve from the video BIOS: 64 KiB, then 1 MiB below, rounded up. */
	amdrdna_training_place(0x17f000000ULL, 0U, &driver_copy, &processor_copy);
	check(driver_copy == 0x17ef00000ULL && processor_copy == 0x17eff8000ULL,
	    "training.default", "the places differ from Linux's arithmetic");

	/* A 2 MiB reserve. */
	amdrdna_training_place(0x17f000000ULL, 0x200000ULL, &driver_copy, &processor_copy);
	check(driver_copy == 0x17ed00000ULL, "training.reserve", "the place below a 2 MiB reserve differs");
}

/* The thermal words against the board's registers. */
static void
test_thermal(void)
{
	unsigned sensor;
	unsigned highest;
	uint32_t word;
	int error;

	/* The cold temperature word 0x5a2d: both fields 45 C (Linux's edge read 43 C). */
	amdrdna_temperature_read(0x00005a2dU, &sensor, &highest);
	check(sensor == 45U && highest == 45U, "thermal.temperature", "0x5a2d does not read 45 C twice");

	/* 50 % of the full duty 0x7d over the cold duty word 0x01813333: 127 / 255 of 125 is 62. */
	error = amdrdna_fan_duty_word(0x01813333U, 0x96091a7dU, 50U, &word);
	check(error == 0 && word == 0x0181333eU, "fan.duty", "the 50 % duty word is not 0x0181333e");
	error = amdrdna_fan_duty_word(0x01813333U, 0x96091a7dU, 100U, &word);
	check(error == 0 && word == 0x0181337dU, "fan.duty.full", "100 % is not the full duty");
	error = amdrdna_fan_duty_word(0U, 0x96091a00U, 50U, &word);
	check(error == EINVAL, "fan.duty.no-full", "a controller without a full duty is driven");
	error = amdrdna_fan_duty_word(0U, 0x7dU, 101U, &word);
	check(error == EINVAL, "fan.duty.over", "101 % is taken");

	/* The cold mode word 0x02c91430 (mode 2, lowest 48 C) turns static with no lowest temperature. */
	word = amdrdna_fan_mode_word(0x02c91430U);
	check(word == 0x02c90c00U, "fan.mode", "the static mode word is not 0x02c90c00");

	/* The cold tachometer 0x15e132 is about 1046 turns a minute; zero reads as stopped. */
	check(amdrdna_fan_speed(0x0015e132U) == 1046U, "fan.speed", "0x15e132 is not 1046 rpm");
	check(amdrdna_fan_speed(0U) == 0U, "fan.speed.zero", "a zero count is not a stopped fan");

	/* The driven duty 62 of 125 is 50 %. */
	check(amdrdna_fan_driven(62U << 9, 0x7dU) == 50U, "fan.driven", "62 of 125 is not 50 %");

	/* The cold alert word with 118 C high and 0 C low is Linux's loaded word. */
	word = amdrdna_alert_word(0x2fff0071U, 118U, 0U);
	check(word == 0xb8ff0076U, "alert.word", "the alert word is not Linux's 0xb8ff0076");
	word = amdrdna_alert_off_word(0xb8ff0076U);
	check(word == 0xbbff0076U, "alert.off", "the masked word is not 0xbbff0076");
	check(amdrdna_alert_clear_word() == 0x38U, "alert.clear", "the clear word is not 0x38");
}

/* Reads a whole file into memory; NULL when it cannot. */
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

/* Reads a little-endian 32-bit word. */
static uint32_t
get32(
	const uint8_t *bytes,
	size_t at)
{
	return amdrdna_le32(bytes, at);
}
