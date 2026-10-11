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
#include <kern/waitq.h>

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
	uint32_t skipped_copies;
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
	uint64_t tfu_ticks;
	uint64_t bin_ticks;
	uint64_t render_ticks;
	uint64_t output_clean_ticks;
	uint64_t release_ticks;
	uint64_t transfer_ticks;
	uint32_t snapshot_hits;
	uint32_t snapshot_fills;
	uint32_t mapped_textures;
	uint32_t private_textures;
	uint64_t mapped_bytes;
	uint32_t largest_size;
	char largest_kind;
	uint32_t render_tiles;
	uint32_t image_tiles;
	/* The performance counters' sums over the window's render runs, and how many runs. */
	uint64_t counters[16];
	uint32_t counter_runs;
	/* The cycle counter split between passes without draws (tile copies and clears) and with draws, and their tiles. */
	uint64_t copy_cycles;
	uint64_t draw_cycles;
	uint32_t copy_tiles;
	uint32_t draw_tiles;
	/* Worker requests: their FIFO waits and worker time, sums and maxima. */
	uint64_t requests;
	uint64_t request_wait_ticks;
	uint64_t request_run_ticks;
	uint64_t request_wait_max;
	uint64_t request_run_max;
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
	/* The IRQ handler advances this queue under guard after latching events; the worker sleeps on it. */
	struct wait_queue events_queue;
	uint32_t hub_ident[4];
	uint32_t core_ident[3];
	uint32_t physical_bits;
	uint32_t virtual_bits;
	uint64_t resets;
	/* Serialized maintenance diagnostics count actual yielding waits, never IRQ work. */
	uint64_t maintenance_yields;
	/* The controller mutex serializes the periodic submission cost report. */
	struct bcm2711_v3d_perf perf;
	/* A boot self-test proved TFU raster-to-UIF output equal to the CPU conversion; until then textures use the CPU. */
	bool tfu_uif_ready;
	/* The performance counters have their sources and run (cleared by a reset). */
	bool counters_ready;
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

/* Reads the event queue's sequence before looking at the events, so a later IRQ ends the next wait at once. */
uint64_t bcm2711_v3d_hardware_event_sequence(struct bcm2711_v3d *engine);

/* Sleeps until an IRQ after the observed sequence, or until the next scheduler tick at the latest. */
void bcm2711_v3d_hardware_event_wait(struct bcm2711_v3d *engine, uint64_t observed);

#endif
