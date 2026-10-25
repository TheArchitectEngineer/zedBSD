/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The kernel under the GHCI driver in the end-to-end test: the PCI glue's
 * part (the register window, the local window and the interrupt are the
 * link's), the locks and wait queues of one thread, the clock of the
 * simulation, the heap, and the service thread, which runs whenever a
 * wait gives it room; and what the display case reads of the display.
 *
 * The test has one thread.  A sleep of the driver runs the GPU's clock
 * and calls the interrupt handler when the line is high, as an interrupt
 * would come in while the thread sleeps.  The service thread's round runs
 * in a sleep or a pump only when none of the device's lock, its ring's
 * lock and its display's lock is held, as the real thread would block on
 * them; a wait that needs the service thread while holding them would
 * hang the kernel too.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/ghci/ghci.h"
#include "kern/clock.h"
#include "kern/cred.h"
#include "kern/poll.h"
#include "drivers/gpu/ghci/hcr.h"

#include "ghci-e2e.h"
#include "link.h"

/* The register window's size: 128 KiB, as BAR0. */
#define REGISTER_WORDS			32768U

/* Where the local window would be on the host's bus (only recorded by the driver). */
#define WINDOW_PHYSICAL			0xe0000000ULL

/* The record page: 64 bytes past the fence word, plane 0's 8 bytes first (display-scan.c). */
#define RECORD_OFFSET			64U

/* The cycles one look of a waiting thread lets the GPU run, at most. */
#define PUMP_CYCLES			2000U

/*
 * The device the test drives.
 *
 * It is filled by e2e_board_start() as the PCI glue fills it, and lives
 * as long as the process.
 */
struct ghci_device e2e_device;

/*
 * The register window's stand-in: only its address is used, as the base
 * that an MMIO access's offset is taken from; the registers themselves
 * are the link's.
 */
static uint32_t register_window[REGISTER_WORDS];

/*
 * The cycles of the GPU per tick of the kernel's clock (KERN_CLOCK_HZ),
 * from the link's clock rate when the board starts.
 */
static uint64_t cycles_per_tick;

/*
 * Whether the service thread's round is running now: a sleep inside it
 * runs the clock and the interrupt but not a second round.
 */
static int service_running_now;

/* The tick the service thread's last round ran at, for its period. */
static uint64_t service_last_tick;

static void board_round(void);
static void service_round(void);

/*
 * Starts the simulated board and the driver on it.
 */
int
e2e_board_start(void)
{
	uint32_t local_base;
	uint32_t local_bytes;
	uint32_t window_bytes;
	uint8_t *local;
	int error;

	/* The GPU of the KC705 board, reset. */
	error = atg_link_start();
	if (error != 0) {
		printf("FAIL e2e: the link could not start\n");
		return EIO;
	}

	/* The kernel's clock counts in ticks of the GPU's cycles. */
	cycles_per_tick = atg_link_clock_hz() / KERN_CLOCK_HZ;
	if (cycles_per_tick == 0U)
		cycles_per_tick = 1U;

	/* The bus fields, as pci-attach.c fills them: BAR0 and BAR2. */
	local = atg_link_local(&local_base, &local_bytes, &window_bytes);
	memset(&e2e_device, 0, sizeof(e2e_device));
	e2e_device.registers = (volatile uint8_t *)register_window;
	e2e_device.register_bytes = sizeof(register_window);
	e2e_device.window = local;
	e2e_device.window_physical = WINDOW_PHYSICAL;
	e2e_device.window_bytes = window_bytes;

	/* The driver's start: identification, reset, firmware, memory, ring, executor, registration. */
	error = ghci_device_start(&e2e_device);
	if (error != 0) {
		printf("FAIL e2e: the driver did not start (%d)\n", error);
		return error;
	}

	/* Succeeded: the node is registered with the fake core. */
	return 0;
}

/*
 * Stops the driver and the simulation.
 */
void
e2e_board_stop(void)
{
	int error;

	/* The detach, which needs every session closed. */
	error = ghci_device_stop(&e2e_device);
	if (error != 0)
		printf("  e2e: the driver did not stop (%d)\n", error);

	/* The simulation ends. */
	atg_link_stop();
}

/*
 * Runs the board for a while as the kernel would between two looks of a
 * waiting thread.
 */
void
e2e_board_pump(void)
{
	/* One round of the clock and the interrupt, then the service thread's turn. */
	board_round();
	service_round();
}

/*
 * The simulated time in nanoseconds since the board started.
 */
uint64_t
e2e_board_now_ns(void)
{
	struct atg_link_stats stats;
	uint64_t nanoseconds;

	/* The cycles run, in nanoseconds of the GPU's clock. */
	atg_link_stats(&stats);
	nanoseconds = stats.cycles * 1000000000ULL / atg_link_clock_hz();
	return nanoseconds;
}

/*
 * Reads what the display shows: the controller's registers, plane 0's
 * record and the driver's image, as nothing else runs.
 */
void
e2e_display_state(
	struct e2e_display_state *state)
{
	struct ghci_display *display;
	uint32_t registers;
	uint64_t record;

	/* Nothing when the GPU has no display. */
	memset(state, 0, sizeof(*state));
	display = &e2e_device.display;
	state->present = display->present;
	if (!display->present)
		return;

	/* The controller's registers, through the DISP2 window. */
	registers = display->registers;
	state->fb_base = ghci_read(&e2e_device, registers + GHCI_DC_FB_BASE);
	state->fb_stride = ghci_read(&e2e_device, registers + GHCI_DC_FB_STRIDE);
	state->ctrl = ghci_read(&e2e_device, registers + GHCI_DC_CTRL);
	state->status = ghci_read(&e2e_device, registers + GHCI_DC_STATUS);
	state->frame_count = ghci_read(&e2e_device, registers + GHCI_DC_FRAME_COUNT);
	state->scan_sum = ghci_read(&e2e_device, registers + GHCI_DC_SCAN_SUM);
	state->scan_words = ghci_read(&e2e_device, registers + GHCI_DC_SCAN_WORDS);

	/* Plane 0's record {value, FRAME_COUNT}, in one 64-bit read as the driver reads it. */
	record = *(volatile uint64_t *)(e2e_device.fence_page->view + RECORD_OFFSET);
	state->record_value = (uint32_t)record;
	state->record_frame = (uint32_t)(record >> 32);

	/* The driver's own view of what it shows. */
	state->shown_base = display->shown.base;
	state->shown_stride = display->shown.stride;
	state->shown_copy_frame = display->shown.copy_frame;
	state->enabled = display->enabled;
	state->adopted = display->adopted;
}

/*
 * Runs the board until the display controller has begun frames more
 * frames, or the time is up.
 */
int
e2e_display_frames(
	uint32_t frames,
	uint64_t timeout_ns)
{
	uint32_t registers;
	uint32_t first;
	uint32_t now;
	uint64_t deadline;

	/* A GPU without a display begins no frame. */
	if (!e2e_device.display.present)
		return ETIMEDOUT;

	/* Rounds of the board, as a waiting thread's, until FRAME_COUNT moved on far enough. */
	registers = e2e_device.display.registers;
	first = ghci_read(&e2e_device, registers + GHCI_DC_FRAME_COUNT);
	deadline = e2e_board_now_ns() + timeout_ns;
	for (;;) {
		now = ghci_read(&e2e_device, registers + GHCI_DC_FRAME_COUNT);
		if (now - first >= frames)
			break;
		if (e2e_board_now_ns() >= deadline)
			return ETIMEDOUT;
		e2e_board_pump();
	}

	/* Succeeded: the frames began. */
	return 0;
}

/* The kernel's MMIO reads: the register window is the link's. */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t offset;
	uint32_t value;

	/* The register's offset in BAR0, read by a transaction on the GPU's register port. */
	offset = (uint32_t)((const volatile uint8_t *)address - (const volatile uint8_t *)register_window);
	value = atg_link_read(offset);

	/* Succeeded: the register's value. */
	return value;
}

/* The kernel's MMIO writes. */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	uint32_t offset;

	/* The register's offset in BAR0, written by a transaction on the GPU's register port. */
	offset = (uint32_t)((volatile uint8_t *)address - (volatile uint8_t *)register_window);
	atg_link_write(offset, value);
}

/* The barriers: the link's accesses are in program order already. */
void
kern_io_write_barrier(void)
{
}

void
kern_io_read_barrier(void)
{
}

/* The mutexes: one thread, so a lock only counts its holders (the service thread looks at the count). */
int
mutex_init(
	struct mutex *mutex,
	enum lock_rank rank,
	const char *name)
{
	(void)rank;
	(void)name;

	/* Not held. */
	memset(mutex, 0, sizeof(*mutex));
	return 0;
}

void
mutex_lock(
	struct mutex *mutex)
{
	/* One more holder. */
	mutex->locked++;
}

void
mutex_unlock(
	struct mutex *mutex)
{
	/* One holder fewer. */
	mutex->locked--;
}

/* The spinlocks: one thread, and the interrupt handler is called only from a sleep. */
void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	(void)rank;
	(void)name;
	memset(lock, 0, sizeof(*lock));
}

unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	(void)lock;
	return 0;
}

void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	(void)lock;
	(void)enabled;
}

/* The wait queues: a sleep is one round of the board, after which the sleeper looks again. */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	(void)name;
	memset(queue, 0, sizeof(*queue));
}

uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	(void)queue;
	return 0;
}

int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *condition_lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	(void)queue;
	(void)condition_lock;
	(void)observed;
	(void)deadline;
	(void)flags;

	/* The clock runs, the interrupt comes, the service thread may run; the sleeper looks again. */
	board_round();
	service_round();
	return 0;
}

void
waitq_wake_all(
	struct wait_queue *queue)
{
	(void)queue;
}

/* The kernel's clock: the GPU's cycles in ticks. */
uint64_t
clock_ticks(void)
{
	struct atg_link_stats stats;

	/* The cycles run so far. */
	atg_link_stats(&stats);
	return stats.cycles / cycles_per_tick;
}

/* The scheduler's sleep: the board runs until the tick comes (one round at least). */
void
sched_sleep(
	uint64_t timeout_tick)
{
	uint64_t now;

	/* Rounds until the clock reaches the tick. */
	do {
		board_round();
		service_round();
		now = clock_ticks();
	} while (now < timeout_tick);
}

/* The caller is root: its sessions are not protected (the harness is a tool). */
const struct ucred *
cred_current(void)
{
	return NULL;
}

int
cred_is_superuser(
	const struct ucred *credentials)
{
	(void)credentials;
	return 1;
}

/* The log goes to standard output. */
int
kern_logf(
	const char *format,
	...)
{
	struct atg_link_stats stats;
	va_list arguments;
	int written;

	/* One line of the driver's log, marked as such, with the GPU's cycle. */
	atg_link_stats(&stats);
	va_start(arguments, format);
	printf("  log %llu: ", (unsigned long long)stats.cycles);
	written = vprintf(format, arguments);
	va_end(arguments);
	return written;
}

/* The kernel's heap is the host's. */
void *
kern_malloc(
	size_t size)
{
	return malloc(size);
}

void *
kern_calloc(
	size_t count,
	size_t size)
{
	return calloc(count, size);
}

void
kern_free(
	void *pointer)
{
	free(pointer);
}

/*
 * RAM below 4 GiB for the GPU: the board has no path to the host's memory
 * (its GPU reaches only its DDR3), so a driver that asks for some is told
 * there is none.
 */
int
kern_pmem_alloc_limited(
	size_t size,
	size_t alignment,
	uint64_t max_address,
	size_t boundary,
	struct kern_pmem *run)
{
	(void)size;
	(void)alignment;
	(void)max_address;
	(void)boundary;
	(void)run;

	/* Nothing to hand out. */
	printf("  e2e: the driver asked for host memory, which the board cannot reach\n");
	return ENOMEM;
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
	return NULL;
}

/* The service thread is not a thread here: its rounds run in the sleeps and pumps. */
int
ghci_service_start(
	struct ghci_device *device)
{
	/* The device says its thread runs, so the stop asks it to leave. */
	device->service_running = 0;
	return 0;
}

/* The waiters on poll readiness: the fake core's poll looks again after each round anyway. */
void
poll_notify(void)
{
}

/* Runs the GPU's clock for one look and calls the interrupt handler when the line is high. */
static void
board_round(void)
{
	int high;

	/* Until the line rises, at most one look's cycles (a cycle at least while it stays high). */
	high = atg_link_irq();
	if (high) {
		(void)atg_link_run(1U, 0);
	} else {
		(void)atg_link_run(PUMP_CYCLES, 1);
	}

	/* The interrupt, as it would come while the thread sleeps. */
	high = atg_link_irq();
	if (high && e2e_device.registers != NULL)
		(void)ghci_cp_interrupt(&e2e_device);
}

/* Runs a round of the service thread when it has work or its period passed, and nothing holds the device. */
static void
service_round(void)
{
	unsigned work;
	uint64_t now;
	uint64_t period;

	/* A round inside a round, or one the device's locks would block, does not run. */
	if (service_running_now)
		return;
	if (e2e_device.lock.locked != 0U ||
	    e2e_device.cp_lock.locked != 0U ||
	    e2e_device.display.lock.locked != 0U)
		return;

	/* Nothing to do before the period ends. */
	now = clock_ticks();
	period = kern_ms_to_ticks(GHCI_SERVICE_PERIOD_MS);
	work = e2e_device.service_work;
	if (work == 0U && now - service_last_tick < period)
		return;

	/* The round: the work taken as the thread takes it. */
	e2e_device.service_work = 0U;
	service_last_tick = now;
	service_running_now = 1;
	ghci_service_step(&e2e_device, work);
	service_running_now = 0;
}
