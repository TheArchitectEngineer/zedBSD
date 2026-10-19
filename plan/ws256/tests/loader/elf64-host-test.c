/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the UEFI loader's ELF plan and placement for the riscv64
 * kernel (bootloader/uefi/elf64.c built with __riscv), on a real vmunix.
 *
 *   elf64-host-test vmunix
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootloader/uefi/elf64.h"

static int failures;

static void
check(int condition, const char *what)
{
	if (condition) {
		printf("PASS %s\n", what);
	} else {
		printf("FAIL %s\n", what);
		failures++;
	}
}

int
main(int argc, char **argv)
{
	static uint8_t header[ZBL_ELF_HEADER_BYTES];
	static uint8_t bad[ZBL_ELF_HEADER_BYTES];
	struct zbl_elf64_plan plan;
	struct zbl_elf64_plan copy;
	FILE *file;
	long size;
	uint32_t index;
	int ok;
	int all;

	if (argc != 2) {
		fprintf(stderr, "usage: %s vmunix\n", argv[0]);
		return 2;
	}
	file = fopen(argv[1], "rb");
	if (file == NULL || fread(header, 1, sizeof(header), file) != sizeof(header))
		return 2;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fclose(file);

	ok = zbl_elf64_plan(header, (uint64_t)size, &plan);
	check(ok == 1, "the riscv64 kernel is accepted");
	check(plan.entry == 0xffffffff80000000ULL, "the entry is the linked virtual base");
	check(plan.link_physical_start == 0x80200000ULL, "the image is linked at 0x80200000");
	check(plan.link_physical_end - plan.link_physical_start <= 0x01000000ULL, "the image fits the 16 MiB window");
	check(plan.segment_count >= 2, "the image has its load segments");

	memcpy(&copy, &plan, sizeof(copy));
	ok = zbl_elf64_place(&copy, 0x80200000ULL);
	all = 1;
	for (index = 0; index < copy.segment_count; index++)
		all &= copy.segment[index].physical == copy.segment[index].virtual_address - 0xffffffff80000000ULL + 0x80200000ULL;
	check(ok == 1 && all, "placed where linked, every segment keeps its linked address");

	memcpy(&copy, &plan, sizeof(copy));
	ok = zbl_elf64_place(&copy, 0x90000000ULL);
	all = 1;
	for (index = 0; index < copy.segment_count; index++)
		all &= copy.segment[index].physical == plan.segment[index].physical - 0x80200000ULL + 0x90000000ULL;
	check(ok == 1 && all, "placed at 0x90000000, every segment moves by the same distance");
	check(copy.physical_start == 0x90000000ULL, "the placed image starts at the base");
	ok = zbl_elf64_place(&copy, 0xa0000000ULL);
	all = 1;
	for (index = 0; index < copy.segment_count; index++)
		all &= copy.segment[index].physical == plan.segment[index].physical - 0x80200000ULL + 0xa0000000ULL;
	check(ok == 1 && all, "placing a placed plan again starts from the linked addresses");

	memcpy(&copy, &plan, sizeof(copy));
	check(zbl_elf64_place(&copy, 0x90001000ULL) == 0, "a base off a megapage is refused");
	memcpy(&copy, &plan, sizeof(copy));
	check(zbl_elf64_place(&copy, 0x4000000000ULL) == 0, "a base past the low 256 GiB is refused");
	memcpy(&copy, &plan, sizeof(copy));
	check(zbl_elf64_place(&copy, 0) == 0, "a zero base is refused");

	memcpy(bad, header, sizeof(bad));
	bad[18] = 62;
	bad[19] = 0;
	check(zbl_elf64_plan(bad, (uint64_t)size, &copy) == 0, "an amd64 machine is refused");
	memcpy(bad, header, sizeof(bad));
	bad[16] = 3;
	check(zbl_elf64_plan(bad, (uint64_t)size, &copy) == 0, "a shared object is refused");
	check(zbl_elf64_plan(header, 1024, &copy) == 0, "segments past the file are refused");

	if (failures != 0) {
		printf("%d FAILED\n", failures);
		return 1;
	}
	printf("all PASS\n");
	return 0;
}
