/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws308-p002: host test of the NVMe I/O command identifiers that name their
 * slot (include/drivers/pci/pci-nvme-protocol.h): never zero, the slot comes
 * back from the identifier through every generation, identifiers of
 * different slots never meet, and an identifier of a slot the queue does not
 * have (or of zero) names none; the driver's sequence of a slot
 * (drv_nvme_io_next_command_id) never names FFFFh.
 *
 *   cc -std=c99 -Wall -Wextra -Werror -I include -o build/tmp/nvme-cid-test \
 *       plan/ws308/tests/host/nvme-cid-test.c && build/tmp/nvme-cid-test
 */

#include <drivers/pci/pci-nvme-protocol.h>

#include <stdio.h>
#include <string.h>

int
main(void)
{
	static unsigned char owner[65536];
	unsigned generation;
	unsigned slot;
	unsigned found;
	uint16_t id;
	int failures;

	failures = 0;
	memset(owner, 0, sizeof(owner));
	for (generation = 0; generation < 2048U; generation++) {
		for (slot = 0; slot < DRV_NVME_IO_CID_SLOTS; slot++) {
			id = drv_nvme_io_command_id(slot, generation);
			if (id == 0U) {
				printf("FAIL zero id slot %u generation %u\n", slot, generation);
				failures++;
			}
			if (!drv_nvme_io_command_slot(id, DRV_NVME_IO_CID_SLOTS, &found) || found != slot) {
				printf("FAIL slot %u generation %u came back as %u\n", slot, generation, found);
				failures++;
			}
			/* One identifier belongs to one slot only. */
			if (owner[id] != 0U && owner[id] != slot + 1U) {
				printf("FAIL id %u of slots %u and %u\n", id, owner[id] - 1U, slot);
				failures++;
			}
			owner[id] = (unsigned char)(slot + 1U);
		}
	}

	/* The driver's sequence of a slot never names FFFFh. */
	for (slot = 0; slot < DRV_NVME_IO_CID_SLOTS; slot++) {
		generation = 0;
		for (found = 0; found < 5000U; found++) {
			id = drv_nvme_io_next_command_id(slot, &generation);
			if (id == 0xffffU || id == 0U) {
				printf("FAIL slot %u named %u\n", slot, id);
				failures++;
				break;
			}
		}
	}

	/* A queue of 10 slots does not have slot 10; zero names nothing. */
	if (drv_nvme_io_command_slot(drv_nvme_io_command_id(10U, 3U), 10U, &found) ||
	    drv_nvme_io_command_slot(0U, 63U, &found) ||
	    drv_nvme_io_command_slot(64U, 63U, &found) ||
	    drv_nvme_io_command_slot(1U, 64U, &found)) {
		printf("FAIL out-of-range identifiers named a slot\n");
		failures++;
	}

	if (failures != 0) {
		printf("nvme-cid-test: %d FAIL\n", failures);
		return 1;
	}
	printf("nvme-cid-test: PASS\n");
	return 0;
}
