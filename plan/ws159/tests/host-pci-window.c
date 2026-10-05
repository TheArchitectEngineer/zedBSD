/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the placement of an unassigned BAR (BUG-210,
 * src/drivers/pci/pci-window.c compiled unchanged).
 *
 * The windows are the memory resources of the Latitude 5330's
 * \_SB.PC00._CRS as the AML host harness evaluates them from the
 * machine's DSDT (plan/ws049/tests/latitude5330/dsdt.dat):
 *
 *   build/.../aml-host --resources '\_SB_.PC00' dsdt.dat ssdt1..15.dat
 *
 * The 32-bit window (_Y0E) and the 64-bit window (_Y0F) are filled at run
 * time from the GNVS fields M32B, M32L, M64B and M64L (an OperationRegion
 * at 0x6150C000 of the machine's memory), which the harness reads as
 * zero: the 32-bit window comes out at 0 with length 0, the 64-bit one at
 * 0x10000 with length 0.  With those windows the BAR fits nowhere, and the
 * kernel logs that and leaves it unassigned.
 *
 * The real values are not in any table.  The 5330's i915 aperture (BAR2,
 * 256 MiB) is at 0x4000000000 (zedBSD's dmesg, "gmadr=0x4000000000"), the
 * lowest a firmware places it in its 64-bit window, and the xHCI BARs are
 * at 0x6055280000 and 0x6055260000 (the same dmesg), so the test fills
 * M64B = 0x4000000000 and M64L = 0x4000000000 (to the CPU's 39-bit end)
 * and M32B = 0x80000000, M32L = 0x40000000 (below ECAM at 0xc0000000) as
 * an inference, not an observation.  The busy ranges are the BARs the
 * dmesg names (the two xHCI, i915's aperture, i915's 16 MiB registers
 * assumed at 0x6054000000), PRRE's _CRS (\_SB.PRRE, PNP0C02, as the
 * harness evaluates it), ECAM 0xc0000000-0xcfffffff and the low RAM.
 * The LPSS I2C BAR0s (64-bit, 4 KiB) must go to 0x4010000000 and
 * 0x4010001000: in the 64-bit window, size aligned, clear of the aperture
 * and of each other.  Then the general cases: a 32-bit BAR, alignment, a
 * busy range in the way, a full window, a window that wraps, a size that
 * is not a power of two, the 32-bit clipping and the legacy windows.
 *
 *   plan/ws159/tests/run-host-pci-window.sh
 */

#include <drivers/pci/pci-window.h>

#include <stdio.h>
#include <stdlib.h>

/*
 * The kernel's error numbers (include/uapi/errno.h), which the allocator,
 * compiled freestanding, reports; the host's <errno.h> numbers differ.
 */
#define KERNEL_EINVAL	3
#define KERNEL_ENOSPC	8

/* The number of entries of a table. */
#define COUNT(table)	((unsigned)(sizeof(table) / sizeof((table)[0])))

/*
 * The memory resources of \_SB.PC00._CRS as the harness evaluates them
 * with the GNVS read as zero, in their order.
 */
static const struct drv_pci_range pc00_zero_nvs[] = {
	{ 0xa0000ULL, 0x20000ULL },
	{ 0xc0000ULL, 0x4000ULL },
	{ 0xc4000ULL, 0x4000ULL },
	{ 0xc8000ULL, 0x4000ULL },
	{ 0xcc000ULL, 0x4000ULL },
	{ 0xd0000ULL, 0x4000ULL },
	{ 0xd4000ULL, 0x4000ULL },
	{ 0xd8000ULL, 0x4000ULL },
	{ 0xdc000ULL, 0x4000ULL },
	{ 0xe0000ULL, 0x4000ULL },
	{ 0xe4000ULL, 0x4000ULL },
	{ 0xe8000ULL, 0x4000ULL },
	{ 0xec000ULL, 0x4000ULL },
	{ 0xf0000ULL, 0x10000ULL },
	{ 0x0ULL, 0x0ULL },
	{ 0x10000ULL, 0x0ULL }
};

/*
 * The same resources with the GNVS filled with the inferred values: the
 * 32-bit window M32B/M32L and the 64-bit window M64B/M64L.
 */
static const struct drv_pci_range pc00_inferred[] = {
	{ 0xa0000ULL, 0x20000ULL },
	{ 0xc0000ULL, 0x4000ULL },
	{ 0xc4000ULL, 0x4000ULL },
	{ 0xc8000ULL, 0x4000ULL },
	{ 0xcc000ULL, 0x4000ULL },
	{ 0xd0000ULL, 0x4000ULL },
	{ 0xd4000ULL, 0x4000ULL },
	{ 0xd8000ULL, 0x4000ULL },
	{ 0xdc000ULL, 0x4000ULL },
	{ 0xe0000ULL, 0x4000ULL },
	{ 0xe4000ULL, 0x4000ULL },
	{ 0xe8000ULL, 0x4000ULL },
	{ 0xec000ULL, 0x4000ULL },
	{ 0xf0000ULL, 0x10000ULL },
	{ 0x80000000ULL, 0x40000000ULL },
	{ 0x4000000000ULL, 0x4000000000ULL }
};

/*
 * The ranges the 5330 already decodes: its BARs from zedBSD's dmesg,
 * \_SB.PRRE's _CRS as the harness evaluates it, ECAM and the low RAM.
 */
static const struct drv_pci_range busy_5330[] = {
	{ 0x6055280000ULL, 0x10000ULL },
	{ 0x6055260000ULL, 0x10000ULL },
	{ 0x4000000000ULL, 0x10000000ULL },
	{ 0x6054000000ULL, 0x1000000ULL },
	{ 0xfe000000ULL, 0x20000ULL },
	{ 0xfe04c000ULL, 0x4000ULL },
	{ 0xfe050000ULL, 0x60000ULL },
	{ 0xfe0d0000ULL, 0x30000ULL },
	{ 0xfe200000ULL, 0x600000ULL },
	{ 0xff000000ULL, 0x1000000ULL },
	{ 0x0ULL, 0x690000ULL },
	{ 0x6b0000ULL, 0x20000ULL },
	{ 0x6f0000ULL, 0x910000ULL },
	{ 0xc0000000ULL, 0x10000000ULL },
	{ 0x0ULL, 0x80000000ULL }
};

/* The number of checks that failed. */
static unsigned failures;

static void expect_place(const char *name, const struct drv_pci_range *windows, unsigned window_count, const struct drv_pci_range *busy, unsigned busy_count, unsigned long long size, int wide, int expected_error, unsigned long long expected_address, unsigned expected_window);
static void test_5330(void);
static void test_general(void);

/* Runs every case and reports whether all passed. */
int
main(void)
{
	/* The 5330's windows and busy ranges. */
	test_5330();

	/* The allocator's rules on made-up windows. */
	test_general();

	/* Reports the result. */
	if (failures != 0) {
		printf("host-pci-window: %u failed\n", failures);
		return 1;
	}

	/* Succeeded: every case passed. */
	printf("host-pci-window: all passed\n");
	return 0;
}

/* Places one BAR and compares the outcome with the expected one. */
static void
expect_place(
	const char *name,
	const struct drv_pci_range *windows,
	unsigned window_count,
	const struct drv_pci_range *busy,
	unsigned busy_count,
	unsigned long long size,
	int wide,
	int expected_error,
	unsigned long long expected_address,
	unsigned expected_window)
{
	uint64_t address;
	unsigned window;
	int error;

	/* Places the BAR. */
	address = 0;
	window = 0;
	error = drv_pci_window_place(windows, window_count, busy, busy_count, size, wide != 0, &address, &window);

	/* A failure must be the expected one. */
	if (error != expected_error) {
		printf("FAIL %s: error %d, expected %d\n", name, error, expected_error);
		failures++;
		return;
	}

	/* An expected failure passes. */
	if (error != 0) {
		printf("ok   %s: error %d\n", name, error);
		return;
	}

	/* A success must give the expected place and window. */
	if (address != expected_address || window != expected_window) {
		printf("FAIL %s: 0x%llx in window %u, expected 0x%llx in window %u\n",
		       name,
		       (unsigned long long)address,
		       window,
		       expected_address,
		       expected_window);
		failures++;
		return;
	}

	/* Succeeded: the expected place. */
	printf("ok   %s: 0x%llx in window %u\n", name, (unsigned long long)address, window);
}

/* The 5330's two LPSS I2C BAR0s, with the harness's windows and with the inferred ones. */
static void
test_5330(void)
{
	struct drv_pci_range busy[COUNT(busy_5330) + 1U];
	unsigned count;
	unsigned index;

	/* With the GNVS read as zero, no window can take the BAR. */
	expect_place("5330 zero GNVS, 64-bit 4 KiB", pc00_zero_nvs, COUNT(pc00_zero_nvs), busy_5330, COUNT(busy_5330), 0x1000ULL, 1, KERNEL_ENOSPC, 0, 0);

	/* With the inferred windows, 00:15.0 goes right above the i915 aperture. */
	expect_place("5330 00:15.0 BAR0", pc00_inferred, COUNT(pc00_inferred), busy_5330, COUNT(busy_5330), 0x1000ULL, 1, 0, 0x4010000000ULL, 15U);

	/* 00:15.1 goes next to it once 00:15.0's page is busy. */
	count = COUNT(busy_5330);
	for (index = 0; index < count; index++)
		busy[index] = busy_5330[index];
	busy[index].base = 0x4010000000ULL;
	busy[index].length = 0x1000ULL;
	expect_place("5330 00:15.1 BAR0", pc00_inferred, COUNT(pc00_inferred), busy, COUNT(busy), 0x1000ULL, 1, 0, 0x4010001000ULL, 15U);

	/* A 32-bit BAR of the 5330 would take the 32-bit window, above the low RAM. */
	expect_place("5330 32-bit 4 KiB", pc00_inferred, COUNT(pc00_inferred), busy_5330, COUNT(busy_5330), 0x1000ULL, 0, 0, 0x80000000ULL, 14U);
}

/* The rules of the placement on made-up windows. */
static void
test_general(void)
{
	static const struct drv_pci_range both[] = {
		{ 0xe0000000ULL, 0x10000000ULL },
		{ 0x800000000ULL, 0x100000000ULL }
	};
	static const struct drv_pci_range low_only[] = {
		{ 0xe0000000ULL, 0x10000000ULL }
	};
	static const struct drv_pci_range straddle[] = {
		{ 0xfff00000ULL, 0x200000ULL }
	};
	static const struct drv_pci_range legacy[] = {
		{ 0xa0000ULL, 0x20000ULL },
		{ 0x0ULL, 0x100000ULL }
	};
	static const struct drv_pci_range wraps[] = {
		{ 0xfffffffffffff000ULL, 0x2000ULL }
	};
	static const struct drv_pci_range small[] = {
		{ 0xe0000000ULL, 0x3000ULL }
	};
	static const struct drv_pci_range busy_first[] = {
		{ 0x800000000ULL, 0x1ULL },
		{ 0xe0000000ULL, 0x100ULL }
	};
	static const struct drv_pci_range busy_small_full[] = {
		{ 0xe0000000ULL, 0x1000ULL },
		{ 0xe0001800ULL, 0x10ULL },
		{ 0xe0002000ULL, 0x1000ULL }
	};
	static const struct drv_pci_range top[] = {
		{ 0xffffffffffffe000ULL, 0x2000ULL }
	};
	static const struct drv_pci_range busy_top_first[] = {
		{ 0xffffffffffffe000ULL, 0x1000ULL }
	};
	static const struct drv_pci_range busy_top_all[] = {
		{ 0xffffffffffffe000ULL, 0x2000ULL }
	};

	/* A 64-bit BAR prefers the window above 4 GiB, a 32-bit one the one below. */
	expect_place("64-bit prefers high", both, COUNT(both), NULL, 0, 0x4000ULL, 1, 0, 0x800000000ULL, 1U);
	expect_place("32-bit takes low", both, COUNT(both), NULL, 0, 0x4000ULL, 0, 0, 0xe0000000ULL, 0U);

	/* A 64-bit BAR falls back to a low window when there is no high one. */
	expect_place("64-bit falls back low", low_only, COUNT(low_only), NULL, 0, 0x4000ULL, 1, 0, 0xe0000000ULL, 0U);

	/* A busy byte moves the BAR to the next aligned address. */
	expect_place("busy byte skipped", both, COUNT(both), busy_first, COUNT(busy_first), 0x100000ULL, 1, 0, 0x800100000ULL, 1U);

	/* A BAR smaller than a page takes a page, aligned to a page. */
	expect_place("small BAR page aligned", both, COUNT(both), busy_first, COUNT(busy_first), 0x100ULL, 0, 0, 0xe0001000ULL, 0U);

	/* A window whose every page is touched has no room. */
	expect_place("small window full", small, COUNT(small), busy_small_full, COUNT(busy_small_full), 0x1000ULL, 0, KERNEL_ENOSPC, 0, 0);

	/* A BAR larger than its window does not fit. */
	expect_place("too large", small, COUNT(small), NULL, 0, 0x4000ULL, 0, KERNEL_ENOSPC, 0, 0);

	/* A 32-bit BAR uses only the part of a window below 4 GiB. */
	expect_place("32-bit clipped fits", straddle, COUNT(straddle), NULL, 0, 0x100000ULL, 0, 0, 0xfff00000ULL, 0U);
	expect_place("32-bit clipped too large", straddle, COUNT(straddle), NULL, 0, 0x200000ULL, 0, KERNEL_ENOSPC, 0, 0);

	/* The legacy ranges below 1 MiB are never used. */
	expect_place("legacy refused", legacy, COUNT(legacy), NULL, 0, 0x1000ULL, 0, KERNEL_ENOSPC, 0, 0);

	/* A window that runs past the end of the address space is no window. */
	expect_place("wrapping window", wraps, COUNT(wraps), NULL, 0, 0x1000ULL, 1, KERNEL_ENOSPC, 0, 0);

	/* The last page of the address space can be taken, and a busy range ending there leaves nothing after it. */
	expect_place("last page", top, COUNT(top), busy_top_first, COUNT(busy_top_first), 0x1000ULL, 1, 0, 0xfffffffffffff000ULL, 0U);
	expect_place("busy to the end", top, COUNT(top), busy_top_all, COUNT(busy_top_all), 0x1000ULL, 1, KERNEL_ENOSPC, 0, 0);

	/* A size that is not a power of two, and a size of zero, are refused. */
	expect_place("size 0x3000", both, COUNT(both), NULL, 0, 0x3000ULL, 1, KERNEL_EINVAL, 0, 0);
	expect_place("size 0", both, COUNT(both), NULL, 0, 0ULL, 1, KERNEL_EINVAL, 0, 0);

	/* A BAR as large as its window fits at its base only when the base is aligned. */
	expect_place("whole window", low_only, COUNT(low_only), NULL, 0, 0x10000000ULL, 0, 0, 0xe0000000ULL, 0U);
	expect_place("unaligned whole window", straddle, COUNT(straddle), NULL, 0, 0x200000ULL, 1, KERNEL_ENOSPC, 0, 0);
}
