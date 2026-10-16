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
 * The power and thermal side of the driver (stage A4, the static-fan
 * tier).
 *
 * mailbox serializes the system-management unit's messages; first_sent
 * and hung follow the mailbox's protocol (the first message does not wait
 * for an answer to a previous one; a unit that stopped answering is not
 * sent another).  The watch thread reads the temperature and the fan once
 * a second; lock protects what it publishes (the readings, the fault and
 * the alert counts) and the thread's stop, and waitq wakes it to stop.
 */
struct amdrdna_thermal {
	struct mutex mailbox;
	bool first_sent;
	bool hung;
	uint32_t smu_version;
	uint32_t interface_version;
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
	uint32_t fan_speed;
	unsigned fan_driven;
	bool faulted;
	uint64_t high_alerts;
	uint64_t low_alerts;
	uint64_t critical_alerts;
	uint64_t smu_interrupts;
};

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

/*
 * One RDNA GPU the driver keeps.
 *
 * The attach fills the identity and runs the read-only stages; the
 * bring-up thread runs every later stage.  stage, target, running and the
 * failure fields form the stage machine: they change only under
 * state_lock, and state_waitq wakes the thread when target rises or the
 * machine is told to stop.  The register mapping, the block table and
 * the readouts are written by one stage at a time and only read after
 * that stage reported ok.  Stage A2 adds the VRAM window and the doorbell
 * page, the pools (memory_lock serializes them, the system-memory table's
 * writes and the CPU's invalidation requests), the dummy page, the table's
 * place, the hubs' context words from before the stage (given back on a
 * failure and at shutdown) and the interrupt ring; memory_ready says the
 * table translates.  Stages A3t, A3 and A4 add the training's result,
 * the security processor's buffers and the power and thermal state;
 * stage A5 the command processor's and the copy engine's rings.  The
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

	struct spinlock state_lock;
	struct wait_queue state_waitq;
	unsigned stage;
	unsigned target;
	unsigned running;
	unsigned failed_stage;
	int failed_error;
	bool stopping;
	bool thread_running;

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

#endif
