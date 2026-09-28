/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-090 host test: the C library's qsort, qsort_r, heapsort and mergesort
 * (src/libc/sort.c, compiled for the host with its functions renamed zed_*).
 *
 *   qsort-host check        orders against the host's qsort, stability of
 *                           mergesort, errors, misaligned arrays, a comparison
 *                           that is not an order, McIlroy's adversary
 *   qsort-host time         the old insertion sort, the new sort and the
 *                           host's qsort on the same inputs
 *
 * Every element is a key in its first bytes and the element's original index
 * in the rest.  A sorted result must have the host qsort's key at every
 * position and hold the same multiset of elements, which is the set equality
 * of each group of equal keys.  Prints QSORT:PASS and exits 0.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void zed_qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void zed_qsort_r(void *, size_t, size_t, int (*)(const void *, const void *, void *), void *);
int zed_heapsort(void *, size_t, size_t, int (*)(const void *, const void *));
int zed_mergesort(void *, size_t, size_t, int (*)(const void *, const void *));

/* The input patterns. */
enum pattern {
	PATTERN_RANDOM,
	PATTERN_SORTED,
	PATTERN_REVERSE,
	PATTERN_EQUAL,
	PATTERN_ORGAN_PIPE,
	PATTERN_DUPLICATES,
	PATTERN_SAWTOOTH,
	PATTERN_NEARLY_SORTED,
	PATTERN_COUNT
};

/* The sorts under test. */
enum sorter {
	SORTER_QSORT,
	SORTER_QSORT_R,
	SORTER_HEAPSORT,
	SORTER_MERGESORT,
	SORTER_COUNT
};

static const char *const pattern_names[PATTERN_COUNT] = {
	"random", "sorted", "reverse", "equal", "organ-pipe", "duplicates", "sawtooth", "nearly-sorted"
};

static const char *const sorter_names[SORTER_COUNT] = {
	"qsort", "qsort_r", "heapsort", "mergesort"
};

/* The element size and key width of the comparisons in progress. */
static size_t element_size;
static size_t key_width;

/* Comparisons made since the counter was last cleared. */
static unsigned long long comparisons;

/* The failures found so far. */
static int failures;

/* The state of the pseudo-random generator (xorshift64). */
static uint64_t random_state = 0x9e3779b97f4a7c15ULL;

/* McIlroy's adversary: the value of each element, the gas value and the frozen count. */
static int *adversary_values;
static int adversary_gas;
static int adversary_solid;
static int adversary_candidate;

static uint64_t next_random(void);
static uint32_t key_of(const unsigned char *element);
static int compare_keys(const void *left, const void *right);
static int compare_keys_argument(const void *left, const void *right, void *argument);
static int compare_bytes(const void *left, const void *right);
static int compare_random(const void *left, const void *right);
static int compare_adversary(const void *left, const void *right);
static void fill(unsigned char *elements, size_t count, enum pattern pattern);
static int run_sorter(enum sorter sorter, unsigned char *elements, size_t count);
static void verify(const char *what, const unsigned char *result, const unsigned char *expected, size_t count, int stable);
static void check_one(size_t count, size_t size, enum pattern pattern);
static void check_errors(void);
static void check_misaligned(void);
static void check_inconsistent(void);
static void check_adversary(size_t count);
static void old_qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
static double seconds_now(void);
static void time_one(size_t count, size_t size, enum pattern pattern, int with_old);

/* Runs the checks or the timings. */
int
main(
	int argc,
	char **argv)
{
	static const size_t sizes[] = { 1, 3, 4, 8, 16, 24 };
	static const size_t counts[] = { 17, 31, 32, 33, 63, 64, 65, 100, 127, 1000, 4097, 10000, 100000, 1000000 };
	size_t size_index;
	size_t count_index;
	size_t count;
	int pattern;

	/* Times the sorts when asked. */
	if (argc > 1 && strcmp(argv[1], "time") == 0) {
		printf("%-14s %8s %4s %12s %12s %12s %14s\n", "pattern", "n", "size", "old ms", "new ms", "host ms", "new cmp/nlog2n");
		for (pattern = 0; pattern < PATTERN_COUNT; pattern++) {
			time_one(1000, 8, pattern, 1);
			time_one(10000, 8, pattern, 1);
			time_one(30000, 8, pattern, 1);
			time_one(1000000, 8, pattern, 0);
			time_one(1000000, 24, pattern, 0);
			time_one(1000000, 3, pattern, 0);
		}
		return 0;
	}

	/* Checks every size, every pattern and every length. */
	for (size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]); size_index++) {
		for (pattern = 0; pattern < PATTERN_COUNT; pattern++) {
			for (count = 0; count <= 16; count++)
				check_one(count, sizes[size_index], pattern);
			for (count_index = 0; count_index < sizeof(counts) / sizeof(counts[0]); count_index++)
				check_one(counts[count_index], sizes[size_index], pattern);
		}
		printf("size %zu: checked\n", sizes[size_index]);
	}

	/* The edge cases. */
	check_errors();
	check_misaligned();
	check_inconsistent();
	check_adversary(10000);
	check_adversary(100000);

	/* Reports the outcome. */
	if (failures != 0) {
		printf("QSORT:FAIL %d\n", failures);
		return 1;
	}

	printf("QSORT:PASS\n");
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

/* Reads the key of an element: its first key_width bytes, most significant first. */
static uint32_t
key_of(
	const unsigned char *element)
{
	uint32_t key;
	size_t index;

	key = 0;
	for (index = 0; index < key_width; index++)
		key = (key << 8) | element[index];
	return key;
}

/* Orders two elements by key. */
static int
compare_keys(
	const void *left,
	const void *right)
{
	uint32_t left_key;
	uint32_t right_key;

	comparisons++;
	left_key = key_of(left);
	right_key = key_of(right);
	if (left_key < right_key)
		return -1;
	if (left_key > right_key)
		return 1;
	return 0;
}

/* Orders two elements by key, checking the argument qsort_r passes through. */
static int
compare_keys_argument(
	const void *left,
	const void *right,
	void *argument)
{
	int ordering;

	/* The argument is the element size the caller set. */
	if (argument != &element_size) {
		printf("qsort_r: wrong argument %p\n", argument);
		failures++;
	}

	ordering = compare_keys(left, right);
	return ordering;
}

/* Orders two elements by all their bytes, which makes equal-key groups canonical. */
static int
compare_bytes(
	const void *left,
	const void *right)
{
	int ordering;

	ordering = memcmp(left, right, element_size);
	return ordering;
}

/* Answers at random: a comparison that is no order at all. */
static int
compare_random(
	const void *left,
	const void *right)
{
	(void)left;
	(void)right;
	comparisons++;
	return (int)(next_random() % 3) - 1;
}

/* McIlroy's adversary: freezes values only as the sort forces it to. */
static int
compare_adversary(
	const void *left,
	const void *right)
{
	int left_index;
	int right_index;

	comparisons++;
	memcpy(&left_index, left, sizeof(left_index));
	memcpy(&right_index, right, sizeof(right_index));
	if (adversary_values[left_index] == adversary_gas && adversary_values[right_index] == adversary_gas) {
		if (left_index == adversary_candidate)
			adversary_values[left_index] = adversary_solid++;
		else
			adversary_values[right_index] = adversary_solid++;
	}
	if (adversary_values[left_index] == adversary_gas)
		adversary_candidate = left_index;
	else if (adversary_values[right_index] == adversary_gas)
		adversary_candidate = right_index;
	return adversary_values[left_index] - adversary_values[right_index];
}

/* Fills an array of COUNT elements of element_size bytes with a pattern. */
static void
fill(
	unsigned char *elements,
	size_t count,
	enum pattern pattern)
{
	uint32_t key_limit;
	uint32_t key;
	size_t index;
	size_t byte;
	unsigned char *element;

	/* The keys that fit in the key bytes. */
	key_limit = key_width >= 4 ? 0xffffffffU : (1U << (8 * key_width)) - 1U;

	for (index = 0; index < count; index++) {
		switch (pattern) {
		case PATTERN_RANDOM:
			key = (uint32_t)next_random();
			break;
		case PATTERN_SORTED:
			key = (uint32_t)index;
			break;
		case PATTERN_REVERSE:
			key = (uint32_t)(count - index);
			break;
		case PATTERN_EQUAL:
			key = 7;
			break;
		case PATTERN_ORGAN_PIPE:
			key = (uint32_t)(index < count / 2 ? index : count - index);
			break;
		case PATTERN_DUPLICATES:
			key = (uint32_t)(next_random() % 10);
			break;
		case PATTERN_SAWTOOTH:
			key = (uint32_t)(index % 97);
			break;
		case PATTERN_NEARLY_SORTED:
		default:
			key = (uint32_t)index;
			if (next_random() % 50 == 0)
				key = (uint32_t)next_random();
			break;
		}

		/* Keeps the key in range; a sorted pattern longer than the range saturates. */
		if (pattern != PATTERN_RANDOM && pattern != PATTERN_NEARLY_SORTED && key > key_limit)
			key = key_limit;
		key &= key_limit;

		/* The key most significant first, then the index. */
		element = elements + index * element_size;
		for (byte = 0; byte < key_width; byte++)
			element[byte] = (unsigned char)(key >> (8 * (key_width - 1 - byte)));
		for (byte = key_width; byte < element_size; byte++)
			element[byte] = (unsigned char)((uint64_t)index >> (8 * ((element_size - 1 - byte) % 8)));
	}
}

/* Sorts an array with one of the sorts under test; reports its return value. */
static int
run_sorter(
	enum sorter sorter,
	unsigned char *elements,
	size_t count)
{
	int status;

	status = 0;
	switch (sorter) {
	case SORTER_QSORT:
		zed_qsort(elements, count, element_size, compare_keys);
		break;
	case SORTER_QSORT_R:
		zed_qsort_r(elements, count, element_size, compare_keys_argument, &element_size);
		break;
	case SORTER_HEAPSORT:
		status = zed_heapsort(elements, count, element_size, compare_keys);
		break;
	case SORTER_MERGESORT:
	default:
		status = zed_mergesort(elements, count, element_size, compare_keys);
		break;
	}
	return status;
}

/*
 * Compares a result with the host qsort's: the same key at every position and
 * the same multiset of elements.  A stable result must equal the input sorted
 * stably, which is checked by the index bytes rising within each key.
 */
static void
verify(
	const char *what,
	const unsigned char *result,
	const unsigned char *expected,
	size_t count,
	int stable)
{
	unsigned char *left;
	unsigned char *right;
	size_t index;
	size_t index_bytes;

	/* The key at every position. */
	for (index = 0; index < count; index++) {
		if (key_of(result + index * element_size) != key_of(expected + index * element_size)) {
			printf("%s: key differs at %zu\n", what, index);
			failures++;
			return;
		}
	}

	/* The multiset: both sorted by every byte must be identical. */
	left = malloc(count * element_size + 1);
	right = malloc(count * element_size + 1);
	if (left == NULL || right == NULL) {
		printf("%s: out of memory\n", what);
		exit(2);
	}
	memcpy(left, result, count * element_size);
	memcpy(right, expected, count * element_size);
	qsort(left, count, element_size, compare_bytes);
	qsort(right, count, element_size, compare_bytes);
	if (memcmp(left, right, count * element_size) != 0) {
		printf("%s: the elements differ\n", what);
		failures++;
	}
	free(left);
	free(right);

	/* Stability, when the index bytes number the elements uniquely. */
	index_bytes = element_size - key_width;
	if (!stable || index_bytes == 0)
		return;
	if (index_bytes < sizeof(size_t) && count > ((size_t)1 << (8 * index_bytes)))
		return;
	for (index = 1; index < count; index++) {
		if (key_of(result + (index - 1) * element_size) != key_of(result + index * element_size))
			continue;
		if (memcmp(result + (index - 1) * element_size + key_width,
			   result + index * element_size + key_width, index_bytes) > 0) {
			printf("%s: equal keys out of their order at %zu\n", what, index);
			failures++;
			return;
		}
	}
}

/* Sorts one input with every sort and verifies each against the host's qsort. */
static void
check_one(
	size_t count,
	size_t size,
	enum pattern pattern)
{
	unsigned char *input;
	unsigned char *expected;
	unsigned char *work;
	char what[128];
	int sorter;
	int status;

	/* The element layout of this size. */
	element_size = size;
	key_width = size < 4 ? 1 : size == 4 ? 2 : 4;

	/* The input and the host's answer. */
	input = malloc(count * size + 1);
	expected = malloc(count * size + 1);
	work = malloc(count * size + 1);
	if (input == NULL || expected == NULL || work == NULL) {
		printf("out of memory\n");
		exit(2);
	}
	fill(input, count, pattern);
	memcpy(expected, input, count * size);
	qsort(expected, count, size, compare_keys);

	/* Every sort under test. */
	for (sorter = 0; sorter < SORTER_COUNT; sorter++) {
		/* The big inputs only for the element sizes that matter most, to keep the run short. */
		if (count > 100000 && sorter != SORTER_QSORT && size != 8 && size != 24)
			continue;

		snprintf(what, sizeof(what), "%s n=%zu size=%zu %s", sorter_names[sorter], count, size, pattern_names[pattern]);
		memcpy(work, input, count * size);
		comparisons = 0;
		status = run_sorter(sorter, work, count);
		if (status != 0) {
			printf("%s: returned %d errno %d\n", what, status, errno);
			failures++;
			continue;
		}
		verify(what, work, expected, count, sorter == SORTER_MERGESORT);
	}

	free(input);
	free(expected);
	free(work);
}

/* Checks the refusals and the arrays with nothing to sort. */
static void
check_errors(void)
{
	unsigned char buffer[4];
	int status;

	memset(buffer, 0x5a, sizeof(buffer));

	/* heapsort and mergesort refuse a zero size with EINVAL. */
	errno = 0;
	status = zed_heapsort(buffer, 4, 0, compare_keys);
	if (status != -1 || errno != EINVAL) {
		printf("heapsort size 0: %d errno %d\n", status, errno);
		failures++;
	}
	errno = 0;
	status = zed_mergesort(buffer, 4, 0, compare_keys);
	if (status != -1 || errno != EINVAL) {
		printf("mergesort size 0: %d errno %d\n", status, errno);
		failures++;
	}

	/* qsort and qsort_r accept a zero size, a zero count and no comparison without touching memory. */
	zed_qsort(buffer, 4, 0, compare_keys);
	zed_qsort(NULL, 0, 4, compare_keys);
	zed_qsort(buffer, 4, 1, NULL);
	zed_qsort_r(buffer, 4, 0, compare_keys_argument, &element_size);
	zed_qsort_r(NULL, 0, 4, compare_keys_argument, &element_size);
	status = zed_heapsort(NULL, 0, 4, compare_keys);
	if (status != 0) {
		printf("heapsort n=0: %d\n", status);
		failures++;
	}
	status = zed_mergesort(NULL, 0, 4, compare_keys);
	if (status != 0) {
		printf("mergesort n=0: %d\n", status);
		failures++;
	}
	if (buffer[0] != 0x5a || buffer[3] != 0x5a) {
		printf("an empty sort wrote the array\n");
		failures++;
	}

	/* mergesort reports ENOMEM for an array whose copy cannot be measured. */
	errno = 0;
	status = zed_mergesort(buffer, SIZE_MAX / 2, 4, compare_keys);
	if (status != -1 || errno != ENOMEM) {
		printf("mergesort huge: %d errno %d\n", status, errno);
		failures++;
	}
	printf("errors: checked\n");
}

/*
 * Sorts arrays that start one, two and four bytes past an aligned address,
 * where the sort must fall back to a narrower unit (UBSan's alignment check
 * catches a wide access).
 */
static void
check_misaligned(void)
{
	static const size_t offsets[] = { 1, 2, 4 };
	static const size_t sizes[] = { 4, 8, 16, 24 };
	unsigned char *storage;
	unsigned char *expected;
	char what[128];
	size_t count;
	size_t offset_index;
	size_t size_index;
	int sorter;

	count = 5000;
	for (size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]); size_index++) {
		element_size = sizes[size_index];
		key_width = element_size == 4 ? 2 : 4;
		storage = malloc(count * element_size + 16);
		expected = malloc(count * element_size);
		for (offset_index = 0; offset_index < sizeof(offsets) / sizeof(offsets[0]); offset_index++) {
			for (sorter = 0; sorter < SORTER_COUNT; sorter++) {
				fill(storage + offsets[offset_index], count, PATTERN_RANDOM);
				memcpy(expected, storage + offsets[offset_index], count * element_size);
				qsort(expected, count, element_size, compare_keys);
				snprintf(what, sizeof(what), "%s misaligned +%zu size=%zu", sorter_names[sorter], offsets[offset_index], element_size);
				run_sorter(sorter, storage + offsets[offset_index], count);
				verify(what, storage + offsets[offset_index], expected, count, sorter == SORTER_MERGESORT);
			}
		}
		free(storage);
		free(expected);
	}
	printf("misaligned: checked\n");
}

/* Sorts with a comparison that is no order: nothing may be lost or written outside. */
static void
check_inconsistent(void)
{
	unsigned char *work;
	unsigned char *before;
	unsigned char *after;
	size_t count;
	int sorter;
	int round;

	element_size = 8;
	key_width = 4;
	count = 20000;
	work = malloc(count * element_size);
	before = malloc(count * element_size);
	after = malloc(count * element_size);
	for (round = 0; round < 5; round++) {
		for (sorter = 0; sorter < SORTER_COUNT; sorter++) {
			fill(work, count, PATTERN_RANDOM);
			memcpy(before, work, count * element_size);
			switch (sorter) {
			case SORTER_QSORT:
			case SORTER_QSORT_R:
				zed_qsort(work, count, element_size, compare_random);
				break;
			case SORTER_HEAPSORT:
				zed_heapsort(work, count, element_size, compare_random);
				break;
			default:
				zed_mergesort(work, count, element_size, compare_random);
				break;
			}
			memcpy(after, work, count * element_size);
			qsort(before, count, element_size, compare_bytes);
			qsort(after, count, element_size, compare_bytes);
			if (memcmp(before, after, count * element_size) != 0) {
				printf("%s with a random comparison lost elements\n", sorter_names[sorter]);
				failures++;
			}
		}
	}
	free(work);
	free(before);
	free(after);
	printf("inconsistent comparison: checked\n");
}

/*
 * Runs McIlroy's adversary against qsort: a plain quicksort takes a quadratic
 * number of comparisons, an introspective sort stays near n log n.
 */
static void
check_adversary(
	size_t count)
{
	int *indices;
	int *values;
	size_t index;
	double per_n_log_n;
	double log2_count;

	indices = malloc(count * sizeof(*indices));
	values = malloc(count * sizeof(*values));
	adversary_values = values;
	adversary_gas = (int)count - 1;
	adversary_solid = 0;
	adversary_candidate = 0;
	for (index = 0; index < count; index++) {
		indices[index] = (int)index;
		values[index] = adversary_gas;
	}
	element_size = sizeof(int);
	comparisons = 0;
	zed_qsort(indices, count, sizeof(int), compare_adversary);

	/* The comparisons per n log2 n. */
	log2_count = 0;
	for (index = count; index > 1; index /= 2)
		log2_count += 1;
	per_n_log_n = (double)comparisons / ((double)count * log2_count);
	printf("adversary n=%zu: %llu comparisons, %.2f per n log2 n\n", count, comparisons, per_n_log_n);
	if (per_n_log_n > 8.0) {
		printf("adversary: too many comparisons\n");
		failures++;
	}

	/* The adversary's own order must come out sorted. */
	for (index = 1; index < count; index++) {
		if (values[indices[index - 1]] > values[indices[index]]) {
			printf("adversary: not sorted at %zu\n", index);
			failures++;
			break;
		}
	}
	free(indices);
	free(values);
}

/* The implementation before BUG-090 (src/libc/stdlib-extra.c), for the timings. */
static void
old_qsort(
	void *base,
	size_t count,
	size_t size,
	int (*compare)(const void *, const void *))
{
	unsigned char *bytes;
	unsigned char *a;
	unsigned char *b;
	unsigned char t;
	size_t i;
	size_t j;
	size_t n;

	bytes = base;
	for (i = 1; i < count; i++) {
		for (j = i; j && compare(bytes + (j - 1) * size, bytes + j * size) > 0; j--) {
			a = bytes + (j - 1) * size;
			b = bytes + j * size;
			for (n = size; n != 0; n--) {
				t = *a;
				*a++ = *b;
				*b++ = t;
			}
		}
	}
}

/* Reports a monotonic time in seconds. */
static double
seconds_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Times the old sort (when asked), the new one and the host's on one input. */
static void
time_one(
	size_t count,
	size_t size,
	enum pattern pattern,
	int with_old)
{
	unsigned char *input;
	unsigned char *work;
	double start;
	double old_ms;
	double new_ms;
	double host_ms;
	double log2_count;
	unsigned long long new_comparisons;
	size_t index;

	element_size = size;
	key_width = size < 4 ? 1 : size == 4 ? 2 : 4;
	input = malloc(count * size);
	work = malloc(count * size);
	fill(input, count, pattern);

	old_ms = -1;
	if (with_old) {
		memcpy(work, input, count * size);
		start = seconds_now();
		old_qsort(work, count, size, compare_keys);
		old_ms = (seconds_now() - start) * 1000;
	}

	memcpy(work, input, count * size);
	comparisons = 0;
	start = seconds_now();
	zed_qsort(work, count, size, compare_keys);
	new_ms = (seconds_now() - start) * 1000;
	new_comparisons = comparisons;

	memcpy(work, input, count * size);
	start = seconds_now();
	qsort(work, count, size, compare_keys);
	host_ms = (seconds_now() - start) * 1000;

	log2_count = 0;
	for (index = count; index > 1; index /= 2)
		log2_count += 1;
	if (with_old) {
		printf("%-14s %8zu %4zu %12.2f %12.2f %12.2f %14.2f\n", pattern_names[pattern], count, size,
		       old_ms, new_ms, host_ms, (double)new_comparisons / ((double)count * log2_count));
	} else {
		printf("%-14s %8zu %4zu %12s %12.2f %12.2f %14.2f\n", pattern_names[pattern], count, size,
		       "-", new_ms, host_ms, (double)new_comparisons / ((double)count * log2_count));
	}
	free(input);
	free(work);
}
