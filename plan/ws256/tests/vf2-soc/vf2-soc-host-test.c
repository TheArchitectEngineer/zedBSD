/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the VisionFive 2's SoC drivers on fake registers: the
 * PLL and clock rates, gates, dividers and resets of the clock
 * generators, the pin controllers' register values for a device's pin
 * groups and GPIO pins, the system control blocks, and the generic device
 * tree helpers they use (children, string lists, DMA coherency).
 *
 *   vf2-soc-host-test vf2-soc.dtb
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/generic/fdt.h>
#include <drivers/platform/visionfive2/vf2-crg.h>
#include <drivers/platform/visionfive2/vf2-pinctrl.h>
#include <drivers/platform/visionfive2/vf2-syscon.h>
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
static uint8_t stg_crg[0x10000];
static uint8_t aon_crg[0x10000];
static uint8_t sys_syscon[0x1000];
static uint8_t stg_syscon[0x1000];
static uint8_t aon_syscon[0x1000];
static uint8_t sys_pinctrl[0x10000];
static uint8_t aon_pinctrl[0x10000];

static struct fake_block blocks[] = {
	{ 0x13020000ULL, sizeof(sys_crg), sys_crg },
	{ 0x10230000ULL, sizeof(stg_crg), stg_crg },
	{ 0x17000000ULL, sizeof(aon_crg), aon_crg },
	{ 0x13030000ULL, sizeof(sys_syscon), sys_syscon },
	{ 0x10240000ULL, sizeof(stg_syscon), stg_syscon },
	{ 0x17010000ULL, sizeof(aon_syscon), aon_syscon },
	{ 0x13040000ULL, sizeof(sys_pinctrl), sys_pinctrl },
	{ 0x17020000ULL, sizeof(aon_pinctrl), aon_pinctrl }
};

/* Each generator's reset assert and status offsets, for the fake status. */
struct fake_reset {
	uint8_t *memory;
	uint32_t assert_offset;
	uint32_t status_offset;
	uint32_t registers;
};

static const struct fake_reset resets[] = {
	{ sys_crg, 0x2f8U, 0x308U, 4U },
	{ stg_crg, 0x74U, 0x78U, 1U },
	{ aon_crg, 0x38U, 0x3cU, 1U }
};

/* When set, the fake status registers stop following the assert ones. */
static int status_stuck;

/* How many register writes happened. */
static unsigned write_count;

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

/* Reads a little-endian register of a fake block. */
static uint32_t
get32(
	const uint8_t *memory,
	uint32_t offset)
{
	uint32_t value;

	memcpy(&value, memory + offset, 4);
	return value;
}

/* Writes a register of a fake block. */
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

void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	uint8_t *target;
	unsigned index;
	uint32_t offset;

	/* Stores the value. */
	target = (uint8_t *)address;
	memcpy(target, &value, 4);
	write_count++;

	/* A reset assert register's status follows it: released is set. */
	for (index = 0; index < sizeof(resets) / sizeof(resets[0]); index++) {
		if (target < resets[index].memory + resets[index].assert_offset)
			continue;
		offset = (uint32_t)(target - resets[index].memory) - resets[index].assert_offset;
		if (offset >= resets[index].registers * 4U)
			continue;
		if (!status_stuck)
			put32(resets[index].memory, resets[index].status_offset + offset, ~value);
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
void kern_logf(const char *format, ...) { va_list ap; va_start(ap, format); vprintf(format, ap); va_end(ap); }

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

/* Finds a node by its compatible string, or the nth one. */
static uint32_t
node_of(
	const struct drv_fdt *fdt,
	const char *compatible)
{
	uint32_t node;
	int error;

	error = drv_fdt_find_compatible(fdt, compatible, DRV_FDT_NO_NODE, &node);
	if (error != 0) {
		printf("no node %s\n", compatible);
		exit(2);
	}
	return node;
}

/* Finds a direct child of a node by name, using drv_fdt_child. */
static uint32_t
child_named(
	const struct drv_fdt *fdt,
	uint32_t parent,
	const char *name)
{
	uint32_t child;
	int error;

	error = drv_fdt_child(fdt, parent, DRV_FDT_NO_NODE, &child);
	while (error == 0) {
		if (strcmp((const char *)fdt->blob + child + 4, name) == 0)
			return child;
		error = drv_fdt_child(fdt, parent, child, &child);
	}
	printf("no child %s\n", name);
	exit(2);
}

/* Sets the fake PLL registers to the boot firmware's usual settings. */
static void
set_plls(
	void)
{
	/* PLL0 integer: 24 MHz x 125 / 2 / 1 = 1.5 GHz. */
	put32(sys_syscon, 0x18, (1U << 24) | (1U << 25));
	put32(sys_syscon, 0x1c, 125U);
	put32(sys_syscon, 0x20, 0U << 28);
	/* PLL1 fractional: 24 MHz x 88.875 / 1 / 2 = 1066.5 MHz; PLL0 prediv 2. */
	put32(sys_syscon, 0x24, 2U | (88U << 17));
	put32(sys_syscon, 0x28, 0xe00000U | (1U << 28));
	/* PLL2 integer: 24 MHz x 99 / 2 / 1 = 1188 MHz; PLL1 prediv 1. */
	put32(sys_syscon, 0x2c, 1U | (1U << 15) | (1U << 16) | (99U << 17));
	put32(sys_syscon, 0x30, 0U << 28);
	put32(sys_syscon, 0x34, 2U);
}

/* Sets the fake clock registers the rate checks read. */
static void
set_clocks(
	void)
{
	put32(sys_crg, 5 * 4, 1U << 24);	/* bus root from PLL2 */
	put32(sys_crg, 7 * 4, 3U);		/* AXI config 0: / 3 */
	put32(sys_crg, 8 * 4, 2U);		/* STG AXI/AHB: / 2 */
	put32(sys_crg, 9 * 4, 0x80000000U);	/* AHB0 open */
	put32(sys_crg, 11 * 4, 4U);		/* APB bus: / 4 */
	put32(sys_crg, 94 * 4, 2U);		/* SD card clock: / 2, gate closed */
	put32(sys_crg, 108 * 4, 12U);		/* GMAC0 GTX source: / 12 */
	put32(sys_crg, 16 * 4, 2U);		/* audio root: PLL2 / 2 */
	put32(sys_crg, 158 * 4, 12U);		/* PWM DAC core: / 12 */
	put32(aon_crg, 4 * 4, 2U);		/* RMII RTX: / 2 */
	put32(aon_crg, 5 * 4, 0U);		/* GMAC0 TX from SYS GTX */
}

static uint64_t
rate_of(
	enum drv_vf2_crg crg,
	uint32_t index,
	int *error)
{
	struct drv_vf2_clock clock;
	uint64_t rate;

	clock.crg = crg;
	clock.index = index;
	rate = 0;
	*error = drv_vf2_clock_get_rate(&clock, &rate);
	return rate;
}

static void
check_rates(
	void)
{
	uint64_t rate;
	int error;

	error = drv_vf2_pll_get_rate(0, &rate);
	check(error == 0 && rate == 1500000000ULL, "PLL0 integer mode 1.5 GHz");
	error = drv_vf2_pll_get_rate(1, &rate);
	check(error == 0 && rate == 1066500000ULL, "PLL1 fractional mode 1066.5 MHz");
	error = drv_vf2_pll_get_rate(2, &rate);
	check(error == 0 && rate == 1188000000ULL, "PLL2 integer mode 1188 MHz");

	rate = rate_of(DRV_VF2_CRG_SYS, 5, &error);
	check(error == 0 && rate == 1188000000ULL, "bus root selects PLL2");
	rate = rate_of(DRV_VF2_CRG_SYS, 7, &error);
	check(error == 0 && rate == 396000000ULL, "AXI config 0 = PLL2 / 3");
	rate = rate_of(DRV_VF2_CRG_SYS, 8, &error);
	check(error == 0 && rate == 198000000ULL, "STG AXI/AHB = AXI config 0 / 2");
	rate = rate_of(DRV_VF2_CRG_SYS, 92, &error);
	check(error == 0 && rate == 198000000ULL, "SDIO1 AHB through AHB0");
	rate = rate_of(DRV_VF2_CRG_SYS, 94, &error);
	check(error == 0 && rate == 198000000ULL, "SD card clock = AXI config 0 / 2");
	rate = rate_of(DRV_VF2_CRG_SYS, 12, &error);
	check(error == 0 && rate == 49500000ULL, "APB0 = STG AXI/AHB / 4");
	rate = rate_of(DRV_VF2_CRG_SYS, 108, &error);
	check(error == 0 && rate == 125000000ULL, "GMAC0 GTX source = PLL0 / 12");
	rate = rate_of(DRV_VF2_CRG_SYS, 111, &error);
	check(error == 0 && rate == 125000000ULL, "GMAC0 GTXC passes its source");
	rate = rate_of(DRV_VF2_CRG_AON, 6, &error);
	check(error == 0 && rate == 125000000ULL, "AON GMAC0 TX inverter from SYS GTX");
	rate = rate_of(DRV_VF2_CRG_AON, 4, &error);
	check(error == 0 && rate == 25000000ULL, "AON RMII RTX = 50 MHz input / 2");
	rate = rate_of(DRV_VF2_CRG_AON, 3, &error);
	check(error == 0 && rate == 198000000ULL, "AON GMAC0 AXI from SYS STG AXI/AHB");

	put32(aon_crg, 5 * 4, 1U << 24);
	rate = rate_of(DRV_VF2_CRG_AON, 5, &error);
	check(error == 0 && rate == 25000000ULL, "AON GMAC0 TX selects RMII RTX");
	put32(aon_crg, 5 * 4, 2U << 24);
	rate = rate_of(DRV_VF2_CRG_AON, 5, &error);
	check(error == EIO, "a selector past the sources is EIO");
	put32(aon_crg, 5 * 4, 0U);

	put32(sys_crg, 5 * 4, 0U);
	rate = rate_of(DRV_VF2_CRG_SYS, 7, &error);
	check(error == 0 && rate == 8000000ULL, "bus root on the oscillator: 24 MHz / 3");
	put32(sys_crg, 5 * 4, 1U << 24);

	rate = rate_of(DRV_VF2_CRG_SYS, 158, &error);
	check(error == 0 && rate == 49500000ULL, "PWM DAC core = PLL2 / 2 (audio root) / 12");
	rate = rate_of(DRV_VF2_CRG_SYS, 0, &error);
	check(error == ENOENT, "CPU root is outside the table");
	rate = rate_of(DRV_VF2_CRG_STG, 10, &error);
	check(error == ENOTSUP, "PCIe TL gate has no rate");

	put32(sys_crg, 7 * 4, 0U);
	rate = rate_of(DRV_VF2_CRG_SYS, 94, &error);
	check(error == EIO, "a zero divider is EIO");
	put32(sys_crg, 7 * 4, 3U);

	put32(sys_syscon, 0x24, 2U | (1U << 15) | (88U << 17));
	error = drv_vf2_pll_get_rate(1, &rate);
	check(error == EIO, "PLL1 with dacpd != dsmpd is EIO");
	put32(sys_syscon, 0x24, 2U | (88U << 17));
	put32(sys_syscon, 0x28, 0xe00000U | (1U << 28) | (1U << 27));
	error = drv_vf2_pll_get_rate(1, &rate);
	check(error == 0 && rate == 0, "a powered-down PLL1 runs at zero");
	put32(sys_syscon, 0x28, 0xe00000U | (1U << 28));
}

static void
check_set_rate(
	const struct drv_fdt *fdt)
{
	struct drv_vf2_clock clock;
	uint32_t before;
	int error;

	clock.crg = DRV_VF2_CRG_SYS;
	clock.index = 94;
	put32(sys_crg, 94 * 4, 0x80000002U);
	error = drv_vf2_clock_set_rate(&clock, 50000000ULL);
	check(error == 0 && get32(sys_crg, 94 * 4) == 0x80000008U,
	      "SD card 50 MHz: divider 8 (49.5 MHz), gate kept");
	error = drv_vf2_clock_set_rate(&clock, 100000000ULL);
	check(error == 0 && get32(sys_crg, 94 * 4) == 0x80000004U, "SD card 100 MHz: divider 4 (99 MHz)");
	error = drv_vf2_clock_set_rate(&clock, 400000000ULL);
	check(error == 0 && get32(sys_crg, 94 * 4) == 0x80000002U, "SD card above the source: smallest divider 2");
	before = get32(sys_crg, 94 * 4);
	error = drv_vf2_clock_set_rate(&clock, 400000ULL);
	check(error == ERANGE && get32(sys_crg, 94 * 4) == before, "SD card 400 kHz: ERANGE, unchanged");

	clock.index = 7;
	error = drv_vf2_clock_set_rate(&clock, 100000000ULL);
	check(error == ENOTSUP && get32(sys_crg, 7 * 4) == 3U, "a shared bus divider refuses set_rate");

	put32(sys_crg, 94 * 4, 0x80000002U);
	error = drv_vf2_clock_apply_assigned(fdt, node_of(fdt, "starfive,jh7110-mmc"));
	check(error == 0 && get32(sys_crg, 94 * 4) == 0x80000008U, "mmc1 assigned-clock-rates sets 50 MHz");
}

static void
check_gates(
	void)
{
	struct drv_vf2_clock clock;
	uint32_t cpu_root;
	int error;

	put32(sys_crg, 9 * 4, 0U);
	put32(sys_crg, 92 * 4, 0U);
	put32(sys_crg, 94 * 4, 8U);
	put32(sys_crg, 108 * 4, 12U);
	put32(aon_crg, 5 * 4, 0U);
	put32(aon_crg, 6 * 4, 0x40000000U);
	cpu_root = 0x01000000U;
	put32(sys_crg, 0, cpu_root);

	clock.crg = DRV_VF2_CRG_SYS;
	clock.index = 94;
	error = drv_vf2_clock_enable(&clock);
	check(error == 0 && get32(sys_crg, 94 * 4) == 0x80000008U && get32(sys_crg, 9 * 4) == 0,
	      "enable SD card clock opens its gate only (no gate above on its path)");
	clock.index = 92;
	error = drv_vf2_clock_enable(&clock);
	check(error == 0 && get32(sys_crg, 92 * 4) == 0x80000000U && get32(sys_crg, 9 * 4) == 0x80000000U,
	      "enable SDIO1 AHB opens AHB0 above it");
	write_count = 0;
	error = drv_vf2_clock_enable(&clock);
	check(error == 0 && write_count == 0, "enabling an open clock writes nothing");

	clock.crg = DRV_VF2_CRG_AON;
	clock.index = 6;
	error = drv_vf2_clock_enable(&clock);
	check(error == 0 && get32(aon_crg, 5 * 4) == 0x80000000U && get32(sys_crg, 108 * 4) == 0x8000000cU &&
	      get32(aon_crg, 6 * 4) == 0x40000000U,
	      "enable GMAC0 TX inverter opens AON TX and SYS GTX, inverter bit kept");
	error = drv_vf2_clock_disable(&clock);
	check(error == 0 && get32(aon_crg, 5 * 4) == 0 && get32(sys_crg, 108 * 4) == 0x8000000cU,
	      "disable GMAC0 TX inverter closes AON TX only");

	clock.crg = DRV_VF2_CRG_SYS;
	clock.index = 9;
	error = drv_vf2_clock_disable(&clock);
	check(error == EBUSY && get32(sys_crg, 9 * 4) == 0x80000000U, "disable shared AHB0 is EBUSY");
	clock.index = 96;
	error = drv_vf2_clock_disable(&clock);
	check(error == EBUSY, "disable the PCIe hosts' shared NOC clock is EBUSY");
	clock.index = 92;
	error = drv_vf2_clock_disable(&clock);
	check(error == 0 && get32(sys_crg, 92 * 4) == 0, "disable SDIO1 AHB closes it");
	clock.index = 0;
	error = drv_vf2_clock_disable(&clock);
	check(error == ENOENT && get32(sys_crg, 0) == cpu_root, "CPU root cannot be stopped");
	clock.index = 58;
	error = drv_vf2_clock_enable(&clock);
	check(error == ENOENT, "a clock outside the table cannot be touched");
	clock.crg = DRV_VF2_CRG_STG;
	clock.index = 10;
	error = drv_vf2_clock_enable(&clock);
	check(error == 0 && get32(stg_crg, 10 * 4) == 0x80000000U, "enable STG PCIe0 TL gate");
}

static void
check_lookups(
	const struct drv_fdt *fdt)
{
	struct drv_vf2_clock clock;
	struct drv_vf2_reset reset;
	uint32_t mmc;
	uint32_t gmac;
	uint32_t pcie;
	uint32_t bad;
	int released;
	int error;

	mmc = node_of(fdt, "starfive,jh7110-mmc");
	gmac = node_of(fdt, "starfive,jh7110-dwmac");
	pcie = node_of(fdt, "starfive,jh7110-pcie");
	bad = child_named(fdt, node_of(fdt, "simple-bus"), "bad@16040000");

	error = drv_vf2_clock_lookup(fdt, mmc, "ciu", &clock);
	check(error == 0 && clock.crg == DRV_VF2_CRG_SYS && clock.index == 94, "mmc1 ciu = SYS 94");
	error = drv_vf2_clock_lookup(fdt, mmc, NULL, &clock);
	check(error == 0 && clock.crg == DRV_VF2_CRG_SYS && clock.index == 92, "mmc1 first clock = SYS 92");
	error = drv_vf2_clock_lookup(fdt, gmac, "tx", &clock);
	check(error == 0 && clock.crg == DRV_VF2_CRG_AON && clock.index == 6, "gmac0 tx = AON 6");
	error = drv_vf2_clock_lookup(fdt, pcie, "tl", &clock);
	check(error == 0 && clock.crg == DRV_VF2_CRG_STG && clock.index == 10, "pcie0 tl = STG 10");
	error = drv_vf2_clock_lookup(fdt, mmc, "nope", &clock);
	check(error == ENOENT, "an unknown clock name is ENOENT");
	error = drv_vf2_clock_lookup(fdt, bad, NULL, &clock);
	check(error == ENODEV, "a clock of the PLL node is no generator's (ENODEV)");
	error = drv_vf2_clock_apply_assigned(fdt, bad);
	check(error == ENOTSUP && get32(sys_crg, 7 * 4) == 3U, "assigned rate of a shared bus is refused");

	error = drv_vf2_reset_lookup(fdt, mmc, "reset", &reset);
	check(error == 0 && reset.crg == DRV_VF2_CRG_SYS && reset.index == 65, "mmc1 reset = SYS 65");
	put32(sys_crg, 0x2f8 + 8, 0xffffffffU);
	put32(sys_crg, 0x308 + 8, 0U);
	error = drv_vf2_reset_deassert(&reset);
	check(error == 0 && get32(sys_crg, 0x2f8 + 8) == 0xfffffffdU, "deassert SYS 65 clears bit 1 of 0x300");
	error = drv_vf2_reset_released(&reset, &released);
	check(error == 0 && released == 1, "SYS 65 status shows released");
	error = drv_vf2_reset_assert(&reset);
	check(error == 0 && get32(sys_crg, 0x2f8 + 8) == 0xffffffffU, "assert SYS 65 sets bit 1 again");
	status_stuck = 1;
	error = drv_vf2_reset_deassert(&reset);
	check(error == ETIMEDOUT, "a status that does not follow is ETIMEDOUT");
	status_stuck = 0;

	error = drv_vf2_reset_lookup(fdt, pcie, "core", &reset);
	check(error == 0 && reset.crg == DRV_VF2_CRG_STG && reset.index == 15, "pcie0 core reset = STG 15");
	put32(stg_crg, 0x74, 0x7fffffU);
	error = drv_vf2_reset_deassert(&reset);
	check(error == 0 && get32(stg_crg, 0x74) == (0x7fffffU & ~(1U << 15)), "deassert STG 15 clears bit 15 of 0x74");
	error = drv_vf2_reset_lookup(fdt, gmac, "ahb", &reset);
	check(error == 0 && reset.crg == DRV_VF2_CRG_AON && reset.index == 1, "gmac0 ahb reset = AON 1");
	reset.index = 8;
	error = drv_vf2_reset_assert(&reset);
	check(error == ERANGE, "AON reset 8 does not exist");
}

static void
check_syscon(
	const struct drv_fdt *fdt)
{
	enum drv_vf2_syscon syscon;
	uint32_t arguments[4];
	uint32_t value;
	unsigned count;
	int error;

	error = drv_vf2_syscon_lookup(fdt, node_of(fdt, "starfive,jh7110-mmc"), "starfive,sysreg",
				      &syscon, arguments, 4, &count);
	check(error == 0 && syscon == DRV_VF2_SYSCON_SYS && count == 3 && arguments[0] == 0x9c &&
	      arguments[1] == 1 && arguments[2] == 0x3e, "mmc1 starfive,sysreg = SYS 0x9c 0x1 0x3e");
	error = drv_vf2_syscon_lookup(fdt, node_of(fdt, "starfive,jh7110-dwmac"), "starfive,syscon",
				      &syscon, arguments, 4, &count);
	check(error == 0 && syscon == DRV_VF2_SYSCON_AON && count == 2 && arguments[0] == 0xc &&
	      arguments[1] == 0x12, "gmac0 starfive,syscon = AON 0xc 0x12");
	error = drv_vf2_syscon_lookup(fdt, node_of(fdt, "starfive,jh7110-pcie"), "starfive,stg-syscon",
				      &syscon, arguments, 4, &count);
	check(error == 0 && syscon == DRV_VF2_SYSCON_STG && count == 0, "pcie0 starfive,stg-syscon = STG");

	put32(sys_syscon, 0x9c, 0xa5a5a5a5U);
	error = drv_vf2_syscon_update(DRV_VF2_SYSCON_SYS, 0x9c, 0x3eU, 0x14U);
	check(error == 0 && get32(sys_syscon, 0x9c) == ((0xa5a5a5a5U & ~0x3eU) | 0x14U), "SYS 0x9c update touches only the mask");
	write_count = 0;
	error = drv_vf2_syscon_update(DRV_VF2_SYSCON_SYS, 0x9c, 0x3eU, 0x14U);
	check(error == 0 && write_count == 0, "an update that changes nothing writes nothing");
	error = drv_vf2_syscon_read(DRV_VF2_SYSCON_AON, 0x1000, &value);
	check(error == EINVAL, "a read past the block is EINVAL");
	error = drv_vf2_syscon_read(DRV_VF2_SYSCON_AON, 0x2, &value);
	check(error == EINVAL, "an unaligned read is EINVAL");
}

/* Reads one selector field of the SYS pin controller. */
static uint32_t
field(
	uint32_t base,
	uint32_t index,
	uint32_t mask)
{
	return (get32(sys_pinctrl, base + (index / 4) * 4) >> ((index % 4) * 8)) & mask;
}

static void
check_pins(
	const struct drv_fdt *fdt)
{
	struct drv_vf2_gpio gpio;
	unsigned changed;
	uint32_t pad;
	int active;
	int error;

	memset(sys_pinctrl, 0, sizeof(sys_pinctrl));
	put32(sys_pinctrl, 0x120 + 10 * 4, 0x11U);	/* pin 10 pad: IE, pull-down */
	error = drv_vf2_pinctrl_apply_default(fdt, node_of(fdt, "starfive,jh7110-mmc"), &changed);
	check(error == 0 && changed > 0, "mmc1 pins applied");
	check(field(0x40, 10, 0x7f) == 55 && field(0x0, 10, 0x3f) == 0, "pin 10: SDIO1 clock out, driving");
	pad = get32(sys_pinctrl, 0x120 + 10 * 4);
	check(pad == (0x08U | (3U << 1)), "pin 10 pad: pull-up, 12 mA, no receiver, no Schmitt, slow");
	check(field(0x40, 9, 0x7f) == 57 && field(0x0, 9, 0x3f) == 19, "pin 9: SDIO1 command out and its enable");
	check(field(0x80, 44, 0x7f) == 11, "SDIO1 command input listens to pin 9 (9 + 2)");
	check(field(0x80, 48, 0x7f) == 10, "SDIO1 data 3 input listens to pin 8 (8 + 2)");
	pad = get32(sys_pinctrl, 0x120 + 9 * 4);
	check(pad == (0x01U | 0x40U | 0x08U | (3U << 1)), "pin 9 pad: receiver, Schmitt, pull-up, 12 mA");
	error = drv_vf2_pinctrl_apply_default(fdt, node_of(fdt, "starfive,jh7110-mmc"), &changed);
	check(error == 0 && changed == 0, "mmc1 pins applied again change nothing");

	error = drv_vf2_pinctrl_apply_default(fdt, node_of(fdt, "starfive,jh7110-pcie"), &changed);
	check(error == 0 && field(0x40, 27, 0x7f) == 0 && field(0x0, 27, 0x3f) == 1 &&
	      get32(sys_pinctrl, 0x120 + 27 * 4) == (0x01U | 0x10U), "pcie0 clkreq pin 27: input, pull-down, 2 mA");
	write_count = 0;
	error = drv_vf2_pinctrl_apply_default(fdt, child_named(fdt, node_of(fdt, "simple-bus"), "bad@16040000"), &changed);
	check(error == EINVAL && write_count == 0, "a 6 mA drive strength is refused before any write");

	error = drv_vf2_gpio_lookup(fdt, node_of(fdt, "starfive,jh7110-mmc"), "cd-gpios", &gpio);
	check(error == 0 && gpio.bank == DRV_VF2_GPIO_SYS && gpio.pin == 41 && gpio.active_low == 1,
	      "mmc1 cd-gpios = SYS 41 active low");
	put32(sys_pinctrl, 0x28, 0U);
	error = drv_vf2_gpio_configure_input(&gpio);
	check(error == 0 && field(0x0, 41, 0x3f) == 1 && (get32(sys_pinctrl, 0x120 + 41 * 4) & 0x41U) == 0x41U,
	      "card detect: not driven, receiver and Schmitt on");
	put32(sys_pinctrl, 0x11c, 0U);
	error = drv_vf2_gpio_read(&gpio, &active);
	check(error == 0 && active == 1, "card detect low = card present");
	put32(sys_pinctrl, 0x11c, 1U << 9);
	error = drv_vf2_gpio_read(&gpio, &active);
	check(error == 0 && active == 0, "card detect high = no card");

	error = drv_vf2_gpio_lookup(fdt, node_of(fdt, "starfive,jh7110-pcie"), "perst-gpios", &gpio);
	check(error == 0 && gpio.pin == 26 && gpio.active_low == 1, "pcie0 perst-gpios = SYS 26 active low");
	put32(sys_pinctrl, 0x18, 0x01010101U);
	put32(sys_pinctrl, 0x58, 0x01010101U);
	error = drv_vf2_gpio_configure_output(&gpio, 1);
	check(error == 0 && field(0x40, 26, 0x7f) == 0 && field(0x0, 26, 0x3f) == 0 &&
	      field(0x0, 25, 0x3f) == 1 && field(0x0, 27, 0x3f) == 1,
	      "PERST asserted: pin 26 driven low, neighbours kept");
	error = drv_vf2_gpio_write(&gpio, 0);
	check(error == 0 && field(0x40, 26, 0x7f) == 1, "PERST released: pin 26 high");
	gpio.pin = 64;
	error = drv_vf2_gpio_write(&gpio, 0);
	check(error == EINVAL, "SYS pin 64 does not exist");
	gpio.bank = DRV_VF2_GPIO_AON;
	gpio.pin = 2;
	gpio.active_low = 0;
	error = drv_vf2_gpio_configure_output(&gpio, 1);
	check(error == 0 && ((get32(aon_pinctrl, 0x4) >> 16) & 0xf) == 1 && ((get32(aon_pinctrl, 0x0) >> 16) & 0x7) == 0,
	      "AON pin 2 driven high");
}

static void
check_coherency(
	const struct drv_fdt *fdt)
{
	uint32_t root;
	uint32_t bus;
	int coherent;

	root = DRV_FDT_NO_NODE;
	drv_fdt_find_compatible(fdt, "starfive,jh7110-mmc", DRV_FDT_NO_NODE, &root);
	drv_fdt_parent(fdt, root, &bus);
	drv_fdt_parent(fdt, bus, &root);

	bus = child_named(fdt, root, "coherent-bus");
	coherent = drv_fdt_dma_coherent(fdt, child_named(fdt, bus, "inherits-coherent"), 0);
	check(coherent == 1, "parent dma-coherent, board default 0 -> 1");
	coherent = drv_fdt_dma_coherent(fdt, child_named(fdt, bus, "says-noncoherent"), 1);
	check(coherent == 0, "node dma-noncoherent under a coherent bus -> 0");
	bus = child_named(fdt, root, "noncoherent-bus");
	coherent = drv_fdt_dma_coherent(fdt, child_named(fdt, bus, "inherits-noncoherent"), 1);
	check(coherent == 0, "parent dma-noncoherent, board default 1 -> 0");
	coherent = drv_fdt_dma_coherent(fdt, child_named(fdt, bus, "says-coherent"), 0);
	check(coherent == 1, "node dma-coherent under a noncoherent bus -> 1");
	bus = child_named(fdt, root, "silent-bus");
	coherent = drv_fdt_dma_coherent(fdt, child_named(fdt, bus, "says-nothing"), 1);
	check(coherent == 1, "nothing up to the root, board default 1 -> 1");
	coherent = drv_fdt_dma_coherent(fdt, child_named(fdt, bus, "says-nothing"), 0);
	check(coherent == 0, "nothing up to the root, board default 0 -> 0");
}

int
main(
	int argc,
	char **argv)
{
	struct drv_fdt fdt;
	unsigned char *blob;
	size_t size;
	int error;

	if (argc != 2) {
		fprintf(stderr, "usage: %s vf2-soc.dtb\n", argv[0]);
		return 2;
	}
	blob = read_file(argv[1], &size);
	error = drv_fdt_open(&fdt, blob, size);
	check(error == 0, "tree opens");
	if (error != 0)
		return 1;

	set_plls();
	set_clocks();
	write_count = 0;
	error = drv_vf2_syscon_init(&fdt);
	check(error == 0, "system control blocks found");
	error = drv_vf2_crg_init(&fdt);
	check(error == 0, "clock generators found");
	error = drv_vf2_pinctrl_init(&fdt);
	check(error == 0, "pin controllers found");

	check(write_count == 0, "init writes no register");

	check_rates();
	check_set_rate(&fdt);
	check_gates();
	check_lookups(&fdt);
	check_syscon(&fdt);
	check_pins(&fdt);
	check_coherency(&fdt);

	printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
