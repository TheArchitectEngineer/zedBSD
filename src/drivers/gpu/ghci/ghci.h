/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The generic driver of a GPU with the GPU Host Controller Interface
 * (GHCI 1.0): the device, its sessions and allocations, and the parts the
 * driver's files share.
 *
 * One register window is one device.  The driver knows a GPU only by its
 * registers: the identification, the capabilities and the extended
 * capability list say what it has, and nothing in the driver names a
 * vendor.  The bus glue (pci-attach.c) maps the window, the optional
 * window onto the GPU's own memory and the interrupt, and hands them to
 * ghci_device_start().
 *
 * Memory.  A GPU with local memory (the MEM capability) is discrete; one
 * without is unified.  The driver hands out three kinds of memory:
 *
 *  - VISIBLE: local memory inside the local window, which the processor
 *    reaches through the bus mapping;
 *  - HIDDEN: local memory past the window, which only the GPU reaches;
 *  - HOST: host RAM below 4 GiB, which the GPU reaches by DMA.
 *
 * A discrete GPU with a window keeps everything in local memory: the
 * ring, the fence page and the bounce buffer of the transfers go in the
 * window, as do the allocations the processor maps, and the aperture is
 * exactly the local memory.  Without a window (or without local memory)
 * those go in host memory and the aperture opens the 32-bit space.
 *
 * Locks.  lock (a mutex) serializes the sessions, the allocators, the
 * lists of allocations, the display and the device's life; cp_lock (a
 * mutex, taken after lock when both are held) serializes the ring and
 * the reset.  irq_lock (a spinlock, taken by the interrupt handler)
 * protects the completed fence, the reset generation, the fault's record
 * and the service thread's work, and is the condition lock of the fence
 * and service wait queues.
 */

#ifndef DRIVERS_GPU_GHCI_H
#define DRIVERS_GPU_GHCI_H

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <drivers/pci/pci.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <kern/waitq.h>

#include "compiler/gsi1-compile.h"
#include "ghci-words.h"

/* The job tags of sessions: 1 to 255; tag 0 is the driver's own work. */
#define GHCI_TAG_DRIVER			0U
#define GHCI_TAG_MAX			255U
#define GHCI_TAG_WORDS			8U

/* The page memory is handed out in. */
#define GHCI_PAGE_BYTES			4096U

/* The ring: 64 KiB (RING_SIZE is its log2). */
#define GHCI_RING_SIZE_LOG2		16U
#define GHCI_RING_BYTES			(1U << GHCI_RING_SIZE_LOG2)

/* The bounce buffer of a transfer to memory the processor does not reach. */
#define GHCI_BOUNCE_BYTES		65536U

/* The runs each allocator of local memory records at most. */
#define GHCI_EXTENTS_MAX		4096U

/* The most allocations one session holds, and the largest one. */
#define GHCI_SESSION_RESOURCES		4096U
#define GHCI_RESOURCE_MAX_BYTES		(256ULL << 20)

/*
 * How long the driver waits for the hardware: a reset to finish, the
 * memory to become ready, a ring's room, a fence the driver itself waits
 * for, and the command processor to halt at the detach.
 */
#define GHCI_RESET_WAIT_MS		1000U
#define GHCI_MEMORY_WAIT_MS		1000U
#define GHCI_RING_WAIT_MS		2000U
#define GHCI_FENCE_WAIT_MS		5000U
#define GHCI_HALT_WAIT_MS		1000U

/*
 * The service thread wakes at least this often, and a fence that has not
 * moved for GHCI_HANG_MS while work is outstanding counts as a hang.
 */
#define GHCI_SERVICE_PERIOD_MS		1000U
#define GHCI_HANG_MS			10000U

/* The fence distance at which a submission waits for the GPU (a quarter of the circle). */
#define GHCI_FENCE_WINDOW		0x40000000U

/* The tries at host memory that does not fall into the local range. */
#define GHCI_HOST_TRIES			8U

/* The work the service thread is woken for. */
#define GHCI_WORK_RECLAIM		1U
#define GHCI_WORK_RECOVER		2U
#define GHCI_WORK_STOP			4U

/*
 * The kinds of memory an allocation is in (see the head of this file).
 */
enum ghci_memory_kind {
	GHCI_MEMORY_VISIBLE = 1,
	GHCI_MEMORY_HIDDEN,
	GHCI_MEMORY_HOST
};

/*
 * The purpose an allocation is asked for, which decides its kind of
 * memory: the driver's own buffers, storage the processor maps or
 * presents, and memory only the GPU uses.
 */
enum ghci_memory_use {
	GHCI_USE_DRIVER = 1,
	GHCI_USE_MAPPABLE,
	GHCI_USE_DEVICE
};

struct ghci_device;
struct ghci_session;

/*
 * One allocation of GPU memory: a session's resource, or a buffer the
 * device keeps for itself (the ring, the fence page, the bounce buffer).
 *
 * A session's allocation is on the session's list until it is destroyed.
 * If the GPU may still use it then (the session's last fence has not
 * passed), it moves to the device's deferred list with that fence and
 * reset generation, until the service thread gives it back.  The device's
 * lock protects the lists and every field.
 *
 * view is the processor's kernel view (NULL for HIDDEN memory), physical
 * the processor's physical address of it (0 for HIDDEN) and gpu_address
 * the address the GPU uses.  bytes is the size handed out (whole pages);
 * requested what the caller asked for.  run is the RAM of HOST memory.
 */
struct ghci_allocation {
	struct ghci_allocation *next;
	struct ghci_session *session;
	enum ghci_memory_kind kind;
	uint32_t gpu_address;
	uint32_t bytes;
	uint64_t requested;
	uint8_t *view;
	uint64_t physical;
	struct kern_pmem run;
	uint32_t fence;
	uint64_t generation;
	int blob;
};

/*
 * One open of a device.
 *
 * tag is the job tag every packet of the session carries.  outstanding
 * says that last_fence (the fence after the session's newest submission)
 * may not have passed yet; it is cleared once seen passed, so an old value
 * is never compared after the 32-bit circle has turned.  The device's
 * lock protects every field.
 */
struct ghci_session {
	struct ghci_device *device;
	struct ghci_session *next;
	struct ghci_allocation *allocations;
	unsigned allocation_count;
	uint32_t tag;
	uint32_t last_fence;
	uint64_t last_generation;
	int outstanding;
};

/*
 * The display controller of a device (display.c): its registers, the one
 * lease, the mode shown and the driver's two frames, into which a
 * presentation copies the session's storage and which the controller
 * scans in turn.
 *
 * The device's lock protects everything but the interrupt's counters,
 * which irq_lock protects: vsyncs counts vertical syncs and flips the
 * flips that completed, and flip_wake wakes a presentation that waits for
 * its flip.  shown is the frame index (0 or 1) on the screen; sequence
 * counts the lease's presentations.
 */
struct ghci_display {
	int present;
	uint32_t registers;
	uint32_t controller_id;
	uint32_t controller_version;
	uint32_t controller_caps;
	unsigned mode_count;
	struct ghci_session *owner;
	uint64_t lease;
	uint64_t next_lease;
	uint64_t sequence;
	uint64_t shown_time_ns;
	unsigned mode;
	uint32_t format;
	int enabled;
	struct ghci_allocation *frames[2];
	unsigned shown;
	uint64_t vsyncs;
	uint64_t flips;
	uint32_t underflows;
	struct wait_queue flip_wake;
};

/*
 * A shader ISA the driver can compile for: its FourCC and its compiler,
 * which turns a SPIR-V module's stage into a program binary of the GHCI
 * program format.  The table lives as long as the kernel.
 */
struct ghci_isa_backend {
	uint32_t fourcc;
	const char *name;
	int (*compile)(const uint32_t *words, size_t word_count, enum drv_gpu_shader_stage stage, const struct ghci_gsi1_options *options, struct ghci_gsi1_program **program, struct ghci_gsi1_diagnostic *diagnostic);
	void (*release)(struct ghci_gsi1_program *program);
};

/*
 * One GHCI device.
 *
 * The bus glue fills the bus fields (pci, the mappings and the interrupt)
 * before ghci_device_start(), and keeps the structure until
 * ghci_device_stop() has returned 0.  The identification and capability
 * fields are read once at the start and never change.  running says the
 * command processor runs the ring; lost says the hardware did not come
 * back from a reset, after which every submission fails.  withdrawing
 * says a detach has begun and new opens are refused.
 */
struct ghci_device {
	/* The bus: the function, its register window, its local window and its interrupt. */
	struct drv_pci_device *pci;
	struct drv_pci_mapping register_map;
	struct drv_pci_mapping window_map;
	volatile uint8_t *registers;
	uint32_t register_bytes;
	volatile uint8_t *window;
	uint64_t window_physical;
	uint64_t window_bytes;
	struct drv_pci_irq irq;
	unsigned irq_count;
	void *irq_cookie;

	/* The locks, the wait queues and the service thread. */
	struct mutex lock;
	struct mutex cp_lock;
	struct spinlock irq_lock;
	struct wait_queue fence_wake;
	struct wait_queue service_wake;
	unsigned service_work;
	int service_running;

	/* The identification and the capabilities. */
	uint32_t version;
	uint32_t vendor_id;
	uint32_t device_id;
	uint32_t isa_id;
	uint32_t device_version;
	uint32_t caps;
	uint32_t caps2;
	uint32_t gclk_hz;
	uint32_t line_bytes;
	uint32_t extcap;
	uint32_t local_base;
	uint64_t local_size;
	uint64_t local_window;
	int protection;
	const struct ghci_isa_backend *isa;
	char name[32];

	/* The registration with the GPU core and the sessions. */
	struct drv_gpu_ops ops;
	struct drv_gpu_device *gpu;
	struct ghci_session *sessions;
	unsigned session_count;
	uint32_t tags[GHCI_TAG_WORDS];
	int withdrawing;

	/* The allocators of local memory and the allocations waiting for the GPU. */
	struct ghci_range_map visible;
	struct ghci_range_map hidden;
	struct ghci_range_extent *visible_extents;
	struct ghci_range_extent *hidden_extents;
	struct ghci_allocation *deferred;
	uint64_t host_bytes;
	uint32_t aperture_base;
	uint32_t aperture_limit;

	/* The ring and the fences. */
	struct ghci_allocation *ring;
	struct ghci_allocation *fence_page;
	struct ghci_allocation *bounce;
	uint32_t ring_dwords;
	uint32_t wptr;
	uint32_t fence_issued;
	int running;
	int lost;

	/*
	 * What the interrupt and the service thread share (irq_lock).
	 * irq_mask is the IRQ_ENABLE the handler restores after it masked
	 * the interrupt: set before the ring starts, zero once it stops.
	 */
	uint32_t irq_mask;
	uint32_t completed;
	uint64_t generation;
	uint64_t progress_tick;
	int fault_pending;
	uint32_t fault_info;
	uint32_t fault_address;
	unsigned resets;

	/* The display controller. */
	struct ghci_display display;
};

/*
 * A submission's place in the fence order: the fence value written after
 * it and the reset generation it was written in.  A reset completes every
 * fence of an older generation with an error.
 */
struct ghci_ticket {
	uint32_t fence;
	uint64_t generation;
};

/*
 * The device (ghci.c).
 */

/* Starts a device whose bus fields are filled: identify, reset, memory, ring, display, registration. */
int ghci_device_start(struct ghci_device *device);

/* Stops a started device for the detach: EBUSY while a session is open. */
int ghci_device_stop(struct ghci_device *device);

/* Reads one register of the window. */
uint32_t ghci_read(struct ghci_device *device, uint32_t offset);

/* Reads one register of the window through an opaque device pointer (the capability walk's reader). */
uint32_t ghci_read_context(void *context, uint32_t offset);

/* Writes one register of the window. */
void ghci_write(struct ghci_device *device, uint32_t offset, uint32_t value);

/* Sleeps about the given milliseconds. */
void ghci_sleep_ms(unsigned milliseconds);

/* Waits up to a time for (register & mask) == value.  Returns 0 or ETIMEDOUT. */
int ghci_poll(struct ghci_device *device, uint32_t offset, uint32_t mask, uint32_t value, unsigned milliseconds);

/*
 * The command processor (cp.c).
 */

/* Resets the command processor and sets the aperture, the ring, the fences and the interrupt, then RUN. */
int ghci_cp_start(struct ghci_device *device);

/* Waits for the driver's last fence and halts the command processor. */
void ghci_cp_stop(struct ghci_device *device);

/*
 * Writes packets for a session (NULL: the driver's own work) into the
 * ring, followed by the driver's fence, and rings the doorbell.  Returns
 * 0 with the submission's ticket, or why nothing was written.
 */
int ghci_cp_submit(struct ghci_device *device, struct ghci_session *session, const uint32_t *packets, uint32_t dwords, struct ghci_ticket *ticket);

/* Waits a bounded time for a ticket.  Returns 0, ETIMEDOUT, or EIO when a reset ended it. */
int ghci_cp_wait(struct ghci_device *device, const struct ghci_ticket *ticket, unsigned milliseconds);

/* Reports the ticket of the newest submission. */
void ghci_cp_last(struct ghci_device *device, struct ghci_ticket *ticket);

/* Reports whether a ticket is done (passed, or ended by a reset): 1 when it is. */
int ghci_cp_done(struct ghci_device *device, uint32_t fence, uint64_t generation);

/* The interrupt handler of the device. */
int ghci_cp_interrupt(void *argument);

/* Starts the service thread of the device. */
int ghci_service_start(struct ghci_device *device);

/* The service thread's body (service.c starts it). */
void ghci_service_main(void *argument);

/* Does one round of the service's work: recovery, the hang check and the reclaim. */
void ghci_service_step(struct ghci_device *device, unsigned work);

/* Stops the service thread and waits for it to leave. */
void ghci_service_stop(struct ghci_device *device);

/* Wakes the service thread for work. */
void ghci_service_wake(struct ghci_device *device, unsigned work);

/*
 * The memory (memory.c).  The device's lock is held for every call but
 * ghci_memory_init and ghci_memory_fini.
 */

/* Sets up the allocators from the local memory and the window. */
int ghci_memory_init(struct ghci_device *device);

/* Gives back the allocators' tables at the detach. */
void ghci_memory_fini(struct ghci_device *device);

/* Takes memory for a use; session NULL keeps it for the device.  The memory is not cleared. */
int ghci_memory_alloc(struct ghci_device *device, struct ghci_session *session, enum ghci_memory_use use, uint64_t bytes, struct ghci_allocation **result);

/* Gives memory back now: the GPU no longer uses it. */
void ghci_memory_free(struct ghci_device *device, struct ghci_allocation *allocation);

/* Gives a session's allocation back now, or once the session's last fence has passed. */
void ghci_memory_release(struct ghci_device *device, struct ghci_allocation *allocation);

/* Keeps an allocation on the deferred list until a ticket is done. */
void ghci_memory_defer(struct ghci_device *device, struct ghci_allocation *allocation, const struct ghci_ticket *ticket);

/* Gives back every deferred allocation whose fence is done.  Returns the bytes given back. */
uint64_t ghci_memory_reclaim(struct ghci_device *device);

/* Reports whether a run of GPU addresses lies inside one of a session's allocations: 1 when it does. */
int ghci_memory_owned(struct ghci_session *session, uint32_t address, uint32_t bytes);

/*
 * The blitter (blit.c).  The device's lock is held.
 */

/* Fills a run of GPU memory with zero through the blitter and waits for it. */
int ghci_blit_clear(struct ghci_device *device, uint32_t gpu_address, uint32_t bytes);

/* Runs one blit of the driver's and waits for it. */
int ghci_blit_run(struct ghci_device *device, const struct ghci_blit *blit);

/* Copies between the processor and memory it does not reach, through the bounce buffer. */
int ghci_blit_transfer(struct ghci_device *device, struct ghci_allocation *allocation, uint64_t offset, void *bytes, uint32_t length, int to_gpu);

/*
 * The shader ISAs (isa.c).
 */

/* Reports the backend that compiles for a device with these capabilities and ISA, or NULL. */
const struct ghci_isa_backend *ghci_isa_select(uint32_t caps, uint32_t isa_id);

/*
 * The display controller (display.c).
 */

/* The display operations of a device with a display controller. */
extern const struct drv_gpu_display_ops ghci_display_ops;

/* Finds and starts the display controller; 0 also when there is none (display.present stays 0). */
int ghci_display_attach(struct ghci_device *device);

/* Turns the display controller off for the detach. */
void ghci_display_detach(struct ghci_device *device);

/* Ends a closing session's lease. */
void ghci_display_session_close(struct ghci_device *device, struct ghci_session *session);

/* Serves the display controller's interrupt; nonzero when it had one. */
int ghci_display_interrupt(struct ghci_device *device);

/* Finds an ADV7511 behind the display controller's I2C master and sets it up: 0, ENODEV or EIO. */
int ghci_adv7511_start(struct ghci_device *device, uint32_t registers);

/*
 * The native command of the test kernels (native.c).
 */

/*
 * Checks and submits a GPU_OP_GHCI_NATIVE command from root.  Weak: a
 * kernel built without CONFIG_GHCI_NATIVE has none, and its devices do
 * not advertise GPU_CAP_COMMAND.
 */
extern int ghci_native_command(struct ghci_device *device, struct ghci_session *session, const void *stream, uint32_t bytes) __attribute__((weak));

#endif
