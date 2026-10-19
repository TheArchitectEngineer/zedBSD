/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the DC8200's own mode (ws277-p005, dc8200.c and
 * dc8200-hdmi.c) on fake register windows: the panel's timing, the
 * plane, the HDMI transmitter's writes and the framebuffer's choice.
 *
 *   own-host-test BOOTFB(0|1) LINE
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
static uint8_t hdmi[0x4000];
static uint8_t subsystem[0x90];
static uint32_t own_pixels[1920U * 1080U];
static int has_bootfb;
static char *boot_line;
static unsigned hdmi_writes;
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
	if (target >= hdmi && target < hdmi + sizeof(hdmi))
		hdmi_writes++;
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
	if (address == 0x29590000ULL && size <= sizeof(hdmi)) {
		*mapped = hdmi;
		return 0;
	}
	if (address == 0x295b0000ULL && size <= sizeof(subsystem)) {
		*mapped = subsystem;
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
	if (!has_bootfb)
		return ENOENT;
	framebuffer->size = 0x7e9000U;
	framebuffer->format = 1U;
	framebuffer->physical_base = 0xfe000000ULL;
	framebuffer->width = 1920U;
	framebuffer->height = 1080U;
	framebuffer->pitch = 7680U;
	return 0;
}

void *hal_get_arch_handoff(const char *name) { (void)name; return boot_line; }
int kern_pmem_alloc_limited(size_t size, size_t alignment, uint64_t max_address, size_t boundary, struct kern_pmem *run) { (void)alignment; (void)boundary; if (size > sizeof(own_pixels) || max_address != 0xffffffffULL) return ENOMEM; run->paddr = 0x80000000ULL; run->size = size; return 0; }
int kern_pmem_map_uncached(const struct kern_pmem *run, void **mapped) { (void)run; *mapped = own_pixels; return 0; }
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

static uint32_t
get(
	uint32_t offset)
{
	uint32_t value;

	memcpy(&value, display + (offset - 0x800U), 4);
	return value;
}

static uint32_t
hdmi_byte(
	uint32_t reg)
{
	uint32_t value;

	memcpy(&value, hdmi + reg * 4U, 4);
	return value;
}

int
main(
	int argc,
	char **argv)
{
	struct drv_powervr_dc_own_place place = {
		{ 0x29400000ULL, 0x100ULL, 0x29400800ULL, 0x2000ULL, 95U },
		0x29590000ULL, 0x4000ULL, 0x295b0000ULL, 0x90ULL
	};
	uint32_t model;
	uint32_t lock;
	int error;
	int wanted;

	if (argc != 3)
		return 2;
	has_bootfb = argv[1][0] == '1';
	boot_line = argv[2];

	/* The controller and the PLL locks. */
	model = POWERVR_DC_MODEL;
	memcpy(control + 0x20, &model, 4);
	lock = 1U;
	memcpy(hdmi + 0x1a9U * 4U, &lock, 4);
	memcpy(hdmi + 0x1afU * 4U, &lock, 4);
	put(0x1518U, 0xffffffffU);
	put(0x1cc0U, 0xffffffffU);
	put(0x1cccU, 0x8U);
	put(0x1418U, 0x222U);

	wanted = drv_powervr_dc_own_wanted();
	error = drv_powervr_dc_own_mode(&place);
	check(error == 0, "own mode set");

	if (has_bootfb && strstr(boot_line, "1280x720") != NULL) {
		check(wanted, "a mode on the line asks for the own mode");
		check(get(0x1400U) == 0xfe000000U, "plane shows the loader's framebuffer");
		check(get(0x1408U) == 7680U, "loader's pitch kept");
		check(get(0x1810U) == (1280U | (720U << 15)), "plane size is the mode");
		check(get(0x1430U) == (1280U | (1650U << 16)), "horizontal timing");
		check(get(0x1448U) == (725U | (730U << 15) | (1U << 30)), "vertical sync, high");
		check(((get(0x1518U) >> 23) & 3U) == 2U, "RGBX takes the ABGR swizzle");
		check(hdmi_byte(0x1a3U) == 99U, "74.25 MHz feedback");
		check(subsystem[0x4] == 0U, "a lit output's subsystem bits are left alone");
	}
	if (!has_bootfb) {
		check(wanted, "no loader screen asks for the own mode");
		check(get(0x1400U) == 0x80000000U, "plane shows the allocated framebuffer");
		check(get(0x1408U) == 1920U * 4U, "allocated pitch (default 1080p)");
		check(own_pixels[0] == 0x00ff0000U && own_pixels[1919] == 0x00ffffffU, "colour bars");
		check(((get(0x1518U) >> 23) & 3U) == 0U, "BGRX takes the ARGB swizzle");
		check((subsystem[0x6] & 0x10U) != 0 && (subsystem[0x8] & 0x8U) != 0, "subsystem bits set");
	}

	/* Common to every own mode. */
	check(((get(0x1518U) >> 26) & 0x3fU) == 5U, "XRGB format");
	check((get(0x1518U) & ((0x1fU << 17) | (0x7U << 11) | (1U << 22))) == 0, "linear, unrotated, unscaled");
	check((get(0x1cc0U) & (1U << 13)) != 0, "plane enabled");
	check((get(0x1cc0U) & ((1U << 19) | (1U << 1))) == 0, "panel 0, uncompressed");
	check((get(0x1cc0U) & (1U << 12)) != 0, "committed");
	check(get(0x2510U) == 2U, "blending off");
	check((get(0x2518U) & 1U) == 0, "shadow released");
	check((get(0x1418U) & 0x1313U) == 0x1111U, "panel signals on, plain polarities, running");
	check(get(0x1cd0U) == 0xaU, "DP RGB888 enabled");
	check(get(0x1cccU) == 1U, "panel 0 started without sync");
	check(hdmi_writes > 30U, "HDMI transmitter set");
	check(hdmi_byte(0xceU) == 1U, "PHY synced");
	check(drv_powervr_dc_own_mode(&place) == EALREADY, "only once");

	printf("%s own-host-test (%u passes, %d failures)\n", failures == 0 ? "PASS" : "FAIL", passes, failures);
	return failures == 0 ? 0 : 1;
}
