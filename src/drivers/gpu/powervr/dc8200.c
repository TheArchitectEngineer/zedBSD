/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The VisionFive 2's display controller as the loader left it.
 *
 * The loader lights the HDMI output and scans its framebuffer through one
 * of the controller's primary planes.  At attach the driver reads the
 * controller's identity and both primary planes, decides whether the
 * plane on the HDMI panel shows exactly the loader's framebuffer, and
 * keeps what it read as the place to return the screen to.  It also
 * watches the panel's scan position and the plane's commit bit for a
 * moment, to learn how they behave.  Nothing is written: the screen stays
 * the loader's, drawn by the copying display node.
 *
 * The outcome is one line of the boot log, which the screen shows.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/powervr/dc8200-adopt.h"
#include "drivers/gpu/powervr/powervr.h"
#include <kern/boot.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/platform.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/*
 * The controller's identity registers, from the control window's base.
 *
 * Values referenced:
 * Linux torvalds/linux master drivers/gpu/drm/verisilicon/vs_dc_top_regs.h
 * (the chip model at 0x20, the revision at 0x24, the customer id at 0x30).
 */
#define DC_MODEL		0x20U
#define DC_REVISION		0x24U
#define DC_CUSTOMER		0x30U

/*
 * Where the display window starts among the controller's offsets, and the
 * registers of primary plane 0 and panel 0; plane and panel 1 are 4 bytes
 * further.
 *
 * Values referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/verisilicon/vs_dc_hw.h (the register base 0x800, the
 * framebuffer's address 0x1400, stride 0x1408, configuration 0x1518 and
 * its extension 0x1cc0, size 0x1810, corners 0x24d8 and 0x24e0,
 * background 0x1528, blending 0x2510; the panel's configuration 0x1418,
 * its extension 0x2518 and the current location 0x1450) and vs_dc_hw.c
 * (the second plane's registers 4 bytes on).
 */
#define DC_DISPLAY_BASE		0x800U
#define DC_PLANE_ADDRESS	0x1400U
#define DC_PLANE_STRIDE		0x1408U
#define DC_PLANE_CONFIG		0x1518U
#define DC_PLANE_EXTRA		0x1cc0U
#define DC_PLANE_SIZE		0x1810U
#define DC_PLANE_TOP_LEFT	0x24d8U
#define DC_PLANE_BOTTOM_RIGHT	0x24e0U
#define DC_PLANE_BACKGROUND	0x1528U
#define DC_PLANE_BLEND		0x2510U
#define DC_PANEL_CONFIG		0x1418U
#define DC_PANEL_EXTRA		0x2518U
#define DC_PANEL_LOCATION	0x1450U
#define DC_UNIT_STEP		4U

/*
 * The panel the HDMI output is driven from.
 *
 * The vendor's driver feeds display 0's pixel clock from the HDMI
 * transmitter, so the HDMI panel is taken to be panel 0; the attach's
 * record shows whether the lit plane agrees.
 *
 * Value referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/verisilicon/vs_dc.c (display 0's pixel clock parent).
 */
#define DC_HDMI_PANEL		0U

/* How long the scan position is watched: a little over two frames at 60 Hz. */
#define DC_WATCH_MICROSECONDS	40000U

/* How many reads stand in for the watch when the kernel's counter is not running. */
#define DC_WATCH_READS		200000U

/*
 * What the panel's scan position and the plane's commit bit did while they
 * were watched.
 *
 * Wraps counts the times the position went down, which a counter of the
 * scan line does once a frame.
 */
struct dc_watch {
	uint32_t first;
	uint32_t lowest;
	uint32_t highest;
	uint32_t wraps;
	uint32_t reads;
	uint32_t commit_seen;
};

/*
 * The controller as the attach found it.
 *
 * Filled once by drv_powervr_dc_probe() on the boot CPU and not changed
 * afterwards; the plane registers it holds are the loader's screen, the
 * place a later takeover returns the screen to.  Probed is zero until
 * then.
 */
struct dc_adoption {
	int probed;
	volatile uint8_t *control;
	volatile uint8_t *display;
	struct powervr_dc_state state;
	struct powervr_dc_screen screen;
	enum powervr_dc_verdict verdict;
	unsigned plane;
	uint32_t panel_config;
	uint32_t panel_extra;
	struct dc_watch watch;
};

/* The board's one controller, as drv_powervr_dc_probe() found it. */
static struct dc_adoption board_adoption;

static uint32_t read_control(const struct dc_adoption *adoption, uint32_t offset);
static uint32_t read_display(const struct dc_adoption *adoption, uint32_t offset);
static void read_state(struct dc_adoption *adoption);
static void watch_scan(struct dc_adoption *adoption, uint32_t extra_offset);
static void log_outcome(const struct dc_adoption *adoption);

/*
 * Reads the display controller the loader left on and decides whether its
 * screen can be taken over, without changing any of its registers.
 */
int
drv_powervr_dc_probe(
	const struct drv_powervr_dc_place *place)
{
	struct dc_adoption *adoption;
	struct kern_boot_framebuffer framebuffer;
	void *mapped;
	unsigned plane;
	int error;

	/* Refuses a missing place, a second probe, or a display window too small for the planes. */
	adoption = &board_adoption;
	if (place == NULL)
		return EINVAL;
	if (adoption->probed)
		return EALREADY;
	if (place->display_size < DC_PANEL_EXTRA + DC_UNIT_STEP * 2U - DC_DISPLAY_BASE)
		return EINVAL;
	if (place->control_size < DC_CUSTOMER + DC_UNIT_STEP)
		return EINVAL;

	/* Reads the loader's screen; without one the controller is not lit and is not touched. */
	error = kern_boot_framebuffer("riscv64.framebuffer", &framebuffer);
	if (error != 0) {
		kern_logf("dc8200: no-go (no boot framebuffer, %d)\n", error);
		return ENOENT;
	}

	adoption->screen.physical = framebuffer.physical_base;
	adoption->screen.pitch = framebuffer.pitch;
	adoption->screen.width = framebuffer.width;
	adoption->screen.height = framebuffer.height;

	/* Maps the identity registers. */
	error = kern_device_map(place->control, (size_t)place->control_size, KERN_DEVICE_UNCACHED, &mapped);
	if (error != 0) {
		kern_logf("dc8200: no-go (control map failed, %d)\n", error);
		return error;
	}

	adoption->control = mapped;

	/* Maps the planes' and panels' registers. */
	error = kern_device_map(place->display, (size_t)place->display_size, KERN_DEVICE_UNCACHED, &mapped);
	if (error != 0) {
		(void)kern_device_unmap((void *)adoption->control, (size_t)place->control_size);
		adoption->control = NULL;
		kern_logf("dc8200: no-go (display map failed, %d)\n", error);
		return error;
	}

	adoption->display = mapped;

	/* Reads the identity and both planes as the loader left them. */
	read_state(adoption);

	/* Decides whether the HDMI panel's plane is exactly the loader's framebuffer. */
	adoption->verdict = powervr_dc_judge(&adoption->state, &adoption->screen, DC_HDMI_PANEL, &plane);
	adoption->plane = plane;

	/* Watches the scan position, and the commit bit of the lit plane (plane 0 when none is lit). */
	if (plane >= POWERVR_DC_PLANES)
		plane = 0;
	watch_scan(adoption, DC_PLANE_EXTRA + DC_UNIT_STEP * plane);

	/* Keeps the record for the takeover and writes it to the boot log. */
	adoption->probed = 1;
	log_outcome(adoption);

	/* Succeeded: the controller is read and the decision recorded, go or not. */
	return 0;
}

/* Reads one of the identity registers. */
static uint32_t
read_control(
	const struct dc_adoption *adoption,
	uint32_t offset)
{
	/* The registers are 32 bits wide at their byte offsets. */
	return kern_mmio_read32(adoption->control + offset);
}

/* Reads one of the planes' or panels' registers by its controller offset. */
static uint32_t
read_display(
	const struct dc_adoption *adoption,
	uint32_t offset)
{
	/* The display window starts at the controller's offset of its base. */
	return kern_mmio_read32(adoption->display + (offset - DC_DISPLAY_BASE));
}

/* Reads the controller's identity, both primary planes and the HDMI panel's settings. */
static void
read_state(
	struct dc_adoption *adoption)
{
	struct powervr_dc_plane *plane;
	uint32_t step;
	unsigned index;

	/* Reads which controller this is. */
	adoption->state.model = read_control(adoption, DC_MODEL);
	adoption->state.revision = read_control(adoption, DC_REVISION);
	adoption->state.customer = read_control(adoption, DC_CUSTOMER);

	/* Reads every register a plane is set up by, for each primary plane. */
	for (index = 0; index < POWERVR_DC_PLANES; index++) {
		plane = &adoption->state.planes[index];
		step = DC_UNIT_STEP * index;
		plane->address = read_display(adoption, DC_PLANE_ADDRESS + step);
		plane->stride = read_display(adoption, DC_PLANE_STRIDE + step);
		plane->config = read_display(adoption, DC_PLANE_CONFIG + step);
		plane->extra = read_display(adoption, DC_PLANE_EXTRA + step);
		plane->size = read_display(adoption, DC_PLANE_SIZE + step);
		plane->top_left = read_display(adoption, DC_PLANE_TOP_LEFT + step);
		plane->bottom_right = read_display(adoption, DC_PLANE_BOTTOM_RIGHT + step);
		plane->background = read_display(adoption, DC_PLANE_BACKGROUND + step);
		plane->blend = read_display(adoption, DC_PLANE_BLEND + step);
	}

	/* Reads the HDMI panel's settings, including the word that holds back shadowed updates. */
	adoption->panel_config = read_display(adoption, DC_PANEL_CONFIG + DC_UNIT_STEP * DC_HDMI_PANEL);
	adoption->panel_extra = read_display(adoption, DC_PANEL_EXTRA + DC_UNIT_STEP * DC_HDMI_PANEL);
}

/*
 * Reads the HDMI panel's scan position and a plane's commit bit over a
 * little more than two frames, keeping the range the position covered,
 * how often it went down, and whether the commit bit was ever set.
 *
 * Only reads: the loader's commit has long been taken, so a set bit means
 * the bit stays set rather than clearing itself.
 */
static void
watch_scan(
	struct dc_adoption *adoption,
	uint32_t extra_offset)
{
	struct dc_watch *watch;
	uint64_t start;
	uint64_t now;
	uint64_t frequency;
	uint64_t span;
	uint32_t location;
	uint32_t previous;
	uint32_t extra;
	bool counting;

	/* Starts the record from the first reading. */
	watch = &adoption->watch;
	location = read_display(adoption, DC_PANEL_LOCATION + DC_UNIT_STEP * DC_HDMI_PANEL);
	extra = read_display(adoption, extra_offset);
	watch->first = location;
	watch->lowest = location;
	watch->highest = location;
	watch->wraps = 0;
	watch->reads = 1;
	watch->commit_seen = 0;
	if ((extra & POWERVR_DC_EXTRA_COMMIT) != 0)
		watch->commit_seen = 1;

	/* Times the watch by the kernel's counter, or by a count of reads when it is not running. */
	counting = kern_rtc_read_counter(&start, &frequency);
	span = 0;
	if (counting && frequency != 0)
		span = (frequency * DC_WATCH_MICROSECONDS) / 1000000U;

	/* Reads the position and the bit until the watch is over. */
	previous = location;
	for (;;) {
		/* Takes one reading of the position and the bit. */
		location = read_display(adoption, DC_PANEL_LOCATION + DC_UNIT_STEP * DC_HDMI_PANEL);
		extra = read_display(adoption, extra_offset);
		watch->reads++;

		/* Widens the range the position covered. */
		if (location < watch->lowest)
			watch->lowest = location;
		if (location > watch->highest)
			watch->highest = location;

		/* Counts a fall of the position, the start of a new frame for a line counter. */
		if (location < previous)
			watch->wraps++;
		previous = location;

		/* Notes a commit bit that is set, which a self-clearing bit never shows here. */
		if ((extra & POWERVR_DC_EXTRA_COMMIT) != 0)
			watch->commit_seen = 1;

		/* Ends by the counter when it runs, otherwise by the count of reads. */
		if (span != 0) {
			counting = kern_rtc_read_counter(&now, &frequency);
			if (!counting || now - start >= span)
				break;
		} else if (watch->reads >= DC_WATCH_READS) {
			break;
		}
	}
}

/* Writes the decision and what it rests on as one line of the boot log. */
static void
log_outcome(
	const struct dc_adoption *adoption)
{
	const struct powervr_dc_plane *plane;
	const char *outcome;
	unsigned shown;

	/* Shows the lit plane, or plane 0 when none is lit. */
	shown = adoption->plane;
	if (shown >= POWERVR_DC_PLANES)
		shown = 0;
	plane = &adoption->state.planes[shown];

	/* Names the outcome the takeover's next step depends on. */
	outcome = "no-go";
	if (adoption->verdict == POWERVR_DC_GO)
		outcome = "go";

	kern_logf("dc8200: %s (%s) model %x rev %x cid %x plane %u addr %x stride %u size %x cfg %x ex %x panel %x/%x loc %x %x-%x wraps %u reads %u commit %u fb %llx/%u\n",
		  outcome,
		  powervr_dc_verdict_name(adoption->verdict),
		  adoption->state.model,
		  adoption->state.revision,
		  adoption->state.customer,
		  shown,
		  plane->address,
		  plane->stride,
		  plane->size,
		  plane->config,
		  plane->extra,
		  adoption->panel_config,
		  adoption->panel_extra,
		  adoption->watch.first,
		  adoption->watch.lowest,
		  adoption->watch.highest,
		  adoption->watch.wraps,
		  adoption->watch.reads,
		  adoption->watch.commit_seen,
		  (unsigned long long)adoption->screen.physical,
		  adoption->screen.pitch);
}
