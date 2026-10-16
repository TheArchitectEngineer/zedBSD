/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The state the amdrdna driver keeps for one RDNA GPU, and the functions
 * its files share.
 */

#ifndef DRIVERS_GPU_AMDRDNA_AMDRDNA_PRIVATE_H
#define DRIVERS_GPU_AMDRDNA_AMDRDNA_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/generic/dma.h>
#include <drivers/pci/pci.h>
#include <kern/lock.h>
#include <kern/waitq.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

struct cdev;
struct thread;

/* Marks a parameter a function does not use. */
#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(parameter) ((void)(parameter))
#endif

/* The longest stage mark, past which a line is cut. */
#define AMDRDNA_MARK_COLUMNS		160U

/*
 * What the readout of the firmware's display found: the timing generator
 * it lit, the chain behind it (the output processor, the blend unit, the
 * plane), the encoder that drives the connector and its kind, the timing,
 * the plane's surface and viewport, the scaler that draws the viewport
 * into the active area, the display clocks, the loader's description of
 * the same picture, and how the surface reaches the screen.
 */
struct amdrdna_gop {
	bool lit;
	unsigned timing;
	unsigned output_processor;
	bool combined;
	unsigned blend;
	unsigned plane;
	bool has_output;
	unsigned output;
	unsigned output_front;
	unsigned output_kind;
	unsigned output_hotplug;
	uint32_t h_total;
	uint32_t v_total;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t tiling;
	uint32_t pitch_word;
	uint32_t viewport_x;
	uint32_t viewport_y;
	uint32_t viewport_width;
	uint32_t viewport_height;
	uint32_t scaler_mode;
	uint32_t scaler_x;
	uint32_t scaler_y;
	uint32_t scaler_width;
	uint32_t scaler_height;
	uint32_t horizontal_ratio;
	uint32_t vertical_ratio;
	uint32_t scaler_automatic;
	struct amdrdna_picture picture;
	uint32_t plane_control;
	uint32_t underflow;
	bool compressed;
	bool locked;
	uint64_t surface_mc;
	bool surface_in_vram;
	uint64_t surface_offset;
	uint32_t display_clock_divider;
	uint32_t pixel_rate_control;
	uint32_t dp_clock_phase;
	uint32_t dp_clock_modulo;
	bool loader_present;
	uint64_t loader_physical;
	uint32_t loader_width;
	uint32_t loader_height;
	uint32_t loader_stride;
	bool loader_on_gpu;
	uint64_t loader_offset;
};

/*
 * A buffer of system memory the GPU reaches through the system-memory
 * table: the CPU's view and bus address (dma), and the GPU address the
 * table maps it at (0 until it is mapped).  allocated says the buffer
 * holds memory; the driver keeps it for as long as the GPU may touch it.
 */
struct amdrdna_system_buffer {
	struct drv_dma_buffer dma;
	bool allocated;
	uint64_t gpu_address;
};

/*
 * The interrupt controller's ring and what serves it.
 *
 * The ring and the copy of its write pointer live in system memory the GPU
 * reaches through the system-memory table.  The message interrupt only
 * notes that something arrived and wakes the interrupt thread, which reads
 * the entries up to the copied write pointer and tells the controller how
 * far it read.  lock protects pending, stopping and thread_running, and
 * waitq wakes the thread; read_offset and the counters belong to the
 * thread alone.  enabled says the ring is on (stage A3 turns it on).
 */
struct amdrdna_interrupts {
	struct amdrdna_system_buffer ring;
	struct amdrdna_system_buffer pointer;
	uint32_t ring_bytes;
	uint32_t read_offset;
	struct drv_pci_irq irq;
	unsigned irq_count;
	void *irq_cookie;
	bool message;
	bool prepared;
	bool enabled;
	struct spinlock lock;
	struct wait_queue waitq;
	bool pending;
	bool stopping;
	bool thread_running;
	uint64_t signals;
	uint64_t entries;
	uint64_t unknown;
	uint64_t faults;
	uint64_t overflows;
};

/* The programs the security processor loads for the driver in stage A3. */
#define AMDRDNA_PSP_PROGRAMS		12U

/* A firmware file read whole into kernel memory; data is NULL when none is held. */
struct amdrdna_blob {
	uint8_t *data;
	size_t size;
};

/*
 * The security processor's side of the driver (stage A3).
 *
 * The private buffer (1 MiB of system memory, at a GPU address on a 1 MiB
 * boundary) is where the boot loader and the secure OS read a piece of
 * firmware the driver hands them.  ring_vram is the start of three pages
 * of VRAM the CPU reaches: the secure OS's ring, the fence page it writes
 * its done value to, and the command buffer.  The programs are system
 * memory the security processor copies into the trusted region, which is
 * VRAM only the GPU reaches.  lock serializes the commands on the ring;
 * the flags say how far the stage got, so the stop undoes only that.  The
 * buffers live as long as the GPU may touch them, which is the record's
 * life.
 */
struct amdrdna_psp {
	struct mutex lock;
	struct amdrdna_system_buffer private_buffer;
	struct amdrdna_system_buffer programs[AMDRDNA_PSP_PROGRAMS];
	uint32_t program_bytes[AMDRDNA_PSP_PROGRAMS];
	uint64_t ring_vram;
	bool pages_taken;
	uint32_t fence_sequence;
	bool booted;
	bool ring_up;
	uint64_t region_vram;
	uint32_t region_bytes;
	bool region_set;
	bool application_loaded;
	uint32_t application_session;
	unsigned loaded;
	uint32_t secure_os_version;
	unsigned warnings;
};

/*
 * How memory training ended (stage A3t): its result and the reason of a
 * skip (the control interface's codes), how long it took, and the 4 KiB
 * of training data the security processor left, kept for a later resume.
 */
struct amdrdna_training {
	unsigned result;
	unsigned reason;
	uint32_t milliseconds;
	uint8_t *data;
};

/*
 * The power and thermal side of the driver (stage A4: the unit's firmware
 * runs the fan and the thermal control, or the driver drives the fan at a
 * static duty when the firmware will not).
 *
 * mailbox serializes the system-management unit's messages and the use of
 * the table page, the VRAM the driver and the firmware exchange tables
 * through (taken once, for the record's life); first_sent and hung follow
 * the mailbox's protocol (the first message does not wait for an answer to
 * a previous one; a unit that stopped answering is not sent another).
 * limits is what the board's power table says, valid once table_built.
 * features_on says the firmware runs its three features (the mode is then
 * AMDRDNA_FAN_FIRMWARE), with the feature words it reported.  The watch
 * thread reads the temperatures and the fan once a second, from the
 * firmware's readings when its features run and from the thermal block
 * always; lock protects what it publishes (the readings, the fault and the
 * alert counts) and the thread's stop, and waitq wakes it to stop.
 */
struct amdrdna_thermal {
	struct mutex mailbox;
	bool first_sent;
	bool hung;
	uint32_t smu_version;
	uint32_t interface_version;
	bool table_taken;
	uint64_t table_vram;
	bool table_built;
	struct amdrdna_power_limits limits;
	bool features_on;
	uint32_t features_low;
	uint32_t features_high;
	unsigned mode;
	unsigned duty_percent;
	unsigned limit;
	unsigned high_alert;
	bool alerts_on;
	struct spinlock lock;
	struct wait_queue waitq;
	bool stopping;
	bool thread_running;
	unsigned temperature;
	unsigned highest;
	unsigned sensor;
	unsigned hotspot;
	unsigned memory_temperature;
	uint32_t fan_speed;
	uint32_t tachometer_speed;
	unsigned fan_driven;
	unsigned readings;
	bool faulted;
	uint64_t high_alerts;
	uint64_t low_alerts;
	uint64_t critical_alerts;
	uint64_t smu_interrupts;
};

/* The most words the commands of the check program's dispatch take. */
#define AMDRDNA_CHECK_COMMAND_WORDS	64U

/* The bytes of the check program's page: the code, its table at 2048 and the workgroup counts at 2304. */
#define AMDRDNA_CHECK_PAGE_BYTES	4096U

/* The compute queues stage A5 attaches, one on each pipe of the first compute engine. */
#define AMDRDNA_COMPUTE_QUEUES		4U

/* The kinds of ring the driver runs. */
enum amdrdna_ring_kind {
	AMDRDNA_RING_SCHEDULER,
	AMDRDNA_RING_GRAPHICS,
	AMDRDNA_RING_COMPUTE,
	AMDRDNA_RING_COPY
};

/*
 * One ring of commands the driver writes and an engine reads.
 *
 * The ring, its queue image and its end-of-pipe buffer are system memory
 * the GPU reaches through the system-memory table; the read pointer the
 * engine reports, the copy of the write pointer it polls and the fence
 * word it writes are words of the engines' write-back page at slot.
 * write_pointer counts every word ever written (dwords, or what the copy
 * engine counts in bytes is four times it) and only grows.  lock
 * serializes the writers of the ring, its sequence and its counts; the
 * interrupt thread takes it to count the ring's interrupts.  image_copy
 * keeps the queue image as it was filled, for a later reset.  up says the
 * engine runs the ring.  The buffers live as long as the GPU may touch
 * them, which is the record's life.
 */
struct amdrdna_ring {
	const char *name;
	enum amdrdna_ring_kind kind;
	struct amdrdna_queue_place place;
	uint32_t doorbell;
	struct amdrdna_system_buffer buffer;
	uint32_t bytes;
	struct amdrdna_system_buffer image;
	uint32_t *image_copy;
	struct amdrdna_system_buffer end_of_pipe;
	uint32_t slot;
	struct spinlock lock;
	uint64_t write_pointer;
	uint32_t sequence;
	uint64_t interrupts;
	bool up;
};

/*
 * The command processor and the copy engine (stage A5).
 *
 * select serializes the shader-array select and the queue select, which
 * every register of a selected array or queue goes through; scheduler
 * serializes the commands of the scheduler's own queue.  writeback is the
 * page of system memory the rings' pointers and fences live in.  The
 * clear state is VRAM the run-list controller reads.  The checks' buffers
 * are the copy's source and destination and the dispatch's output in
 * system memory with the indirect buffer the graphics ring's dispatch
 * runs from, and the dispatch's program and the copy's middle stop
 * in VRAM.  The shape words are
 * what the stage read from the fuses.  direct says the graphics ring runs
 * without the scheduler (amdrdna.kiq=0), in which case no compute queue
 * is attached.  The flags say how far the stage got, so the stop undoes
 * only that; the counts are what the checks saw.
 */
struct amdrdna_engines {
	struct mutex select;
	struct mutex scheduler_lock;
	struct amdrdna_system_buffer writeback;
	struct amdrdna_ring scheduler;
	struct amdrdna_ring graphics;
	struct amdrdna_ring compute[AMDRDNA_COMPUTE_QUEUES];
	struct amdrdna_ring copy;
	uint64_t clear_state_vram;
	uint32_t clear_state_words;
	struct amdrdna_system_buffer source;
	struct amdrdna_system_buffer destination;
	struct amdrdna_system_buffer output;
	struct amdrdna_system_buffer indirect;
	uint64_t program_vram;
	uint64_t middle_vram;
	bool direct;
	unsigned compute_count;
	uint32_t address_config;
	uint32_t backends;
	unsigned units;
	uint32_t channels_off;
	uint32_t steering;
	bool buffers_ready;
	bool interrupts_on;
	bool scheduler_up;
	bool graphics_running;
	bool compute_running;
	bool copy_running;
	uint64_t copy_traps;
	uint64_t command_errors;
	uint32_t fences;
	uint32_t copied_bytes;
	uint32_t dispatch_words;
	uint32_t faults_seen;
};

struct amdrdna_node;

/* The frames the display shows clients' storage in, flipped between. */
#define AMDRDNA_DISPLAY_FRAMES		2U

/*
 * The display the driver took over from the firmware (stages A6 to A8).
 *
 * The takeover changes only what the plane fetches and the cursor drawn
 * over it: the timing, the link and the clocks stay the firmware's.
 * taken says the plane's words from before the takeover are saved (the
 * stop writes them back); console is the GPU address of the firmware's
 * surface, which the text console keeps writing to and which the plane is
 * flipped back to when the display is given up, and requested the address
 * the plane was last asked to show.  width, height and stride describe the
 * surface every frame must have (the firmware's).  The frames and the
 * cursor's image are VRAM of the CPU's pool, the driver's for the GPU's
 * lifetime; the cursor has two image slots, so a new image is written
 * into the slot the cursor does not read (cursor_slot is the one it
 * reads).  These fields change in one stage at a time and afterwards
 * only under the node's display lock.
 *
 * lock (an IRQ-safe spinlock, never held across a wait) guards what the
 * interrupt thread changes: the event counts, the refresh count built
 * from the timing generator's frame counter and the time it last moved,
 * the topology sequence (one at first, one more for every change of the
 * monitor) and whether a monitor is connected, and the words of the sync
 * status, the flip interrupt and the hot-plug line, which both the thread
 * and the takeover rewrite.  waitq wakes whoever waits for a frame or for
 * a flip.
 */
struct amdrdna_display {
	bool taken;
	bool cursor_ready;
	bool cursor_shown;
	bool interrupts_on;
	bool blanked;
	bool has_hotplug;
	unsigned hotplug;
	uint64_t console;
	uint64_t requested;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t refresh_millihz;
	uint64_t frames[AMDRDNA_DISPLAY_FRAMES];
	uint64_t frame_bytes;
	uint64_t cursor_vram;
	unsigned cursor_slot;
	uint32_t saved_flip_control;
	uint32_t saved_space_select;
	uint32_t saved_cursor_control;
	uint32_t saved_cursor_timing;
	uint32_t saved_pixel_cursor;
	uint32_t saved_pixel_cursor_scale;
	struct spinlock lock;
	struct wait_queue waitq;
	uint64_t frame_signals;
	uint64_t flip_signals;
	uint64_t hotplug_signals;
	uint64_t flips;
	bool frame_valid;
	uint32_t frame_last;
	uint64_t refresh_count;
	uint64_t refresh_time_ns;
	uint64_t topology;
	bool connected;
};

/*
 * A run of VRAM that GPU work reads and writes: a blob of a session, the
 * executor's own memory, or a check's buffer.
 *
 * vram is its offset and bytes its size in whole pages; asked is the size
 * it was made for; cpu says it lies in the CPU's window (else in the VRAM
 * only the GPU reaches).  refs counts its holders (the blob, every export and
 * import of it, every address space it is mapped in for the executor),
 * under the address spaces' lock; the last one gives the VRAM back unless
 * kept is set: GPU work nobody could stop may still touch it.
 */
struct amdrdna_backing {
	uint64_t vram;
	uint64_t bytes;
	uint64_t asked;
	uint32_t refs;
	bool cpu;
	bool kept;
};

/*
 * One page table of an address space, in VRAM: a directory of 512
 * entries pointing at the tables of the next level (below holds them),
 * or a leaf table of 512 page entries (below is NULL).  count is the
 * entries in use, so an empty leaf table can be told from a used one.
 */
struct amdrdna_table {
	uint64_t vram;
	struct amdrdna_table **below;
	unsigned count;
};

/*
 * One GPU address space: the four levels of page tables of a session (or
 * of a check of the driver's own) and what its runs need.
 *
 * id is the identifier the interrupt controller attaches to the page
 * faults of the space's context, root the root table and root_word the
 * word a hub context is pointed at it with.  lock serializes the tables
 * and addresses, the free part of the space's addresses (from 4 GiB on,
 * on 64 KiB boundaries).  Under the address spaces' lock: generation
 * counts the changes of the tables (a context drops its translations
 * when it lags), context is the hub context the space held last (0 for
 * none), lost says the space's work failed (a page fault, a run that did
 * not end) with its error, faults counts its page faults, and session
 * names the node's session to report a loss to (NULL for the driver's
 * own).  next links the device's spaces.
 */
struct amdrdna_space {
	struct amdrdna_device *device;
	struct amdrdna_space *next;
	uint32_t id;
	struct amdrdna_table *root;
	uint64_t root_word;
	struct mutex lock;
	struct amdrdna_pool *addresses;
	uint64_t generation;
	unsigned context;
	bool lost;
	int lost_error;
	uint64_t faults;
	void *session;
};

/*
 * The GPU's address spaces (stage A5s on).
 *
 * lock (an IRQ-safe spinlock, never held across a wait) guards the pool
 * of hub contexts, the identifiers, the list of spaces with each space's
 * fields named above, every backing's reference count and the counts.
 * It is taken after a space's own lock and before a ring's.  ready says
 * runs may be submitted; faulted that the device failed and nothing new
 * runs.  held counts the spaces kept with their tables because their
 * work could not be confirmed stopped; runs, lost and resets count the
 * runs submitted, the spaces lost and the graphics queue's resets.
 */
struct amdrdna_spaces {
	struct spinlock lock;
	struct amdrdna_context_pool pool;
	struct amdrdna_space_ids ids;
	struct amdrdna_space *list;
	unsigned count;
	unsigned held;
	bool ready;
	bool faulted;
	uint64_t runs;
	uint64_t lost;
	uint64_t resets;
};

/*
 * One RDNA GPU the driver keeps.
 *
 * The attach fills the identity and runs the read-only stages; the
 * bring-up thread runs every later stage.  stage, target, running and the
 * failure fields form the stage machine: they change only under
 * state_lock, and state_waitq wakes the thread when target rises or the
 * machine is told to stop.  The register mapping, the block table and
 * the readouts are written by one stage at a time and only read after
 * that stage reported ok.  attach_counted says the boot's stages will
 * register the GPU device (at attach_stage) and the GPU counts as
 * attaching until they did or failed; only the bring-up thread changes it
 * after the attach.  Stage A2 adds the VRAM window and the doorbell
 * page, the pools (memory_lock serializes them, the system-memory table's
 * writes and the CPU's invalidation requests), the dummy page, the table's
 * place, the hubs' context words from before the stage (given back on a
 * failure and at shutdown) and the interrupt ring; memory_ready says the
 * table translates.  Stages A3t, A3 and A4 add the training's result,
 * the security processor's buffers and the power and thermal state;
 * stage A5 the command processor's and the copy engine's rings; stage A5s
 * the address spaces and the GPU device (node) of the sessions; stages A6
 * to A8 the display taken over from the firmware.  The
 * record lives until the PCI function is detached, which the driver
 * refuses while the thread runs.
 */
struct amdrdna_device {
	struct drv_pci_device *pci;
	struct drv_pci_address address;
	uint16_t product;
	uint8_t revision;
	uint16_t subvendor;
	uint16_t subproduct;
	const struct amdrdna_product *known;

	struct drv_pci_enable_state enable_state;
	bool enable_saved;
	bool registers_claimed;
	struct drv_pci_mapping registers;
	uint64_t vram_bus;
	uint64_t vram_window_bytes;
	uint64_t doorbell_bytes;
	uint64_t register_bytes;
	unsigned resize_capability;
	uint32_t resize_sizes;
	uint32_t resize_control;

	struct spinlock vram_window_lock;
	struct spinlock far_lock;

	uint64_t vram_bytes;
	uint32_t vram_mib;
	uint32_t platform_ready_word;
	uint32_t boot_word;
	uint32_t secure_os_word;
	uint32_t smu_state;
	uint32_t engine_status;
	uint32_t scratch_accelerated;
	uint32_t scratch_post;
	bool posted;
	uint64_t frame_buffer_base;
	uint64_t frame_buffer_top;
	uint64_t frame_buffer_physical;
	struct amdrdna_block_table blocks;
	const char *chip;
	struct amdrdna_boot_database boot_database;

	struct amdrdna_gop gop;
	struct amdrdna_layout layout;

	uint8_t *vbios;
	uint32_t vbios_bytes;
	uint32_t vbios_source;
	struct amdrdna_vbios_info vbios_info;

	struct drv_dma_device *dma;
	bool window_claimed;
	struct drv_pci_mapping window;
	uint64_t window_from;
	bool doorbells_claimed;
	struct drv_pci_mapping doorbells;
	struct mutex memory_lock;
	struct amdrdna_pool cpu_pool;
	struct amdrdna_pool gpu_pool;
	struct amdrdna_pool gart_space;
	struct amdrdna_system_buffer dummy_page;
	bool memory_ready;
	uint64_t frame_buffer_end;
	uint64_t gart_start;
	uint64_t gart_table;
	uint64_t scratch_page;
	bool contexts_recorded;
	uint32_t context_before[AMDRDNA_HUB_COUNT][16];
	uint32_t underflow_before;
	uint32_t underflow_after;
	struct amdrdna_interrupts interrupts;

	struct amdrdna_training training;
	struct amdrdna_psp psp;
	struct amdrdna_thermal thermal;
	struct amdrdna_engines engines;
	struct amdrdna_spaces spaces;
	struct amdrdna_node *node;
	struct amdrdna_display display;

	struct spinlock state_lock;
	struct wait_queue state_waitq;
	unsigned stage;
	unsigned target;
	unsigned running;
	unsigned failed_stage;
	int failed_error;
	bool stopping;
	bool thread_running;
	bool attach_counted;
	unsigned attach_stage;

	struct cdev *control;
};

/* stage.c: the stage marks and the boot parameters. */
void
amdrdna_mark(
	const char *format,
	...) __attribute__((format(printf, 1, 2)));

const char *
amdrdna_stage_name(
	unsigned stage);

bool
amdrdna_parameter_off(void);

unsigned
amdrdna_parameter_auto(void);

/* mmio.c: the register and VRAM access. */
uint32_t
amdrdna_read(
	struct amdrdna_device *device,
	uint32_t index);

void
amdrdna_write(
	struct amdrdna_device *device,
	uint32_t index,
	uint32_t value);

int
amdrdna_block_index(
	struct amdrdna_device *device,
	enum amdrdna_block block,
	unsigned window,
	uint32_t offset,
	uint32_t *index);

uint32_t
amdrdna_block_read(
	struct amdrdna_device *device,
	enum amdrdna_block block,
	unsigned window,
	uint32_t offset);

void
amdrdna_block_write(
	struct amdrdna_device *device,
	enum amdrdna_block block,
	unsigned window,
	uint32_t offset,
	uint32_t value);

uint32_t
amdrdna_far_read(
	struct amdrdna_device *device,
	uint32_t address);

void
amdrdna_vram_read(
	struct amdrdna_device *device,
	uint64_t offset,
	void *buffer,
	size_t length);

/* identify.c: stage A0. */
int
amdrdna_a0_identify(
	struct amdrdna_device *device);

void
amdrdna_a0_release(
	struct amdrdna_device *device);

/* readout.c: stage A1. */
int
amdrdna_a1_readout(
	struct amdrdna_device *device);

int
amdrdna_plan_layout(
	struct amdrdna_device *device);

/* vbios.c: stage A1v. */
int
amdrdna_a1v_vbios(
	struct amdrdna_device *device);

void
amdrdna_vbios_release(
	struct amdrdna_device *device);

bool
amdrdna_training_allowed(
	const struct amdrdna_device *device);

/* gmc.c: stage A2 and the memory it sets up. */
int
amdrdna_a2_memory(
	struct amdrdna_device *device);

void
amdrdna_a2_stop(
	struct amdrdna_device *device);

void
amdrdna_host_path_flush(
	struct amdrdna_device *device);

void
amdrdna_host_path_drop(
	struct amdrdna_device *device);

int
amdrdna_tlb_flush(
	struct amdrdna_device *device,
	enum amdrdna_hub hub,
	unsigned context);

int
amdrdna_gart_map(
	struct amdrdna_device *device,
	struct amdrdna_system_buffer *buffer,
	uint64_t alignment);

void
amdrdna_vm_fault(
	struct amdrdna_device *device,
	const struct amdrdna_interrupt *interrupt);

void
amdrdna_fault_interrupts(
	struct amdrdna_device *device,
	bool on);

/* ih.c: the interrupt ring. */
int
amdrdna_ih_prepare(
	struct amdrdna_device *device);

void
amdrdna_ih_stop(
	struct amdrdna_device *device);

/* mmio.c: VRAM writes through the indirect window. */
void
amdrdna_vram_write(
	struct amdrdna_device *device,
	uint64_t offset,
	const void *buffer,
	size_t length);

/* gmc.c: VRAM for the later stages. */
volatile uint8_t *
amdrdna_vram_cpu(
	struct amdrdna_device *device,
	uint64_t offset);

int
amdrdna_vram_take(
	struct amdrdna_device *device,
	bool cpu,
	uint64_t bytes,
	uint64_t alignment,
	uint64_t *offset);

int
amdrdna_training_range_release(
	struct amdrdna_device *device);

/* ih.c: turning the interrupt ring on. */
void
amdrdna_ih_enable(
	struct amdrdna_device *device);

/* firmware.c: the firmware files. */
int
amdrdna_firmware_read(
	const char *name,
	size_t limit,
	struct amdrdna_blob *blob);

void
amdrdna_firmware_free(
	struct amdrdna_blob *blob);

/* training.c: stage A3t. */
int
amdrdna_a3t_training(
	struct amdrdna_device *device);

/* psp.c: stage A3. */
int
amdrdna_a3_psp(
	struct amdrdna_device *device);

void
amdrdna_psp_stop(
	struct amdrdna_device *device);

/* smu.c: stage A4. */
int
amdrdna_a4_power(
	struct amdrdna_device *device);

void
amdrdna_power_stop(
	struct amdrdna_device *device);

void
amdrdna_thermal_interrupt(
	struct amdrdna_device *device,
	const struct amdrdna_interrupt *interrupt);

/* cp.c: stage A5's command processor. */
int
amdrdna_cp_settings(
	struct amdrdna_device *device);

int
amdrdna_cp_run_list(
	struct amdrdna_device *device);

int
amdrdna_cp_start(
	struct amdrdna_device *device);

void
amdrdna_cp_stop(
	struct amdrdna_device *device);

int
amdrdna_cp_graphics_reset(
	struct amdrdna_device *device,
	unsigned context);

int
amdrdna_ring_submit(
	struct amdrdna_device *device,
	struct amdrdna_ring *ring,
	const uint32_t *words,
	unsigned count,
	bool interrupt,
	uint32_t *sequence);

int
amdrdna_ring_wait(
	struct amdrdna_device *device,
	struct amdrdna_ring *ring,
	uint32_t sequence,
	unsigned milliseconds);

int
amdrdna_ring_test(
	struct amdrdna_device *device,
	struct amdrdna_ring *ring);

volatile uint32_t *
amdrdna_ring_word(
	struct amdrdna_device *device,
	const struct amdrdna_ring *ring,
	unsigned which);

/* sdma.c: stage A5's copy engine. */
int
amdrdna_sdma_start(
	struct amdrdna_device *device);

void
amdrdna_sdma_stop(
	struct amdrdna_device *device);

/* engines.c: stage A5. */
int
amdrdna_a5_engines(
	struct amdrdna_device *device);

void
amdrdna_engines_stop(
	struct amdrdna_device *device);

void
amdrdna_engines_interrupt(
	struct amdrdna_device *device,
	const struct amdrdna_interrupt *interrupt);

int
amdrdna_engines_clear(
	struct amdrdna_device *device,
	uint64_t vram,
	uint64_t bytes);

void
amdrdna_check_load(
	volatile uint8_t *vram,
	uint64_t code_address,
	uint64_t output_address,
	uint32_t output_words,
	const uint32_t *groups);

unsigned
amdrdna_check_commands(
	struct amdrdna_device *device,
	uint32_t *words,
	uint64_t code_address,
	const uint32_t *groups,
	bool compute);

/* stage.c: numbers on the boot line. */
unsigned
amdrdna_parameter_number(
	const char *key,
	unsigned fallback);

/* control.c: the control device. */
int
amdrdna_control_create(
	struct amdrdna_device *device);

/* amdrdna.c: the stage machine. */
int
amdrdna_request_target(
	struct amdrdna_device *device,
	unsigned target);

/* gmc.c: VRAM given back, its bus address, and the hubs' registers for ring commands. */
void
amdrdna_vram_give(
	struct amdrdna_device *device,
	bool cpu,
	uint64_t offset,
	uint64_t bytes);

uint64_t
amdrdna_vram_bus(
	struct amdrdna_device *device,
	uint64_t offset);

uint32_t
amdrdna_hub_register(
	struct amdrdna_device *device,
	enum amdrdna_hub hub,
	enum amdrdna_hub_register which,
	unsigned instance);

/* cp.c: a ring's fences. */
uint32_t
amdrdna_ring_fence(
	struct amdrdna_device *device,
	const struct amdrdna_ring *ring);

uint32_t
amdrdna_ring_sequence(
	struct amdrdna_ring *ring);

uint64_t
amdrdna_ring_fence_address(
	const struct amdrdna_device *device,
	const struct amdrdna_ring *ring);

/* space.c: the address spaces, their backings and their runs. */
void
amdrdna_spaces_init(
	struct amdrdna_device *device);

void
amdrdna_spaces_start(
	struct amdrdna_device *device);

void
amdrdna_spaces_fault(
	struct amdrdna_device *device);

int
amdrdna_space_create(
	struct amdrdna_device *device,
	void *session,
	struct amdrdna_space **result);

bool
amdrdna_space_destroy(
	struct amdrdna_space *space);

int
amdrdna_space_map(
	struct amdrdna_space *space,
	const struct amdrdna_backing *backing,
	uint64_t *address);

void
amdrdna_space_unmap(
	struct amdrdna_space *space,
	uint64_t address,
	uint64_t bytes);

int
amdrdna_space_run(
	struct amdrdna_space *space,
	unsigned tracked,
	uint64_t address,
	uint32_t words,
	unsigned milliseconds);

bool
amdrdna_space_quiet(
	struct amdrdna_space *space);

bool
amdrdna_space_lost(
	struct amdrdna_space *space);

void
amdrdna_space_fault(
	struct amdrdna_device *device,
	const struct amdrdna_vm_fault *fault,
	unsigned context);

void
amdrdna_space_describe(
	struct amdrdna_space *space,
	uint64_t address);

int
amdrdna_backing_create(
	struct amdrdna_device *device,
	uint64_t bytes,
	bool cpu,
	struct amdrdna_backing **result);

void
amdrdna_backing_get(
	struct amdrdna_device *device,
	struct amdrdna_backing *backing);

void
amdrdna_backing_put(
	struct amdrdna_device *device,
	struct amdrdna_backing *backing);

void
amdrdna_backing_keep(
	struct amdrdna_device *device,
	struct amdrdna_backing *backing);

volatile uint8_t *
amdrdna_backing_cpu(
	struct amdrdna_device *device,
	const struct amdrdna_backing *backing);

/* node.c: the GPU device of the sessions. */
int
amdrdna_node_start(
	struct amdrdna_device *device);

void
amdrdna_node_stop(
	struct amdrdna_device *device);

void
amdrdna_node_session_lost(
	struct amdrdna_device *device,
	void *session,
	int error);

void
amdrdna_node_device_failed(
	struct amdrdna_device *device,
	int error);

/* sessions.c: stage A5s. */
int
amdrdna_a5s_sessions(
	struct amdrdna_device *device);

/* vk-node.c: stage A9. */
int
amdrdna_a9_draw(
	struct amdrdna_device *device);

/* display.c: stages A6 and A7 and the display taken over from the firmware. */
bool
amdrdna_display_wanted(void);

int
amdrdna_a6_takeover(
	struct amdrdna_device *device);

int
amdrdna_a7_cursor(
	struct amdrdna_device *device);

void
amdrdna_display_stop(
	struct amdrdna_device *device);

void
amdrdna_display_interrupt(
	struct amdrdna_device *device,
	const struct amdrdna_interrupt *interrupt);

uint64_t
amdrdna_display_address(
	const struct amdrdna_device *device,
	uint64_t vram);

int
amdrdna_display_show(
	struct amdrdna_device *device,
	uint64_t address);

int
amdrdna_display_flip(
	struct amdrdna_device *device,
	uint64_t address);

int
amdrdna_display_flip_wait(
	struct amdrdna_device *device,
	uint64_t address);

void
amdrdna_display_refresh_sample(
	struct amdrdna_device *device,
	uint64_t *count,
	uint64_t *time_ns);

int
amdrdna_display_refresh_wait(
	struct amdrdna_device *device,
	uint64_t cursor,
	uint64_t deadline,
	uint64_t *count,
	uint64_t *time_ns);

void
amdrdna_display_cursor_place(
	struct amdrdna_device *device,
	int32_t x,
	int32_t y);

void
amdrdna_display_cursor_hide(
	struct amdrdna_device *device);

volatile uint8_t *
amdrdna_display_cursor_cpu(
	struct amdrdna_device *device,
	unsigned slot);

void
amdrdna_display_cursor_use(
	struct amdrdna_device *device,
	unsigned slot);

int
amdrdna_display_blank(
	struct amdrdna_device *device,
	bool blank);

uint64_t
amdrdna_display_topology(
	struct amdrdna_device *device,
	bool *connected);

uint64_t
amdrdna_display_now_us(void);

struct amdrdna_display_state;

void
amdrdna_display_report(
	struct amdrdna_device *device,
	struct amdrdna_display_state *state);

struct amdrdna_screen;

int
amdrdna_display_screen(
	struct amdrdna_device *device,
	struct amdrdna_screen *screen);

/* present.c: stage A8, the display's operations of the GPU device. */
int
amdrdna_a8_register(
	struct amdrdna_device *device);

#endif
