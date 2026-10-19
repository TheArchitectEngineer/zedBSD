/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of drv_powervr_dc_restore_fatal() on fake register windows.
 *
 *   fatal-host-test
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/dc8200-adopt.h"
#include "drivers/gpu/powervr/powervr.h"
#include <hal/hal.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/lock.h>
#include <kern/platform.h>
#include <uapi/errno.h>

#define WRITES_MAX	64U

static uint8_t control[0x100];
static uint8_t display[0x2000];
static uint32_t write_offsets[WRITES_MAX];
static uint32_t write_values[WRITES_MAX];
static unsigned write_count;
static unsigned trylock_calls;
static int lock_busy;
static unsigned current_cpu;
static int failures;
static unsigned passes;

static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}
	printf("FAIL %s\n", what);
	failures++;
}

static void
put(
	uint32_t offset,
	uint32_t value)
{
	memcpy(display + (offset - 0x800U), &value, 4);
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

	target = (uint8_t *)address;
	memcpy(target, &value, 4);
	if (write_count < WRITES_MAX && target >= display && target < display + sizeof(display)) {
		write_offsets[write_count] = (uint32_t)(target - display) + 0x800U;
		write_values[write_count] = value;
	}
	write_count++;
}

int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	(void)attributes;
	if (address == 0x29400000ULL && size <= sizeof(control)) {
		*mapped = control;
		return 0;
	}
	if (address == 0x29400800ULL && size <= sizeof(display)) {
		*mapped = display;
		return 0;
	}
	return ENOMEM;
}

int kern_device_unmap(void *mapped, size_t size) { (void)mapped; (void)size; return 0; }
bool kern_rtc_read_counter(uint64_t *counter, uint64_t *frequency) { (void)counter; (void)frequency; return false; }
void kern_logf(const char *format, ...) { va_list ap; va_start(ap, format); vprintf(format, ap); va_end(ap); }
unsigned hal_cpu_current(void) { return current_cpu; }

int
kern_boot_framebuffer(
	const char *prefix,
	struct kern_boot_framebuffer *framebuffer)
{
	(void)prefix;
	memset(framebuffer, 0, sizeof(*framebuffer));
	framebuffer->physical_base = 0xfe000000ULL;
	framebuffer->width = 1920U;
	framebuffer->height = 1080U;
	framebuffer->pitch = 7680U;
	return 0;
}

int
spin_trylock(
	struct spinlock *lock)
{
	trylock_calls++;
	if (lock_busy)
		return 0;
	lock->held.value = 1U;
	return 1;
}

void
spin_unlock(
	struct spinlock *lock)
{
	lock->held.value = 0U;
}

int
main(
	void)
{
	static const uint32_t order[] = {
		0x2518U, 0x1cc0U, 0x1400U, 0x1408U, 0x1518U, 0x1810U,
		0x24d8U, 0x24e0U, 0x1528U, 0x2510U, 0x1cc0U, 0x2518U
	};
	struct drv_powervr_dc_place place = { 0x29400000ULL, 0x100ULL, 0x29400800ULL, 0x2000ULL, 95U };
	uint32_t model;
	unsigned index;
	int ordered;
	int error;

	/* A loader's 1080p screen on plane 0 of panel 0, U-Boot holding nothing back. */
	model = POWERVR_DC_MODEL;
	memcpy(control + 0x20, &model, 4);
	put(0x1400U, 0xfe000000U);
	put(0x1408U, 7680U);
	put(0x1518U, 0x4U << 26);
	put(0x1cc0U, POWERVR_DC_EXTRA_ENABLED);
	put(0x1810U, 1920U | (1080U << POWERVR_DC_SIZE_HEIGHT_SHIFT));
	put(0x24e0U, 1920U | (1080U << 15));
	put(0x2510U, 0x7U);
	put(0x2518U, 0x10U);

	/* The probe only reads. */
	write_count = 0;
	error = drv_powervr_dc_probe(&place);
	check(error == 0 && write_count == 0, "the probe writes nothing");
	check(!drv_powervr_dc_fatal(), "not fatal before a fatal stop");

	/* A program's buffer is on the plane when the stop comes. */
	put(0x1400U, 0x80000000U);
	put(0x1518U, 0x5U << 26);

	write_count = 0;
	trylock_calls = 0;
	drv_powervr_dc_restore_fatal();
	check(drv_powervr_dc_fatal(), "a fatal stop marks the controller fatal");
	check(write_count == 12U, "a fatal stop writes 12 registers");
	ordered = 1;
	for (index = 0; index < 12U && index < write_count; index++) {
		if (write_offsets[index] != order[index])
			ordered = 0;
	}
	check(ordered, "hold, commit clear, plane, commit, release in order");
	check(write_values[0] == 0x11U && write_values[11] == 0x10U, "the panel's hold bit set then cleared, other bits kept");
	check(write_values[1] == POWERVR_DC_EXTRA_ENABLED && write_values[10] == (POWERVR_DC_EXTRA_ENABLED | POWERVR_DC_EXTRA_COMMIT),
	      "the commit bit cleared then set on the probed extension");
	check(write_values[2] == 0xfe000000U && write_values[4] == (0x4U << 26), "the probed address and format are back");
	check(trylock_calls == 1U, "a free commit lock is taken at once");

	/* Another CPU holds the lock forever: bounded tries, written anyway. */
	write_count = 0;
	trylock_calls = 0;
	lock_busy = 1;
	drv_powervr_dc_restore_fatal();
	check(trylock_calls == 4096U && write_count == 12U, "a held lock is tried 4096 times and the plane written anyway");
	lock_busy = 0;

	printf("%s fatal-host-test (%u passes, %d failures)\n", failures == 0 ? "PASS" : "FAIL", passes, failures);
	return failures == 0 ? 0 : 1;
}
