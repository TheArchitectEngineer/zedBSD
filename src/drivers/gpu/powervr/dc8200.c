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
 * moment, to learn how they behave.  Nothing is written at attach: the
 * screen stays the loader's, drawn by the copying display node.
 *
 * When the screen can be taken over, the display node then turns on the
 * panel's vertical-blank interrupt, which counts frames for the rest of
 * the system's life, and commits a program's buffer to the same plane at
 * the loader's extent and row pitch, or the loader's framebuffer back.
 * Every commit holds the shadowed registers back while it writes them,
 * under a lock that also keeps a fatal stop's return of the plane from
 * being overwritten.
 *
 * The outcome of the attach is one line of the boot log, which the screen
 * shows.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/powervr/dc8200-adopt.h"
#include "drivers/gpu/powervr/powervr.h"
#include <hal/hal.h>
#include <kern/atomic.h>
#include <kern/boot.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/platform.h>
#include <kern/pmem.h>
#include <kern/sched.h>
#include <kern/waitq.h>
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
 * The interrupt registers in the control window: the pending panels,
 * cleared by reading them, and the enabled ones, one bit per panel.
 *
 * Values referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/verisilicon/vs_dc_hw.h (AQ_INTR_ACKNOWLEDGE 0x10,
 * AQ_INTR_ENBL 0x14), vs_dc_hw.c (dc_hw_enable_interrupt: the enable bits
 * are the CRTC mask) and vs_crtc.c (vs_crtc_enable_vblank: one bit per
 * CRTC in its order).
 */
#define DC_INTERRUPT_PENDING	0x10U
#define DC_INTERRUPT_ENABLE	0x14U

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
 * The panel bit that holds back the shadowed registers while it is set.
 *
 * Value referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/verisilicon/vs_dc_hw.c (dc_hw_enable_shadow_register:
 * holding sets bit 0 of the panel's extension and clears the planes'
 * commit bit; releasing does the reverse).
 */
#define DC_PANEL_EXTRA_HOLD	(1U << 0)

/*
 * The pixel layout of a program's frame on the plane: 32 bits a pixel
 * with the alpha byte ignored, and the order of its colour bytes.
 *
 * Values referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/verisilicon/vs_dc_hw.h (FORMAT_X8R8G8B8 5; the swizzles
 * ARGB 0 and ABGR 2) and vs_dc.c (XRGB8888, bytes B G R X, takes ARGB;
 * XBGR8888, bytes R G B X, takes ABGR).
 */
#define DC_CONFIG_FORMAT_SHIFT	26U
#define DC_CONFIG_FORMAT_MASK	0x3fU
#define DC_CONFIG_FORMAT_XRGB	5U
#define DC_CONFIG_SWIZZLE_SHIFT	23U
#define DC_CONFIG_SWIZZLE_MASK	0x3U
#define DC_CONFIG_SWIZZLE_ARGB	0U
#define DC_CONFIG_SWIZZLE_ABGR	2U

/*
 * How long the vertical blanks may stop before a wait for them gives up
 * and the output counts as lost: six frames at 60 Hz.
 */
#define DC_STALL_TICKS		KERN_MS_TO_TICKS(100U)

/* How many vertical blanks' scan positions the interrupt keeps for a test. */
#define DC_BLANK_RING		64U

/*
 * How many times a fatal stop tries the commit lock before it writes
 * without it.
 */
#define DC_FATAL_LOCK_TRIES	4096U

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
	uint32_t interrupt;
	struct powervr_dc_state state;
	struct powervr_dc_screen screen;
	enum powervr_dc_verdict verdict;
	unsigned plane;
	uint32_t panel_config;
	uint32_t panel_extra;
	struct dc_watch watch;
};

/*
 * The vertical-blank count of the HDMI panel.
 *
 * Started by drv_powervr_dc_vblank_start() and kept for the life of the
 * system.  The interrupt alone advances sequence and fills the ring; both
 * change under lock, and sequence is also read without it through an
 * acquire load.  Last_tick is the scheduler tick of the newest blank.  The
 * ring keeps the panel's raw scan position as each interrupt read it, so
 * a test sees where in the frame the interrupt comes.  Others counts
 * interrupts for the other panel only, which do not advance the count.
 */
struct dc_blanks {
	struct spinlock lock;
	struct wait_queue wake;
	int started;
	int irq;
	uint64_t sequence;
	uint64_t last_tick;
	uint64_t others;
	uint32_t lowest;
	uint32_t highest;
	uint32_t next;
	uint32_t ring[DC_BLANK_RING];
};

/* The board's one controller, as drv_powervr_dc_probe() found it. */
static struct dc_adoption board_adoption;

/* The HDMI panel's vertical blanks, counted once started. */
static struct dc_blanks board_blanks;

/*
 * Serializes every write of the planes' registers: a commit's whole
 * sequence and a fatal stop's.  A leaf lock, held for a few register
 * writes with interrupts disabled.
 */
static struct spinlock dc_commit_lock = {
	{ 0 }, LOCK_RANK_DEVICE, "dc8200 commit", 0, 0
};

/*
 * Set once by a fatal stop and never cleared: from then on no commit
 * writes the planes.  Read under dc_commit_lock by a commit.
 */
static volatile unsigned dc_fatal;

static uint32_t read_control(const struct dc_adoption *adoption, uint32_t offset);
static uint32_t read_display(const struct dc_adoption *adoption, uint32_t offset);
static void write_display(const struct dc_adoption *adoption, uint32_t offset, uint32_t value);
static int commit_held_here(void);
static void read_state(struct dc_adoption *adoption);
static void watch_scan(struct dc_adoption *adoption, uint32_t extra_offset);
static void log_outcome(const struct dc_adoption *adoption);
static void write_control(const struct dc_adoption *adoption, uint32_t offset, uint32_t value);
static void write_plane(const struct dc_adoption *adoption, const struct powervr_dc_plane *plane);
static void blank_interrupt(int irq, kern_irq_ack_t acknowledge, void *argument);

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
	adoption->interrupt = place->interrupt;

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

/*
 * Returns the screen to the loader's plane for a fatal stop.
 */
void
drv_powervr_dc_restore_fatal(
	void)
{
	const struct dc_adoption *adoption;
	const struct powervr_dc_plane *plane;
	unsigned tries;
	int locked;

	/* Stops every later commit before anything is written. */
	atomic_raw_store_release(&dc_fatal, 1U);

	/* Leaves a controller the probe did not find showing the loader's screen alone. */
	adoption = &board_adoption;
	if (!adoption->probed || adoption->verdict != POWERVR_DC_GO)
		return;
	if (adoption->plane >= POWERVR_DC_PLANES)
		return;

	/*
	 * Waits a bounded time for a commit on another CPU to finish.  A
	 * commit this CPU was in when it stopped is not waited for: trying a
	 * lock this CPU holds would trap.
	 */
	locked = 0;
	tries = 0;
	if (commit_held_here())
		tries = DC_FATAL_LOCK_TRIES;
	for (; tries < DC_FATAL_LOCK_TRIES; tries++) {
		/* Stops at the first try that takes the lock. */
		if (spin_trylock(&dc_commit_lock)) {
			locked = 1;
			break;
		}
	}

	/* Writes the plane as the probe read it and lets the panel take it at its next frame. */
	plane = &adoption->state.planes[adoption->plane];
	write_plane(adoption, plane);

	/* Releases the lock when it was taken. */
	if (locked)
		spin_unlock(&dc_commit_lock);
}

/*
 * Reports whether a fatal stop has taken the screen back.
 */
int
drv_powervr_dc_fatal(
	void)
{
	/* Pairs with the release store of the fatal stop. */
	return atomic_raw_load_acquire(&dc_fatal) != 0U;
}

/*
 * Turns on the HDMI panel's vertical-blank interrupt and starts counting
 * the panel's frames.
 */
int
drv_powervr_dc_vblank_start(
	void)
{
	struct dc_adoption *adoption;
	struct dc_blanks *blanks;
	uint32_t enabled_panels;
	int error;

	/* Only a controller whose screen can be taken over, and only once. */
	adoption = &board_adoption;
	blanks = &board_blanks;
	if (!adoption->probed || adoption->verdict != POWERVR_DC_GO)
		return ENODEV;
	if (blanks->started)
		return EALREADY;

	/* The count starts at zero, with no scan position seen yet. */
	spin_init(&blanks->lock, LOCK_RANK_DEVICE, "dc8200 blanks");
	waitq_init(&blanks->wake, "dc8200 vblank");
	blanks->irq = (int)adoption->interrupt;
	blanks->sequence = 0;
	blanks->last_tick = 0;
	blanks->others = 0;
	blanks->lowest = 0xffffffffU;
	blanks->highest = 0;
	blanks->next = 0;

	/* Drops whatever the loader left pending before the handler can see it. */
	(void)read_control(adoption, DC_INTERRUPT_PENDING);

	/* Installs the handler before the panel may raise the interrupt. */
	error = kern_irq_register(blanks->irq, blank_interrupt, blanks);
	if (error != 0) {
		kern_logf("dc8200: vblank interrupt %d not registered (%d)\n", blanks->irq, error);
		return error;
	}

	blanks->started = 1;

	/* Lets the HDMI panel raise it, leaving the other panel's bit as it was. */
	enabled_panels = read_control(adoption, DC_INTERRUPT_ENABLE);
	enabled_panels |= 1U << DC_HDMI_PANEL;
	write_control(adoption, DC_INTERRUPT_ENABLE, enabled_panels);
	kern_logf("dc8200: vblank interrupt %d on, panel %u\n", blanks->irq, DC_HDMI_PANEL);

	/* Succeeded: the panel's frames are counted from now on. */
	return 0;
}

/*
 * Reports whether a program's buffer may be committed to the plane: the
 * screen could be taken over, frames are being counted and one was seen,
 * and no fatal stop took the screen back.
 */
int
drv_powervr_dc_scanout_ready(
	void)
{
	const struct dc_adoption *adoption;
	uint64_t sequence;
	int fatal;

	/* Only the controller whose screen can be taken over. */
	adoption = &board_adoption;
	if (!adoption->probed || adoption->verdict != POWERVR_DC_GO)
		return 0;

	/* A commit is judged by blanks, so the interrupt must have come at least once. */
	if (!board_blanks.started)
		return 0;

	sequence = atomic_u64_load_acquire(&board_blanks.sequence);
	if (sequence == 0)
		return 0;

	/* A fatal stop keeps the loader's plane for good. */
	fatal = drv_powervr_dc_fatal();
	if (fatal)
		return 0;

	return 1;
}

/*
 * Reports whether a frame of this extent and row pitch is scanned by the
 * plane as the loader set it up: only the address and the byte order of a
 * commit change, never the extent or the pitch.
 */
int
drv_powervr_dc_frame_fits(
	uint32_t width,
	uint32_t height,
	uint32_t stride)
{
	const struct dc_adoption *adoption;
	const struct powervr_dc_plane *plane;
	uint32_t plane_width;
	uint32_t plane_height;

	/* Only the controller whose screen can be taken over has a plane to match. */
	adoption = &board_adoption;
	if (!adoption->probed || adoption->verdict != POWERVR_DC_GO)
		return 0;

	plane = &adoption->state.planes[adoption->plane];

	/* The row pitch the plane reads with. */
	if (stride != plane->stride)
		return 0;

	/* The extent the plane shows. */
	plane_width = plane->size & POWERVR_DC_SIZE_WIDTH_MASK;
	plane_height = (plane->size >> POWERVR_DC_SIZE_HEIGHT_SHIFT) & POWERVR_DC_SIZE_WIDTH_MASK;
	if (width != plane_width)
		return 0;
	if (height != plane_height)
		return 0;

	return 1;
}

/*
 * Commits a program's frame, or the loader's framebuffer when frame is
 * NULL, to the plane, and reports the vertical-blank sequence read right
 * after the commit was written.
 */
int
drv_powervr_dc_commit(
	const struct drv_powervr_dc_frame *frame,
	uint64_t *base)
{
	const struct dc_adoption *adoption;
	struct powervr_dc_plane plane;
	unsigned long enabled;
	uint32_t format;
	uint32_t swizzle;

	/* Only the controller whose screen can be taken over. */
	adoption = &board_adoption;
	if (!adoption->probed || adoption->verdict != POWERVR_DC_GO)
		return ENODEV;

	/* Starts from the plane as the loader set it up; that is the framebuffer's commit. */
	plane = adoption->state.planes[adoption->plane];

	/*
	 * A frame changes only the address and the byte order: the extent and
	 * the pitch are the loader's, which the caller checked the frame has.
	 * The alpha byte is ignored, as the loader's blending is kept.
	 */
	if (frame != NULL) {
		if (frame->physical > 0xffffffffULL)
			return EINVAL;

		swizzle = DC_CONFIG_SWIZZLE_ARGB;
		if (frame->rgba)
			swizzle = DC_CONFIG_SWIZZLE_ABGR;

		format = DC_CONFIG_FORMAT_XRGB;
		plane.address = (uint32_t)frame->physical;
		plane.config &= ~(DC_CONFIG_FORMAT_MASK << DC_CONFIG_FORMAT_SHIFT);
		plane.config &= ~(DC_CONFIG_SWIZZLE_MASK << DC_CONFIG_SWIZZLE_SHIFT);
		plane.config |= format << DC_CONFIG_FORMAT_SHIFT;
		plane.config |= swizzle << DC_CONFIG_SWIZZLE_SHIFT;
	}

	/*
	 * Writes the plane under the commit lock with interrupts off, so a
	 * fatal stop on another CPU either comes first and is seen here, or
	 * waits for this commit and writes after it.  The sequence is read
	 * after the panel's hold is let go: a blank that came while the
	 * registers were written does not count for the commit.
	 */
	enabled = spin_lock_irqsave(&dc_commit_lock);

	if (drv_powervr_dc_fatal()) {
		spin_unlock_irqrestore(&dc_commit_lock, enabled);
		return ENXIO;
	}

	write_plane(adoption, &plane);
	*base = atomic_u64_load_acquire(&board_blanks.sequence);

	spin_unlock_irqrestore(&dc_commit_lock, enabled);

	/* Succeeded: the plane takes the commit at one of the next blanks. */
	return 0;
}

/*
 * Reports the HDMI panel's vertical-blank count.
 */
uint64_t
drv_powervr_dc_sequence(
	void)
{
	/* Pairs with the interrupt's release store. */
	return atomic_u64_load_acquire(&board_blanks.sequence);
}

/*
 * Waits until the vertical-blank count reaches target, or until no blank
 * has come for the stall limit (ETIMEDOUT).  The count reached is stored
 * through reached either way.
 */
int
drv_powervr_dc_wait(
	uint64_t target,
	uint64_t *reached)
{
	struct dc_blanks *blanks;
	unsigned long enabled;
	uint64_t start;
	uint64_t since;
	uint64_t deadline;
	uint64_t observed;
	uint64_t now;
	int error;

	/* Without the interrupt no blank ever comes. */
	blanks = &board_blanks;
	*reached = 0;
	if (!blanks->started)
		return ENODEV;

	/*
	 * Sleeps on the interrupt's wakeup.  The stall limit runs from the
	 * newest blank, or from the start of the wait when that is later, so
	 * a panel that keeps counting is waited for however long the target
	 * is away.
	 */
	error = 0;
	start = sched_ticks();
	enabled = spin_lock_irqsave(&blanks->lock);

	for (;;) {
		/* Stops once the count is there. */
		*reached = blanks->sequence;
		if (blanks->sequence >= target)
			break;

		/* Gives up once the blanks stopped for the limit. */
		since = blanks->last_tick;
		if (since < start)
			since = start;
		deadline = since + DC_STALL_TICKS;
		now = sched_ticks();
		if (now >= deadline) {
			error = ETIMEDOUT;
			break;
		}

		/* Sleeps until the next blank or the deadline; EAGAIN means one came meanwhile. */
		observed = waitq_sequence(&blanks->wake);
		(void)waitq_sleep(&blanks->wake, &blanks->lock, observed, deadline, 0);
	}

	spin_unlock_irqrestore(&blanks->lock, enabled);

	return error;
}

/*
 * Writes what the vertical-blank interrupt saw to the boot log: how many
 * blanks came, and the range of scan positions it read them at.
 */
void
drv_powervr_dc_vblank_report(
	void)
{
	struct dc_blanks *blanks;
	unsigned long enabled;
	uint64_t sequence;
	uint64_t others;
	uint32_t lowest;
	uint32_t highest;
	uint32_t newest;

	/* Nothing to say before the interrupt was started. */
	blanks = &board_blanks;
	if (!blanks->started)
		return;

	/* Samples the counters together. */
	enabled = spin_lock_irqsave(&blanks->lock);

	sequence = blanks->sequence;
	others = blanks->others;
	lowest = blanks->lowest;
	highest = blanks->highest;
	newest = blanks->ring[(blanks->next + DC_BLANK_RING - 1U) % DC_BLANK_RING];

	spin_unlock_irqrestore(&blanks->lock, enabled);

	kern_logf("dc8200: vblank %llu other %llu loc %x-%x newest %x\n",
		  (unsigned long long)sequence,
		  (unsigned long long)others,
		  lowest,
		  highest,
		  newest);
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

/*
 * Reports whether this CPU holds the commit lock.
 *
 * Reads the lock's owner the way spin_trylock() does before it traps on a
 * recursive acquisition: the owner's CPU is valid once owner_valid is
 * published with a release.
 */
static int
commit_held_here(
	void)
{
	/* A free lock is nobody's. */
	if (atomic_load_acquire(&dc_commit_lock.held) == 0U)
		return 0;

	/* A held lock whose owner is not yet published is another CPU's. */
	if (atomic_raw_load_acquire(&dc_commit_lock.owner_valid) == 0U)
		return 0;

	/* The lock is this CPU's when the published owner is this CPU. */
	return dc_commit_lock.owner_cpu == hal_cpu_current();
}

/* Writes one of the planes' or panels' registers by its controller offset. */
static void
write_display(
	const struct dc_adoption *adoption,
	uint32_t offset,
	uint32_t value)
{
	/* The display window starts at the controller's offset of its base. */
	kern_mmio_write32(adoption->display + (offset - DC_DISPLAY_BASE), value);
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

/* Writes one of the control window's registers. */
static void
write_control(
	const struct dc_adoption *adoption,
	uint32_t offset,
	uint32_t value)
{
	/* The registers are 32 bits wide at their byte offsets. */
	kern_mmio_write32(adoption->control + offset, value);
}

/*
 * Writes a plane's registers to the chosen plane and commits them: holds
 * back the HDMI panel's shadowed registers and drops the commit bit,
 * writes the plane, sets the commit bit and lets the panel take the
 * registers at its next frame.  The caller holds the commit lock, or is
 * a fatal stop that could not take it.
 *
 * Procedure referenced:
 * StarFive Linux JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * drivers/gpu/drm/verisilicon/vs_dc_hw.c (dc_hw_enable_shadow_register
 * around the plane writes); Linux torvalds/linux master
 * drivers/gpu/drm/verisilicon/vs_primary_plane.c (atomic_update: the
 * commit bit set after the plane's registers).
 */
static void
write_plane(
	const struct dc_adoption *adoption,
	const struct powervr_dc_plane *plane)
{
	uint32_t step;
	uint32_t panel;

	/* The chosen plane's registers and the HDMI panel's extension. */
	step = DC_UNIT_STEP * adoption->plane;
	panel = DC_PANEL_EXTRA + DC_UNIT_STEP * DC_HDMI_PANEL;

	/* Holds the shadowed registers back and drops the commit bit, so setting it later is an edge. */
	write_display(adoption, panel, adoption->panel_extra | DC_PANEL_EXTRA_HOLD);
	write_display(adoption, DC_PLANE_EXTRA + step, plane->extra & ~POWERVR_DC_EXTRA_COMMIT);

	/* Writes every register the plane is set up by. */
	write_display(adoption, DC_PLANE_ADDRESS + step, plane->address);
	write_display(adoption, DC_PLANE_STRIDE + step, plane->stride);
	write_display(adoption, DC_PLANE_CONFIG + step, plane->config);
	write_display(adoption, DC_PLANE_SIZE + step, plane->size);
	write_display(adoption, DC_PLANE_TOP_LEFT + step, plane->top_left);
	write_display(adoption, DC_PLANE_BOTTOM_RIGHT + step, plane->bottom_right);
	write_display(adoption, DC_PLANE_BACKGROUND + step, plane->background);
	write_display(adoption, DC_PLANE_BLEND + step, plane->blend);

	/* Commits the plane and lets the panel take it. */
	write_display(adoption, DC_PLANE_EXTRA + step, plane->extra | POWERVR_DC_EXTRA_COMMIT);
	write_display(adoption, panel, adoption->panel_extra & ~DC_PANEL_EXTRA_HOLD);
}

/*
 * Handles the controller's interrupt: reading the pending panels clears
 * them; a blank of the HDMI panel advances the count, notes the time and
 * the scan position, and wakes the waiters.  Nothing else is done here --
 * buffers are let go by the threads that wait.
 */
static void
blank_interrupt(
	int irq,
	kern_irq_ack_t acknowledge,
	void *argument)
{
	struct dc_blanks *blanks;
	uint32_t pending;
	uint32_t location;
	unsigned long enabled;

	(void)irq;
	blanks = argument;

	/* Reads and clears the pending panels, and where the HDMI panel's scan is. */
	pending = read_control(&board_adoption, DC_INTERRUPT_PENDING);
	location = read_display(&board_adoption, DC_PANEL_LOCATION + DC_UNIT_STEP * DC_HDMI_PANEL);

	/* Counts the blank of the HDMI panel only; the other panel's does not advance it. */
	enabled = spin_lock_irqsave(&blanks->lock);

	if ((pending & (1U << DC_HDMI_PANEL)) != 0) {
		atomic_u64_store_release(&blanks->sequence, blanks->sequence + 1U);
		blanks->last_tick = sched_ticks();
		blanks->ring[blanks->next] = location;
		blanks->next = (blanks->next + 1U) % DC_BLANK_RING;
		if (location < blanks->lowest)
			blanks->lowest = location;
		if (location > blanks->highest)
			blanks->highest = location;
		waitq_wake_all(&blanks->wake);
	} else {
		blanks->others++;
	}

	spin_unlock_irqrestore(&blanks->lock, enabled);

	/* Hands the interrupt back to the controller of interrupts. */
	kern_irq_send_eoi(acknowledge);
}
