/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-090 guest probe: the image's libc.so sorts large arrays in O(n log n).
 *
 *   qsort-guest [COUNT]      (COUNT defaults to 1000000)
 *
 * Sorts COUNT random 8-byte keys and COUNT/4 24-byte records with qsort, the
 * records again with qsort_r and heapsort, and with mergesort checks that
 * records of equal keys keep their order.  Prints the milliseconds of each
 * and QSORT-GUEST:PASS, and exits 0.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * One 24-byte record: a key with many duplicates and the record's original
 * position, which the stability check reads.
 */
struct record {
	uint32_t key;
	uint32_t pad;
	uint64_t position;
	uint64_t check;
};

/* The state of the pseudo-random generator (xorshift64). */
static uint64_t random_state = 0x9e3779b97f4a7c15ULL;

/* The failures found so far. */
static int failures;

static uint64_t next_random(void);
static double milliseconds_now(void);
static int compare_keys(const void *left, const void *right);
static int compare_records(const void *left, const void *right);
static int compare_records_argument(const void *left, const void *right, void *argument);
static void fill_records(struct record *records, size_t count);
static void check_records(const char *what, const struct record *records, size_t count, int stable);

/* Runs the sorts and reports their times. */
int
main(
	int argc,
	char **argv)
{
	uint64_t *keys;
	uint64_t held;
	struct record *records;
	size_t count;
	size_t record_count;
	size_t index;
	double start;
	int status;

	/* The number of keys. */
	count = 1000000;
	if (argc > 1)
		count = (size_t)strtoul(argv[1], NULL, 10);
	record_count = count / 4;

	keys = malloc(count * sizeof(*keys));
	records = malloc(record_count * sizeof(*records));
	if (keys == NULL || records == NULL) {
		printf("QSORT-GUEST:FAIL out of memory\n");
		return 1;
	}

	/* qsort on random 8-byte keys. */
	for (index = 0; index < count; index++)
		keys[index] = next_random();
	start = milliseconds_now();
	qsort(keys, count, sizeof(*keys), compare_keys);
	printf("qsort %zu x 8 bytes random: %.1f ms\n", count, milliseconds_now() - start);
	for (index = 1; index < count; index++) {
		if (keys[index - 1] > keys[index]) {
			printf("qsort keys: out of order at %zu\n", index);
			failures++;
			break;
		}
	}

	/* qsort on sorted and reverse keys, which the old insertion sort took quadratic time on in reverse. */
	start = milliseconds_now();
	qsort(keys, count, sizeof(*keys), compare_keys);
	printf("qsort %zu x 8 bytes sorted: %.1f ms\n", count, milliseconds_now() - start);
	for (index = 0; index < count / 2; index++) {
		held = keys[index];
		keys[index] = keys[count - 1 - index];
		keys[count - 1 - index] = held;
	}
	start = milliseconds_now();
	qsort(keys, count, sizeof(*keys), compare_keys);
	printf("qsort %zu x 8 bytes reverse: %.1f ms\n", count, milliseconds_now() - start);
	for (index = 1; index < count; index++) {
		if (keys[index - 1] > keys[index]) {
			printf("qsort reverse: out of order at %zu\n", index);
			failures++;
			break;
		}
	}

	/* qsort, qsort_r, heapsort and mergesort on 24-byte records with duplicate keys. */
	fill_records(records, record_count);
	start = milliseconds_now();
	qsort(records, record_count, sizeof(*records), compare_records);
	printf("qsort %zu x 24 bytes duplicates: %.1f ms\n", record_count, milliseconds_now() - start);
	check_records("qsort", records, record_count, 0);

	fill_records(records, record_count);
	start = milliseconds_now();
	qsort_r(records, record_count, sizeof(*records), compare_records_argument, &failures);
	printf("qsort_r %zu x 24 bytes duplicates: %.1f ms\n", record_count, milliseconds_now() - start);
	check_records("qsort_r", records, record_count, 0);

	fill_records(records, record_count);
	start = milliseconds_now();
	status = heapsort(records, record_count, sizeof(*records), compare_records);
	printf("heapsort %zu x 24 bytes duplicates: %.1f ms (%d)\n", record_count, milliseconds_now() - start, status);
	check_records("heapsort", records, record_count, 0);

	fill_records(records, record_count);
	start = milliseconds_now();
	status = mergesort(records, record_count, sizeof(*records), compare_records);
	printf("mergesort %zu x 24 bytes duplicates: %.1f ms (%d)\n", record_count, milliseconds_now() - start, status);
	check_records("mergesort", records, record_count, 1);

	free(keys);
	free(records);

	/* Reports the outcome. */
	if (failures != 0) {
		printf("QSORT-GUEST:FAIL %d\n", failures);
		return 1;
	}

	printf("QSORT-GUEST:PASS\n");
	return 0;
}

/* Steps the xorshift64 generator. */
static uint64_t
next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return random_state;
}

/* Reports a monotonic time in milliseconds. */
static double
milliseconds_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}

/* Orders two 8-byte keys. */
static int
compare_keys(
	const void *left,
	const void *right)
{
	uint64_t left_key;
	uint64_t right_key;

	memcpy(&left_key, left, sizeof(left_key));
	memcpy(&right_key, right, sizeof(right_key));
	if (left_key < right_key)
		return -1;
	if (left_key > right_key)
		return 1;
	return 0;
}

/* Orders two records by key alone. */
static int
compare_records(
	const void *left,
	const void *right)
{
	const struct record *left_record;
	const struct record *right_record;

	left_record = left;
	right_record = right;
	if (left_record->key < right_record->key)
		return -1;
	if (left_record->key > right_record->key)
		return 1;
	return 0;
}

/* Orders two records by key, checking the argument qsort_r passes through. */
static int
compare_records_argument(
	const void *left,
	const void *right,
	void *argument)
{
	int ordering;

	if (argument != &failures) {
		printf("qsort_r: wrong argument\n");
		failures++;
	}
	ordering = compare_records(left, right);
	return ordering;
}

/* Fills records with keys of 1000 values and their positions. */
static void
fill_records(
	struct record *records,
	size_t count)
{
	size_t index;

	for (index = 0; index < count; index++) {
		records[index].key = (uint32_t)(next_random() % 1000);
		records[index].pad = 0;
		records[index].position = index;
		records[index].check = index ^ 0x5a5a5a5a5a5a5a5aULL;
	}
}

/* Checks the order of records, every record's integrity, and optionally stability. */
static void
check_records(
	const char *what,
	const struct record *records,
	size_t count,
	int stable)
{
	unsigned char *seen;
	size_t index;

	seen = calloc(count, 1);
	if (seen == NULL) {
		printf("%s: out of memory\n", what);
		failures++;
		return;
	}
	for (index = 0; index < count; index++) {
		if (records[index].check != (records[index].position ^ 0x5a5a5a5a5a5a5a5aULL) ||
		    records[index].position >= count || seen[records[index].position]) {
			printf("%s: record %zu damaged or repeated\n", what, index);
			failures++;
			break;
		}
		seen[records[index].position] = 1;
		if (index == 0)
			continue;
		if (records[index - 1].key > records[index].key) {
			printf("%s: out of order at %zu\n", what, index);
			failures++;
			break;
		}
		if (stable && records[index - 1].key == records[index].key &&
		    records[index - 1].position > records[index].position) {
			printf("%s: equal keys out of their order at %zu\n", what, index);
			failures++;
			break;
		}
	}
	free(seen);
}
