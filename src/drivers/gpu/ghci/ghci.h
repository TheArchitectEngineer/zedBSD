/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The generic driver of a GPU with the GPU Host Controller Interface
 * (GHCI 1.0): the device, the ring, the fences, the interrupt, the
 * recovery and the parts the driver's files share.
 *
 * One register window is one device.  The driver knows a GPU only by its
 * registers: GHCI_MAGIC and the major version say it is GHCI 1.0, and the
 * capability registers and the extended capability list say what it has.
 * Nothing in the driver names a vendor or branches on a minor version.
 * The bus glue (pci-attach.c) maps the register window and the optional
 * local window and fills bus_ops, which the start calls to take the
 * interrupt and to turn on bus mastering once the GPU has been reset; it
 * then hands the device to ghci_device_start().  On a bus that delivers
 * no interrupt of the GPU's the device is polled: a thread does the
 * interrupt handler's work every GHCI_POLL_MS, and IRQ_ENABLE stays zero.
 *
 * The ring.  The driver alone writes the ring (host memory, 256 KiB).
 * Every path that places packets passes the gate (ghci_cp_enter): while a
 * recovery runs, the gate lets go of every lock the caller holds and
 * waits for the recovery to end, and the caller starts again.  Every
 * submission ends with a CMD_FENCE that writes a 64-bit sequence into the
 * fence page and raises the fence interrupt; a ticket is that sequence
 * and the reset generation it was placed in.
 *
 * Faults.  Every fault, every watchdog expiry, a hang and a failed check
 * of the device are recovered by a reset of the device, whatever
 * STATUS.FAULTED says; only the service thread recovers.  A reset ends
 * every ticket placed before it that had not passed with an error.  A
 * device that does not come back from a reset, or faults too often, is
 * lost: every later submission fails, and the memory the GPU may still
 * reach is never given back while bus mastering is on.
 *
 * Locks, in the order they are taken: lock (a mutex: the sessions and
 * the device's life), display_lock (a mutex: the display), cp_lock (a
 * mutex: the ring, the doorbell, the reset and the submission log) and
 * irq_lock (a spinlock: the completed fence, the generations, the
 * recovery's mark and records, and the service thread's work).  Nothing
 * waits for a ticket while it holds lock or display_lock.
 */

#ifndef DRIVERS_GPU_GHCI_H
#define DRIVERS_GPU_GHCI_H

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <kern/waitq.h>

#include "ghci-regs.h"
#include "ghci-words.h"

/* The ring: 256 KiB of host memory (a power of two between 4 KiB and 16 MiB). */
#define GHCI_RING_BYTES			(256U * 1024U)

/* The CP_HEAP of a GPU without a page-table walker: session 0's 16 KiB root only. */
#define GHCI_FLAT_HEAP_BYTES		GHCI_ROOT_BYTES_VA32

/* The smallest CP_HEAP the start settles for when the full one cannot be had. */
#define GHCI_HEAP_MIN_BYTES		(64U * 1024U)

/* The bytes of the fence page; the device's fence is its first 64-bit word. */
#define GHCI_FENCE_PAGE_BYTES		GHCI_PAGE_BYTES

/*
 * How long the driver waits for the hardware: the memory to become ready
 * after a reset, the processor to halt, and a fence the driver itself
 * places to see that the processor runs (the liveness).
 */
#define GHCI_MEMORY_WAIT_MS		1000U
#define GHCI_HALT_WAIT_MS		1000U
#define GHCI_LIVENESS_MS		1000U

/* The watchdog time the driver programs into WDT_CYCLES. */
#define GHCI_WATCHDOG_MS		2000U

/*
 * The service thread wakes at least this often, and a fence that is
 * behind the last one placed while neither it nor RING_RPTR moves for
 * GHCI_HANG_MS counts as a hang.
 */
#define GHCI_SERVICE_PERIOD_MS		100U
#define GHCI_HANG_MS			10000U

/* How often a polled device, and a wait for the ring's room, look at the device. */
#define GHCI_POLL_MS			1U

/*
 * The faults that make a device lost: three faults no session caused,
 * or two CP_HEAP exhaustions, within GHCI_FAULT_STORM_MS; and recoveries
 * whose checks fail GHCI_RECOVERY_TRIES times in a row.
 */
#define GHCI_FAULT_STORM_MS		60000U
#define GHCI_FAULT_STORM		3U
#define GHCI_OOM_STORM			2U
#define GHCI_RECOVERY_TRIES		3U

/* The submissions the driver remembers until their fences pass. */
#define GHCI_SUBMIT_LOG			1024U

/* The fault records the interrupt keeps for the service thread. */
#define GHCI_FAULT_RECORDS		8U

/* The ends of the reset generations the driver remembers, to answer an old ticket. */
#define GHCI_RESET_HISTORY		16U

/* The values the start writes to SCRATCH and reads back. */
#define GHCI_SCRATCH_FIRST		0x5a5a5a5aU
#define GHCI_SCRATCH_SECOND		0xa5a5a5a5U

/* The work the service thread is woken for. */
#define GHCI_WORK_RECLAIM		1U
#define GHCI_WORK_RECOVER		2U
#define GHCI_WORK_STOP			4U
#define GHCI_WORK_DISPLAY		8U

/* The outer locks a caller of the gate holds, which the gate lets go of when it waits. */
#define GHCI_HOLD_DEVICE		1U
#define GHCI_HOLD_DISPLAY		2U

/*
 * The life of a device (design section 6.1): the start, the ring running,
 * a recovery resetting the device and replaying what it lost, a device
 * that did not come back, and a device whose detach has finished.
 */
enum ghci_device_state {
	GHCI_STATE_ATTACHING = 0,
	GHCI_STATE_RUNNING,
	GHCI_STATE_RECOVERING,
	GHCI_STATE_REPLAYING,
	GHCI_STATE_LOST,
	GHCI_STATE_DETACHED
};

/* What the gate answers: the ring is the caller's, a recovery ran, or the device is lost. */
enum ghci_gate {
	GHCI_GATE_OPEN = 0,
	GHCI_GATE_RETRY,
	GHCI_GATE_LOST
};

/* The state of a ticket: its fence has not passed, it has, or a reset ended it. */
enum ghci_ticket_state {
	GHCI_TICKET_PENDING = 0,
	GHCI_TICKET_DONE,
	GHCI_TICKET_FAILED
};

/* The kinds of memory an allocation is in: host RAM, or the GPU's local memory. */
enum ghci_memory_kind {
	GHCI_MEMORY_HOST = 1,
	GHCI_MEMORY_LOCAL
};

struct ghci_device;

/*
 * One run of physically contiguous memory the GPU reaches.
 *
 * bus is the address the GPU uses (on the platforms the driver knows,
 * the processor's physical address), bytes its size in whole pages, view
 * the processor's kernel view (the direct map, or an uncached mapping on
 * a bus that does not snoop the caches) and run the RAM it is.  The
 * allocation lives until its owner gives it back after the GPU is done
 * with it; on a lost device it waits in the quarantine instead.
 */
struct ghci_allocation {
	struct ghci_allocation *next;
	enum ghci_memory_kind kind;
	uint64_t bus;
	uint64_t bytes;
	uint8_t *view;
	struct kern_pmem run;
};

/*
 * A submission's place in the fence order: the 64-bit sequence of the
 * CMD_FENCE placed after it and the reset generation it was placed in.
 * A reset ends every ticket of an older generation whose fence had not
 * passed with an error.
 */
struct ghci_ticket {
	uint64_t seq;
	uint64_t generation;
};

/*
 * The end of one reset generation: the fence the device had written when
 * the reset ended it.  A ticket of that generation passed if its sequence
 * is at most completed, and failed otherwise.
 */
struct ghci_reset_record {
	uint64_t generation;
	uint64_t completed;
};

/*
 * One submission whose fence has not been seen passed: the dwords of the
 * ring it took, its fence's sequence and the session it ran in (0 for the
 * driver).  The recovery reads the log to find which submission a fault
 * of a GPU without a page-table walker belongs to.
 */
struct ghci_submit_entry {
	uint32_t start;
	uint32_t end;
	uint64_t seq;
	uint32_t session;
};

/*
 * What the interrupt saw of one fault or watchdog expiry, or what the
 * service saw of a hang, kept for the service thread with the recovery
 * generation it happened in: a record of an older generation is of a
 * fault a finished recovery has already reset.
 */
struct ghci_fault_record {
	uint64_t generation;
	uint32_t irq_status;
	uint32_t status;
	uint32_t fault_info;
	uint64_t fault_address;
	uint32_t session_active;
	uint32_t rptr;
	uint32_t cp_pc;
	uint32_t cp_status;
	uint64_t fence;
	int hang;
};

/*
 * The ring: its memory, its size in dwords and the next dword the driver
 * writes (cp_lock).
 */
struct ghci_ring {
	struct ghci_allocation *memory;
	uint32_t dwords;
	uint32_t wptr;
};

/*
 * The operations the bus glue gives the start: taking the interrupt (or
 * leaving the device polled), letting it go, and turning bus mastering on
 * or off.  The start calls them only after the GPU has been reset, so no
 * interrupt or DMA a kernel before this one left behind reaches this one.
 */
struct ghci_bus_ops {
	int (*interrupt_take)(struct ghci_device *device);
	void (*interrupt_release)(struct ghci_device *device);
	int (*bus_master)(struct ghci_device *device, int on);
};

/*
 * One open of the device by the GPU core.  The device's lock protects
 * the list it is on.
 */
struct ghci_session {
	struct ghci_device *device;
	struct ghci_session *next;
};

/*
 * One GHCI device.
 *
 * The bus glue fills the bus fields before ghci_device_start(), and keeps
 * the structure until ghci_device_stop() has returned 0.  The capability
 * fields are read once at the start and never change.  The comment of
 * each group says which lock protects it.
 */
struct ghci_device {
	/*
	 * The bus, filled by the glue: its private record, its operations,
	 * the register window, the local window, the function's IDs, whether
	 * the device is polled, whether host memory is reached uncached (the
	 * bus does not snoop the caches), and whether bus mastering was on
	 * when the driver found the function (a boot display may be using it).
	 */
	void *bus;
	const struct ghci_bus_ops *bus_ops;
	volatile uint8_t *registers;
	uint32_t register_bytes;
	volatile uint8_t *window;
	uint64_t window_physical;
	uint64_t window_bytes;
	uint16_t pci_vendor;
	uint16_t pci_device;
	int polled;
	int host_uncached;
	int bus_master_found;

	/*
	 * What the start turned on, so that the detach and a failed start
	 * undo only that: the interrupt, and bus mastering.
	 */
	int interrupt_taken;
	int bus_master_set;

	/* The locks, the wait queues and the threads. */
	struct mutex lock;
	struct mutex display_lock;
	struct mutex cp_lock;
	struct spinlock irq_lock;
	struct wait_queue fence_wake;
	struct wait_queue ring_wake;
	struct wait_queue gate_wake;
	struct wait_queue service_wake;
	struct wait_queue poll_wake;
	unsigned service_work;
	int service_running;
	int poll_stop;
	int poll_running;

	/* The identification and the capabilities, read at the start. */
	uint32_t version;
	struct ghci_caps caps;
	uint32_t wdt_cycles;
	uint32_t reset_wait_ms;
	int executor;
	char name[32];

	/* The registration with the GPU core and the sessions (lock). */
	struct drv_gpu_ops ops;
	struct drv_gpu_device *gpu;
	struct ghci_session *sessions;
	unsigned session_count;
	int withdrawing;

	/*
	 * The device's own memory: the ring, the fence page and CP_HEAP,
	 * from the start to the detach.  quarantined is memory the GPU may
	 * still reach on a lost device, given back only once bus mastering
	 * is off.
	 */
	struct ghci_ring ring;
	struct ghci_allocation *fence_page;
	struct ghci_allocation *heap;
	struct ghci_allocation *quarantined;

	/*
	 * The ring's bookkeeping (cp_lock): the sequence of the last fence
	 * placed, and the submissions whose fences have not been seen passed,
	 * oldest first, in a circle of GHCI_SUBMIT_LOG.
	 */
	uint64_t fence_placed;
	struct ghci_submit_entry submit_log[GHCI_SUBMIT_LOG];
	unsigned submit_first;
	unsigned submit_count;

	/*
	 * What the interrupt, the gate and the service share (irq_lock).
	 *
	 * state is the device's life; it changes under cp_lock as well.
	 * irq_mask is the IRQ_ENABLE the handler restores after masking.
	 * completed is the fence the device has written, as the driver has
	 * accepted it; it does not move while recovering, so that no ticket
	 * passes on a fence a fault may have made meaningless.  issued is the
	 * last fence placed, which the hang check compares.  generation is
	 * the tickets' reset generation.  recovering is the mark that closes
	 * the gate; recovery_generation stamps the fault records and
	 * gate_generation wakes the gate when a recovery ends.  fault_count
	 * counts every fault and watchdog the handler saw, fence_interrupts
	 * every fence interrupt.  The records are the faults the service
	 * thread has not taken yet.
	 */
	enum ghci_device_state state;
	uint32_t irq_mask;
	uint64_t completed;
	uint64_t issued;
	uint64_t generation;
	int recovering;
	uint64_t recovery_generation;
	uint64_t gate_generation;
	unsigned fault_count;
	unsigned fence_interrupts;
	struct ghci_fault_record faults[GHCI_FAULT_RECORDS];
	unsigned fault_records;
	unsigned faults_dropped;
	struct ghci_reset_record history[GHCI_RESET_HISTORY];

	/*
	 * The hang check (irq_lock): the fence and RING_RPTR it last saw,
	 * and the tick it last saw either move.
	 */
	uint64_t progress_fence;
	uint32_t progress_rptr;
	uint64_t progress_tick;

	/*
	 * The service thread's own counts: the checks that failed in a row,
	 * the ticks of the recent faults no session caused and of the recent
	 * CP_HEAP exhaustions, and the resets done.
	 */
	unsigned recovery_failures;
	uint64_t storm_ticks[GHCI_FAULT_STORM];
	unsigned storm_count;
	uint64_t oom_ticks[GHCI_OOM_STORM];
	unsigned oom_count;
	unsigned resets;
};

int ghci_device_start(struct ghci_device *device);
int ghci_device_stop(struct ghci_device *device);
uint32_t ghci_read(struct ghci_device *device, uint32_t offset);
void ghci_write(struct ghci_device *device, uint32_t offset, uint32_t value);
void ghci_sleep_ms(unsigned milliseconds);
int ghci_wait_register(struct ghci_device *device, uint32_t offset, uint32_t mask, uint32_t value, unsigned milliseconds);
uint64_t ghci_fence_read(struct ghci_device *device);
void ghci_fence_write(struct ghci_device *device, uint64_t value);

int ghci_cp_start(struct ghci_device *device);
void ghci_cp_stop(struct ghci_device *device);
enum ghci_gate ghci_cp_enter(struct ghci_device *device, unsigned held, int wait);
void ghci_cp_leave(struct ghci_device *device);
int ghci_cp_place(struct ghci_device *device, const uint32_t *packets, uint32_t dwords);
void ghci_cp_doorbell(struct ghci_device *device);
int ghci_cp_place_fence(struct ghci_device *device, uint32_t session, uint32_t start, struct ghci_ticket *ticket);
int ghci_cp_submit(struct ghci_device *device, uint32_t session, const uint32_t *packets, uint32_t dwords, struct ghci_ticket *ticket);
int ghci_cp_wait(struct ghci_device *device, const struct ghci_ticket *ticket, unsigned milliseconds);
enum ghci_ticket_state ghci_cp_ticket(struct ghci_device *device, const struct ghci_ticket *ticket);
int ghci_cp_await_fence(struct ghci_device *device, uint64_t seq, unsigned milliseconds);
void ghci_cp_ring_reset(struct ghci_device *device);
void ghci_cp_run(struct ghci_device *device);
void ghci_cp_irq_enable(struct ghci_device *device);
int ghci_cp_interrupt(void *argument);

int ghci_service_start(struct ghci_device *device);
void ghci_service_main(void *argument);
void ghci_service_step(struct ghci_device *device, unsigned work);
void ghci_service_stop(struct ghci_device *device);
void ghci_service_wake(struct ghci_device *device, unsigned work);
void ghci_recover_wait(struct ghci_device *device);
int ghci_poll_start(struct ghci_device *device);
void ghci_poll_main(void *argument);
void ghci_poll_stop(struct ghci_device *device);

int ghci_memory_alloc(struct ghci_device *device, uint64_t bytes, uint64_t limit, struct ghci_allocation **result);
int ghci_memory_free(struct ghci_device *device, struct ghci_allocation *allocation);
void ghci_memory_quarantine(struct ghci_device *device, struct ghci_allocation *allocation);
void ghci_memory_release_quarantine(struct ghci_device *device);
uint64_t ghci_memory_limit(const struct ghci_device *device);

#endif
