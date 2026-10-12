/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Parses the Raspberry Pi 4 device tree with the HAL's parser on the host
 * and checks what the boot needs from it: the peripherals, the memory and
 * the four spin-table CPUs (ws208-p003).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hal/hal.h>
#include "hal/arm64/bsp-rpi4/fdt.h"

/* The parser's one HAL dependency, done the host's way. */
void *
hal_memset(
	void *pointer,
	int value,
	size_t size)
{
	/* Fills the bytes. */
	memset(pointer, value, size);

	/* Succeeded: the filled memory. */
	return pointer;
}

/* Reports a failed check and counts it. */
static int
check(
	int condition,
	const char *what)
{
	/* A check that holds says nothing. */
	if (condition)
		return 0;

	fprintf(stderr, "rpi4-fdt-host-test: FAIL: %s\n", what);

	/* Failed: one more failure. */
	return 1;
}

/*
 * Runs the test on the device tree blob named on the command line.
 */
int
main(
	int argc,
	char **argv)
{
	static unsigned char blob[2U * 1024U * 1024U];
	static const unsigned long long release[4] = {0xd8, 0xe0, 0xe8, 0xf0};
	struct rpi4_fdt_info info;
	FILE *file;
	size_t length;
	unsigned index;
	int failures;
	int error;

	/* Requires the blob's path. */
	if (argc != 2) {
		fprintf(stderr, "usage: rpi4-fdt-host-test bcm2711-rpi-4-b.dtb\n");
		return 2;
	}

	/* Reads the blob. */
	file = fopen(argv[1], "rb");
	if (file == NULL) {
		perror(argv[1]);
		return 2;
	}
	length = fread(blob, 1, sizeof(blob), file);
	fclose(file);

	/* Parses it as the HAL does at boot. */
	error = rpi4_fdt_parse(blob, length, &info);
	if (error != 0) {
		fprintf(stderr, "rpi4-fdt-host-test: FAIL: parse: %s\n", rpi4_fdt_error(error));
		return 1;
	}

	/* The peripherals the boot maps. */
	failures = 0;
	failures += check(info.compatible_rpi4, "the tree is a Raspberry Pi 4's");
	failures += check(info.uart_base != 0, "the PL011 is found");
	failures += check(info.mailbox_base != 0, "the mailbox is found");
	failures += check(info.gic_dist_base != 0, "the GIC distributor is found");
	failures += check(info.gic_cpu_base != 0, "the GIC CPU interface is found");
	failures += check(info.sdhci_base != 0, "the eMMC2 controller is found");

	/* The four cores, each held in the firmware's spin table. */
	failures += check(info.cpu_count == 4, "four CPUs are listed");
	for (index = 0; index < info.cpu_count && index < 4; index++) {
		failures += check(info.cpus[index].mpidr == index, "a CPU's affinity is its index");
		failures += check(info.cpus[index].spin_table != 0, "a CPU uses the spin table");
		failures += check(info.cpus[index].release_addr == release[index], "a CPU's release address");
	}

	/* Reports the result. */
	if (failures != 0)
		return 1;
	printf("rpi4-fdt-host-test: PASS (%u CPUs, gic %llx/%llx)\n",
	       info.cpu_count,
	       (unsigned long long)info.gic_dist_base,
	       (unsigned long long)info.gic_cpu_base);

	/* Succeeded. */
	return 0;
}
