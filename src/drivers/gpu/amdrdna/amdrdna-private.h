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
 * table translates.  The record lives until the PCI function is
 * detached, which the driver refuses while the thread runs.
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
