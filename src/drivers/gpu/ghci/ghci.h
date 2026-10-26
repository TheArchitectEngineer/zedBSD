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
 * ghci_device_start().  On a bus that delivers no interrupt of the GPU's
 * (the VisionFive 2's root complex takes no MSI and the board may have no
 * INTx pin) the device is polled: a thread runs the interrupt handler's
 * work every GHCI_POLL_MS, and the GPU's IRQ_ENABLE stays zero.
 *
 * Memory.  A GPU with local memory (the MEM capability) is discrete; one
 * without is unified.  The driver hands out three kinds of memory:
 *
 *  - WINDOWED: local memory in the range the local window shows;
 *  - UNWINDOWED: local memory past that range;
 *  - HOST: host RAM below the GPU's HOST_LIMIT and outside its local
 *    memory, which the GPU reaches by DMA.
 *
 * The processor reaches only host memory (GHCI section 11.3): the ring,
 * the fence page, the bounce buffer, the executor's own buffers and every
 * allocation the processor maps are host memory, and memory only the GPU
 * uses is local on a discrete GPU (past the window first; the window's
 * range is ordinary local memory to the driver).  Data moves between the
 * two by the GPU's copies.  The driver does not use the local window and
 * closes it once the device is published (the display controller is set
 * dark at the attach, so no boot framebuffer is shown through it), and
 * opens it again at the detach.  The aperture covers the host addresses
 * and the local memory.  Where the GPU's accesses do not snoop the
 * processor's caches, the processor reaches host memory through an
 * uncached view of it, and so do the user mappings.
 *
 * Locks.  lock (a mutex) serializes the sessions, the allocators, the
 * lists of allocations and the device's life; display.lock (a mutex,
 * taken after lock) serializes the display; cp_lock (a mutex, taken after
 * both) serializes the ring and the reset.  irq_lock (a spinlock, taken by the interrupt handler)
 * protects the completed fence, the reset generation, the fault's record
 * and the service thread's work, and is the condition lock of the fence
 * service and poll wait queues.
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
#define GHCI_FIRMWARE_WAIT_MS		1000U
#define GHCI_RING_WAIT_MS		2000U
#define GHCI_FENCE_WAIT_MS		5000U
#define GHCI_HALT_WAIT_MS		1000U

/*
 * The service thread wakes at least this often, and a fence that has not
 * moved for GHCI_HANG_MS while work is outstanding counts as a hang.
 */
#define GHCI_SERVICE_PERIOD_MS		1000U
#define GHCI_HANG_MS			10000U

/* How often a polled device's interrupt status and fence page are looked at. */
#define GHCI_POLL_MS			1U

/* The fence distance at which a submission waits for the GPU (a quarter of the circle). */
#define GHCI_FENCE_WINDOW		0x40000000U

/* The most modes a display controller can name (DC_CAPS[11:9]). */
#define GHCI_DISPLAY_MODES_MAX		8U

/*
 * The presents the display keeps waiting for the controller's one
 * pending flip: two, and one more for a flip a reset gave back.
 */
#define GHCI_DISPLAY_QUEUE		3U
#define GHCI_DISPLAY_QUEUE_DEPTH	2U

/* The held memory the display lets go of once its lock is let go. */
#define GHCI_DISPLAY_DROPS		16U

/* The tries at host memory that does not fall into the local range. */
#define GHCI_HOST_TRIES			8U

/* The work the service thread is woken for. */
#define GHCI_WORK_RECLAIM		1U
#define GHCI_WORK_RECOVER		2U
#define GHCI_WORK_STOP			4U
#define GHCI_WORK_REPLY			8U
#define GHCI_WORK_DISPLAY		16U

/*
 * The kinds of memory an allocation is in (see the head of this file).
 */
enum ghci_memory_kind {
	GHCI_MEMORY_WINDOWED = 1,
	GHCI_MEMORY_UNWINDOWED,
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
 * view is the processor's kernel view (NULL for local memory), physical
 * the processor's physical address of it (0 for local memory) and
 * gpu_address the address the GPU uses.  bytes is the size handed out (whole pages);
 * requested what the caller asked for.  run is the RAM of HOST memory.
 *
 * holds counts the users that keep the memory although its owner may
 * give it back (a frame writing its replies into it, the blobs that show
 * a Vulkan memory object, the exports and their imports); released says
 * the owner gave it back while it was held: it then waits for the newest
 * fence when its last user lets it go.  A
 * blob of a Vulkan memory object has no memory of its own: backing is the
 * allocation it shows, which it holds.
 *
 * Session protection (docs/ghci.md section 11.6): prot_owned says the
 * firmware was given the allocation as the protected session's own memory
 * (a synthetic GHCI_MEMORY, see ghci_vk_prot_own), prot_granted that an
 * import was granted to its protected session (BUFFER_GRANT), so that the
 * release takes it back.
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
	unsigned holds;
	int released;
	struct ghci_allocation *backing;
	int prot_owned;
	int prot_granted;
};

/*
 * One open of a device.
 *
 * tag is the job tag every packet of the session carries.  outstanding
 * says that last_fence (the fence after the session's newest submission)
 * may not have passed yet; it is cleared once seen passed, so an old value
 * is never compared after the 32-bit circle has turned.  vk is the
 * session's Vulkan executor (NULL on a device without one).  protected
 * says the firmware's session was opened PROTECTED (a GPU with the PROT
 * capability, an opener that is not root).  The device's lock protects
 * every field.
 */
struct ghci_vk_session;
struct ghci_vk_engine;

struct ghci_session {
	struct ghci_device *device;
	struct ghci_vk_session *vk;
	struct ghci_session *next;
	struct ghci_allocation *allocations;
	unsigned allocation_count;
	uint32_t tag;
	uint32_t last_fence;
	uint64_t last_generation;
	int outstanding;
	int protected;
};

/*
 * One mode of the display controller, as its mode table (MODE_INDEX and
 * the MODE registers) names it: the size, the refresh the clock and the
 * totals give, the porches and syncs, the sync polarity (SYNC_POL) and the
 * PCLK_SEL value that picks its pixel clock.  Read once at the attach.
 */
struct ghci_display_mode {
	uint32_t width;
	uint32_t height;
	uint32_t refresh_millihz;
	uint32_t h_front;
	uint32_t h_sync;
	uint32_t h_back;
	uint32_t v_front;
	uint32_t v_sync;
	uint32_t v_back;
	uint32_t sync_polarity;
	uint32_t clock_select;
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
 * One image of plane 0 the display has recorded: a present's frame.
 *
 * sequence is the present's number in the lease (0 for none).  base is
 * the GPU address the controller scans, with the rows stride bytes apart
 * in the controller's format; mode is the mode the image fills.  A shared
 * image (a blob the session presents) is memory the display holds until
 * the controller no longer scans it; a copied one is one of the driver's
 * two frames (copy_frame 0 or 1, memory NULL).
 */
struct ghci_display_image {
	uint64_t sequence;
	uint32_t base;
	uint32_t stride;
	uint32_t format;
	unsigned mode;
	struct ghci_allocation *memory;
	int copy_frame;
};

/*
 * The one flip of a plane that the driver has placed in the ring and
 * whose record it has not seen yet.  The driver places a plane's next
 * flip only after this one's record, so the controller's one pending
 * flip per plane never makes the ring wait.
 *
 * value is the 31-bit value the record carries; light says the flip is
 * part of a run that lights the controller.  image is plane 0's,
 * surface the cursor surface of plane 1.
 */
struct ghci_display_flip {
	int placed;
	uint32_t value;
	int light;
	struct ghci_display_image image;
	int surface;
};

/*
 * The display controller of a device (display.c and display-scan.c, the
 * DISP2 capability): one output with a primary plane and, when the
 * controller has one, a cursor plane.  The ring changes what it shows: a
 * present is a FLIP of a shared image or of one of the driver's two
 * frames into which it copied the session's storage.
 *
 * lock serializes everything here but topology and the interrupt's
 * wake-up; it is taken after the device's lock and before cp_lock.
 * topology (the event sequence of the output) is read and moved under
 * irq_lock.  The registers, the identity and the mode table are read at
 * the attach and never change.
 *
 * The lease: owner and lease name the holder; ending says a lease ended
 * and the controller is still going dark (no claim until it is), dark_ticket
 * the fence of the packet that turns it off.  sequence is the lease's last
 * present number and adopted the highest one shown with every lower one
 * (replaced ones count); uncertain says an adoption did not come in time,
 * after which the lease's presents fail.
 *
 * What is lit: enabled, the mode, the format and the stride of plane 0;
 * shown is the image it scans.  flips[] are the planes' placed flips,
 * queue[] the presents still waiting for plane 0's flip, oldest first.
 * copy_frame_busy is the driver's frame a copied present is filling (-1
 * for none): no present is recorded until it is.  drops[] is held memory
 * let go of once the lock is let go (the device's lock frees it).
 *
 * The cursor: surfaces[] are the driver's two images of plane 1 and
 * staging the copy of a session's rows; cursor_shown is the surface the
 * controller has (-1 for none), cursor_pending one to flip once the
 * controller is lit (-1 for none); cursor_copying says a copy into the
 * staging runs.  The errors and the connection are the service thread's.
 */
struct ghci_display {
	int present;
	uint32_t registers;
	uint32_t controller_id;
	uint32_t controller_version;
	uint32_t controller_caps;
	struct ghci_display_mode modes[GHCI_DISPLAY_MODES_MAX];
	unsigned mode_count;
	struct mutex lock;
	struct wait_queue flip_wake;
	uint64_t topology;

	/* The connection and the output's generation. */
	int connected;
	uint64_t generation;
	uint64_t hotplug_tick;

	/* The lease. */
	struct ghci_session *owner;
	uint64_t lease;
	uint64_t next_lease;
	int ending;
	int dark_placed;
	struct ghci_ticket dark_ticket;
	uint64_t sequence;
	uint64_t adopted;
	uint64_t shown_time_ns;
	int uncertain;

	/* What the controller shows and what waits for it. */
	int enabled;
	unsigned mode;
	uint32_t format;
	uint32_t stride;
	struct ghci_display_image shown;
	struct ghci_display_flip flips[2];
	struct ghci_display_image queue[GHCI_DISPLAY_QUEUE];
	unsigned queue_count;
	uint32_t next_value;
	struct ghci_allocation *frames[2];
	int copy_frame_busy;
	struct ghci_allocation *drops[GHCI_DISPLAY_DROPS];
	unsigned drop_count;

	/* The cursor plane. */
	struct ghci_allocation *surfaces[2];
	struct ghci_allocation *staging;
	int cursor_shown;
	int cursor_pending;
	int cursor_image;
	int cursor_copying;

	/* The scan-out errors: the count of this second, the mask and the reads that kept failing. */
	unsigned error_count;
	uint64_t error_tick;
	int error_masked;
	unsigned read_errors;
	unsigned underflows;
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

	/*
	 * How the bus glue left the device: polled says no interrupt is taken
	 * (the poll thread does the handler's work and IRQ_ENABLE stays zero);
	 * host_uncached says the GPU does not snoop the processor's caches, so
	 * host memory is reached through an uncached view.  Both are set
	 * before ghci_device_start() and never change.
	 */
	int polled;
	int host_uncached;

	/* The locks, the wait queues and the service thread. */
	struct mutex lock;
	struct mutex cp_lock;
	struct spinlock irq_lock;
	struct wait_queue fence_wake;
	struct wait_queue service_wake;
	unsigned service_work;
	int service_running;

	/*
	 * The poll thread of a polled device (irq_lock): poll_stop asks it to
	 * leave, and it clears poll_running as it leaves.
	 */
	struct wait_queue poll_wake;
	int poll_stop;
	int poll_running;

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
	uint64_t hardware_window;
	uint64_t host_limit;
	uint32_t mem_registers;
	int protection;
	uint32_t prot_registers;
	struct ghci_allocation *prot_table;
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
	struct ghci_range_map windowed;
	struct ghci_range_map unwindowed;
	struct ghci_range_extent *windowed_extents;
	struct ghci_range_extent *unwindowed_extents;
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
	 * The firmware (firmware.c).  fw_page holds the replies of INIT and of
	 * the diagnostics (with the ring, from ghci_cp_start to ghci_cp_stop).
	 * fw_version and fw_features are INIT's last answer (fw_features 0: a
	 * firmware without INIT, or none); fw_tokens counts the INITs sent, so
	 * each carries a token of its own.  The service thread and the start
	 * write them, one at a time.
	 */
	struct ghci_allocation *fw_page;
	uint32_t fw_version;
	uint32_t fw_features;
	uint32_t fw_tokens;

	/*
	 * A firmware image staged for a reload (device lock): fw_image holds
	 * IMEM's words and then DMEM's, fw_iwords and fw_dwords how many of
	 * each were sent; fw_image_local is the copy's memory of a reload
	 * through local memory; fw_ibase and fw_dbase are where the load reads
	 * them.  The memory is kept until the detach.
	 */
	struct ghci_allocation *fw_image;
	struct ghci_allocation *fw_image_local;
	uint32_t fw_iwords;
	uint32_t fw_dwords;
	uint32_t fw_ibase;
	uint32_t fw_dbase;

	/* The diagnostics' buffers, kept until the detach, and whether a run goes on (device lock). */
	struct ghci_allocation *diag_host;
	struct ghci_allocation *diag_local;
	int diag_running;

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

	/* The Vulkan executor; NULL when the command processor has no firmware or no shader unit. */
	struct ghci_vk_engine *vk;
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

/* Starts the command processor's firmware after a reset, when the GPU has a CPFW capability: 0 or EIO. */
int ghci_firmware_start(struct ghci_device *device);

/*
 * The firmware's handshake, diagnostics and reload (firmware.c).
 */

/* Sends GHCI_INIT and checks the reply: 0, or EIO or ETIMEDOUT with the reason logged. */
int ghci_firmware_init(struct ghci_device *device);

/* Runs the diagnostics a number of rounds, logging every step: 0 when all passed, EIO when one failed. */
int ghci_firmware_diag(struct ghci_device *device, unsigned rounds);

/* Takes GPU_OP_GHCI_DIAG or GPU_OP_GHCI_FIRMWARE from root. */
int ghci_firmware_command(struct ghci_device *device, struct ghci_session *session, const void *stream, uint32_t bytes);

/* Loads the staged image into the processor's memories after a reset: 0 or EIO. */
int ghci_firmware_load(struct ghci_device *device);

/* Gives back the staged image and the diagnostics' buffers at the detach. */
void ghci_firmware_release(struct ghci_device *device);

/*
 * The command processor (cp.c).
 */

/* Resets the command processor and sets the aperture, the ring, the fences and the interrupt, then RUN. */
int ghci_cp_start(struct ghci_device *device);

/* Waits for the driver's last fence and halts the command processor. */
void ghci_cp_stop(struct ghci_device *device);

/* Resets the processor, loads the staged firmware image, starts it and sends INIT: 0, or EIO. */
int ghci_cp_reload(struct ghci_device *device);

/*
 * Writes packets for a session (NULL: the driver's own work) into the
 * ring, followed by the driver's fence, and rings the doorbell.  Returns
 * 0 with the submission's ticket, or why nothing was written.
 */
int ghci_cp_submit(struct ghci_device *device, struct ghci_session *session, const uint32_t *packets, uint32_t dwords, struct ghci_ticket *ticket);

/* Writes packets of the driver's own into the ring without a fence, and rings the doorbell. */
int ghci_cp_submit_unfenced(struct ghci_device *device, const uint32_t *packets, uint32_t dwords);

/* Waits a bounded time for a ticket.  Returns 0, ETIMEDOUT, or EIO when a reset ended it. */
int ghci_cp_wait(struct ghci_device *device, const struct ghci_ticket *ticket, unsigned milliseconds);

/* Reports the ticket of the newest submission. */
void ghci_cp_last(struct ghci_device *device, struct ghci_ticket *ticket);

/* Reports the fence the GPU has completed, read afresh. */
uint32_t ghci_cp_completed(struct ghci_device *device);

/* Reports whether a ticket is done (passed, or ended by a reset): 1 when it is. */
int ghci_cp_done(struct ghci_device *device, uint32_t fence, uint64_t generation);

/* The interrupt handler of the device. */
int ghci_cp_interrupt(void *argument);

/* Writes IRQ_ENABLE from irq_mask, or zero on a polled device. */
void ghci_cp_irq_enable(struct ghci_device *device);

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

/* Starts the poll thread of a polled device (nothing on another). */
int ghci_poll_start(struct ghci_device *device);

/* The poll thread's body (service.c starts it). */
void ghci_poll_main(void *argument);

/* Stops the poll thread and waits for it to leave. */
void ghci_poll_stop(struct ghci_device *device);

/*
 * The memory (memory.c).  The device's lock is held for every call but
 * ghci_memory_init, ghci_memory_fini and ghci_memory_window_set.
 */

/* Sets up the aperture and the allocators of local memory. */
int ghci_memory_init(struct ghci_device *device);

/* Gives back the allocators' tables at the detach. */
void ghci_memory_fini(struct ghci_device *device);

/* Opens or closes the local window to the host (the MEM capability's WINDOW_CTRL). */
void ghci_memory_window_set(struct ghci_device *device, int open);

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

/* Keeps an allocation from going back while its user still writes it. */
void ghci_memory_hold(struct ghci_allocation *allocation);

/* Lets a held allocation go; one its owner gave back meanwhile goes back now or after its fence. */
void ghci_memory_unhold(struct ghci_device *device, struct ghci_allocation *allocation);

/*
 * The allocations of a session (ghci.c).  The device's lock is held.
 */

/* Makes a cleared allocation for a session and lists it. */
int ghci_allocation_make(struct ghci_device *device, struct ghci_session *session, enum ghci_memory_use use, uint64_t bytes, int blob, struct ghci_allocation **result);

/* Takes an allocation off its session's list and gives it back now or after the session's last fence. */
void ghci_allocation_drop(struct ghci_device *device, struct ghci_session *session, struct ghci_allocation *allocation);

/* Finds a blob of a session by its resource id (its GPU address), or NULL. */
struct ghci_allocation *ghci_allocation_blob(struct ghci_session *session, uint32_t resource_id);

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

/* The display operations of a device with a display controller, with and without a cursor plane. */
extern const struct drv_gpu_display_ops ghci_display_ops;
extern const struct drv_gpu_display_ops ghci_display_cursor_ops;

/* The scan-out route's query of a device with a display controller. */
extern const struct drv_gpu_scanout_ops ghci_scanout_ops;

/*
 * The sharing of memory between a device's sessions (share.c).
 */

/* The sharing operations of every device. */
extern const struct drv_gpu_share_ops ghci_share_ops;

/* Finds and starts the display controller; 0 also when there is none (display.present stays 0). */
int ghci_display_attach(struct ghci_device *device);

/* Turns the display controller off for the detach. */
void ghci_display_detach(struct ghci_device *device);

/* Ends a closing session's lease. */
void ghci_display_session_close(struct ghci_device *device, struct ghci_session *session);

/* Does the display's part of a round of the service thread: records, errors, the connection, a lease's end. */
void ghci_display_service(struct ghci_device *device, unsigned work);

/* Takes the display's lock before a reset of the command processor. */
void ghci_display_recover_begin(struct ghci_device *device);

/* Counts the display's flips again while the reset processor is idle; cp_lock is held. */
void ghci_display_recover_count(struct ghci_device *device);

/* Places again what the reset lost (when the GPU came back) and lets the display's lock go. */
void ghci_display_recover_end(struct ghci_device *device, int running);

/* Finds an ADV7511 behind the display controller's I2C master and sets it up: 0, ENODEV or EIO. */
int ghci_adv7511_start(struct ghci_device *device, uint32_t registers);

/* Reads whether the ADV7511 sees a sink on its hot-plug line: 0 with the answer, or EIO. */
int ghci_adv7511_sensed(struct ghci_device *device, uint32_t registers, int *connected);

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
