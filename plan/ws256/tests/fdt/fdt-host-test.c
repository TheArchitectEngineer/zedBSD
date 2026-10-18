/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the riscv64 HAL's device tree reader against two trees:
 * one in the shape of the emulator's virt machine, one in the shape of the
 * JH7110.
 *
 *   fdt-host-test virt.dtb jh7110.dtb
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdt.h"

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
static unsigned char *
read_file(
	const char *path,
	size_t *size)
{
	unsigned char *data;
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
	*size = (size_t)length;
	return data;
}

/* Checks the virt-shaped tree. */
static void
check_virt(
	const char *path)
{
	static struct riscv64_fdt_info info;
	unsigned char *blob;
	const char *bootargs;
	unsigned context;
	size_t size;
	int error;

	/* Parses the tree. */
	blob = read_file(path, &size);
	error = riscv64_fdt_parse(blob, size, &info);
	check("virt: parse", error == RISCV64_FDT_OK);

	/* The harts, the timebase and the extensions in both spellings. */
	check("virt: two usable harts", info.cpu_count == 2 && info.cpus[0].usable && info.cpus[1].usable);
	check("virt: hart ids", info.cpus[0].hart_id == 0 && info.cpus[1].hart_id == 1);
	check("virt: timebase 10 MHz", info.timebase_frequency == 10000000U);
	check("virt: sstc from the list", (info.cpus[0].extensions & RISCV64_FDT_EXT_SSTC) != 0);
	check("virt: svpbmt from the string", (info.cpus[1].extensions & RISCV64_FDT_EXT_SVPBMT) != 0);
	check("virt: no svpbmt on hart 0", (info.cpus[0].extensions & RISCV64_FDT_EXT_SVPBMT) == 0);

	/* Memory and the reservations. */
	check("virt: one bank", info.memory_count == 1 && info.memory[0].base == 0x80000000U &&
	      info.memory[0].size == 0x80000000U);
	check("virt: memreserve and both reserved nodes kept", info.reserved_count == 3);
	check("virt: only the no-map node is no-map", info.nomap_count == 1 &&
	      info.nomap[0].base == 0x80000000U && info.nomap[0].size == 0x40000U);

	/* The devices. */
	check("virt: PLIC", info.plic_base == 0xc000000U && info.plic_size == 0x600000U &&
	      info.plic_sources == 0x5f);
	check("virt: four PLIC contexts", info.plic_context_count == 4);
	error = riscv64_fdt_s_context(&info, 0, &context);
	check("virt: hart 0 supervisor context 1", error == 0 && context == 1);
	error = riscv64_fdt_s_context(&info, 1, &context);
	check("virt: hart 1 supervisor context 3", error == 0 && context == 3);
	error = riscv64_fdt_s_context(&info, 7, &context);
	check("virt: no context for an unknown hart", error != 0);
	check("virt: ns16550a UART", info.uart_kind == RISCV64_FDT_UART_NS16550A &&
	      info.uart_base == 0x10000000U && info.uart_irq == 10 && info.uart_reg_shift == 0);
	check("virt: fw_cfg", info.fwcfg_base == 0x10100000U && info.fwcfg_size == 0x18);

	/* The chosen node. */
	bootargs = (const char *)blob + info.bootargs_offset;
	check("virt: bootargs", info.bootargs_offset != 0 && strcmp(bootargs, "cpus=1 console=ttyS0") == 0);
	check("virt: initrd", info.initrd_start == 0x88000000U && info.initrd_end == 0x88100000U);

	/* A buffer smaller than the tree is refused. */
	error = riscv64_fdt_parse(blob, size - 1U, &info);
	check("virt: short buffer refused", error == RISCV64_FDT_BAD_SIZE);
	free(blob);
}

/* Checks the JH7110-shaped tree. */
static void
check_jh7110(
	const char *path)
{
	static struct riscv64_fdt_info info;
	unsigned char *blob;
	unsigned context;
	unsigned usable;
	unsigned index;
	size_t size;
	int error;

	/* Parses the tree. */
	blob = read_file(path, &size);
	error = riscv64_fdt_parse(blob, size, &info);
	check("jh7110: parse", error == RISCV64_FDT_OK);

	/* The monitor core is listed but not usable. */
	usable = 0;
	for (index = 0; index < info.cpu_count; index++) {
		if (info.cpus[index].usable)
			usable++;
	}
	check("jh7110: five harts, four usable", info.cpu_count == 5 && usable == 4);
	check("jh7110: hart 0 not usable", info.cpus[0].hart_id == 0 && !info.cpus[0].usable);
	check("jh7110: timebase 4 MHz", info.timebase_frequency == 4000000U);
	check("jh7110: no sstc", (info.cpus[1].extensions & RISCV64_FDT_EXT_SSTC) == 0);

	/* The supervisor contexts are 2h for harts 1 to 4. */
	error = riscv64_fdt_s_context(&info, 1, &context);
	check("jh7110: hart 1 context 2", error == 0 && context == 2);
	error = riscv64_fdt_s_context(&info, 4, &context);
	check("jh7110: hart 4 context 8", error == 0 && context == 8);
	error = riscv64_fdt_s_context(&info, 0, &context);
	check("jh7110: monitor core has no supervisor context", error != 0);

	/* The devices. */
	check("jh7110: PLIC", info.plic_base == 0xc000000U && info.plic_sources == 136 &&
	      info.plic_context_count == 9);
	check("jh7110: DesignWare UART", info.uart_kind == RISCV64_FDT_UART_DW_APB &&
	      info.uart_reg_shift == 2 && info.uart_reg_io_width == 4 && info.uart_irq == 32);
	check("jh7110: cache controller", info.ccache_base == 0x2010000U && info.ccache_size == 0x4000U);
	check("jh7110: 4 GiB at 1 GiB", info.memory_count == 1 && info.memory[0].base == 0x40000000U &&
	      info.memory[0].size == 0x100000000ULL);
	check("jh7110: no fw_cfg", info.fwcfg_size == 0);
	free(blob);
}

int
main(
	int argc,
	char **argv)
{
	/* Two trees are needed. */
	if (argc != 3) {
		fprintf(stderr, "usage: %s virt.dtb jh7110.dtb\n", argv[0]);
		return 2;
	}
	check_virt(argv[1]);
	check_jh7110(argv[2]);

	/* One summary line. */
	printf("%s %d failed\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
