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
 * One RDNA GPU the driver keeps.
 *
 * The attach fills the identity and runs the read-only stages; the
 * bring-up thread runs every later stage.  stage, target, running and the
 * failure fields form the stage machine: they change only under
 * state_lock, and state_waitq wakes the thread when target rises or the
 * machine is told to stop.  The register mapping, the block table and
 * the readouts are written by one stage at a time and only read after
 * that stage reported ok.  The record lives until the PCI function is
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
