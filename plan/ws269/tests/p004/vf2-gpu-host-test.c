/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the VisionFive 2's GPU bring-up on fake registers: the
 * power management unit's command, the GPU's clocks, rate and resets in
 * their order, the PowerVR early stage's identity check and uncached-view
 * check, and the power-down after a failure.
 *
 *   vf2-gpu-host-test vf2-gpu.dtb ok|unknown|mismatch
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/generic/fdt.h>
#include <drivers/platform/visionfive2/vf2-crg.h>
#include <drivers/platform/visionfive2/vf2-gpu.h>
#include <drivers/platform/visionfive2/vf2-syscon.h>
#include <kern/dcache.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/* One fake register block at a physical address. */
struct fake_block {
	uint64_t address;
	size_t size;
	uint8_t *memory;
};

static uint8_t sys_crg[0x10000];
static uint8_t sys_syscon[0x1000];
static uint8_t pmu[0x1000];
static uint8_t gpu[0x100000];

static struct fake_block blocks[] = {
	{ 0x13020000ULL, sizeof(sys_crg), sys_crg },
	{ 0x13030000ULL, sizeof(sys_syscon), sys_syscon },
	{ 0x17030000ULL, sizeof(pmu), pmu },
	{ 0x18000000ULL, sizeof(gpu), gpu }
};

/* The order register writes happened in: one entry per write. */
struct write_record {
	const uint8_t *memory;
	uint32_t offset;
	uint32_t value;
};

static struct write_record writes[4096];
static unsigned write_count;

/* The fake RAM run the uncached check takes, and the other view's memory. */
static uint64_t run_memory[0x10000 / 8];
static uint64_t other_memory[0x10000 / 8];
static int alias_differs;

/* The last boot log line. */
static char last_line[256];

/* How many checks failed. */
static int failures;

/* Prints one check's outcome and counts a failure. */
static void
check(
	int condition,
	const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

static uint32_t
get32(
	const uint8_t *memory,
	uint32_t offset)
{
	uint32_t value;

	memcpy(&value, memory + offset, 4);
	return value;
}

static void
put32(
	uint8_t *memory,
	uint32_t offset,
	uint32_t value)
{
	memcpy(memory + offset, &value, 4);
}

uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t value;

	memcpy(&value, (const void *)address, 4);
	return value;
}

uint64_t
kern_mmio_read64(
	const volatile void *address)
{
	uint64_t value;

	memcpy(&value, (const void *)address, 8);
	return value;
}

void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	uint8_t *target;
	uint32_t offset;
	uint32_t state;
	unsigned index;

	target = (uint8_t *)address;
	memcpy(target, &value, 4);

	/* Records the write and which block it went to. */
	for (index = 0; index < sizeof(blocks) / sizeof(blocks[0]); index++) {
		if (target >= blocks[index].memory && target < blocks[index].memory + blocks[index].size) {
			offset = (uint32_t)(target - blocks[index].memory);
			if (write_count < sizeof(writes) / sizeof(writes[0])) {
				writes[write_count].memory = blocks[index].memory;
				writes[write_count].offset = offset;
				writes[write_count].value = value;
				write_count++;
			}
		}
	}

	/* The SYS generator's reset status follows its assert registers. */
	if (target >= sys_crg + 0x2f8 && target < sys_crg + 0x2f8 + 16) {
		offset = (uint32_t)(target - sys_crg) - 0x2f8U;
		put32(sys_crg, 0x308U + offset, ~value);
	}

	/* The unit switches the named domain at the last command byte. */
	if (target == pmu + 0x44) {
		state = get32(pmu, 0x80);
		if (value == 0x50U)
			state |= get32(pmu, 0x0c);
		if (value == 0xa0U)
			state &= ~get32(pmu, 0x10);
		put32(pmu, 0x80, state);
	}
}

int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	unsigned index;

	(void)attributes;
	for (index = 0; index < sizeof(blocks) / sizeof(blocks[0]); index++) {
		if (blocks[index].address == address && size <= blocks[index].size) {
			*mapped = blocks[index].memory;
			return 0;
		}
	}
	printf("unexpected map of %llx\n", (unsigned long long)address);
	return ENOMEM;
}

int kern_device_unmap(void *mapped, size_t size) { (void)mapped; (void)size; return 0; }
unsigned long spin_lock_irqsave(struct spinlock *lock) { (void)lock; return 0; }
void spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled) { (void)lock; (void)enabled; }

void
kern_logf(
	const char *format,
	...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf(last_line, sizeof(last_line), format, ap);
	va_end(ap);
	printf("log: %s", last_line);
}

int
kern_pmem_alloc_limited(
	size_t size,
	size_t alignment,
	uint64_t max_address,
	size_t boundary,
	struct kern_pmem *run)
{
	(void)alignment;
	(void)boundary;
	if (size > sizeof(run_memory) || max_address != 0xffffffffULL)
		return ENOMEM;
	run->paddr = 0x80000000ULL;
	run->size = size;
	return 0;
}

int kern_pmem_free(struct kern_pmem *run) { (void)run; return 0; }
void *kern_pmem_to_kernel(hal_physaddr_t address) { return address == 0x80000000ULL ? run_memory : NULL; }

/* The uncached view is the same memory, or other memory when the check must fail. */
int
kern_pmem_map_uncached(
	const struct kern_pmem *run,
	void **mapped)
{
	(void)run;
	*mapped = alias_differs ? (void *)other_memory : (void *)run_memory;
	return 0;
}

int kern_pmem_unmap_uncached(void *mapped, size_t size) { (void)mapped; (void)size; return 0; }
void kern_dcache_clean_range(const void *address, size_t size) { (void)address; (void)size; }
void kern_dcache_invalidate_range(const void *address, size_t size) { (void)address; (void)size; }

/* Reads a whole file into memory. */
static unsigned char *
read_file(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		exit(2);
	}
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length);
	if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
		perror(path);
		exit(2);
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}

/* Finds the position of the first write of a value's bits to a register, or -1. */
static int
first_write(
	const uint8_t *memory,
	uint32_t offset,
	uint32_t bits,
	int set)
{
	unsigned index;

	for (index = 0; index < write_count; index++) {
		if (writes[index].memory != memory || writes[index].offset != offset)
			continue;
		if (set && (writes[index].value & bits) == bits)
			return (int)index;
		if (!set && (writes[index].value & bits) == 0)
			return (int)index;
	}
	return -1;
}

int
main(
	int argc,
	char **argv)
{
	static const struct drv_vf2_gpu_place place = { 0x18000000ULL, 0x100000ULL, 82U };
	struct drv_fdt fdt;
	unsigned char *blob;
	const char *mode;
	uint64_t identity;
	uint32_t asserted;
	size_t size;
	int error;
	int axi;
	int apb;
	int core;
	int bus_reset;
	int core_reset;

	if (argc != 3) {
		fprintf(stderr, "usage: %s vf2-gpu.dtb ok|unknown|mismatch\n", argv[0]);
		return 2;
	}
	mode = argv[2];

	/* PLL2 integer: 24 MHz x 99 / 2 / 1 = 1188 MHz, as the board's firmware sets it. */
	put32(sys_syscon, 0x2c, 1U | (1U << 15) | (1U << 16) | (99U << 17));
	put32(sys_syscon, 0x30, 0U << 28);
	put32(sys_syscon, 0x34, 2U);

	/* The GPU root starts on PLL1, the core divider and timer at their reset values. */
	put32(sys_crg, 0x0c, 1U << 24);
	put32(sys_crg, 0xb4, 3U);
	put32(sys_crg, 0xc4, 12U);
	put32(sys_crg, 0x50, 2U);
	put32(sys_crg, 0x54, 2U);

	/* Every SYS reset line held, the status following. */
	put32(sys_crg, 0x2f8, 0xffffffffU);
	put32(sys_crg, 0x2fc, 0xffffffffU);
	put32(sys_crg, 0x308, 0);
	put32(sys_crg, 0x30c, 0);

	/* The core's identity: the BXE-4-32, or another core. */
	identity = (36ULL << 48) | (50ULL << 32) | (54ULL << 16) | 182ULL;
	if (strcmp(mode, "unknown") == 0)
		identity = (36ULL << 48) | (52ULL << 32) | (104ULL << 16) | 182ULL;
	memcpy(gpu + 0x20, &identity, 8);
	alias_differs = strcmp(mode, "mismatch") == 0;

	blob = read_file(argv[1], &size);
	error = drv_fdt_open(&fdt, blob, size);
	check(error == 0, "the tree opens");
	error = drv_vf2_syscon_init(&fdt);
	check(error == 0, "the system control block maps");
	error = drv_vf2_crg_init(&fdt);
	check(error == 0, "the clock generator maps");

	/* Brings the GPU up. */
	error = drv_vf2_gpu_attach(&fdt, &place);

	if (strcmp(mode, "ok") == 0 || strcmp(mode, "mismatch") == 0) {
		check(error == 0, "the attach succeeds");
		check((get32(pmu, 0x80) & 0x4U) != 0, "the GPU domain is on");
		check(get32(pmu, 0x0c) == 0x4U, "only the GPU domain was named");
		check((get32(pmu, 0x04) & 0x80U) != 0 && (get32(pmu, 0x08) & 0x80U) != 0, "the GPU's hardware events are masked");
		check(((get32(sys_crg, 0x0c) >> 24) & 0x3fU) == 0, "the GPU root runs from PLL2");
		check((get32(sys_crg, 0xb4) & 0xffffffU) == 3U, "the core clock divides PLL2 by three");
		check((get32(sys_crg, 0xb8) & 0x80000000U) != 0, "the core clock runs");
		check((get32(sys_crg, 0xbc) & 0x80000000U) != 0, "the system clock runs");
		check((get32(sys_crg, 0xc0) & 0x80000000U) != 0, "the register bus clock runs");
		check((get32(sys_crg, 0xc4) & 0x80000000U) != 0, "the timer clock runs");
		check((get32(sys_crg, 0xc8) & 0x80000000U) != 0, "the bus master clock runs");
		asserted = get32(sys_crg, 0x2f8);
		check((asserted & (1U << 21)) == 0 && (asserted & (1U << 22)) == 0, "both GPU resets are released");
		check((asserted & (1U << 27)) != 0, "the bus master's reset is untouched");
		axi = first_write(sys_crg, 0xc8, 0x80000000U, 1);
		apb = first_write(sys_crg, 0xc0, 0x80000000U, 1);
		core = first_write(sys_crg, 0xb8, 0x80000000U, 1);
		bus_reset = first_write(sys_crg, 0x2f8, 1U << 21, 0);
		core_reset = first_write(sys_crg, 0x2f8, 1U << 22, 0);
		check(axi >= 0 && axi < apb && apb < core, "bus master, then register bus, then core clock");
		check(core >= 0 && core < bus_reset && bus_reset < core_reset, "clocks, then the register bus reset, then the core's");
		if (strcmp(mode, "ok") == 0)
			check(strcmp(last_line, "powervr: BXE 36.50.54.182 at 396000000 Hz, ALIAS OK\n") == 0, "the boot log line");
		else
			check(strcmp(last_line, "powervr: BXE 36.50.54.182 at 396000000 Hz, ALIAS MISMATCH\n") == 0, "the boot log line names the mismatch");
	} else {
		check(error == ENODEV, "an unknown core is refused");
		check((get32(pmu, 0x80) & 0x4U) == 0, "the GPU domain is off again");
		asserted = get32(sys_crg, 0x2f8);
		check((asserted & (1U << 21)) != 0 && (asserted & (1U << 22)) != 0, "both GPU resets are held again");
		check((get32(sys_crg, 0xb8) & 0x80000000U) == 0, "the core clock is stopped");
		check((get32(sys_crg, 0xc8) & 0x80000000U) == 0, "the bus master clock is stopped");
		check(strcmp(last_line, "powervr: unknown core 36.52.104.182; powered down\n") == 0, "the boot log line");
	}

	printf("%s vf2-gpu-host-test %s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", mode, failures);
	return failures == 0 ? 0 : 1;
}
