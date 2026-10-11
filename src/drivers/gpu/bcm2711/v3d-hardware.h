/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Private, persistent MMU/cache/IRQ ownership for the single V3D 4.2 core. */
#ifndef KERN_DRIVERS_GPU_BCM2711_V3D_HARDWARE_H
#define KERN_DRIVERS_GPU_BCM2711_V3D_HARDWARE_H

#include <stdbool.h>
#include <stdint.h>
#include <kern/lock.h>

struct bcm2711_buffer;
struct bcm2711_v3d;

/* The IRQ guard protects these latches; a serialized worker consumes them. */
struct bcm2711_v3d_events {
	uint32_t core;
	uint32_t hub;

	/* Arrival time diagnoses completion-to-worker delay; it never controls retirement. */
	uint64_t arrival_tick;
	uint32_t fault_client;
	uint32_t fault_address;
	int error;
};

/*
 * Diagnostic totals of native submissions over one reporting window.
 *
 * The controller mutex serializes every update and report.  Wall times
 * include preemption by other runnable threads and never control lifetime,
 * retirement or scheduling.
 */
struct bcm2711_v3d_perf {
	uint64_t window_start;
	uint32_t submissions;
	uint32_t render_passes;
	uint32_t clear_passes;
	uint32_t image_passes;
	uint32_t draws;
	uint64_t staged_bytes;
	uint64_t total_ticks;
	uint64_t max_ticks;
	uint64_t prepare_ticks;
	uint64_t texture_ticks;
	uint64_t texel_copy_ticks;
	uint64_t program_ticks;
	uint64_t record_ticks;
	uint64_t allocation_ticks;
	uint64_t mapping_ticks;
	uint64_t clean_ticks;
	uint64_t execute_ticks;
	uint64_t bin_ticks;
	uint64_t render_ticks;
	uint64_t output_clean_ticks;
	uint64_t release_ticks;
	uint64_t transfer_ticks;
};

/*
 * The engine retains both allocations once the MMU can fetch them.
 * The single worker owns cache/MMU commands and ready/reset counters.  guard
 * protects IRQ admission, faulted, and event latches; no waits occur under it.
 * initialized is published before installing the persistent IRQ owner.
 */
struct bcm2711_v3d_hardware {
	struct spinlock guard;
	struct bcm2711_buffer *pages;
	struct bcm2711_buffer *scratch;

	/* An uncertain boot diagnostic retains its allocation and occupied VA interval. */
	struct bcm2711_buffer *diagnostic;
	uint32_t diagnostic_address;
	struct bcm2711_v3d_events events;
	uint32_t hub_ident[4];
	uint32_t core_ident[3];
	uint32_t physical_bits;
	uint32_t virtual_bits;
	uint64_t resets;
	/* Serialized maintenance diagnostics count actual yielding waits, never IRQ work. */
	uint64_t maintenance_yields;
	/* The controller mutex serializes the periodic submission cost report. */
	struct bcm2711_v3d_perf perf;
	bool initialized;
	bool ready;
	bool mmu_published;
	bool irq_live;
	bool faulted;
	bool job_busy;
};

int bcm2711_v3d_hardware_start(struct bcm2711_v3d *engine);
int bcm2711_v3d_diagnostic(struct bcm2711_v3d *engine);
void bcm2711_v3d_hardware_mask(struct bcm2711_v3d *engine);
int bcm2711_v3d_hardware_reset(struct bcm2711_v3d *engine);
int bcm2711_v3d_hardware_pages_sync(struct bcm2711_v3d *engine, uint32_t address, uint64_t bytes);
int bcm2711_v3d_hardware_invalidate(struct bcm2711_v3d *engine);
int bcm2711_v3d_hardware_clean(struct bcm2711_v3d *engine);
void bcm2711_v3d_hardware_events(struct bcm2711_v3d *engine, struct bcm2711_v3d_events *events);

#endif
