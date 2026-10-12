/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The real flip state and vblank service run against an HVS model whose
 * current-list register moves only when the test delivers a frame (WS141,
 * and the WS214 p003a lists made from the recorded state: three slots,
 * replacement of a list not yet taken, coalescing, borrowed images let go
 * in thread context, the lease copy and its revocation).
 */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/clock.h>
#include <kern/dcache.h>
#include <kern/device-io.h>
#include <kern/pmem.h>
#include <kern/irq.h>
#include <kern/waitq.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"

/* What a sleeping wait sees happen: the hardware takes the next list, nothing happens, or a scripted event. */
#define SLEEP_ADOPT	0U
#define SLEEP_STALL	1U
#define SLEEP_SCRIPT	2U
#define SLEEP_REVOKE	3U

/* The three list slots' addresses in SRAM words. */
#define SLOT0 256U
#define SLOT1 512U
#define SLOT2 768U

/* Persistent display mappings and owner survive every IRQ in one scenario. */
static struct bcm2711_display test_display;

/* SRAM and register windows model HVS and both PV sources without real DMA. */
static uint32_t test_hvs[8192];
static uint32_t test_pv[2][32];

/* Three disjoint physical buffers share geometry but have separate ownership. */
static struct drv_bcm2711_boot_screen test_frames[3];
static uint8_t test_mapping;

/* Effects are checked at publication, while sleeps control real source delivery. */
static unsigned test_failures;
static unsigned test_writes;
static unsigned test_cleaned;
static unsigned test_barriers;
static unsigned test_sleeps;
static unsigned test_sleep_mode;
static unsigned test_publications;
static unsigned test_fresh;
static uint32_t test_expected_physical;

/* The sleeps' scripted wait: wrong lists and ports first, then the newest list. */
static unsigned test_script_step;

/* The owner a SLEEP_REVOKE wait revokes. */
static struct bcm2711_flip_owner test_revoked;

/* Borrowed images released through the real reap, and whether one was released in the IRQ. */
static unsigned test_released;
static bool test_in_irq;
static struct bcm2711_buffer *test_released_last;

/* The cadence scenario drives the clock in milliseconds and keeps the last report line. */
static uint64_t test_clock;
static bool test_logging;
static char test_log[512];

/* The model's scheduler clock (display-lock-host.c). */
extern uint64_t display_test_ticks;

static void check(bool condition, const char *what);
static void setup(uint32_t port, uint32_t format);
static void deliver(uint32_t port, uint32_t current);
static void normal_flips(uint32_t port, uint32_t format);
static void uncertain_flips(void);
static void cursor_flips(void);
static void coalesced_flips(void);
static void borrowed_flips(void);
static void lease_flips(void);
static void copy_target_flips(void);
static void overlay_flips(void);
static void overlay_change(struct bcm2711_flip_change *change, uint32_t plane, uint64_t serial, const struct drv_bcm2711_boot_screen *frame);
static void cadence_report(void);
static void refusals(void);
static bool slot_used(uint32_t list);

/*
 * Checks publication, adoption and borrowed-image lifetime through real code.
 * Electrical scanout, firmware RAM lifetime and SMP lock ordering remain untested.
 */
int
main(
	void)
{
	/* Runs both physical port choices and firmware RGB byte orders. */
	normal_flips(0, 0);
	normal_flips(1, 1);
	uncertain_flips();
	cursor_flips();
	coalesced_flips();
	borrowed_flips();
	lease_flips();
	copy_target_flips();
	overlay_flips();
	cadence_report();
	refusals();

	/* Reports software outcomes without implying physical display acceptance. */
	if (test_failures != 0) {
		printf("display-flip-host-test FAIL %u\n", test_failures);
		return 1;
	}

	/* Succeeded: the production component honors all modeled lifetime boundaries. */
	puts("display-flip-host-test PASS");
	return 0;
}

/*
 * Models accepted provider requests outside the IRQ guard, serialized by the clock mutex.
 */
int
drv_rpi4_firmware_property(
	uint32_t tag,
	uint32_t *values,
	unsigned request_count,
	unsigned capacity,
	uint32_t *answered)
{
	/* Firmware requests sleep: under the clock mutex, never under the spinlock. */
	assert(test_display.flip.clock_mutex.locked == 1 && test_display.flip.guard.held.value == 0);
	assert(tag == 0x00038002U && request_count == 3 && capacity == 3);
	assert(values[0] == 4 && values[2] == 0);
	*answered = 8;

	/* Succeeded: the modeled provider accepted the requested floor. */
	return 0;
}

/*
 * Models ordered reads from persistent register windows.
 */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	/* Mapped pointers identify ordinary model storage rather than device memory. */
	return *(const volatile uint32_t *)address;
}

/*
 * Models W1C sources and checks complete lists before next-pointer publication.
 */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t word)
{
	unsigned port;
	uint32_t *list;
	uint32_t index;
	uint32_t expected_order;

	/* Tracks writes and quiets HVS channel IRQ status using W1C semantics. */
	test_writes++;
	if (address == &test_hvs[0x04 / 4]) {
		test_hvs[0x04 / 4] &= ~word;
		return;
	}

	/* Retires only the addressed PV source; a fresh publication discards a stale vblank. */
	for (port = 0; port < 2; port++) {
		/* A write to one output must not acknowledge the other output's IRQ. */
		if (address == &test_pv[port][0x28 / 4]) {
			test_pv[port][0x28 / 4] &= ~word;
			if (!test_in_irq)
				test_fresh++;
			return;
		}
	}

	/* Every SRAM write goes to a slot the hardware cannot be reading. */
	if (address >= (volatile void *)&test_hvs[0x4000 / 4 + SLOT0] && address < (volatile void *)&test_hvs[0x4000 / 4 + 1024]) {
		index = (uint32_t)((volatile uint32_t *)address - &test_hvs[0x4000 / 4]);
		check(index - (index % 256U) != test_hvs[0x30 / 4], "the current list is never written");
		check(test_display.flip.guard.held.value == 1, "lists are written under the guard");
	}

	/* Validates the hardware-visible list with literal independent word values. */
	if (address == &test_hvs[0x20 / 4]) {
		/* Publication occurs under lock after the cache clean, with the list complete. */
		test_publications++;
		check(test_display.flip.guard.held.value == 1, "publication guarded");
		check(test_cleaned != 0 && test_barriers >= 1, "cache/barrier before publication");
		check(test_hvs[0x30 / 4] != word || word == 43, "the current list is never republished");
		if (word != 43) {
			/* One of the three slots, with an END after at most three planes. */
			check(word == SLOT0 || word == SLOT1 || word == SLOT2, "dedicated SRAM slot");
			list = &test_hvs[0x4000 / 4 + word];
			check(list[8] == 0x80000000U || list[16] == 0x80000000U || list[24] == 0x80000000U, "END after the planes");
			expected_order = 0x4800d807U;
			if (test_display.screen.format == 1)
				expected_order = 0x4800f807U;
			if (test_expected_physical != 0 && list[5] != (0xc0000000U | (uint32_t)test_display.screen.physical)) {
				/* An opaque full-screen primary first, the console left out. */
				check(list[0] == expected_order && list[1] == 0, "literal RGB plane header");
				check(list[2] == 0x4000fff0U && list[3] == 0x04380780U, "literal dimensions/alpha");
				check(list[4] == 0xc0c0c0c0U && list[6] == 0xc0c0c0c0U, "fresh writable contexts");
				check(list[5] == (0xc0000000U | test_expected_physical), "candidate DMA pointer");
				check(list[7] == 7680, "pitch");
			}
		}
	}

	/* Pointer publication does not advance current until a modeled frame starts. */
	*(volatile uint32_t *)address = word;
}

/*
 * Lets a sleeping wait progress: by default the hardware takes the newest list at the next frame.
 */
void
display_test_sleep(
	struct wait_queue *queue,
	uint64_t deadline)
{
	/* The wait released the guard before sleeping. */
	(void)queue;
	assert(test_display.flip.guard.held.value == 0);
	test_sleeps++;
	display_test_ticks++;

	/* What the scenario lets happen during this sleep. */
	switch (test_sleep_mode) {
	case SLEEP_STALL:
		/* No frame comes: time runs to the deadline. */
		if (deadline != 0)
			display_test_ticks = deadline;
		break;
	case SLEEP_SCRIPT:
		/* An old-list frame, then another port's frame, then the adoption. */
		test_script_step++;
		if (test_script_step == 1) {
			/* The selected PV has a genuine event but HVS still uses the preceding list. */
			deliver(test_display.port, test_hvs[0x30 / 4]);
		} else if (test_script_step == 2) {
			/* Matching HVS adoption on another PV's event is insufficient. */
			test_hvs[0x30 / 4] = test_hvs[0x20 / 4];
			deliver(1U - test_display.port, test_hvs[0x20 / 4]);
			test_hvs[0x30 / 4] = test_display.flip.active_list;
		} else {
			/* The selected source and adopted current pointer jointly complete the call. */
			deliver(test_display.port, test_hvs[0x20 / 4]);
		}
		break;
	case SLEEP_REVOKE:
		/* The lease ends while its present waits; no frame comes. */
		(void)bcm2711_display_flip_revoke(&test_display, &test_revoked);
		break;
	default:
		/* The selected source's frame with the newest list. */
		deliver(test_display.port, test_hvs[0x20 / 4]);
		break;
	}
}

/*
 * Counts a borrowed image's release, which must come in thread context.
 */
void
bcm2711_buffer_release(
	struct bcm2711_buffer *buffer)
{
	/* Releases may free memory: never in the IRQ, never under the guard. */
	check(!test_in_irq, "no release in the IRQ");
	check(test_display.flip.guard.held.value == 0, "no release under the guard");
	test_released++;
	test_released_last = buffer;
}

/*
 * Supplies a stable token for all bounded caller-owned physical mappings.
 */
void *
kern_pmem_to_kernel(
	hal_physaddr_t physical)
{
	/* Physical descriptors are validated by production code, not this mapping stub. */
	(void)physical;

	/* Succeeded: modeled CPU access remains valid throughout cache preparation. */
	return &test_mapping;
}

/*
 * Observes cache publication outside the guard.
 */
void
kern_dcache_clean_range(
	const void *address,
	size_t length)
{
	/* Cache cleaning must occur outside the IRQ guard and cover complete rows. */
	assert(address == &test_mapping);
	assert(length == 8294400U);
	assert(test_display.flip.guard.held.value == 0);
	test_cleaned++;
}

/*
 * Observes explicit ordering boundaries without simulating ARM bus behavior.
 */
void
kern_io_write_barrier(
	void)
{
	/* Counters let next-pointer publication verify its required predecessors. */
	test_barriers++;
}

/*
 * Supplies inert line masking because real source callbacks are invoked directly.
 */
void
kern_irq_unmask(
	int irq)
{
	/* The model keeps all persistent source owners alive regardless of routing. */
	(void)irq;
}

/*
 * Supplies inert line masking for the executor's shared IRQ helper references.
 */
void
kern_irq_mask(
	int irq)
{
	/* Device-source W1C is modeled separately from interrupt-controller masking. */
	(void)irq;
}

/* Records one failure without hiding subsequent independent lifetime assertions. */
static void
check(
	bool condition,
	const char *what)
{
	/* Retains the first and later failures for a bounded, readable host report. */
	if (!condition) {
		printf("FAIL %s\n", what);
		test_failures++;
	}
}

/* Prepares a running R0 fixture with a preserved console list and registered IRQs. */
static void
setup(
	uint32_t port,
	uint32_t format)
{
	unsigned slot;
	int error;

	/* Clears one stopped model before attaching any source callbacks. */
	memset(&test_display, 0, sizeof(test_display));
	memset(test_hvs, 0, sizeof(test_hvs));
	memset(test_pv, 0, sizeof(test_pv));
	test_display.compositor.mapped = (volatile uint8_t *)test_hvs;
	test_display.compositor.size = sizeof(test_hvs);
	test_display.compositor_irq.registered = true;
	test_display.port = port;
	test_display.channel = 0;
	test_display.screen.physical = 0x01000000U;
	test_display.screen.size = 8294400U;
	test_display.screen.width = 1920;
	test_display.screen.height = 1080;
	test_display.screen.pitch = 7680;
	test_display.screen.format = format;
	test_display.scanout_started = true;
	test_display.max_core_hz = 500000000;
	test_display.console_core_hz = 137600000;
	test_display.refresh_millihz = 60000;
	test_display.adoption_armed = true;
	test_display.first_frame = true;
	for (slot = 0; slot < 2; slot++) {
		/* Each PV owns its own IRQ source and complete mapped register window. */
		test_display.timing[slot].mapped = (volatile uint8_t *)test_pv[slot];
		test_display.timing[slot].size = sizeof(test_pv[slot]);
		test_display.timing_irq[slot].registered = true;
	}

	/* Gives the production service the exact sole channel-zero R0 register state. */
	test_hvs[0x40 / 4] = 0x87800438U;
	test_hvs[0x14 / 4] = 0xc0000000U;
	test_hvs[0x18 / 4] = 0xc0000000U;
	test_hvs[(0x18U - port * 4U) / 4] = 0;
	test_hvs[0x20 / 4] = 43;
	test_hvs[0x30 / 4] = 43;
	test_hvs[0x4000 / 4 + 43] = 0x11223344U;
	test_hvs[0x4000 / 4 + 51] = 0x80000000U;
	test_pv[port][0] = 1;
	test_pv[port][1] = 1;
	error = bcm2711_display_irq_prepare(&test_display);
	check(error == 0, "real source owners prepared");

	/* Creates disjoint borrowed buffers with the established RGB32 geometry. */
	for (slot = 0; slot < 3; slot++) {
		/* Each buffer starts sixteen MiB after the preceding physical allocation. */
		test_frames[slot] = test_display.screen;
		test_frames[slot].physical = 0x02000000U + slot * 0x01000000U;
	}

	/* Resets per-operation observations while retaining the total failure count. */
	test_writes = 0;
	test_cleaned = 0;
	test_barriers = 0;
	test_sleeps = 0;
	test_sleep_mode = SLEEP_ADOPT;
	test_script_step = 0;
	test_publications = 0;
	test_fresh = 0;
	test_released = 0;
	test_released_last = NULL;
	display_test_ticks = 1000;
	test_expected_physical = (uint32_t)test_frames[0].physical;
}

/* Delivers one real PV service against a specified hardware current-list value. */
static void
deliver(
	uint32_t port,
	uint32_t current)
{
	bool handled;

	/* Hardware adoption is independent of the source whose interrupt is delivered. */
	test_hvs[0x30 / 4] = current;
	test_pv[port][0x28 / 4] = 0x80;
	test_in_irq = true;
	handled = test_display.timing_irq[port].service(test_display.timing_irq[port].owner);
	test_in_irq = false;
	check(handled && test_pv[port][0x28 / 4] == 0, "actual PV source acknowledged");
}

/* Tells whether the flip state has a list slot at this address in use. */
static bool
slot_used(
	uint32_t list)
{
	/* Slot addresses are 256 words apart from word 256. */
	if (list < SLOT0 || list > SLOT2 || list % 256U != 0)
		return false;
	return test_display.flip.slots[list / 256U - 1U].used;
}

/* Exercises successive presents, their lists and the console's return on both outputs. */
static void
normal_flips(
	uint32_t port,
	uint32_t format)
{
	struct bcm2711_flip_status status;
	unsigned index;
	int error;

	/* Attachment is read-only and publishes no unverified boot hardware changes. */
	setup(port, format);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0 && test_writes == 0, "read-only attach");
	error = bcm2711_display_flip_attach(&test_display);
	check(error == EBUSY && test_writes == 0, "live owner cannot be reattached");
	for (index = 0; index < 3; index++) {
		/* Each call adopts its list and frees the slot of the one before. */
		test_sleep_mode = SLEEP_SCRIPT;
		test_script_step = 0;
		test_expected_physical = (uint32_t)test_frames[index].physical;
		error = bcm2711_display_flip_present(&test_display, &test_frames[index]);
		check(error == 0, "synchronous present");
		bcm2711_display_flip_snapshot(&test_display, &status);
		check(!status.uncertain && status.pending_count == 0, "normal completion status");
		check(status.adopted_serial == status.state_serial, "every record adopted");
		check(status.active_list == test_hvs[0x30 / 4] && status.used_mask == (1U << (status.active_list / 256U - 1U)), "only the adopted slot is used");
		check(status.active_list == SLOT0 + (index % 2U) * 256U, "slots alternate while one list is out at a time");
		check(status.frames[status.active_list / 256U - 1U].physical == test_frames[index].physical, "borrowed frame identity");
		check(test_hvs[0x4000 / 4 + status.active_list + 8] == 0x80000000U, "the console is left out under an opaque full-screen frame");
	}

	/* Selected old/new frames count, while other-port events do not. */
	check(status.frame_sequence == 6, "selected-source frame sequence");
	check(test_hvs[0x4000 / 4 + 43] == 0x11223344U, "console list remains intact");

	/* Presenting the shown buffer again is a new record of the same frame. */
	test_sleep_mode = SLEEP_ADOPT;
	test_expected_physical = (uint32_t)test_frames[2].physical;
	error = bcm2711_display_flip_present(&test_display, &test_frames[2]);
	check(error == 0, "the shown buffer presented again");

	/* The console comes back and every slot is free; the clock goes down again. */
	error = bcm2711_display_flip_restore(&test_display);
	check(error == 0, "console restored through fresh selected frame");
	bcm2711_display_flip_snapshot(&test_display, &status);
	check(status.used_mask == 0 && status.active_list == 43, "restore retires all lists");
	check(status.core_hz == 137600000U && !status.restoring, "console clock after the restore");
}

/* Exercises timeout, late adoption and repeated restoration without premature release. */
static void
uncertain_flips(
	void)
{
	struct bcm2711_flip_status status;
	struct bcm2711_flip_primary primary;
	struct bcm2711_flip_owner owner;
	struct bcm2711_buffer *held;
	uint64_t serial;
	unsigned before;
	int error;

	/* Establishes one adopted borrowed frame before stalling the next flip. */
	setup(0, 0);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0, "attach timeout scenario");
	owner.session = NULL;
	owner.lease = 0;
	memset(&primary, 0, sizeof(primary));
	primary.frame = test_frames[0];
	held = (struct bcm2711_buffer *)&test_frames[0];
	primary.hold = held;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == 0, "first borrowed frame recorded");
	error = bcm2711_display_flip_wait(&test_display, &owner, serial);
	check(error == 0, "first buffer adopted before timeout");
	test_sleep_mode = SLEEP_STALL;
	primary.frame = test_frames[1];
	primary.hold = (struct bcm2711_buffer *)&test_frames[1];
	test_expected_physical = (uint32_t)test_frames[1].physical;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == 0, "second borrowed frame recorded");
	error = bcm2711_display_flip_wait(&test_display, &owner, serial);
	check(error == ETIMEDOUT, "missing adoption times out");
	bcm2711_display_flip_snapshot(&test_display, &status);
	check(status.uncertain && status.used_mask == 3U && status.retiring_count == 1, "timeout keeps both lists and the replaced image");
	check(test_released == 0, "nothing released after the timeout");

	/* A late selected interrupt cannot silently change the error's retirement policy. */
	deliver(0, SLOT1);
	bcm2711_display_flip_reap(&test_display);
	bcm2711_display_flip_snapshot(&test_display, &status);
	check(status.uncertain && status.used_mask == 3U && test_released == 0, "late adoption keeps both holds");
	before = test_writes;
	error = bcm2711_display_flip_present(&test_display, &test_frames[2]);
	check(error == EBUSY && test_writes == before, "ordinary flip refuses uncertain DMA");
	error = bcm2711_display_flip_restore(&test_display);
	check(error == ETIMEDOUT, "stalled restoration times out");
	bcm2711_display_flip_snapshot(&test_display, &status);
	check(status.uncertain && test_released == 0, "failed restoration never retires holds");

	/* A later successful console commit supplies the missing retirement proof. */
	test_sleep_mode = SLEEP_ADOPT;
	error = bcm2711_display_flip_restore(&test_display);
	check(error == 0, "retry console restoration");
	bcm2711_display_flip_snapshot(&test_display, &status);
	check(!status.uncertain && status.used_mask == 0 && status.primary.hold == NULL, "console adoption ends the uncertainty");
	check(test_released == 2 && status.retiring_count == 0, "console adoption releases both borrowed images");
}

/* Exercises cursor lists: published without waiting, replaced while not taken, coalesced, clipped and hidden. */
static void
cursor_flips(
	void)
{
	struct bcm2711_flip_cursor cursor;
	struct bcm2711_flip_owner owner;
	uint32_t *list;
	unsigned fresh;
	unsigned images;
	int error;

	/* One composed frame is shown from the first slot, over the console. */
	setup(0, 0);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0, "attach cursor scenario");
	owner.session = NULL;
	owner.lease = 0;
	error = bcm2711_display_flip_compose(&test_display, &test_frames[0], 0, 0, true);
	check(error == 0 && test_display.flip.active_list == SLOT0, "composed frame shown");
	list = &test_hvs[0x4000 / 4 + SLOT0];
	check(list[5] == (0xc0000000U | 0x01000000U) && list[10] == 0x2000fff0U && list[16] == 0x80000000U, "console, then a translucent frame");

	/* A shown cursor goes out at once in the next slot, without waiting. */
	memset(&cursor, 0, sizeof(cursor));
	cursor.shown = true;
	cursor.physical = 0x05000000U;
	cursor.width = 64;
	cursor.height = 64;
	cursor.pitch = 256;
	cursor.x = 100;
	cursor.y = 200;
	fresh = test_fresh;
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	list = &test_hvs[0x4000 / 4 + SLOT1];
	check(error == 0 && test_hvs[0x20 / 4] == SLOT1 && test_display.flip.pending_count == 1, "cursor list out at once");
	check(test_fresh == fresh + 1U, "a fresh publication discards a stale vblank");
	check(memcmp(list, &test_hvs[0x4000 / 4 + SLOT0], 16U * 4U) == 0, "same console and frame planes");
	check(list[17] == ((200U << 16) | 100U) && list[19] == ((64U << 16) | 64U), "cursor position and size");
	check(list[21] == (0xc0000000U | 0x05000000U) && list[23] == 256U && list[24] == 0x80000000U, "cursor image and END");

	/* A move while that list is not yet taken replaces it, leaving the vblank status alone. */
	cursor.x = -10;
	cursor.y = -5;
	cursor.physical = 0x05100000U;
	fresh = test_fresh;
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	list = &test_hvs[0x4000 / 4 + SLOT2];
	check(error == 0 && test_hvs[0x20 / 4] == SLOT2 && test_display.flip.pending_count == 2, "the waiting list replaced");
	check(test_fresh == fresh, "a replacement leaves the vblank status");
	check(list[17] == 0U && list[19] == ((59U << 16) | 54U), "clipped at the top-left");
	check(list[21] == (0xc0000000U | (0x05100000U + 5U * 256U + 10U * 4U)), "clipped image start");

	/* Every slot is now possibly read: three images are named, besides the recorded one. */
	images = 0;
	if (bcm2711_display_flip_cursor_named(&test_display, 0x05000000U))
		images++;
	if (bcm2711_display_flip_cursor_named(&test_display, 0x05100000U))
		images++;
	check(images == 2 && slot_used(SLOT0) && slot_used(SLOT1) && slot_used(SLOT2), "three slots in use");

	/* A third change while two lists are out waits for the next adoption. */
	cursor.physical = 0x05200000U;
	cursor.x = 300;
	cursor.y = 400;
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	check(error == 0 && test_hvs[0x20 / 4] == SLOT2 && test_display.flip.list_serial < test_display.flip.state_serial, "coalesced while two lists are out");
	check(bcm2711_display_flip_cursor_named(&test_display, 0x05200000U), "the recorded cursor names its image");

	/* The hardware took the first cursor list: the change goes out over the second, still waiting. */
	deliver(0, SLOT1);
	check(test_display.flip.active_list == SLOT1 && test_display.flip.slots[0].serial > test_display.flip.slots[1].serial, "the older list retired and its slot reused");
	check(test_hvs[0x20 / 4] == SLOT0 && test_display.flip.pending_count == 2, "the coalesced change replaces the waiting list");
	list = &test_hvs[0x4000 / 4 + SLOT0];
	check(list[17] == ((400U << 16) | 300U) && list[21] == (0xc0000000U | 0x05200000U), "the newest cursor in the new list");
	check(!bcm2711_display_flip_cursor_named(&test_display, 0x05000000U) || slot_used(SLOT1), "the first image named only while its list is read");

	/* The hardware skipped the replaced list: both older lists retire at once. */
	deliver(0, SLOT0);
	check(test_display.flip.active_list == SLOT0 && test_display.flip.pending_count == 0, "skip to the newest list");
	check(!slot_used(SLOT1) && !slot_used(SLOT2), "skipped list and old list retired");
	check(test_display.flip.adopted_serial == test_display.flip.state_serial, "every record adopted");
	check(!bcm2711_display_flip_cursor_named(&test_display, 0x05100000U), "a skipped list's image is free");

	/* A hidden cursor leaves the frame alone on top. */
	cursor.shown = false;
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	check(error == 0 && test_hvs[0x4000 / 4 + test_hvs[0x20 / 4] + 16] == 0x80000000U, "hidden cursor list");
	deliver(0, test_hvs[0x20 / 4]);

	/* Hiding a hidden cursor makes no list. */
	fresh = test_publications;
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	check(error == 0 && test_publications == fresh, "no list for an unchanged hidden cursor");

	/* A frame while a cursor list is out is taken at once, not refused. */
	cursor.shown = true;
	cursor.x = 1920;
	cursor.y = 0;
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	check(error == 0 && test_hvs[0x4000 / 4 + test_hvs[0x20 / 4] + 16] == 0x80000000U, "off-output cursor adds no plane");
	check(test_display.flip.pending_count == 1, "cursor list out");
	error = bcm2711_display_flip_compose(&test_display, &test_frames[1], 0, 0, true);
	check(error == 0, "a frame never waits behind a cursor list");
	cursor.width = 65;
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	check(error == EINVAL, "cursor larger than 64 refused");
}

/* Exercises primary records without waiting: replacement, coalescing and the meaning of the adopted number. */
static void
coalesced_flips(
	void)
{
	struct bcm2711_flip_primary primary;
	struct bcm2711_flip_owner owner;
	uint64_t first;
	uint64_t second;
	uint64_t third;
	int error;

	/* Three frames recorded one after the other without an adoption between them. */
	setup(0, 0);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0, "attach coalesce scenario");
	owner.session = NULL;
	owner.lease = 0;
	memset(&primary, 0, sizeof(primary));
	primary.frame = test_frames[0];
	test_expected_physical = (uint32_t)test_frames[0].physical;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &first);
	check(error == 0 && test_hvs[0x20 / 4] == SLOT0, "first frame out");
	primary.frame = test_frames[1];
	test_expected_physical = (uint32_t)test_frames[1].physical;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &second);
	check(error == 0 && test_hvs[0x20 / 4] == SLOT1, "second frame replaces the first's list");
	primary.frame = test_frames[2];
	test_expected_physical = 0;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &third);
	check(error == 0 && test_hvs[0x20 / 4] == SLOT1 && third > second && second > first, "third frame waits for an adoption");

	/* The hardware took the first list: only the first record is adopted, the third goes out now. */
	deliver(0, SLOT0);
	check(test_display.flip.adopted_serial == first, "adopted up to the first record");
	check(test_hvs[0x20 / 4] == SLOT2 && test_hvs[0x4000 / 4 + SLOT2 + 5] == (0xc0000000U | (uint32_t)test_frames[2].physical), "third frame replaces the second's list");

	/* Waiting for the second record returns once the list with the third, made later, is adopted. */
	error = bcm2711_display_flip_wait(&test_display, &owner, second);
	check(error == 0 && test_display.flip.adopted_serial == third, "a later list's adoption adopts every lower record");
	check(!slot_used(SLOT0) && !slot_used(SLOT1) && slot_used(SLOT2), "only the adopted list remains used");
}

/* Exercises borrowed images: each goes, in thread context, once a list made after its replacement is adopted. */
static void
borrowed_flips(
	void)
{
	struct bcm2711_flip_primary primary;
	struct bcm2711_flip_owner owner;
	struct bcm2711_flip_status status;
	struct bcm2711_buffer *images[3];
	uint64_t serial;
	unsigned index;
	int error;

	/* One allocation holding two images borrows twice, one reference per record. */
	setup(0, 0);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0, "attach borrowed scenario");
	owner.session = NULL;
	owner.lease = 0;
	for (index = 0; index < 3; index++)
		images[index] = (struct bcm2711_buffer *)&test_frames[index];
	memset(&primary, 0, sizeof(primary));
	primary.frame = test_frames[0];
	primary.hold = images[0];
	test_expected_physical = (uint32_t)test_frames[0].physical;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == 0, "first image recorded");
	primary.hold = images[0];
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == 0 && test_display.flip.retiring_count == 1, "the same allocation recorded again waits to go once");

	/* The IRQ adopts the list but releases nothing; the waiter does. */
	test_sleep_mode = SLEEP_STALL;
	deliver(0, test_hvs[0x20 / 4]);
	check(test_released == 0 && test_display.flip.retiring_count == 1, "nothing released in the IRQ");
	test_sleep_mode = SLEEP_ADOPT;
	error = bcm2711_display_flip_wait(&test_display, &owner, serial);
	check(error == 0 && test_released == 1 && test_released_last == images[0], "the replaced reference goes after the adoption");

	/* A replacement not yet adopted keeps the replaced image. */
	primary.frame = test_frames[1];
	primary.hold = images[1];
	test_expected_physical = (uint32_t)test_frames[1].physical;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == 0, "second image recorded");
	bcm2711_display_flip_reap(&test_display);
	check(test_released == 1, "the shown image stays while its replacement is not adopted");
	deliver(0, test_hvs[0x20 / 4]);
	bcm2711_display_flip_reap(&test_display);
	check(test_released == 2 && test_released_last == images[0], "the shown image goes after the adoption");
	bcm2711_display_flip_snapshot(&test_display, &status);
	check(status.primary.hold == images[1] && status.retiring_count == 0, "the recorded image stays borrowed");

	/* The console's return lets the recorded image go too. */
	error = bcm2711_display_flip_restore(&test_display);
	check(error == 0 && test_released == 3 && test_released_last == images[1], "the restore releases the recorded image");
}

/* Exercises the lease copy: only the owner records, and a revocation ends a waiting present. */
static void
lease_flips(
	void)
{
	struct bcm2711_flip_primary primary;
	struct bcm2711_flip_owner owner;
	struct bcm2711_flip_owner other;
	struct bcm2711_flip_cursor cursor;
	uint64_t serial;
	int session;
	int error;

	/* A lease owns the planes; the driver's own records and other opens are refused. */
	setup(0, 0);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0, "attach lease scenario");
	owner.session = &session;
	owner.lease = 5;
	other.session = &session;
	other.lease = 6;
	bcm2711_display_flip_own(&test_display, &owner);
	error = bcm2711_display_flip_present(&test_display, &test_frames[0]);
	check(error == EBUSY, "the driver's record refused during a lease");
	memset(&primary, 0, sizeof(primary));
	primary.frame = test_frames[0];
	error = bcm2711_display_flip_record(&test_display, &other, &primary, &serial);
	check(error == EBUSY, "another lease refused");
	memset(&cursor, 0, sizeof(cursor));
	error = bcm2711_display_flip_cursor(&test_display, &other, &cursor);
	check(error == EBUSY, "another lease's cursor refused");
	error = bcm2711_display_flip_revoke(&test_display, &other);
	check(error == EINVAL, "another lease cannot end this one");

	/* The owner's present waits; its release ends the wait with EBUSY and nothing becomes uncertain. */
	test_expected_physical = (uint32_t)test_frames[0].physical;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == 0, "the owner records");
	test_revoked = owner;
	test_sleep_mode = SLEEP_REVOKE;
	error = bcm2711_display_flip_wait(&test_display, &owner, serial);
	check(error == EBUSY && !test_display.flip.uncertain, "a revoked wait returns EBUSY");
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == EBUSY, "a revoked lease records nothing");
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	check(error == EBUSY, "a revoked lease's cursor refused");

	/* The console comes back and the planes go back to the driver. */
	test_sleep_mode = SLEEP_ADOPT;
	error = bcm2711_display_flip_restore(&test_display);
	check(error == 0, "restore after the revocation");
	bcm2711_display_flip_disown(&test_display);
	test_expected_physical = (uint32_t)test_frames[1].physical;
	error = bcm2711_display_flip_present(&test_display, &test_frames[1]);
	check(error == 0, "the driver records again");
}

/* Exercises the copy path's question: which storage the lists or the recorded frame name, and waiting for a list to retire. */
static void
copy_target_flips(
	void)
{
	struct bcm2711_flip_owner owner;
	struct bcm2711_flip_primary primary;
	uint64_t serial;
	unsigned use;
	int error;

	/* Frame 0 shown, frame 1 recorded and out but not taken. */
	setup(0, 0);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0, "attach copy scenario");
	owner.session = NULL;
	owner.lease = 0;
	error = bcm2711_display_flip_present(&test_display, &test_frames[0]);
	check(error == 0, "frame 0 shown");
	memset(&primary, 0, sizeof(primary));
	primary.frame = test_frames[1];
	test_expected_physical = (uint32_t)test_frames[1].physical;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == 0, "frame 1 out");
	use = bcm2711_display_flip_frame_use(&test_display, test_frames[0].physical, test_frames[0].size);
	check(use == BCM2711_FLIP_FRAME_LISTED, "the shown frame is named by its list only");
	use = bcm2711_display_flip_frame_use(&test_display, test_frames[1].physical, test_frames[1].size);
	check(use == BCM2711_FLIP_FRAME_RECORDED, "the recorded frame is named by the state");
	use = bcm2711_display_flip_frame_use(&test_display, test_frames[2].physical, test_frames[2].size);
	check(use == BCM2711_FLIP_FRAME_FREE, "other storage is free");

	/* Waiting for the shown frame to leave every list returns at the next adoption. */
	error = bcm2711_display_flip_wait_unlisted(&test_display, &owner, test_frames[0].physical, test_frames[0].size);
	use = bcm2711_display_flip_frame_use(&test_display, test_frames[0].physical, test_frames[0].size);
	check(error == 0 && use == BCM2711_FLIP_FRAME_FREE, "the old frame is free after the adoption");

	/* A list that does not move is a timeout, not a hang. */
	primary.frame = test_frames[2];
	test_expected_physical = (uint32_t)test_frames[2].physical;
	error = bcm2711_display_flip_record(&test_display, &owner, &primary, &serial);
	check(error == 0, "frame 2 out");
	test_sleep_mode = SLEEP_STALL;
	error = bcm2711_display_flip_wait_unlisted(&test_display, &owner, test_frames[1].physical, test_frames[1].size);
	check(error == ETIMEDOUT, "a stalled list times out");
	test_sleep_mode = SLEEP_ADOPT;
}

/* Describes a shown change of a plane: the primary over the whole output, or an overlay's 200x100 part at (100, 50) shown at (300, 400). */
static void
overlay_change(
	struct bcm2711_flip_change *change,
	uint32_t plane,
	uint64_t serial,
	const struct drv_bcm2711_boot_screen *frame)
{
	memset(change, 0, sizeof(*change));
	change->plane = plane;
	change->serial = serial;
	change->shown = true;
	if (frame != NULL) {
		change->image = true;
		change->frame = *frame;
		change->hold = (struct bcm2711_buffer *)frame;
	}
	if (plane == 0) {
		change->src_width = 1920;
		change->src_height = 1080;
		change->dst_width = 1920;
		change->dst_height = 1080;
		return;
	}
	change->src_x = 100;
	change->src_y = 50;
	change->src_width = 200;
	change->src_height = 100;
	change->dst_x = 300;
	change->dst_y = 400;
	change->dst_width = 200;
	change->dst_height = 100;
}

/*
 * Exercises overlay commits: the list's words, all planes at once, dropped
 * serials, the limits and the budget, hiding, the lease's start and end,
 * the sleeping number wait, and the report's pair and record times.
 */
static void
overlay_flips(
	void)
{
	struct bcm2711_flip_change changes[3];
	struct bcm2711_flip_owner owner;
	struct drv_bcm2711_boot_screen window;
	struct drv_bcm2711_boot_screen band;
	struct bcm2711_flip_cursor cursor;
	uint64_t serial;
	uint64_t adopted;
	uint64_t ticks;
	uint64_t before;
	uint32_t *list;
	int session;
	int error;

	/* A lease, and two uncached images for the overlays. */
	setup(0, 0);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0, "attach overlay scenario");
	owner.session = &session;
	owner.lease = 9;
	bcm2711_display_flip_own(&test_display, &owner);
	window = test_frames[1];
	window.uncached = true;
	band = test_frames[2];
	band.uncached = true;
	test_expected_physical = 0;

	/* An overlay over no primary is refused; the primary goes first. */
	overlay_change(&changes[0], 1, 1, &window);
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == EINVAL, "no overlay over the console alone");
	overlay_change(&changes[0], 0, 1, &test_frames[0]);
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == 0 && changes[0].recorded && serial == test_display.flip.state_serial, "primary committed");
	error = bcm2711_display_flip_wait(&test_display, &owner, serial);
	check(error == 0, "primary adopted");

	/* The window's overlay: its source part at its place, with per-pixel alpha, above the primary. */
	overlay_change(&changes[0], 1, 1, &window);
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	list = &test_hvs[0x4000 / 4 + test_hvs[0x20 / 4]];
	check(error == 0 && test_display.flip.pending_count == 1, "overlay list out without waiting");
	check(list[0] == 0x4800d807U && list[5] == (0xc0000000U | (uint32_t)test_frames[0].physical), "primary first, no console");
	check(list[8] == 0x4800d807U && list[9] == ((400U << 16) | 300U) && list[10] == 0x2000fff0U, "overlay header, place and alpha");
	check(list[11] == ((100U << 16) | 200U) && list[13] == (0xc0000000U | ((uint32_t)window.physical + 50U * 7680U + 100U * 4U)), "overlay size and source start");
	check(list[15] == 7680U && list[16] == 0x80000000U, "overlay pitch, then END");

	/* A serial at or below the last one is dropped and changes nothing. */
	before = test_display.flip.state_serial;
	overlay_change(&changes[0], 1, 1, NULL);
	changes[0].dst_x = 10;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == 0 && !changes[0].recorded && serial == before && test_display.flip.overlays[0].dst_x == 300U, "dropped serial");

	/* Two overlays at once go out in one list, the band above the window. */
	overlay_change(&changes[0], 1, 2, NULL);
	changes[0].dst_x = 10;
	overlay_change(&changes[1], 2, 1, &band);
	changes[1].src_x = 0;
	changes[1].src_y = 0;
	changes[1].dst_x = 0;
	changes[1].dst_y = 0;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 2, &serial, &adopted);
	list = &test_hvs[0x4000 / 4 + test_hvs[0x20 / 4]];
	check(error == 0 && serial == before + 1U && changes[0].recorded && changes[1].recorded, "one record for two planes");
	check(list[9] == ((400U << 16) | 10U) && list[17] == 0U && list[21] == (0xc0000000U | (uint32_t)band.physical) && list[24] == 0x80000000U, "window then band in one list");

	/* Out of the limits: a cached image, a scaled part, a place outside the output, a kept image that is not there. */
	before = test_display.flip.state_serial;
	window.uncached = false;
	overlay_change(&changes[0], 1, 3, &window);
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == EINVAL, "a cached overlay image refused");
	window.uncached = true;
	overlay_change(&changes[0], 1, 3, &window);
	changes[0].dst_width = 400;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == ENOSPC, "no scaling: outside the overlay's limits");
	overlay_change(&changes[0], 1, 3, NULL);
	changes[0].dst_x = 1800;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == EINVAL, "a place outside the output refused");
	overlay_change(&changes[0], 1, 3, NULL);
	changes[0].shown = false;
	changes[0].image = true;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == EINVAL && test_display.flip.state_serial == before, "an image for a hidden plane refused; nothing changed");

	/* The budget: three full planes fit 60 Hz, not 61 Hz; nothing changes when refused. */
	overlay_change(&changes[0], 1, 3, &window);
	changes[0].src_x = 0;
	changes[0].src_y = 0;
	changes[0].dst_x = 0;
	changes[0].dst_y = 0;
	changes[0].src_width = 1920;
	changes[0].src_height = 1080;
	changes[0].dst_width = 1920;
	changes[0].dst_height = 1080;
	changes[1] = changes[0];
	changes[1].plane = 2;
	changes[1].serial = 2;
	changes[1].frame = band;
	changes[1].hold = (struct bcm2711_buffer *)&test_frames[2];
	test_display.refresh_millihz = 61000;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 2, &serial, &adopted);
	check(error == ENOSPC && test_display.flip.state_serial == before, "over the budget: ENOSPC and nothing changed");
	test_display.refresh_millihz = 60000;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 2, &serial, &adopted);
	check(error == 0, "within the budget");

	/* Hiding the band lets its image go after the adoption, in thread context. */
	error = bcm2711_display_flip_wait(&test_display, &owner, serial);
	check(error == 0, "full overlays adopted");
	before = test_released;
	overlay_change(&changes[0], 2, 3, NULL);
	changes[0].shown = false;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == 0 && test_display.flip.retiring_count >= 1U, "hidden band waits to go");
	error = bcm2711_display_flip_wait(&test_display, &owner, serial);
	check(error == 0 && test_released > before && test_released_last == (struct bcm2711_buffer *)&test_frames[2], "the band's image goes after the adoption");

	/* The number wait: EAGAIN without time, the adoption, and EBUSY for another lease. */
	overlay_change(&changes[0], 1, 4, NULL);
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == 0, "a move to wait for");
	error = bcm2711_display_flip_wait_number(&test_display, &owner, serial, 0, &adopted, &ticks);
	check(error == EAGAIN && adopted < serial, "no time and no adoption: EAGAIN");
	error = bcm2711_display_flip_wait_number(&test_display, &owner, serial, 10, &adopted, &ticks);
	check(error == 0 && adopted >= serial && ticks != 0, "the number wait sleeps until the adoption");
	owner.lease = 10;
	error = bcm2711_display_flip_wait_number(&test_display, &owner, serial, 10, &adopted, &ticks);
	check(error == EBUSY, "another lease's wait refused");
	owner.lease = 9;

	/* The report: a list moving the window and the cursor together, and the record's times. */
	test_display.flip.cadence_state = BCM2711_FLIP_CADENCE_ON;
	memset(&cursor, 0, sizeof(cursor));
	cursor.shown = true;
	cursor.physical = 0x05000000U;
	cursor.width = 16;
	cursor.height = 16;
	cursor.pitch = 256;
	cursor.x = 50;
	error = bcm2711_display_flip_cursor(&test_display, &owner, &cursor);
	check(error == 0, "cursor shown");
	overlay_change(&changes[0], 1, 5, NULL);
	changes[0].dst_x = 20;
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == 0 && test_display.flip.times[0].plane == 1 && test_display.flip.times[0].plane_serial == 5, "the record's time joins the ring");
	deliver(0, test_hvs[0x20 / 4]);
	check(test_display.flip.perf.pair_window == 1 && test_display.flip.perf.pair_both == 1, "a list moving window and cursor together counts as a pair");
	check(test_display.flip.times[0].adopted != 0, "the record's adoption time");
	test_display.flip.cadence_state = BCM2711_FLIP_CADENCE_OFF;

	/* The lease's end hides the overlays and lets every image go; the next lease counts serials afresh. */
	before = test_released;
	error = bcm2711_display_flip_revoke(&test_display, &owner);
	check(error == 0, "the lease ends");
	error = bcm2711_display_flip_restore(&test_display);
	check(error == 0 && !test_display.flip.overlays[0].shown && test_display.flip.overlays[0].hold == NULL, "the restore hides the overlays");
	check(test_released >= before + 2U, "the restore releases the primary's and the window's images");
	bcm2711_display_flip_disown(&test_display);
	owner.lease = 11;
	bcm2711_display_flip_own(&test_display, &owner);
	overlay_change(&changes[0], 0, 1, &test_frames[0]);
	error = bcm2711_display_flip_commit(&test_display, &owner, changes, 1, &serial, &adopted);
	check(error == 0 && changes[0].recorded, "serials start again at the next lease");
}

/* Exercises refusals before any publication and the foreign-owner checks. */
static void
refusals(
	void)
{
	struct bcm2711_flip_status status;
	struct drv_bcm2711_boot_screen bad;
	unsigned before;
	int error;

	/* Rejects invalid DMA geometry before any hardware publication. */
	setup(0, 0);
	error = bcm2711_display_flip_attach(&test_display);
	check(error == 0, "attach before refusal cases");
	before = test_writes;
	bad = test_frames[0];
	bad.physical = 0x40000000ULL;
	error = bcm2711_display_flip_present(&test_display, &bad);
	check(error == EINVAL && test_writes == before, "one-GiB DMA limit");
	bad = test_frames[0];
	bad.height++;
	error = bcm2711_display_flip_present(&test_display, &bad);
	check(error == EINVAL && test_writes == before, "mode cannot change");
	bad = test_frames[0];
	bad.size--;
	error = bcm2711_display_flip_present(&test_display, &bad);
	check(error == EINVAL && test_writes == before, "last DMA row bounded");
	bad = test_frames[0];
	bad.format = 2;
	error = bcm2711_display_flip_present(&test_display, &bad);
	check(error == ENOTSUP && test_writes == before, "unknown RGB encoding refused");
	bad = test_display.screen;
	error = bcm2711_display_flip_present(&test_display, &bad);
	check(error == EINVAL && test_writes == before, "console overlap refused");

	/* Detects foreign channel ownership without overwriting its live SRAM. */
	test_hvs[0x50 / 4] = 0x80000000U;
	error = bcm2711_display_flip_present(&test_display, &test_frames[0]);
	check(error == EBUSY && test_publications == 0, "foreign enabled channel untouched");
	bcm2711_display_flip_snapshot(&test_display, &status);
	check(status.uncertain && status.used_mask == 0 && status.primary.hold == NULL, "prepublication error never holds candidate");
	before = test_publications;
	error = bcm2711_display_flip_restore(&test_display);
	check(error == EBUSY && test_publications == before, "recovery cannot reset foreign channel");
	test_hvs[0x50 / 4] = 0;
	test_hvs[0x30 / 4] = 99;
	error = bcm2711_display_flip_restore(&test_display);
	check(error == EBUSY && test_publications == before, "foreign current list untouched");

	/* Refuses attaching a component before the real initial scanout has succeeded. */
	setup(0, 0);
	test_display.scanout_started = false;
	error = bcm2711_display_flip_attach(&test_display);
	check(error == ENODEV && test_writes == 0, "R0 prerequisite preserved");

	/* Refuses attaching a register window that does not reach the third slot. */
	setup(0, 0);
	test_display.compositor.size = 0x4fffU;
	error = bcm2711_display_flip_attach(&test_display);
	check(error == ENODEV, "the window reaches the last list slot");
}

/*
 * Supplies a stationary clock for this fixture, which does not measure runtime.
 */
uint64_t
clock_ticks(
	void)
{
	/* No time elapses in the host's synchronous fake display, except where the cadence scenario sets it. */
	return test_clock;
}

/* Reports the cost report as off by default; the cadence scenario turns it on directly. */
bool
bcm2711_stage_perf_on(
	void)
{
	/* No boot line in this model. */
	return false;
}

/*
 * Discards production diagnostics while retaining real error returns in the fixture.
 */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	/* Keeps only the cadence scenario's line. */
	if (!test_logging)
		return;
	va_start(arguments, format);
	vsnprintf(test_log, sizeof(test_log), format, arguments);
	va_end(arguments);
}

/*
 * Measures the cadence of cursor moves and sets (WS213): nothing without
 * v3d.perf=1, then a two-second window of moves every 8 ms and four sets,
 * reported once with rates and interval minimum, median and maximum.
 */
static void
cadence_report(
	void)
{
	uint64_t milliseconds;

	/* Off: an event keeps nothing. */
	setup(0, 0);
	test_display.flip.cadence_state = BCM2711_FLIP_CADENCE_OFF;
	test_clock = KERN_CLOCK_HZ;
	bcm2711_display_flip_note(&test_display, BCM2711_FLIP_CADENCE_MOVE);
	check(test_display.flip.cadence[BCM2711_FLIP_CADENCE_MOVE].count == 0, "cadence off keeps nothing");

	/* On: moves every 8 ms from 1 ms, sets at 100, 110, 130 and 160 ms. */
	test_display.flip.cadence_state = BCM2711_FLIP_CADENCE_ON;
	test_logging = true;
	test_log[0] = '\0';
	for (milliseconds = 1; milliseconds <= 2001U; milliseconds++) {
		/* Each event at its millisecond. */
		test_clock = milliseconds * KERN_CLOCK_HZ / 1000U;
		if (milliseconds == 100U || milliseconds == 110U || milliseconds == 130U || milliseconds == 160U)
			bcm2711_display_flip_note(&test_display, BCM2711_FLIP_CADENCE_SET);
		if ((milliseconds - 1U) % 8U == 0)
			bcm2711_display_flip_note(&test_display, BCM2711_FLIP_CADENCE_MOVE);
		if (milliseconds < 2001U)
			check(test_log[0] == '\0', "no cadence report before two seconds");
	}
	check(strcmp(test_log, "bcm2711: cadence ms=2000 per_s/min/med/max_us set=2/10000/20000/30000 move=125/8000/8000/8000 flip=0/0/0/0 overlay=0/0/0/0\n") == 0, "cadence report");
	check(!test_display.flip.cadence_reporting && test_display.flip.cadence[BCM2711_FLIP_CADENCE_MOVE].count == 0, "cadence window restarts");

	/* The next window measures from each kind's last event. */
	test_log[0] = '\0';
	test_clock = 2009U * KERN_CLOCK_HZ / 1000U;
	bcm2711_display_flip_note(&test_display, BCM2711_FLIP_CADENCE_MOVE);
	check(test_log[0] == '\0' && test_display.flip.cadence[BCM2711_FLIP_CADENCE_MOVE].stored == 1, "next window");
	check(test_display.flip.cadence[BCM2711_FLIP_CADENCE_MOVE].intervals[0] == 8000U, "interval across windows");

	/* The flip report counts records, lists, replacements and coalesced changes. */
	test_log[0] = '\0';
	test_display.flip.perf.start = 1;
	test_display.flip.perf.primary_records = 3;
	test_display.flip.perf.cursor_records = 7;
	test_display.flip.perf.lists = 8;
	test_display.flip.perf.replaced = 4;
	test_display.flip.perf.coalesced = 2;
	test_display.underruns = 5;
	test_display.flip.perf.underruns = 1;
	test_clock = 4000U * KERN_CLOCK_HZ / 1000U;
	bcm2711_display_flip_report(&test_display, true, 0, 0, 0);
	check(strstr(test_log, "records primary=3 cursor=7 overlay=0 dropped=0 lists=8 replaced=4 coalesced=2 underruns=4 pair=0/0\n") != NULL, "flip report counts");
	test_logging = false;
	test_clock = 0;
	printf("WS213 display cadence: off by default, two-second report of rates and min/median/max intervals: %s\n", test_failures == 0 ? "PASS" : "FAIL");
}
