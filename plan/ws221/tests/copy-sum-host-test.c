/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the kernel's word copy (kern_memcpy, src/kern/kcrt.c)
 * and the Internet checksum (src/kern/net/checksum.c), each built as the
 * kernel builds it: every pairing of source and destination alignment and
 * every length up to 80 bytes, then longer ones, against the host's memcpy
 * (the bytes around the destination untouched), and the checksum against a
 * byte-at-a-time reference at even and odd addresses.  Built for 64 and 32
 * bits by run-copy-sum-host-test.sh.  Prints "copy-sum-host-test: PASS" or
 * the first failures.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kern_memcpy(void *destination, const void *source, size_t count);
uint16_t net_checksum(const void *data, size_t length);

/* How many checks failed. */
static unsigned failures;

static void test_copy(void);
static void test_checksum(void);
static uint16_t reference_checksum(const uint8_t *data, size_t length);

/* Runs both tests and reports the result. */
int
main(void)
{
	/* A fixed seed: the same bytes each run. */
	srand(221);
	test_copy();
	test_checksum();

	/* The verdict. */
	if (failures != 0) {
		printf("copy-sum-host-test: FAIL (%u)\n", failures);
		return 1;
	}

	/* Succeeded: both tests agreed with the references. */
	printf("copy-sum-host-test: PASS (word %zu bytes)\n", sizeof(unsigned long));
	return 0;
}

/* Copies every alignment pairing and length and compares with the host's copy. */
static void
test_copy(void)
{
	static uint8_t source[4200];
	static uint8_t mine[4300];
	static uint8_t theirs[4300];
	size_t lengths[] = { 0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 79, 80, 1448, 1460, 1514, 2048, 4096 };
	size_t from;
	size_t to;
	size_t index;
	size_t length;
	int differs;

	/* Random bytes to copy. */
	for (index = 0; index < sizeof(source); index++)
		source[index] = (uint8_t)rand();

	/* Each source and destination offset within a word, each length. */
	for (from = 0; from < 16U; from++) {
		for (to = 0; to < 16U; to++) {
			for (index = 0; index < sizeof(lengths) / sizeof(lengths[0]); index++) {
				length = lengths[index];
				memset(mine, 0x5a, sizeof(mine));
				memset(theirs, 0x5a, sizeof(theirs));
				kern_memcpy(mine + 64 + to, source + from, length);
				memcpy(theirs + 64 + to, source + from, length);
				differs = memcmp(mine, theirs, sizeof(mine));
				if (differs != 0) {
					if (failures < 10)
						printf("  FAIL: copy from %zu to %zu of %zu bytes\n", from, to, length);
					failures++;
				}
			}
		}
	}
}

/* Sums buffers at even and odd addresses and compares with the reference. */
static void
test_checksum(void)
{
	static uint8_t buffer[4200];
	size_t lengths[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 20, 40, 60, 1448, 1460, 1480, 1514, 4097 };
	size_t offset;
	size_t index;
	size_t length;
	uint16_t mine;
	uint16_t expected;

	/* Random bytes, then all ones (the carries' worst case). */
	for (index = 0; index < sizeof(buffer); index++)
		buffer[index] = (uint8_t)rand();
	for (offset = 0; offset < 16U; offset++) {
		for (index = 0; index < sizeof(lengths) / sizeof(lengths[0]); index++) {
			length = lengths[index];
			mine = net_checksum(buffer + offset, length);
			expected = reference_checksum(buffer + offset, length);
			if (mine != expected) {
				if (failures < 20)
					printf("  FAIL: checksum at %zu of %zu bytes: %04x, not %04x\n",
					    offset, length, mine, expected);
				failures++;
			}
		}
	}

	/* The worst case for the carries: every bit set. */
	memset(buffer, 0xff, sizeof(buffer));
	for (offset = 0; offset < 8U; offset++) {
		mine = net_checksum(buffer + offset, 4097);
		expected = reference_checksum(buffer + offset, 4097);
		if (mine != expected) {
			printf("  FAIL: all-ones checksum at %zu: %04x, not %04x\n", offset, mine, expected);
			failures++;
		}
	}
}

/* The Internet checksum a byte pair at a time (RFC 1071). */
static uint16_t
reference_checksum(
	const uint8_t *data,
	size_t length)
{
	uint32_t sum;
	size_t index;

	/* Each big-endian word, then a trailing byte as the high half. */
	sum = 0;
	for (index = 0; index + 1U < length; index += 2U)
		sum += (uint32_t)(data[index] << 8) | data[index + 1U];
	if (length % 2U != 0)
		sum += (uint32_t)data[length - 1U] << 8;

	/* Folds the carries and complements. */
	while ((sum >> 16) != 0)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)~sum;
}
