/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Synchronous primary-plane flips within the boot display's established mode. */
#ifndef KERN_DRIVERS_GPU_BCM2711_DISPLAY_FLIP_H
#define KERN_DRIVERS_GPU_BCM2711_DISPLAY_FLIP_H

#include <stdbool.h>
#include <stdint.h>
#include <kern/lock.h>

#include "drivers/gpu/bcm2711/bcm2711-gpu.h"

/* Two private SRAM lists alternate while the boot console list stays intact. */
#define BCM2711_FLIP_SLOTS 2U

/*
 * The cursor plane drawn above a composed frame: an unscaled image of at most
 * 64x64 pixels with premultiplied alpha, in contiguous RAM below one GiB that
 * stays unchanged while a list names it.  x and y place its top-left corner
 * and may lie partly outside the output, which clips it.
 */
struct bcm2711_flip_cursor {
	bool shown;
	uint32_t physical;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t format;
	int32_t x;
	int32_t y;
};

/*
 * The intervals between events of one kind in a cadence window (v3d.perf=1
 * only): the time of the last event, the events of the window, and the
 * first intervals of the window in microseconds.
 */
#define BCM2711_FLIP_CADENCE_SAMPLES	256U
struct bcm2711_flip_cadence {
	uint64_t last;
	uint32_t count;
	uint32_t stored;
	uint32_t intervals[BCM2711_FLIP_CADENCE_SAMPLES];
};

/* The kinds of display event whose cadence is measured: cursor sets, cursor moves and adopted flips. */
#define BCM2711_FLIP_CADENCE_SET	0U
#define BCM2711_FLIP_CADENCE_MOVE	1U
#define BCM2711_FLIP_CADENCE_FLIP	2U
#define BCM2711_FLIP_CADENCE_KINDS	3U

/* Whether the cadence is measured: the boot line is read at the first event. */
#define BCM2711_FLIP_CADENCE_UNREAD	0U
#define BCM2711_FLIP_CADENCE_OFF	1U
#define BCM2711_FLIP_CADENCE_ON		2U

/* One display owns this state until shutdown; guard protects IRQ and callers. */
struct bcm2711_flip_state {
	struct spinlock guard;
	bool initialized;
	bool attached;
	bool busy;
	bool uncertain;
	bool completed;
	uint32_t active_list;
	uint32_t pending_list;
	uint32_t retained_mask;
	uint64_t frame_sequence;
	uint32_t core_hz;
	int clock_error;
	struct drv_bcm2711_boot_screen frames[BCM2711_FLIP_SLOTS];
	/*
	 * The planes of each slot's composed list before the cursor (console and
	 * frame), so that a cursor change republishes the shown frame with the
	 * cursor in the other slot; composed[slot] says the slot has them.
	 * cursor_dirty says the cursor changed since the last list was written.
	 */
	uint32_t base_words[BCM2711_FLIP_SLOTS][16];
	bool composed[BCM2711_FLIP_SLOTS];
	/* The cursor image each slot's list shows, 0 for none. */
	uint32_t cursor_images[BCM2711_FLIP_SLOTS];
	struct bcm2711_flip_cursor cursor;
	bool cursor_dirty;
	uint64_t cursor_lists;
	/* Diagnostic flip-stage tick totals of one reporting window, written only by the BUSY owner. */
	uint64_t perf_start;
	uint64_t perf_flips;
	uint64_t perf_clean;
	uint64_t perf_raise;
	uint64_t perf_wait;
	uint64_t perf_finish;
	/*
	 * The cursor images set in the window and those that needed their rows
	 * copied (the others were shown again unchanged); added atomically by
	 * the display device under its own mutex, read and cleared by the report.
	 */
	uint64_t perf_cursor_sets;
	uint64_t perf_cursor_copies;
	/*
	 * The cadence of display events (WS213), kept under guard; reporting
	 * says one caller is printing the copied window, so no other starts.
	 */
	unsigned cadence_state;
	uint64_t cadence_start;
	bool cadence_reporting;
	struct bcm2711_flip_cadence cadence[BCM2711_FLIP_CADENCE_KINDS];
};

/* A caller-owned snapshot names borrowed buffers that must remain DMA-readable. */
struct bcm2711_flip_status {
	bool attached;
	bool busy;
	bool uncertain;
	uint32_t active_list;
	uint32_t pending_list;
	uint32_t retained_mask;
	uint64_t frame_sequence;
	uint32_t core_hz;
	int clock_error;
	struct drv_bcm2711_boot_screen frames[BCM2711_FLIP_SLOTS];
};

/* The driver keeps this owner and its mapped registers alive throughout use. */
struct bcm2711_display;

/* Initialization precedes IRQ publication; it must never reset a live owner. */
void bcm2711_display_flip_init(struct bcm2711_display *display);

/* Attachment requires a successful R0 and sole ownership of HVS channel zero. */
int bcm2711_display_flip_attach(struct bcm2711_display *display);

/* Registration unwind may detach only at the confirmed, unheld console list. */
int bcm2711_display_flip_detach(struct bcm2711_display *display);

/*
 * Caller serializes buffer ownership, freezes bytes before submission and keeps
 * storage contiguous and mapped. Never free or modify a retained buffer, even
 * after an error. Snapshot after each call determines which buffers are held.
 * Calls may busy-wait up to 100 ms; never call from IRQ or while holding a lock.
 */
int bcm2711_display_flip_present(struct bcm2711_display *display, const struct drv_bcm2711_boot_screen *frame);

/* Composes an unscaled upper plane over the console, with optional premultiplied alpha. */
int bcm2711_display_flip_compose(struct bcm2711_display *display, const struct drv_bcm2711_boot_screen *frame, uint32_t x, uint32_t y, bool premultiplied);

/*
 * Records the cursor and shows it from the next refresh boundary without
 * waiting: over the shown composed frame now when no list is pending, or in
 * the next list otherwise.  The image must stay unchanged until a list
 * without it is adopted (bcm2711_display_flip_settle after the change).
 */
int bcm2711_display_flip_cursor(struct bcm2711_display *display, const struct bcm2711_flip_cursor *cursor);

/* Counts one cursor image set for the flip report, copied or shown again unchanged. */
void bcm2711_display_flip_count_cursor(struct bcm2711_display *display, bool copied);

/*
 * Notes the time of one display event of a cadence kind, and prints the
 * cadence window every two seconds; nothing without v3d.perf=1.  The
 * caller holds no spinlock.
 */
void bcm2711_display_flip_note(struct bcm2711_display *display, unsigned kind);

/* Tells whether a retained list or the recorded cursor names a cursor image, which must then stay unchanged. */
bool bcm2711_display_flip_cursor_named(struct bcm2711_display *display, uint32_t physical);

/* Waits until no published list is pending; the caller excludes other publishers. */
int bcm2711_display_flip_settle(struct bcm2711_display *display);

/* Only confirmed boot-console adoption releases buffers after uncertain DMA. */
int bcm2711_display_flip_restore(struct bcm2711_display *display);
void bcm2711_display_flip_snapshot(struct bcm2711_display *display, struct bcm2711_flip_status *status);

/* Only the selected PV service calls this, with flip.guard held after W1C. */
void bcm2711_display_flip_vblank_locked(struct bcm2711_display *display, uint32_t current);

#endif
