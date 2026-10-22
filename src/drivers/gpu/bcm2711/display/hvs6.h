/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The BCM2712's display path: the HVS6 compositor and the two pixel valves
 * that feed the HDMI ports.
 *
 * The firmware sets the mode and keeps the HDMI encoders, the pixel valves'
 * timing and the compositor's own configuration; the driver takes over only
 * the display list of the channel the firmware shows the console on, and
 * the pixel valve's vertical-blank interrupt.
 */

#ifndef KERN_DRIVERS_GPU_BCM2711_DISPLAY_HVS6_H
#define KERN_DRIVERS_GPU_BCM2711_DISPLAY_HVS6_H

#include <stdbool.h>
#include <stdint.h>

#include <drivers/generic/fdt.h>
#include <kern/lock.h>
#include <kern/platform.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"

/* The stage-mark family of the HVS6 display, and its boot parameter prefix. */
#define BCM2712_FAMILY_DISPLAY		"hvs6"

/* The compositor's output channels and the pixel valves (HDMI0, HDMI1). */
#define BCM2712_HVS_CHANNELS		3U
#define BCM2712_HVS_VALVES		2U

/* The most planes of one display list the readout keeps. */
#define BCM2712_HVS_LIST_PLANES		8U

/*
 * One plane of a display list, as the readout decoded it.
 *
 * start is the word the plane begins at and words how many it has; the
 * fields are those of an unscaled raster plane, and scaled says the plane
 * has the scaled-size word, which shifts the others by one.
 */
struct bcm2712_hvs_plane {
	uint32_t start;
	uint32_t words;
	uint32_t control;
	uint32_t format;
	uint32_t order;
	uint32_t address_mode;
	bool scaled;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t pointer0;
	uint64_t address;
	uint32_t pitch;
	uint32_t upm_handle;
	uint32_t upm_base;
};

/*
 * The display path.
 *
 * One instance exists for the system.  Discovery fills the windows and the
 * interrupt lines; the readout fills the rest from the hardware.  guard
 * protects vblank_count against the pixel valve's handler.
 */
struct bcm2712_display {
	bool present;
	struct bcm2711_window compositor;
	struct bcm2711_window valve[BCM2712_HVS_VALVES];
	struct bcm2711_irq_line valve_irq[BCM2712_HVS_VALVES];
	struct bcm2711_window iommu;
	struct kern_boot_framebuffer screen;
	bool screen_known;

	/* The register layout: C0 (0x53) or D0 (0x54), and where channel 0's block starts and how far apart the blocks are. */
	uint32_t version;
	uint32_t channel_base;
	uint32_t channel_stride;

	/* The display list memory's words and the unified pixel memory's size in 256-byte words. */
	uint32_t list_words;
	uint32_t ubm_words;

	/* The channel the firmware shows, its list, and where that list ends (its END word). */
	uint32_t channel;
	uint32_t firmware_head;
	uint32_t firmware_end;
	uint32_t plane_count;
	uint32_t skipped_elements;
	struct bcm2712_hvs_plane planes[BCM2712_HVS_LIST_PLANES];

	/* The driver's own list, once it took the channel over. */
	bool adopted;
	uint32_t own_head;
	uint32_t own_words;

	/* Vertical blanks counted by the pixel valve's handler. */
	struct spinlock guard;
	volatile uint64_t vblank_count;
};

int bcm2712_display_discover(const struct drv_fdt *fdt, struct bcm2712_display *display, const struct kern_boot_framebuffer *screen);
int bcm2712_display_readout(struct bcm2712_display *display);
int bcm2712_display_adopt(struct bcm2712_display *display);
int bcm2712_display_vblank_test(struct bcm2712_display *display);
int bcm2712_display_overlay_test(struct bcm2712_display *display);

#endif
