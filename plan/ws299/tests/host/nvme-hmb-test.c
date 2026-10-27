/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the NVMe Host Memory Buffer helpers of
 * include/drivers/pci/pci-nvme-protocol.h: the Identify Controller fields,
 * the plan of the buffer, the Set Features command and the descriptor.
 *
 *   cc -std=c99 -Wall -Wextra -Werror -I include \
 *       -o build/tmp/nvme-hmb-test plan/ws299/tests/host/nvme-hmb-test.c
 */

#include <drivers/pci/pci-nvme-protocol.h>

#include <stdio.h>
#include <string.h>

#define MIB (1024ULL * 1024ULL)

/* How many checks failed; the test passes only when it stays zero. */
static unsigned failures;

static void
check(int condition, const char *what)
{
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void
put32(uint8_t *bytes, uint32_t value)
{
	unsigned index;

	for (index = 0; index < 4U; index++)
		bytes[index] = (uint8_t)(value >> (8U * index));
}

int
main(void)
{
	static uint8_t identify[DRV_NVME_IDENTIFY_SIZE];
	struct drv_nvme_controller_profile profile;
	struct drv_nvme_command command;
	uint64_t target, chunk, smallest, minimum;
	unsigned count;
	uint8_t entry[DRV_NVME_HMB_DESCRIPTOR_SIZE];

	/* Identify Controller of a BG5-like drive: HMPRE 16384, HMMIN 3584, HMMAXD 1024. */
	memset(identify, 0, sizeof(identify));
	identify[512] = 0x66;
	identify[513] = 0x44;
	put32(identify + 516, 1);
	put32(identify + 272, 16384);
	put32(identify + 276, 3584);
	put32(identify + 332, 0);
	identify[336] = 0x00;
	identify[337] = 0x04;
	check(drv_nvme_identify_controller_validate(identify, sizeof(identify), &profile) == 0, "identify valid");
	check(profile.hmb_preferred == 16384 && profile.hmb_minimum == 3584, "HMPRE and HMMIN");
	check(profile.hmb_minimum_descriptor == 0 && profile.hmb_maximum_descriptors == 1024, "HMMINDS and HMMAXD");

	/* 64 MiB in 16 MiB chunks, at least 14 MiB, at most 16 chunks. */
	check(drv_nvme_hmb_plan(&profile, 64 * MIB, 16 * MIB, 2 * MIB, 16, &target, &chunk, &smallest, &count, &minimum),
	    "plan");
	check(target == 64 * MIB && chunk == 16 * MIB && smallest == 2 * MIB && count == 16 && minimum == 14 * MIB,
	    "BG5 plan");

	/* A smaller limit than the minimum gives none. */
	drv_nvme_hmb_plan(&profile, 8 * MIB, 16 * MIB, 2 * MIB, 16, &target, &chunk, &smallest, &count, &minimum);
	check(target == 0, "minimum over the limit");

	/* No preference gives none. */
	profile.hmb_preferred = 0;
	drv_nvme_hmb_plan(&profile, 64 * MIB, 16 * MIB, 2 * MIB, 16, &target, &chunk, &smallest, &count, &minimum);
	check(target == 0, "no preference");

	/* HMMAXD and HMMINDS bound the chunks. */
	profile.hmb_preferred = 8192;
	profile.hmb_minimum = 0;
	profile.hmb_maximum_descriptors = 2;
	profile.hmb_minimum_descriptor = 1024;
	drv_nvme_hmb_plan(&profile, 64 * MIB, 16 * MIB, 2 * MIB, 16, &target, &chunk, &smallest, &count, &minimum);
	check(target == 32 * MIB && chunk == 16 * MIB && smallest == 4 * MIB && count == 2 && minimum == 4 * MIB,
	    "HMMAXD and HMMINDS");
	profile.hmb_minimum_descriptor = 8192;
	drv_nvme_hmb_plan(&profile, 64 * MIB, 16 * MIB, 2 * MIB, 16, &target, &chunk, &smallest, &count, &minimum);
	check(target == 0, "HMMINDS over the chunk size");

	/* A preference that is not a power of two takes the largest power below it. */
	profile.hmb_preferred = 3000;
	profile.hmb_minimum_descriptor = 0;
	profile.hmb_maximum_descriptors = 0;
	drv_nvme_hmb_plan(&profile, 64 * MIB, 16 * MIB, 2 * MIB, 16, &target, &chunk, &smallest, &count, &minimum);
	check(target == 3000ULL * 4096 && chunk == 8 * MIB && count == 16, "odd preference");

	/* The Set Features command. */
	check(drv_nvme_set_hmb_command(&command, 7, 1, 1, 16384, 0x12345670ULL | (0x9ULL << 32), 4), "set command");
	check(command.cdw0 == (0x09U | (7U << 16)) && command.cdw10 == 0x0DU && command.cdw11 == 3U &&
	    command.cdw12 == 16384 && command.cdw13 == 0x12345670U && command.cdw14 == 9 && command.cdw15 == 4,
	    "set command words");
	check(!drv_nvme_set_hmb_command(&command, 7, 1, 0, 16384, 0x12345678ULL, 4), "unaligned list refused");
	check(drv_nvme_set_hmb_command(&command, 7, 0, 1, 0, 0, 0) && command.cdw11 == 0 && command.cdw12 == 0,
	    "disable command");

	/* The descriptor. */
	drv_nvme_hmb_descriptor(entry, 0x0000000123456000ULL, 4096);
	check(entry[0] == 0x00 && entry[1] == 0x60 && entry[2] == 0x45 && entry[3] == 0x23 && entry[4] == 0x01 &&
	    entry[5] == 0 && entry[8] == 0x00 && entry[9] == 0x10 && entry[10] == 0 && entry[15] == 0, "descriptor bytes");

	if (failures != 0) {
		printf("nvme-hmb-test: FAIL (%u)\n", failures);
		return 1;
	}
	printf("nvme-hmb-test: PASS\n");
	return 0;
}
