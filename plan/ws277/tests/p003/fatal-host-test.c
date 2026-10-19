/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the DC8200's commits, vertical-blank count and fatal stop
 * (dc8200.c) on fake register windows and a fake interrupt.
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
#include <kern/irq.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <kern/waitq.h>
#include <kern/platform.h>
#include <kern/pmem.h>
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
static kern_irq_handler_t irq_handler;
static void *irq_argument;
static int irq_number;
static unsigned eoi_count;
static uint64_t ticks;
static unsigned blanks_per_sleep;
static unsigned sleeps;
static unsigned held_irqsave;

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

void *hal_get_arch_handoff(const char *name) { (void)name; return NULL; }
int kern_pmem_alloc_limited(size_t size, size_t alignment, uint64_t max_address, size_t boundary, struct kern_pmem *run) { (void)size; (void)alignment; (void)max_address; (void)boundary; (void)run; return ENOMEM; }
int kern_pmem_map_uncached(const struct kern_pmem *run, void **mapped) { (void)run; (void)mapped; return ENOMEM; }
int kern_pmem_free(struct kern_pmem *run) { (void)run; return 0; }
void kern_usleep_range(unsigned min_us, unsigned max_us) { (void)min_us; (void)max_us; }
uint64_t sched_ticks(void) { return ticks; }
void kern_irq_send_eoi(kern_irq_ack_t acknowledge) { (void)acknowledge; eoi_count++; }
void spin_init(struct spinlock *lock, enum lock_rank rank, const char *name) { (void)rank; (void)name; lock->held.value = 0U; }
unsigned long spin_lock_irqsave(struct spinlock *lock) { lock->held.value = 1U; held_irqsave++; return 0; }
void spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled) { (void)enabled; lock->held.value = 0U; held_irqsave--; }
void waitq_init(struct wait_queue *queue, const char *name) { memset(queue, 0, sizeof(*queue)); queue->name = name; }
uint64_t waitq_sequence(const struct wait_queue *queue) { return queue->sequence; }
void waitq_wake_all(struct wait_queue *queue) { queue->sequence++; }

static void
raise_blank(
	uint32_t pending,
	uint32_t location)
{
	uint32_t acknowledge;

	acknowledge = pending;
	memcpy(control + 0x10, &acknowledge, 4);
	put(0x1450U, location);
	irq_handler(irq_number, 0, irq_argument);
}

int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *condition_lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	unsigned index;

	(void)queue;
	(void)observed;
	(void)flags;
	sleeps++;

	/* The panel either keeps counting (a blank each 16 ticks) or has stopped (time runs to the deadline). */
	condition_lock->held.value = 0U;
	if (blanks_per_sleep == 0) {
		ticks = deadline;
		condition_lock->held.value = 1U;
		return ETIMEDOUT;
	}
	for (index = 0; index < blanks_per_sleep; index++) {
		ticks += 16U;
		raise_blank(1U, 0x400U + index);
	}
	condition_lock->held.value = 1U;
	return 0;
}

int
kern_irq_register(
	int irq,
	kern_irq_handler_t handler,
	void *argument)
{
	irq_number = irq;
	irq_handler = handler;
	irq_argument = argument;
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
	struct drv_powervr_dc_frame frame;
	uint64_t base;
	uint64_t reached;
	uint32_t enable;
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

	/* The blanks: off until started, then only the HDMI panel's bit counts. */
	check(!drv_powervr_dc_scanout_ready(), "not ready before the interrupt is started");
	write_count = 0;
	error = drv_powervr_dc_vblank_start();
	check(error == 0 && irq_number == 95 && irq_handler != NULL, "the interrupt 95 is registered");
	memcpy(&enable, control + 0x14, 4);
	check(enable == 1U, "the HDMI panel's enable bit is set");
	check(drv_powervr_dc_vblank_start() == EALREADY, "started once");
	check(!drv_powervr_dc_scanout_ready(), "not ready before the first blank");
	raise_blank(2U, 0x10U);
	check(drv_powervr_dc_sequence() == 0 && eoi_count == 1U, "the other panel's interrupt does not count");
	raise_blank(1U, 0x123U);
	check(drv_powervr_dc_sequence() == 1 && eoi_count == 2U, "the HDMI panel's blank counts");
	check(drv_powervr_dc_scanout_ready(), "ready after the first blank");

	/* The frames that fit the plane: the loader's extent and pitch only. */
	check(drv_powervr_dc_frame_fits(1920U, 1080U, 7680U), "the loader's extent and pitch fit");
	check(!drv_powervr_dc_frame_fits(1280U, 720U, 5120U), "a smaller frame does not fit");
	check(!drv_powervr_dc_frame_fits(1920U, 1080U, 7936U), "another pitch does not fit");

	/* A commit of a program's frame: the order, the address, and the byte order. */
	frame.physical = 0x80000000ULL;
	frame.stride = 7680U;
	frame.width = 1920U;
	frame.height = 1080U;
	frame.rgba = 1;
	write_count = 0;
	error = drv_powervr_dc_commit(&frame, &base);
	check(error == 0 && base == 1, "a commit reads the count after writing");
	ordered = write_count == 12U;
	for (index = 0; index < 12U && index < write_count; index++) {
		if (write_offsets[index] != order[index])
			ordered = 0;
	}
	check(ordered, "commit: hold, commit clear, plane, commit, release in order");
	check(write_values[2] == 0x80000000U, "commit: the frame's address");
	check(write_values[3] == 7680U && write_values[5] == (1920U | (1080U << 15)), "commit: the loader's pitch and extent");
	check(write_values[4] == ((5U << 26) | (2U << 23)), "commit: XRGB with the ABGR swizzle for R G B X bytes");
	check(write_values[0] == 0x11U && write_values[11] == 0x10U, "commit: the panel's hold set then cleared");
	check(held_irqsave == 0, "commit: the lock is let go");
	frame.physical = 0x100000000ULL;
	check(drv_powervr_dc_commit(&frame, &base) == EINVAL, "commit: an address above 4 GiB is refused");
	frame.physical = 0x80000000ULL;
	frame.rgba = 0;
	write_count = 0;
	(void)drv_powervr_dc_commit(&frame, &base);
	check(write_values[4] == (5U << 26), "commit: the ARGB swizzle for B G R X bytes");
	write_count = 0;
	(void)drv_powervr_dc_commit(NULL, &base);
	check(write_values[2] == 0xfe000000U && write_values[4] == (0x4U << 26), "commit: the framebuffer back as the loader set it");

	/* The wait: reached while the panel counts, ETIMEDOUT once it stopped for 100 ms. */
	blanks_per_sleep = 1;
	sleeps = 0;
	error = drv_powervr_dc_wait(base + 2U, &reached);
	check(error == 0 && reached == base + 2U && sleeps == 2U, "the wait ends two blanks later");
	check(held_irqsave == 0, "the wait lets the lock go");
	blanks_per_sleep = 0;
	ticks += 1000U;
	error = drv_powervr_dc_wait(reached + 2U, &reached);
	check(error == ETIMEDOUT && reached == base + 2U, "a stopped panel ends the wait with ETIMEDOUT");

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

	/* After the fatal stop no commit writes. */
	write_count = 0;
	error = drv_powervr_dc_commit(&frame, &base);
	check(error == ENXIO && write_count == 0, "a commit after the fatal stop writes nothing and reports ENXIO");
	check(!drv_powervr_dc_scanout_ready(), "not ready after the fatal stop");

	printf("%s fatal-host-test (%u passes, %d failures)\n", failures == 0 ? "PASS" : "FAIL", passes, failures);
	return failures == 0 ? 0 : 1;
}
