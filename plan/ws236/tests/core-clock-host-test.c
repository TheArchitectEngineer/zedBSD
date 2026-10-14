/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws236-p011: host test of the core clock's floors in rpi4-firmware.c.
 *
 * The production file is compiled unchanged against a model of the
 * mailbox and of the firmware's clock tags.  The model keeps the core
 * clock's rate, answers the rate and the highest rate, and counts the
 * changes.  The test checks that the clock runs the highest floor, that
 * the display's lowering and raising change nothing while the video floor
 * is held (the T1-685 stall), that the rate from before the first floor
 * comes back when nobody asks, and that a refused change keeps the floor.
 *
 * Usage: core-clock-host-test
 */

#include "drivers/platform/rpi4/rpi4-firmware.h"
#include <kern/clock.h>
#include <kern/dcache.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/klog.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The model's mailbox registers and request buffer. */
#define MODEL_BUFFER_PHYSICAL	0x00123000ULL
#define MODEL_CORE_BASE		200000000U
#define MODEL_CORE_MAXIMUM	500000000U

/* The mailbox registers the client maps. */
static uint32_t model_registers[0x40U / 4U];

/* The request buffer the client fills. */
static uint32_t model_buffer[4096U / 4U] __attribute__((aligned(4096)));

/* The word the model hands back through mailbox 0, and whether one waits. */
static uint32_t model_answer;
static bool model_answer_waiting;

/* The core clock's rate, how many changes it saw, and whether the next change is refused. */
static uint32_t model_core_hz;
static unsigned model_changes;
static bool model_refuse;

static int failures;

static void check(bool condition, const char *what);
static void model_request(uint32_t word);

int
main(void)
{
	struct drv_fdt fdt;
	int error;

	/* The mailbox, as the device tree would give it. */
	memset(&fdt, 0, sizeof(fdt));
	model_core_hz = MODEL_CORE_BASE;
	error = drv_rpi4_firmware_init(&fdt);
	check(error == 0, "the mailbox is prepared");

	/* The display raises the clock for its planes: one change, to the floor. */
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_DISPLAY, 500000000U);
	check(error == 0 && model_core_hz == 500000000U && model_changes == 1U, "the display's floor is run");

	/* A video decoder takes the highest rate: the clock is already there, nothing changes. */
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_VIDEO, DRV_RPI4_FIRMWARE_CORE_MAXIMUM);
	check(error == 0 && model_core_hz == MODEL_CORE_MAXIMUM && model_changes == 1U, "the video floor at the highest rate changes nothing");

	/* The display gives the output back and lowers its floor, then raises it again: no change under the decode. */
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_DISPLAY, 137600000U);
	check(error == 0 && model_core_hz == MODEL_CORE_MAXIMUM && model_changes == 1U, "the display's lowering changes nothing under a decode");
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_DISPLAY, 500000000U);
	check(error == 0 && model_core_hz == MODEL_CORE_MAXIMUM && model_changes == 1U, "the display's raising changes nothing under a decode");

	/* The display lowers again, then the decoder goes: the clock follows the display. */
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_DISPLAY, 137600000U);
	check(error == 0 && model_changes == 1U, "the display's lowering waits for the decoder");
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_VIDEO, 0U);
	check(error == 0 && model_core_hz == 137600000U && model_changes == 2U, "the decoder's going lets the clock follow the display");

	/* Nobody asks: the rate from before the first floor. */
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_DISPLAY, 0U);
	check(error == 0 && model_core_hz == MODEL_CORE_BASE && model_changes == 3U, "the rate before the first floor comes back");

	/* A decoder on the console raises the clock before it decodes, and gives it back. */
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_VIDEO, DRV_RPI4_FIRMWARE_CORE_MAXIMUM);
	check(error == 0 && model_core_hz == MODEL_CORE_MAXIMUM && model_changes == 4U, "a decoder on the console raises the clock");

	/* A refused change keeps the owner's floor as it was. */
	model_refuse = true;
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_VIDEO, 0U);
	check(error == EIO && model_core_hz == MODEL_CORE_MAXIMUM, "a refused change reports the refusal");
	model_refuse = false;
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_DISPLAY, 300000000U);
	check(error == 0 && model_core_hz == MODEL_CORE_MAXIMUM && model_changes == 4U, "the refused owner's floor is still held");
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_VIDEO, 0U);
	check(error == 0 && model_core_hz == 300000000U && model_changes == 5U, "the floor is given back later");

	/* A floor above the highest rate runs at the highest rate; an unknown owner is refused. */
	error = drv_rpi4_firmware_core_clock(DRV_RPI4_FIRMWARE_CORE_DISPLAY, 900000000U);
	check(error == 0 && model_core_hz == MODEL_CORE_MAXIMUM, "a floor above the highest rate is the highest rate");
	error = drv_rpi4_firmware_core_clock((enum drv_rpi4_firmware_core_owner)DRV_RPI4_FIRMWARE_CORE_OWNERS, 1U);
	check(error == EINVAL, "an owner outside the enumeration is refused");

	/* Reports the outcome. */
	if (failures != 0) {
		printf("core-clock-host-test: FAIL (%d)\n", failures);
		return EXIT_FAILURE;
	}
	printf("core-clock-host-test: PASS\n");
	return EXIT_SUCCESS;
}

/* Counts and prints a failed check. */
static void
check(
	bool condition,
	const char *what)
{
	/* A passed check says nothing. */
	if (condition)
		return;
	printf("FAIL: %s (rate %u, changes %u)\n", what, (unsigned)model_core_hz, model_changes);
	failures++;
}

/* Plays the firmware: answers the clock tags in the request buffer. */
static void
model_request(
	uint32_t word)
{
	uint32_t tag;

	/* The request's tag and its answer's header. */
	tag = model_buffer[2];
	model_buffer[1] = 0x80000000U;
	model_buffer[4] = 0x80000000U | 8U;

	/* The clock's rate now, its highest rate, or a change of its rate. */
	if (tag == 0x00030002U) {
		check(model_buffer[5] == 4U, "a question names the core clock");
		model_buffer[6] = model_core_hz;
	} else if (tag == 0x00030004U) {
		check(model_buffer[5] == 4U, "a question names the core clock");
		model_buffer[6] = MODEL_CORE_MAXIMUM;
	} else if (tag == 0x00038002U) {
		check(model_buffer[5] == 4U && model_buffer[7] == 0U, "a change names the core clock and leaves turbo");
		if (model_refuse) {
			model_buffer[1] = 0x80000001U;
		} else {
			model_core_hz = model_buffer[6];
			model_changes++;
		}
	} else {
		check(false, "only clock tags are asked");
	}

	/* The same word comes back through mailbox 0. */
	model_answer = word;
	model_answer_waiting = true;
}

/*
 * The device tree and the kernel services the client calls.
 */

int
drv_fdt_find_compatible(
	const struct drv_fdt *fdt,
	const char *compatible,
	uint32_t after,
	uint32_t *node)
{
	(void)fdt;
	(void)after;
	check(strcmp(compatible, "brcm,bcm2835-mbox") == 0, "the mailbox is looked up");
	*node = 1U;
	return 0;
}

bool
drv_fdt_node_enabled(
	const struct drv_fdt *fdt,
	uint32_t node)
{
	(void)fdt;
	(void)node;
	return true;
}

int
drv_fdt_reg(
	const struct drv_fdt *fdt,
	uint32_t node,
	unsigned index,
	uint64_t *address,
	uint64_t *size)
{
	(void)fdt;
	(void)node;
	(void)index;
	*address = 0xfe00b880ULL;
	*size = 0x40U;
	return 0;
}

uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	unsigned offset;

	/* Mailbox 1 is never full; mailbox 0 holds the answer while one waits. */
	offset = (unsigned)((const volatile uint8_t *)address - (const volatile uint8_t *)model_registers);
	if (offset == 0x38U)
		return 0U;
	if (offset == 0x18U) {
		if (model_answer_waiting)
			return 0U;
		return 0x40000000U;
	}
	check(offset == 0U, "only the mailbox's registers are read");
	model_answer_waiting = false;
	return model_answer;
}

void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	unsigned offset;

	/* A post to mailbox 1 is a request. */
	offset = (unsigned)((volatile uint8_t *)address - (volatile uint8_t *)model_registers);
	check(offset == 0x20U, "only mailbox 1 is written");
	model_request(value);
}

int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	(void)address;
	(void)size;
	(void)attributes;
	*mapped = model_registers;
	return 0;
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
	(void)max_address;
	(void)boundary;
	run->paddr = MODEL_BUFFER_PHYSICAL;
	run->size = size;
	return 0;
}

int
kern_pmem_free(
	struct kern_pmem *run)
{
	(void)run;
	return 0;
}

void *
kern_pmem_to_kernel(
	hal_physaddr_t address)
{
	(void)address;
	return model_buffer;
}

void
kern_dcache_clean_range(
	const void *address,
	size_t size)
{
	(void)address;
	(void)size;
}

void
kern_dcache_invalidate_range(
	const void *address,
	size_t size)
{
	(void)address;
	(void)size;
}

void
kern_usleep_range(
	unsigned min_us,
	unsigned max_us)
{
	(void)min_us;
	(void)max_us;
}

bool
kern_irq_disable(void)
{
	return true;
}

void
kern_irq_enable(void)
{
}

void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	printf("  log: ");
	vprintf(format, arguments);
	va_end(arguments);
}
