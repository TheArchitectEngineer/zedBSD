/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the riscv64 loader's /chosen boot-hartid reader
 * (bootloader/uefi/fdt-hartid.c) on trees built by dtc, and on damaged
 * copies of one of them.
 *
 *   fdt-hartid-host-test u32.dtb u64.dtb no-property.dtb no-chosen.dtb bad-length.dtb
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootloader/uefi/fdt-hartid.h"

/* The value the reader must leave alone when it finds no id. */
#define UNTOUCHED 0x5a5a5a5a5a5a5a5aULL

/* How many checks failed. */
static int failures;

/* Prints one check's outcome and counts a failure. */
static void
check(
	const char *name,
	int condition)
{
	/* One line per check. */
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

/* Reads a whole file into memory. */
static uint8_t *
read_file(
	const char *path,
	uint32_t *size)
{
	uint8_t *data;
	FILE *file;
	long length;

	/* Opens and measures the file. */
	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		exit(2);
	}
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* Reads it whole. */
	data = malloc((size_t)length);
	if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
		perror(path);
		exit(2);
	}
	fclose(file);
	*size = (uint32_t)length;
	return data;
}

/* Writes a big-endian 32-bit word. */
static void
put_be32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0] = (uint8_t)(value >> 24);
	bytes[1] = (uint8_t)(value >> 16);
	bytes[2] = (uint8_t)(value >> 8);
	bytes[3] = (uint8_t)value;
}

/* Reads a big-endian 32-bit word. */
static uint32_t
get_be32(
	const uint8_t *bytes)
{
	return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

/* Runs the reader on a tree and checks its answer and the id it gave. */
static void
expect(
	const char *name,
	const uint8_t *tree,
	uint32_t size,
	enum zbl_fdt_hartid_result wanted,
	uint64_t wanted_hart)
{
	enum zbl_fdt_hartid_result result;
	uint64_t hart;
	char label[160];

	/* Reads the tree with the id preset, so a refusal that writes it is seen. */
	hart = UNTOUCHED;
	result = zbl_fdt_boot_hartid(tree, size, &hart);
	printf("     %s: %s, hart 0x%llx\n", name, zbl_fdt_hartid_result_name(result), (unsigned long long)hart);

	/* Checks the answer, and the id: the wanted one when found, untouched otherwise. */
	snprintf(label, sizeof(label), "%s: result %s", name, zbl_fdt_hartid_result_name(wanted));
	check(label, result == wanted);
	snprintf(label, sizeof(label), "%s: hart", name);
	if (wanted == ZBL_FDT_HARTID_FOUND) {
		check(label, hart == wanted_hart);
	} else {
		check(label, hart == UNTOUCHED);
	}
}

/* Checks the reader on the five trees and on damaged copies of the first. */
int
main(
	int argc,
	char **argv)
{
	uint8_t *trees[5];
	uint32_t sizes[5];
	uint8_t *copy;
	uint32_t off_struct;
	int index;

	/* Takes the five trees. */
	if (argc != 6) {
		fprintf(stderr, "usage: %s u32.dtb u64.dtb no-property.dtb no-chosen.dtb bad-length.dtb\n", argv[0]);
		return 2;
	}
	for (index = 0; index < 5; index++)
		trees[index] = read_file(argv[index + 1], &sizes[index]);

	/* The trees as dtc built them. */
	expect("u32", trees[0], sizes[0], ZBL_FDT_HARTID_FOUND, 3);
	expect("u64", trees[1], sizes[1], ZBL_FDT_HARTID_FOUND, 0x100000002ULL);
	expect("no property (only a child of /chosen has one)", trees[2], sizes[2], ZBL_FDT_HARTID_NO_PROPERTY, 0);
	expect("no /chosen (only /soc/chosen)", trees[3], sizes[3], ZBL_FDT_HARTID_NO_CHOSEN, 0);
	expect("bad length", trees[4], sizes[4], ZBL_FDT_HARTID_BAD_LENGTH, 0);

	/* Damaged copies of the u32 tree, each refused as malformed. */
	copy = malloc(sizes[0] > sizes[2] ? sizes[0] : sizes[2]);
	if (copy == NULL)
		return 2;
	off_struct = get_be32(trees[0] + 8);

	memcpy(copy, trees[0], sizes[0]);
	put_be32(copy, 0xd00dfeeeU);
	expect("broken: magic", copy, sizes[0], ZBL_FDT_HARTID_MALFORMED, 0);

	memcpy(copy, trees[0], sizes[0]);
	expect("broken: totalsize beyond the bytes given", copy, sizes[0] - 1U, ZBL_FDT_HARTID_MALFORMED, 0);

	memcpy(copy, trees[0], sizes[0]);
	put_be32(copy + 20, 16);
	expect("broken: version 16", copy, sizes[0], ZBL_FDT_HARTID_MALFORMED, 0);

	memcpy(copy, trees[0], sizes[0]);
	put_be32(copy + 36, get_be32(copy + 36) + 4096U);
	expect("broken: structure block leaves the tree", copy, sizes[0], ZBL_FDT_HARTID_MALFORMED, 0);

	/*
	 * A structure block cut before its end token: the reader stops at the
	 * property, which lies inside the block, so the u32 tree still answers;
	 * the tree without the property must reach the cut and refuse it.
	 */
	memcpy(copy, trees[0], sizes[0]);
	put_be32(copy + 36, get_be32(copy + 36) - 4U);
	expect("cut before the end token, property inside", copy, sizes[0], ZBL_FDT_HARTID_FOUND, 3);
	memcpy(copy, trees[2], sizes[2]);
	put_be32(copy + 36, get_be32(copy + 36) - 4U);
	expect("broken: structure block without its end token", copy, sizes[2], ZBL_FDT_HARTID_MALFORMED, 0);

	memcpy(copy, trees[0], sizes[0]);
	put_be32(copy + 32, 4U);
	expect("broken: property names outside the strings block", copy, sizes[0], ZBL_FDT_HARTID_MALFORMED, 0);

	memcpy(copy, trees[0], sizes[0]);
	put_be32(copy + off_struct, 7U);
	expect("broken: unknown token", copy, sizes[0], ZBL_FDT_HARTID_MALFORMED, 0);

	/* The root's first property follows its 8-byte begin token and empty name. */
	memcpy(copy, trees[0], sizes[0]);
	check("u32 tree: first property at struct offset 8", get_be32(copy + off_struct + 8) == 3U);
	put_be32(copy + off_struct + 12, 0xfffffff0U);
	expect("broken: property length runs off the block", copy, sizes[0], ZBL_FDT_HARTID_MALFORMED, 0);

	memcpy(copy, trees[0], sizes[0]);
	put_be32(copy + off_struct, 2U);
	expect("broken: end of a node with none open", copy, sizes[0], ZBL_FDT_HARTID_MALFORMED, 0);

	expect("broken: no tree", NULL, 0, ZBL_FDT_HARTID_MALFORMED, 0);

	/* Releases the trees. */
	free(copy);
	for (index = 0; index < 5; index++)
		free(trees[index]);

	/* Reports the count. */
	printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
