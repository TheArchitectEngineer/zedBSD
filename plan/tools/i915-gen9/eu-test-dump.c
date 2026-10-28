/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compute test's fixture on the host (ws229-p005).
 *
 * Builds the batch and the interface descriptor of
 * src/drivers/gpu/i915/tests/execution/eu-test.c for Gen12 and for Gen9,
 * checks each batch's PIPELINE_SELECT words with the test's own check, and
 * writes the batches, the descriptors and the Gen9 store kernel as
 * little-endian dwords for eu-test-gen9-host.sh to judge.  The test file is
 * compiled in whole; the linker drops the functions that need a GPU.
 *
 *   eu-test-dump OUTDIR       writes OUTDIR/eu-gen12.bin, .idd, eu-gen9.bin, .idd and kernel-gen9.bin
 */

#include "../../../src/drivers/gpu/i915/tests/execution/eu-test.c"

#include <stdio.h>

/* The room each batch is built in, as the test's batch object has. */
#define EU_TEST_DUMP_DWORDS	1024U

/* The thread limit the dump builds with: one GT2 subslice set of each version, as the test computes it. */
#define EU_TEST_DUMP_THREADS_GEN12	(112U * 6U - 1U)
#define EU_TEST_DUMP_THREADS_GEN9	(56U * 3U - 1U)

static int eu_test_dump_write(const char *directory, const char *name, const uint32_t *words, unsigned count);
static int eu_test_dump_batch(const char *directory, const char *name, uint32_t max_threads, unsigned graphics_ver);

/*
 * Writes the two batches and the Gen9 kernel.
 */
int
main(
	int argc,
	char **argv)
{
	int error;

	/* Refuses a call without the output directory. */
	if (argc != 2) {
		fprintf(stderr, "usage: eu-test-dump OUTDIR\n");
		return 2;
	}

	/* Builds and writes the Gen12 batch. */
	error = eu_test_dump_batch(argv[1], "eu-gen12", EU_TEST_DUMP_THREADS_GEN12, 12U);
	if (error != 0)
		return 1;

	/* Builds and writes the Gen9 batch. */
	error = eu_test_dump_batch(argv[1], "eu-gen9", EU_TEST_DUMP_THREADS_GEN9, 9U);
	if (error != 0)
		return 1;

	/* Writes the Gen9 store kernel. */
	error = eu_test_dump_write(argv[1], "kernel-gen9.bin", drv_i915_test_eu_kernel_gen9, I915_TEST_EU_KERNEL_DWORDS);
	if (error != 0)
		return 1;

	/* Succeeded: every file is written. */
	return 0;
}

/* Builds one version's batch and descriptor, checks the batch's selects and writes NAME.bin and NAME.idd. */
static int
eu_test_dump_batch(
	const char *directory,
	const char *name,
	uint32_t max_threads,
	unsigned graphics_ver)
{
	static uint32_t cmds[EU_TEST_DUMP_DWORDS];
	struct i915_test_pipeline_select check;
	uint32_t idd[8];
	char file[256];
	unsigned count;
	int error;

	/* Builds the batch the test submits, at the test's addresses. */
	count = drv_i915_test_eu_build_batch_gen(cmds, EU_TEST_DUMP_DWORDS, I915_TEST_EU_SHARED_VA, I915_TEST_EU_SHARED_VA, max_threads, graphics_ver);
	if (count == 0U) {
		fprintf(stderr, "%s: the batch does not fit\n", name);
		return 1;
	}

	/* Refuses a batch the test itself would refuse to submit. */
	error = drv_i915_test_eu_check_pipeline_select_gen(cmds, count, &check, graphics_ver);
	if (error != 0) {
		fprintf(stderr, "%s: PIPELINE_SELECT check failed: 3d=%u gpgpu=%u bad=%u bad_word=%08x\n",
		    name,
		    check.n_3d,
		    check.n_gpgpu,
		    check.n_bad,
		    check.bad_word);
		return 1;
	}

	/* Writes it. */
	snprintf(file, sizeof(file), "%s.bin", name);
	error = eu_test_dump_write(directory, file, cmds, count);
	if (error != 0)
		return 1;

	/* Builds and writes the descriptor the test writes for the same version. */
	drv_i915_test_eu_fill_idd(idd, graphics_ver);
	snprintf(file, sizeof(file), "%s.idd", name);
	error = eu_test_dump_write(directory, file, idd, 8U);
	if (error != 0)
		return 1;

	/* Says how long it is and where its selects are. */
	printf("%s: %u dwords, PIPELINE_SELECT 3D at %u, GPGPU at %u\n", name, count, check.idx_3d, check.idx_gpgpu);

	/* Succeeded: the batch is written. */
	return 0;
}

/* Writes dwords to a file of the output directory, little-endian. */
static int
eu_test_dump_write(
	const char *directory,
	const char *name,
	const uint32_t *words,
	unsigned count)
{
	unsigned char bytes[4];
	char path[4096];
	FILE *file;
	size_t written;
	unsigned i;
	int length;
	int closed;

	/* Names the file. */
	length = snprintf(path, sizeof(path), "%s/%s", directory, name);
	if (length < 0 || (size_t)length >= sizeof(path)) {
		fprintf(stderr, "%s: the path is too long\n", name);
		return 1;
	}

	/* Opens it. */
	file = fopen(path, "wb");
	if (file == NULL) {
		perror(path);
		return 1;
	}

	/* Writes each dword, low byte first. */
	for (i = 0U; i < count; i++) {
		bytes[0] = (unsigned char)words[i];
		bytes[1] = (unsigned char)(words[i] >> 8);
		bytes[2] = (unsigned char)(words[i] >> 16);
		bytes[3] = (unsigned char)(words[i] >> 24);
		written = fwrite(bytes, 1U, 4U, file);
		if (written != 4U) {
			fclose(file);
			fprintf(stderr, "%s: write failed\n", path);
			return 1;
		}
	}

	/* Closes it, which may report a late write failure. */
	closed = fclose(file);
	if (closed != 0) {
		perror(path);
		return 1;
	}

	/* Succeeded: the file holds the words. */
	return 0;
}
