/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the static TLS placement in uapi/tls.h.
 *
 * Variant II (amd64, i386) is compared against the arithmetic elf.c and
 * static-tls.c used before the placement moved into the header.  Variant I
 * (m68k) is checked against the thread pointer m68k-read-tp.c computes.
 * Each further argument "vaddr:align:memsz:symbol:displacement" is one
 * thread-local access the m68k linker resolved, checked to land on the
 * variable's byte in the placed block.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "include/uapi/tls.h"

/* The page size both the loader and the C library round the block to. */
#define TEST_PAGE_SIZE		4096U

/* The m68k thread pointer's distance past the block's start. */
#define TEST_M68K_TP_OFFSET	0x7000U

/* The number of failed checks. */
static unsigned failures;

static void check(int condition, const char *what, uint64_t vaddr, size_t alignment, uint64_t memsz);
static void check_variant_ii(uint64_t vaddr, size_t alignment, uint64_t memsz);
static void check_variant_i(uint64_t vaddr, size_t alignment, uint64_t memsz);
static void check_m68k_access(const char *argument);

/* Runs the placement checks and reports how many failed. */
int
main(
	int argc,
	char **argv)
{
	static const uint64_t sizes[] = {
		0, 1, 3, 4, 8, 20, 4095, 4096, 4097, 8191, 65536,
		KERN_TLS_MEMORY_MAX - 1U, KERN_TLS_MEMORY_MAX
	};
	static const size_t alignments[] = { 1, 2, 4, 8, 16, 64, 256, 4096 };
	static const uint64_t addresses[] = {
		0x00401000U, 0x00401004U, 0x00401010U, 0x00402ff0U,
		0x00500000U, 0x7fffe123U
	};
	size_t s;
	size_t a;
	size_t v;
	int i;

	/* Sweeps sizes, alignments and segment addresses for both variants. */
	for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		for (a = 0; a < sizeof(alignments) / sizeof(alignments[0]); a++) {
			for (v = 0; v < sizeof(addresses) / sizeof(addresses[0]); v++) {
				check_variant_ii(addresses[v], alignments[a], sizes[s]);
				check_variant_i(addresses[v] & ~(uint64_t)(alignments[a] - 1U),
				    alignments[a], sizes[s]);
			}
		}
	}

	/* Checks each access the m68k linker resolved. */
	for (i = 1; i < argc; i++)
		check_m68k_access(argv[i]);

	/* Reports the outcome. */
	if (failures != 0) {
		printf("FAIL: %u checks (pointer %u bits)\n", failures,
		    (unsigned)(sizeof(void *) * 8U));
		return 1;
	}

	/* Succeeded. */
	printf("PASS: tls layout (pointer %u bits, %d m68k accesses)\n",
	    (unsigned)(sizeof(void *) * 8U), argc - 1);
	return 0;
}

/* Counts and reports one failed check. */
static void
check(
	int condition,
	const char *what,
	uint64_t vaddr,
	size_t alignment,
	uint64_t memsz)
{
	/* A passing check leaves no trace. */
	if (condition)
		return;

	failures++;
	printf("fail: %s (vaddr %#llx align %lu memsz %llu)\n", what,
	    (unsigned long long)vaddr, (unsigned long)alignment,
	    (unsigned long long)memsz);
}

/* Compares the variant II placement with the former arithmetic. */
static void
check_variant_ii(
	uint64_t vaddr,
	size_t alignment,
	uint64_t memsz)
{
	struct kern_tls_static_layout layout;
	size_t old_distance;
	size_t old_payload;
	size_t distance;

	/* The former elf.c arithmetic (the thread pointer at payload's end). */
	old_distance = (size_t)memsz +
	    (size_t)((0U - vaddr - memsz) & (alignment - 1U));
	old_payload = (old_distance + TEST_PAGE_SIZE - 1U) &
	    ~(size_t)(TEST_PAGE_SIZE - 1U);

	/* The header's placement. */
	distance = kern_tls_static_distance(vaddr, memsz, alignment,
	    KERN_TLS_VARIANT_II);
	kern_tls_static_layout(distance, TEST_PAGE_SIZE, KERN_TLS_VARIANT_II,
	    &layout);

	/* The same distance, mapping, control block and block start. */
	check(distance == old_distance, "II distance", vaddr, alignment, memsz);
	check(layout.mapping_size == old_payload + KERN_TLS_TCB_RESERVE,
	    "II mapping size", vaddr, alignment, memsz);
	check(layout.control_offset == old_payload, "II control offset",
	    vaddr, alignment, memsz);
	check(layout.block_offset == old_payload - old_distance,
	    "II block offset", vaddr, alignment, memsz);

	/* The block's start keeps the segment's address modulo its alignment. */
	check(((layout.block_offset - vaddr) & (alignment - 1U)) == 0,
	    "II block alignment", vaddr, alignment, memsz);
}

/* Checks the variant I placement against the m68k thread pointer. */
static void
check_variant_i(
	uint64_t vaddr,
	size_t alignment,
	uint64_t memsz)
{
	struct kern_tls_static_layout layout;
	size_t distance;
	size_t pointer;

	/* The header's placement. */
	distance = kern_tls_static_distance(vaddr, memsz, alignment,
	    KERN_TLS_VARIANT_I);
	kern_tls_static_layout(distance, TEST_PAGE_SIZE, KERN_TLS_VARIANT_I,
	    &layout);

	/* The control block opens the mapping and the block follows its reserve. */
	check(distance == memsz, "I distance", vaddr, alignment, memsz);
	check(layout.control_offset == 0, "I control offset", vaddr, alignment,
	    memsz);
	check(layout.block_offset == KERN_TLS_TCB_RESERVE, "I block offset",
	    vaddr, alignment, memsz);

	/* The whole block fits in the mapping, in whole pages. */
	check(layout.block_offset + memsz <= layout.mapping_size,
	    "I block inside mapping", vaddr, alignment, memsz);
	check((layout.mapping_size & (TEST_PAGE_SIZE - 1U)) == 0,
	    "I whole pages", vaddr, alignment, memsz);

	/* m68k-read-tp.c's thread pointer lies 0x7000 past the block's start. */
	pointer = layout.control_offset + KERN_TLS_TCB_RESERVE +
	    TEST_M68K_TP_OFFSET;
	check(pointer - TEST_M68K_TP_OFFSET == layout.block_offset,
	    "I thread pointer", vaddr, alignment, memsz);

	/* An aligned segment's variables stay aligned in the block. */
	check((layout.block_offset & (alignment - 1U)) == 0, "I block alignment",
	    vaddr, alignment, memsz);
}

/* Checks one linker-resolved m68k access lands on its variable. */
static void
check_m68k_access(
	const char *argument)
{
	struct kern_tls_static_layout layout;
	unsigned long long vaddr;
	unsigned long long memsz;
	unsigned long long symbol;
	unsigned long alignment;
	long displacement;
	size_t distance;
	long pointer;
	long variable;
	int fields;

	/* Reads vaddr:align:memsz:symbol:displacement. */
	fields = sscanf(argument, "%llx:%lx:%llx:%llx:%ld", &vaddr, &alignment,
	    &memsz, &symbol, &displacement);
	if (fields != 5) {
		failures++;
		printf("fail: bad access argument %s\n", argument);
		return;
	}

	/* Places the block as exec does on the m68k. */
	distance = kern_tls_static_distance(vaddr, memsz, alignment,
	    KERN_TLS_VARIANT_I);
	kern_tls_static_layout(distance, TEST_PAGE_SIZE, KERN_TLS_VARIANT_I,
	    &layout);

	/* The access's address and the variable's byte in the placed block. */
	pointer = (long)(layout.control_offset + KERN_TLS_TCB_RESERVE +
	    TEST_M68K_TP_OFFSET);
	variable = (long)(layout.block_offset + (symbol - vaddr));
	check(pointer + displacement == variable, argument, vaddr, alignment,
	    memsz);
}
