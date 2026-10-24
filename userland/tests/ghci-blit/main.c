/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI blitter test: on a kernel built with the GHCI driver's native
 * command (root only), it makes a mappable blob on the GHCI GPU, sends
 * blits through GPU_OP_GHCI_NATIVE and checks the memory through the
 * mapping:
 *
 *  - a new blob reads zero;
 *  - a 32-bit fill, and the pixels around it untouched;
 *  - a 32-bit copy;
 *  - a 16-bit fill of an odd width;
 *  - an overlapping copy forward (the destination before the source)
 *    and one in reverse (the destination after it);
 *  - many small fills, each with its fence, so the ring turns several
 *    times;
 *  - streams the driver must refuse (EINVAL): a DRAW, a fill past the
 *    blob, a register outside the blitter's.
 *
 * Every blit is followed by a fence of the test's own, written into the
 * blob, which the test waits for through the mapping.  The result is one
 * line:
 *
 *   GHCI-BLIT PASS checks=N
 *   GHCI-BLIT FAIL step=STEP error=E
 *
 *   ghci-blit [--node=/dev/gpuN] [--rounds=N]
 */

#include <uapi/gpu.h>
#include <uapi/gpu-op.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* The GPU nodes looked at for a GHCI device, and the prefix of its driver name. */
#define NODES_MAX			16
#define GHCI_PREFIX			"ghci:"

/* The blob: 1 MiB, with the fence words in its last page. */
#define BLOB_BYTES			(1U << 20)
#define FENCE_OFFSET			(BLOB_BYTES - 4096U)

/* The ring turns after this many small fills of the default run. */
#define ROUNDS_DEFAULT			1000U

/* How long a fence is waited for, in milliseconds. */
#define FENCE_WAIT_MS			1000U

/*
 * The packet envelope and the standard micro-operations, and the
 * blitter's state words and operation bits.
 *
 * Values referenced: GHCI 1.0 (draft) sections 7.1 (the envelope),
 * 7.3 (the micro-operations) and 8.2 (the blitter range).
 */
#define OP_SET_REG			1U
#define OP_BLIT				2U
#define OP_DRAW				3U
#define OP_FENCE			5U
#define OP_INVALIDATE			12U
#define BLT_OP				0x000U
#define BLT_DST_BASE			0x001U
#define BLT_DST_STRIDE			0x002U
#define BLT_SRC_BASE			0x003U
#define BLT_SRC_STRIDE			0x004U
#define BLT_DST_XY			0x005U
#define BLT_SRC_XY			0x006U
#define BLT_SIZE			0x007U
#define BLT_COLOR			0x008U
#define BLT_FILL			0U
#define BLT_COPY			1U
#define BLT_BPP16			(1U << 8)
#define BLT_REVERSE			(1U << 12)
#define REGISTER_OUTSIDE		0x020U

/*
 * One native command being built: the gpu-op's head and its packets.
 * count is the number of packets, dwords the words used so far.
 */
struct native {
	uint32_t words[256];
	uint32_t dwords;
	uint32_t count;
};

/*
 * The test's state: the node, the blob's handle, GPU address and
 * mapping, the next fence value and the checks that held.
 */
struct test {
	int fd;
	uint64_t handle;
	uint32_t gpu;
	volatile uint8_t *map;
	uint32_t fence;
	unsigned checks;
};

static int find_node(const char *named, int *fd);
static int make_blob(struct test *test);
static void native_begin(struct native *native);
static void native_packet(struct native *native, uint32_t op, uint32_t flags, const uint32_t *payload, uint32_t count);
static void native_set(struct native *native, const uint32_t *pairs, uint32_t pairs_count);
static int native_send(struct test *test, struct native *native);
static int blit_and_wait(struct test *test, const uint32_t *pairs, uint32_t pairs_count);
static int wait_fence(struct test *test, uint32_t value);
static uint32_t word_at(const struct test *test, uint32_t offset);
static int test_zero(struct test *test);
static int test_fill(struct test *test);
static int test_copy(struct test *test);
static int test_fill16(struct test *test);
static int test_overlap(struct test *test);
static int test_rounds(struct test *test, unsigned rounds);
static int test_refusals(struct test *test);

int
main(
	int argc,
	char **argv)
{
	struct test test;
	const char *named;
	const char *step;
	unsigned rounds;
	int index;
	int error;

	/* The command line. */
	named = NULL;
	rounds = ROUNDS_DEFAULT;
	for (index = 1; index < argc; index++) {
		if (strncmp(argv[index], "--node=", 7U) == 0) {
			named = argv[index] + 7;
			continue;
		}

		/* How many small fills turn the ring. */
		if (strncmp(argv[index], "--rounds=", 9U) == 0) {
			rounds = (unsigned)strtoul(argv[index] + 9, NULL, 10);
			continue;
		}

		/* Anything else. */
		fprintf(stderr, "usage: ghci-blit [--node=/dev/gpuN] [--rounds=N]\n");
		return 2;
	}

	/* The GHCI node, and a blob mapped into the test. */
	memset(&test, 0, sizeof(test));
	step = "node";
	error = find_node(named, &test.fd);
	if (error == 0) {
		step = "blob";
		error = make_blob(&test);
	}

	/* Each test in turn, stopping at the first that fails. */
	if (error == 0) {
		step = "zero";
		error = test_zero(&test);
	}
	if (error == 0) {
		step = "fill";
		error = test_fill(&test);
	}
	if (error == 0) {
		step = "copy";
		error = test_copy(&test);
	}
	if (error == 0) {
		step = "fill16";
		error = test_fill16(&test);
	}
	if (error == 0) {
		step = "overlap";
		error = test_overlap(&test);
	}
	if (error == 0) {
		step = "rounds";
		error = test_rounds(&test, rounds);
	}
	if (error == 0) {
		step = "refusals";
		error = test_refusals(&test);
	}

	/* The result. */
	if (error != 0) {
		printf("GHCI-BLIT FAIL step=%s error=%d\n", step, error);
		return 1;
	}
	printf("GHCI-BLIT PASS checks=%u\n", test.checks);
	return 0;
}

/* Opens the named node, or the first GPU node whose driver is GHCI's. */
static int
find_node(
	const char *named,
	int *fd)
{
	struct gpu_info info;
	char path[32];
	int index;
	int status;
	int candidate;

	/* Looks at the named node, or at /dev/gpu0 to /dev/gpu15. */
	for (index = 0; index < NODES_MAX; index++) {
		if (named != NULL) {
			snprintf(path, sizeof(path), "%s", named);
		} else {
			snprintf(path, sizeof(path), "/dev/gpu%d", index);
		}

		/* A node that does not open is skipped. */
		candidate = open(path, O_RDWR | O_CLOEXEC);
		if (candidate < 0) {
			if (named != NULL)
				return errno;
			continue;
		}

		/* The driver's name says GHCI, and the native command is there. */
		memset(&info, 0, sizeof(info));
		info.version = GPU_ABI_VERSION;
		info.size = sizeof(info);
		status = ioctl(candidate, GPU_GET_INFO, &info);
		if (status == 0 &&
		    strncmp(info.driver_name, GHCI_PREFIX, strlen(GHCI_PREFIX)) == 0 &&
		    (info.capabilities & GPU_CAP_COMMAND) != 0U) {
			printf("ghci-blit: %s is %s\n", path, info.driver_name);
			*fd = candidate;
			return 0;
		}

		/* Not the node. */
		close(candidate);
		if (named != NULL)
			return ENODEV;
	}

	/* No GHCI node with the native command (a kernel without CONFIG_GHCI_NATIVE, or no GPU). */
	return ENODEV;
}

/* Makes the mappable blob and maps it. */
static int
make_blob(
	struct test *test)
{
	struct gpu_blob_create create;
	struct gpu_resource_map map;
	void *address;
	int status;

	/* The blob; its resource id is its GPU address. */
	memset(&create, 0, sizeof(create));
	create.version = GPU_ABI_VERSION;
	create.size = sizeof(create);
	create.bytes = BLOB_BYTES;
	create.flags = GPU_BLOB_MAPPABLE;
	status = ioctl(test->fd, GPU_BLOB_CREATE, &create);
	if (status != 0)
		return errno;
	test->handle = create.handle;
	test->gpu = create.resource_id;

	/* The mapping's offset, then the mapping. */
	memset(&map, 0, sizeof(map));
	map.version = GPU_ABI_VERSION;
	map.size = sizeof(map);
	map.handle = create.handle;
	status = ioctl(test->fd, GPU_RESOURCE_MAP, &map);
	if (status != 0)
		return errno;
	address = mmap(NULL, (size_t)map.bytes, PROT_READ | PROT_WRITE, MAP_SHARED, test->fd, (off_t)map.offset);
	if (address == MAP_FAILED)
		return errno;
	test->map = address;
	printf("ghci-blit: blob of %u bytes at GPU address %08x\n", BLOB_BYTES, (unsigned)test->gpu);

	/* Succeeded: the blob is mapped. */
	return 0;
}

/* Starts a native command. */
static void
native_begin(
	struct native *native)
{
	native->words[0] = GPU_OP_GHCI_NATIVE;
	native->words[1] = 0U;
	native->words[2] = 0U;
	native->dwords = 3U;
	native->count = 0U;
}

/* Appends one class 0 packet; the driver gives it the session's tag. */
static void
native_packet(
	struct native *native,
	uint32_t op,
	uint32_t flags,
	const uint32_t *payload,
	uint32_t count)
{
	uint32_t index;

	native->words[native->dwords] = (op << 24) | (flags << 8) | count;
	for (index = 0U; index < count; index++)
		native->words[native->dwords + 1U + index] = payload[index];
	native->dwords += 1U + count;
	native->count++;
}

/* Appends a SET_REG of (register, value) pairs. */
static void
native_set(
	struct native *native,
	const uint32_t *pairs,
	uint32_t pairs_count)
{
	native_packet(native, OP_SET_REG, 0U, pairs, 2U * pairs_count);
}

/* Sends a native command; 0 or the errno. */
static int
native_send(
	struct test *test,
	struct native *native)
{
	struct gpu_command command;
	int status;

	native->words[2] = native->count;
	memset(&command, 0, sizeof(command));
	command.version = GPU_ABI_VERSION;
	command.size = sizeof(command);
	command.address = (uint64_t)(uintptr_t)native->words;
	command.bytes = native->dwords * 4U;
	status = ioctl(test->fd, GPU_COMMAND, &command);
	if (status != 0)
		return errno;
	return 0;
}

/* Sends an invalidation, a SET_REG of the pairs, a BLIT and a fence, and waits for the fence. */
static int
blit_and_wait(
	struct test *test,
	const uint32_t *pairs,
	uint32_t pairs_count)
{
	struct native native;
	uint32_t fence[2];
	int error;

	native_begin(&native);
	native_packet(&native, OP_INVALIDATE, 1U, NULL, 0U);
	native_set(&native, pairs, pairs_count);
	native_packet(&native, OP_BLIT, 0U, NULL, 0U);
	test->fence++;
	fence[0] = test->gpu + FENCE_OFFSET;
	fence[1] = test->fence;
	native_packet(&native, OP_FENCE, 0U, fence, 2U);
	error = native_send(test, &native);
	if (error != 0)
		return error;
	return wait_fence(test, test->fence);
}

/* Waits through the mapping for the fence word to reach a value. */
static int
wait_fence(
	struct test *test,
	uint32_t value)
{
	struct timespec pause;
	unsigned waited;

	pause.tv_sec = 0;
	pause.tv_nsec = 1000000;
	for (waited = 0U; waited < FENCE_WAIT_MS; waited++) {
		if ((int32_t)(word_at(test, FENCE_OFFSET) - value) >= 0)
			return 0;
		nanosleep(&pause, NULL);
	}
	return ETIMEDOUT;
}

/* Reads a word of the blob through the mapping. */
static uint32_t
word_at(
	const struct test *test,
	uint32_t offset)
{
	return *(const volatile uint32_t *)(test->map + offset);
}

/* A new blob reads zero. */
static int
test_zero(
	struct test *test)
{
	uint32_t offset;

	for (offset = 0U; offset < BLOB_BYTES; offset += 4U) {
		if (word_at(test, offset) != 0U) {
			printf("ghci-blit: word at %u is %08x, not 0\n", (unsigned)offset, (unsigned)word_at(test, offset));
			return EIO;
		}
	}
	test->checks++;
	return 0;
}

/* A 32-bit fill of 256 x 64 pixels at (16, 8) of a 1024-pixel-wide image, with the pixels around it untouched. */
static int
test_fill(
	struct test *test)
{
	uint32_t pairs[] = {
		BLT_OP, BLT_FILL,
		BLT_DST_BASE, 0U,
		BLT_DST_STRIDE, 4096U,
		BLT_DST_XY, (8U << 16) | 16U,
		BLT_SIZE, (64U << 16) | 256U,
		BLT_COLOR, 0x11223344U
	};
	uint32_t x;
	uint32_t y;
	uint32_t expected;
	uint32_t word;
	int error;

	pairs[3] = test->gpu;
	error = blit_and_wait(test, pairs, 6U);
	if (error != 0)
		return error;

	/* Every pixel of rows 7 to 72 and columns 15 to 272. */
	for (y = 7U; y <= 72U; y++) {
		for (x = 15U; x <= 272U; x++) {
			expected = 0U;
			if (y >= 8U && y < 72U && x >= 16U && x < 272U)
				expected = 0x11223344U;
			word = word_at(test, y * 4096U + x * 4U);
			if (word != expected) {
				printf("ghci-blit: fill pixel (%u, %u) is %08x, not %08x\n", (unsigned)x, (unsigned)y, (unsigned)word, (unsigned)expected);
				return EIO;
			}
		}
	}
	test->checks++;
	return 0;
}

/* A 32-bit copy of 64 x 32 pixels of the fill to 256 KiB further on. */
static int
test_copy(
	struct test *test)
{
	uint32_t pairs[] = {
		BLT_OP, BLT_COPY,
		BLT_DST_BASE, 0U,
		BLT_DST_STRIDE, 4096U,
		BLT_SRC_BASE, 0U,
		BLT_SRC_STRIDE, 4096U,
		BLT_DST_XY, 0U,
		BLT_SRC_XY, (8U << 16) | 16U,
		BLT_SIZE, (32U << 16) | 64U
	};
	uint32_t x;
	uint32_t y;
	uint32_t word;
	int error;

	pairs[3] = test->gpu + 0x40000U;
	pairs[7] = test->gpu;
	error = blit_and_wait(test, pairs, 8U);
	if (error != 0)
		return error;

	/* Every copied pixel holds the fill's colour; the one past the row does not. */
	for (y = 0U; y < 32U; y++) {
		for (x = 0U; x <= 64U; x++) {
			word = word_at(test, 0x40000U + y * 4096U + x * 4U);
			if (word != (x < 64U ? 0x11223344U : 0U)) {
				printf("ghci-blit: copy pixel (%u, %u) is %08x\n", (unsigned)x, (unsigned)y, (unsigned)word);
				return EIO;
			}
		}
	}
	test->checks++;
	return 0;
}

/* A 16-bit fill of 3 pixels at a 2-byte offset, with its neighbours untouched. */
static int
test_fill16(
	struct test *test)
{
	uint32_t pairs[] = {
		BLT_OP, BLT_FILL | BLT_BPP16,
		BLT_DST_BASE, 0U,
		BLT_DST_STRIDE, 64U,
		BLT_DST_XY, 0U,
		BLT_SIZE, (1U << 16) | 3U,
		BLT_COLOR, 0xbeefU
	};
	const volatile uint16_t *halves;
	int error;

	pairs[3] = test->gpu + 0x80002U;
	error = blit_and_wait(test, pairs, 6U);
	if (error != 0)
		return error;

	/* Half-words 0 and 4 untouched, 1 to 3 filled. */
	halves = (const volatile uint16_t *)(test->map + 0x80000U);
	if (halves[0] != 0U || halves[1] != 0xbeefU || halves[2] != 0xbeefU || halves[3] != 0xbeefU || halves[4] != 0U) {
		printf("ghci-blit: 16-bit fill %04x %04x %04x %04x %04x\n", halves[0], halves[1], halves[2], halves[3], halves[4]);
		return EIO;
	}
	test->checks++;
	return 0;
}

/*
 * Overlapping copies inside one row of 256 pixels: forward by 16 pixels
 * to the left, then in reverse by 16 pixels to the right; each must give
 * what memmove gives.
 */
static int
test_overlap(
	struct test *test)
{
	uint32_t pairs[] = {
		BLT_OP, BLT_COPY,
		BLT_DST_BASE, 0U,
		BLT_DST_STRIDE, 4096U,
		BLT_SRC_BASE, 0U,
		BLT_SRC_STRIDE, 4096U,
		BLT_DST_XY, 0U,
		BLT_SRC_XY, 16U,
		BLT_SIZE, (1U << 16) | 200U
	};
	uint32_t expected[256];
	uint32_t index;
	volatile uint32_t *row;
	int error;

	/* A row of distinct words. */
	row = (volatile uint32_t *)(test->map + 0xc0000U);
	for (index = 0U; index < 256U; index++) {
		row[index] = 0x1000U + index;
		expected[index] = 0x1000U + index;
	}

	/* Forward: pixels 16..215 to 0..199. */
	pairs[3] = test->gpu + 0xc0000U;
	pairs[7] = test->gpu + 0xc0000U;
	error = blit_and_wait(test, pairs, 8U);
	if (error != 0)
		return error;
	memmove(&expected[0], &expected[16], 200U * 4U);
	for (index = 0U; index < 256U; index++) {
		if (row[index] != expected[index]) {
			printf("ghci-blit: forward overlap word %u is %08x, not %08x\n", (unsigned)index, (unsigned)row[index], (unsigned)expected[index]);
			return EIO;
		}
	}
	test->checks++;

	/* Reverse: pixels 0..199 to 16..215. */
	pairs[1] = BLT_COPY | BLT_REVERSE;
	pairs[11] = 16U;
	pairs[13] = 0U;
	error = blit_and_wait(test, pairs, 8U);
	if (error != 0)
		return error;
	memmove(&expected[16], &expected[0], 200U * 4U);
	for (index = 0U; index < 256U; index++) {
		if (row[index] != expected[index]) {
			printf("ghci-blit: reverse overlap word %u is %08x, not %08x\n", (unsigned)index, (unsigned)row[index], (unsigned)expected[index]);
			return EIO;
		}
	}
	test->checks++;
	return 0;
}

/* Many small fills, each with its fence, so the ring turns several times. */
static int
test_rounds(
	struct test *test,
	unsigned rounds)
{
	uint32_t pairs[] = {
		BLT_OP, BLT_FILL,
		BLT_DST_BASE, 0U,
		BLT_DST_STRIDE, 64U,
		BLT_DST_XY, 0U,
		BLT_SIZE, (1U << 16) | 16U,
		BLT_COLOR, 0U
	};
	unsigned round;
	int error;

	pairs[3] = test->gpu + 0xd0000U;
	for (round = 0U; round < rounds; round++) {
		pairs[11] = 0xa0000000U + round;
		error = blit_and_wait(test, pairs, 6U);
		if (error != 0)
			return error;
		if (word_at(test, 0xd0000U + 60U) != 0xa0000000U + round)
			return EIO;
	}
	test->checks++;
	return 0;
}

/* Streams the driver must refuse with EINVAL. */
static int
test_refusals(
	struct test *test)
{
	uint32_t pairs[] = {
		BLT_OP, BLT_FILL,
		BLT_DST_BASE, 0U,
		BLT_DST_STRIDE, 4096U,
		BLT_DST_XY, 0U,
		BLT_SIZE, (257U << 16) | 1024U
	};
	uint32_t outside[] = { REGISTER_OUTSIDE, 0U };
	struct native native;
	int error;

	/* A DRAW: the native command takes the blitter only. */
	native_begin(&native);
	native_packet(&native, OP_DRAW, 0U, NULL, 0U);
	error = native_send(test, &native);
	if (error != EINVAL)
		return error == 0 ? EIO : error;
	test->checks++;

	/* A fill one row past the blob. */
	pairs[3] = test->gpu;
	native_begin(&native);
	native_set(&native, pairs, 5U);
	native_packet(&native, OP_BLIT, 0U, NULL, 0U);
	error = native_send(test, &native);
	if (error != EINVAL)
		return error == 0 ? EIO : error;
	test->checks++;

	/* A register outside the blitter's range. */
	native_begin(&native);
	native_set(&native, outside, 1U);
	error = native_send(test, &native);
	if (error != EINVAL)
		return error == 0 ? EIO : error;
	test->checks++;
	return 0;
}
