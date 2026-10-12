/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Display lists made from the recorded plane state.
 *
 * Every change of a plane (the primary frame, the cursor) is recorded under
 * the guard with a new state number and a list made from the whole state is
 * published at once, without waiting: into a free slot when no list is
 * waiting for the hardware, over the waiting list when one is (the hardware
 * takes one complete list or the other), and otherwise at the next adoption,
 * which then carries every change made meanwhile.  The selected pixelvalve's
 * vblank IRQ records which list the hardware took and wakes the waiters.
 * Borrowed images are let go only in thread context, once a list made after
 * their replacement is adopted.
 */
#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <kern/dcache.h>
#include <kern/device-io.h>
#include <kern/pmem.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/sched.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"
#include "drivers/gpu/bcm2711/buffer.h"
#include "drivers/platform/rpi4/rpi4-firmware.h"

/* The boot console list R0 left in SRAM, and the empty pointer of the old submitter state. */
#define FLIP_CONSOLE_LIST 43U

/*
 * The three private list slots: 256 words each from SRAM word 256 to 1023,
 * clear of the firmware's words, the resident filter and the console list.
 */
#define FLIP_FIRST_SLOT_LIST 256U
#define FLIP_SLOT_WORDS 256U

/* One unscaled plane is eight words; a list ends with one terminator word. */
#define FLIP_PLANE_WORDS 8U
#define FLIP_LIST_END 0x80000000U

/* The longest list: console, primary, cursor and the terminator. */
#define FLIP_LIST_MAX_WORDS (3U * FLIP_PLANE_WORDS + 1U)

/* How long a recorded state may wait for the hardware to adopt a list showing it. */
#define FLIP_ADOPTION_TICKS KERN_MS_TO_TICKS(100U)

/* How many times a record raises the clock again after a restore lowered it meanwhile. */
#define FLIP_CLOCK_ATTEMPTS 3U

static int check_primary(const struct bcm2711_display *display, const struct bcm2711_flip_primary *primary);
static int encode_plane(const struct drv_bcm2711_boot_screen *extent, const struct drv_bcm2711_boot_screen *frame, uint32_t *words);
static bool console_hidden(const struct bcm2711_display *display, const struct bcm2711_flip_primary *primary);
static uint32_t required_clock(const struct bcm2711_display *display, const struct bcm2711_flip_primary *primary);
static int raise_clock(struct bcm2711_display *display, uint32_t required);
static void lower_clock(struct bcm2711_display *display, uint32_t hz);
static int request_clock(uint32_t hz);
static int check_pipeline(struct bcm2711_display *display, bool recovering);
static bool frames_overlap(const struct drv_bcm2711_boot_screen *a, const struct drv_bcm2711_boot_screen *b);
static bool range_overlaps(const struct drv_bcm2711_boot_screen *frame, uint64_t physical, uint64_t bytes);
static int owner_matches_locked(const struct bcm2711_display *display, const struct bcm2711_flip_owner *owner);
static int publish_locked(struct bcm2711_display *display);
static uint32_t write_list_locked(struct bcm2711_display *display, uint32_t slot);
static void publish_list(struct bcm2711_display *display, uint32_t list, bool fresh);
static uint32_t slot_list(uint32_t slot);
static uint32_t cursor_words(const struct bcm2711_display *display, uint32_t *words);
static void adopt_slot_locked(struct bcm2711_display *display, uint32_t index, uint32_t current);
static void enable_underrun_locked(struct bcm2711_display *display);
static int wait_console(struct bcm2711_display *display);
static bool perf_enabled(struct bcm2711_display *display);
static void report_cadence(uint64_t milliseconds);
static void cadence_summary(struct bcm2711_flip_cadence *cadence, uint64_t milliseconds, uint64_t summary[4]);

/* The window the note that ends it copies and prints, owned by the one caller that set cadence_reporting. */
static struct bcm2711_flip_cadence cadence_copy[BCM2711_FLIP_CADENCE_KINDS];

/*
 * Initializes persistent IRQ state before installing source callback owners.
 * The enclosing display is zeroed at discovery and outlives all submissions.
 */
void
bcm2711_display_flip_init(
	struct bcm2711_display *display)
{
	int error;

	/* Initializes once, so repeated IRQ preparation cannot release DMA holds. */
	if (display->flip.initialized)
		return;

	/* The clock requests sleep, so they have a mutex of their own; without it nothing attaches. */
	error = mutex_init(&display->flip.clock_mutex, LOCK_RANK_DEVICE, "bcm2711-display-clock");
	if (error != 0)
		return;

	/* The guard, the adoption waiters and an adopted console list with nothing pending. */
	spin_init(&display->flip.guard, LOCK_RANK_DEVICE, "bcm2711-display");
	waitq_init(&display->flip.adoption_waitq, "bcm2711-display-adoption");
	display->flip.active_list = FLIP_CONSOLE_LIST;
	display->flip.active_slot = BCM2711_FLIP_NO_SLOT;
	display->flip.initialized = true;

	/* Succeeded: the private flip state starts with no pending publication. */
	return;
}

/*
 * Attaches flip ownership to the already running, selected boot output.
 * No display register or framebuffer byte is changed by attachment.
 */
int
bcm2711_display_flip_attach(
	struct bcm2711_display *display)
{
	unsigned long enabled;
	int error;

	/* Refuses attachment until R0's actual IRQ/list completion was verified. */
	if (!display->flip.initialized || !display->scanout_started)
		return ENODEV;
	if (display->port >= BCM2711_TIMING_COUNT || display->channel != 0)
		return EINVAL;

	/* The register window reaches past the last word of the third list slot. */
	if (display->compositor.mapped == NULL || display->compositor.size < 0x5000U)
		return ENODEV;
	if (display->timing[display->port].mapped == NULL || display->timing[display->port].size < 0x2cU)
		return ENODEV;

	/* Claims the boot primary only when no competing display state is present. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	if (display->flip.attached) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return EBUSY;
	}

	/* Confirms exclusive use of the established channel and both list pointers. */
	error = check_pipeline(display, false);
	if (error != 0) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return error;
	}

	/* Publishes permission to submit only after the hardware ownership check. */
	display->flip.attached = true;
	display->flip.core_hz = display->console_core_hz;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the caller may submit buffers within the unchanged boot mode. */
	return 0;
}

/*
 * Detaches a registration attempt that never left the confirmed console list.
 * No live or uncertain DMA buffer can be released through this operation.
 */
int
bcm2711_display_flip_detach(
	struct bcm2711_display *display)
{
	unsigned long enabled;
	int error;

	/* Requires the persistent initialized guard before observing attachment. */
	if (!display->flip.initialized)
		return ENODEV;

	/* Withdraws ownership only when no record, recovery or borrowed DMA exists. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	if (display->flip.uncertain ||
	    display->flip.restoring ||
	    display->flip.primary.hold != NULL ||
	    display->flip.retiring_count != 0) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return EBUSY;
	}

	/* A current console pointer alone cannot prove no pending list will replace it. */
	error = check_pipeline(display, false);
	if (error != 0 || display->flip.active_list != FLIP_CONSOLE_LIST) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return EBUSY;
	}

	/* No hardware reference is withdrawn; the console keeps its existing list. */
	display->flip.attached = false;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: an unpublished device owner may unwind its private buffers. */
	return 0;
}

/*
 * Gives the planes to a new lease.
 *
 * The caller holds the display device's controller mutex and has restored
 * the console, so no list names anything; the cursor starts hidden.
 */
void
bcm2711_display_flip_own(
	struct bcm2711_display *display,
	const struct bcm2711_flip_owner *owner)
{
	unsigned long enabled;

	/* Later records are checked against this copy, without the controller mutex. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.owner = owner->session;
	display->flip.lease = owner->lease;
	display->flip.revoking = false;
	kern_memset(&display->flip.cursor, 0, sizeof(display->flip.cursor));

	spin_unlock_irqrestore(&display->flip.guard, enabled);
}

/*
 * Starts ending a lease before its owner takes the controller mutex.
 *
 * From now on the lease's records fail with EBUSY, and every waiter wakes
 * and returns EBUSY, so the console restore does not wait behind them.
 */
int
bcm2711_display_flip_revoke(
	struct bcm2711_display *display,
	const struct bcm2711_flip_owner *owner)
{
	unsigned long enabled;

	/* Only the open holding the lease ends it. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	if (owner->session == NULL ||
	    display->flip.owner != owner->session ||
	    display->flip.lease != owner->lease) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return EINVAL;
	}

	/*
	 * revoking ends the lease for every record and waiter at once; it stays
	 * set until the planes go back to the driver, so a failed restore leaves
	 * the lease unable to show anything until its release is retried.
	 */
	display->flip.revoking = true;
	waitq_wake_all(&display->flip.adoption_waitq);

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the lease is ending. */
	return 0;
}

/* Gives the planes back to the driver once the console list is adopted. */
void
bcm2711_display_flip_disown(
	struct bcm2711_display *display)
{
	unsigned long enabled;

	/* No open owns the planes; the driver's own records may use them again. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.owner = NULL;
	display->flip.lease = 0;
	display->flip.revoking = false;

	spin_unlock_irqrestore(&display->flip.guard, enabled);
}

/* Tells whether an owner may change the planes now. */
int
bcm2711_display_flip_owned(
	struct bcm2711_display *display,
	const struct bcm2711_flip_owner *owner)
{
	unsigned long enabled;
	int error;

	/* Nothing owns the planes before the guard exists. */
	if (!display->flip.initialized)
		return ENODEV;

	/* The lease copy is read under the guard, where records check it too. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	error = owner_matches_locked(display, owner);

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Reports another open's lease, an ending one, or none. */
	if (error != 0)
		return error;

	/* Succeeded: the owner may record. */
	return 0;
}

/*
 * Records the primary plane and publishes a list showing it, without waiting.
 *
 * The clock is raised and the caches cleaned first, in this thread; the state
 * is written and the list published under the guard.  The replaced primary's
 * borrowed image waits for the adoption of a list made after this record.
 */
int
bcm2711_display_flip_record(
	struct bcm2711_display *display,
	const struct bcm2711_flip_owner *owner,
	const struct bcm2711_flip_primary *primary,
	uint64_t *serial)
{
	unsigned long enabled;
	uint32_t required;
	uint32_t attempt;
	uint64_t mark;
	uint64_t raise_ticks;
	uint64_t clean_ticks;
	void *mapping;
	void *console_mapping;
	bool console_shown;
	int error;

	/* Rejects unrepresentable geometry before touching ownership or hardware. */
	*serial = 0;
	if (!display->flip.initialized)
		return ENODEV;
	error = check_primary(display, primary);
	if (error != 0)
		return error;

	/* An ending or foreign lease does no clock or cache work; the guard checks again below. */
	error = bcm2711_display_flip_owned(display, owner);
	if (error != 0)
		return error;

	/* The frame and, when it shows through, the console are cleaned from this thread. */
	mapping = kern_pmem_to_kernel(primary->frame.physical);
	if (mapping == NULL)
		return EFAULT;
	console_shown = !console_hidden(display, primary);
	console_mapping = NULL;
	if (console_shown) {
		/* The lower console may have received newer cached text since R0. */
		console_mapping = kern_pmem_to_kernel(display->screen.physical);
		if (console_mapping == NULL)
			return EFAULT;
	}

	/* Images replaced by adopted lists go first, so the waiting room has space. */
	bcm2711_display_flip_reap(display);

	/* Computes the clock the new list needs and the cache work outside the guard. */
	required = required_clock(display, primary);
	raise_ticks = 0;
	clean_ticks = 0;
	for (attempt = 0; attempt < FLIP_CLOCK_ATTEMPTS; attempt++) {
		/* Raises the clock before any list that needs it is written. */
		mark = clock_ticks();
		error = raise_clock(display, required);
		raise_ticks += clock_ticks() - mark;
		if (error != 0)
			return error;

		/* Publishes the console and frame bytes before the list can name them. */
		mark = clock_ticks();
		if (console_mapping != NULL)
			kern_dcache_clean_range(console_mapping, (size_t)display->screen.size);
		if (!primary->frame.uncached)
			kern_dcache_clean_range(mapping, (size_t)primary->frame.size);
		kern_io_write_barrier();
		clean_ticks += clock_ticks() - mark;

		/* Checks ownership and the hardware, then records the plane. */
		enabled = spin_lock_irqsave(&display->flip.guard);

		error = owner_matches_locked(display, owner);
		if (error == 0 && !display->flip.attached)
			error = ENODEV;
		if (error == 0 && display->flip.restoring)
			error = EBUSY;
		if (error == 0 && display->flip.uncertain)
			error = EBUSY;
		if (error != 0) {
			spin_unlock_irqrestore(&display->flip.guard, enabled);
			return error;
		}

		/* A restore lowered the clock after it was raised: raise it again. */
		if (display->flip.core_hz < required) {
			spin_unlock_irqrestore(&display->flip.guard, enabled);
			continue;
		}

		/* The replaced image needs a place among those waiting for adoption. */
		if (display->flip.primary.hold != NULL && display->flip.retiring_count >= BCM2711_FLIP_RETIRING) {
			spin_unlock_irqrestore(&display->flip.guard, enabled);
			return EBUSY;
		}

		/* A list goes out now unless two are out: the hardware must still be ours. */
		if (display->flip.pending_count < 2U && !display->flip.console_pending) {
			error = check_pipeline(display, false);
			if (error != 0) {
				display->flip.uncertain = true;
				waitq_wake_all(&display->flip.adoption_waitq);
				spin_unlock_irqrestore(&display->flip.guard, enabled);
				return error;
			}
		}

		/*
		 * The record's number is the state's next one; the replaced image
		 * waits for the adoption of a list made from this number or later.
		 */
		display->flip.state_serial++;
		if (display->flip.primary.hold != NULL) {
			display->flip.retiring[display->flip.retiring_count].hold = display->flip.primary.hold;
			display->flip.retiring[display->flip.retiring_count].serial = display->flip.state_serial;
			display->flip.retiring_count++;
		}

		/* The new primary is the recorded one, with the caller's reference. */
		display->flip.primary = *primary;
		display->flip.primary.shown = true;
		display->flip.perf.primary_records++;
		display->flip.perf.clean += clean_ticks;
		display->flip.perf.raise += raise_ticks;
		*serial = display->flip.state_serial;

		/* The checked pipeline takes the list now, or the next adoption carries it. */
		(void)publish_locked(display);

		spin_unlock_irqrestore(&display->flip.guard, enabled);

		/* Succeeded: the frame shows from the refresh boundary that adopts a list with it. */
		return 0;
	}

	/* The clock kept going down under this record. */
	return EBUSY;
}

/*
 * Waits for the adoption of a list made from the record numbered serial or later.
 *
 * The wait holds no lock while it sleeps; the vblank IRQ wakes it.  A
 * timeout leaves the state uncertain: every borrowed image stays until the
 * console is restored, and the lease's later records fail.
 */
int
bcm2711_display_flip_wait(
	struct bcm2711_display *display,
	const struct bcm2711_flip_owner *owner,
	uint64_t serial)
{
	unsigned long enabled;
	uint64_t deadline;
	uint64_t observed;
	uint64_t now;
	int error;

	/* Nothing was recorded before the guard exists. */
	if (!display->flip.initialized)
		return ENODEV;

	/* Sleeps until the adoption, the lease's end, uncertainty or the deadline. */
	deadline = sched_ticks() + FLIP_ADOPTION_TICKS;
	enabled = spin_lock_irqsave(&display->flip.guard);

	for (;;) {
		/* A list showing this record, or a later one, was adopted. */
		if (display->flip.adopted_serial >= serial) {
			error = 0;
			break;
		}

		/* The lease ended, or another open owns the planes now. */
		error = owner_matches_locked(display, owner);
		if (error != 0)
			break;

		/* Another waiter's timeout left every list uncertain. */
		if (display->flip.uncertain) {
			error = ETIMEDOUT;
			break;
		}

		/* The deadline passed: the hardware did not take the list. */
		now = sched_ticks();
		if (now >= deadline) {
			display->flip.uncertain = true;
			waitq_wake_all(&display->flip.adoption_waitq);
			error = ETIMEDOUT;
			break;
		}

		/* Sleeps until the IRQ or a revocation wakes the queue, or until the deadline. */
		observed = waitq_sequence(&display->flip.adoption_waitq);
		(void)waitq_sleep(&display->flip.adoption_waitq, &display->flip.guard, observed, deadline, 0U);
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Images the adoption no longer shows go now, in this thread. */
	bcm2711_display_flip_reap(display);

	/* Reports the timeout, uncertainty or the lease's end. */
	if (error != 0)
		return error;

	/* Succeeded: the hardware shows this record or a later one. */
	return 0;
}

/*
 * Presents one buffer of the driver's own over the whole output and waits for it.
 */
int
bcm2711_display_flip_present(
	struct bcm2711_display *display,
	const struct drv_bcm2711_boot_screen *frame)
{
	int error;

	/* An opaque frame at the origin covers the output and hides the console. */
	error = bcm2711_display_flip_compose(display, frame, 0, 0, false);
	if (error != 0)
		return error;

	/* Succeeded: the selected PV observed actual primary adoption. */
	return 0;
}

/*
 * Composes an unscaled frame of the driver's own over the console and waits for its adoption.
 */
int
bcm2711_display_flip_compose(
	struct bcm2711_display *display,
	const struct drv_bcm2711_boot_screen *frame,
	uint32_t x,
	uint32_t y,
	bool premultiplied)
{
	struct bcm2711_flip_primary primary;
	struct bcm2711_flip_owner owner;
	uint64_t serial;
	uint64_t mark;
	uint64_t wait_ticks;
	int error;

	/* Refuses a missing frame before describing it. */
	if (frame == NULL)
		return EINVAL;

	/* The driver's own record: no lease, no borrowed allocation. */
	kern_memset(&primary, 0, sizeof(primary));
	primary.shown = true;
	primary.premultiplied = premultiplied;
	primary.x = x;
	primary.y = y;
	primary.frame = *frame;
	owner.session = NULL;
	owner.lease = 0;

	/* Records the frame and publishes its list. */
	error = bcm2711_display_flip_record(display, &owner, &primary, &serial);
	if (error != 0)
		return error;

	/* Waits for the list's adoption. */
	mark = clock_ticks();
	error = bcm2711_display_flip_wait(display, &owner, serial);
	wait_ticks = clock_ticks() - mark;
	if (error != 0)
		return error;

	/* The adoption is a cadence event and a flip of the report. */
	bcm2711_display_flip_note(display, BCM2711_FLIP_CADENCE_FLIP);
	bcm2711_display_flip_report(display, 0, 0, wait_ticks);

	/* Succeeded: a selected-PV frame adopted a list with the frame. */
	return 0;
}

/*
 * Records the cursor and publishes a list with it, without waiting.
 *
 * While two lists are out, the next adoption carries the change; a change
 * that shows nothing new (a hidden cursor hidden again) makes no list.
 */
int
bcm2711_display_flip_cursor(
	struct bcm2711_display *display,
	const struct bcm2711_flip_owner *owner,
	const struct bcm2711_flip_cursor *cursor)
{
	unsigned long enabled;
	int error;

	/* A shown image is small, unscaled and readable by the HVS below one GiB. */
	if (!display->flip.initialized)
		return ENODEV;
	if (cursor->shown) {
		if (cursor->width == 0 || cursor->width > 64U || cursor->height == 0 || cursor->height > 64U)
			return EINVAL;
		if (cursor->format > 1U || cursor->pitch < cursor->width * 4U || (cursor->pitch & 3U) != 0)
			return EINVAL;
		if (cursor->physical == 0 || (cursor->physical & 3U) != 0)
			return EINVAL;
		if ((uint64_t)cursor->physical + (uint64_t)cursor->pitch * cursor->height > 0x40000000ULL)
			return EINVAL;
	}

	/* The newest cursor replaces any change not yet in a list. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	error = owner_matches_locked(display, owner);
	if (error == 0 && !display->flip.attached)
		error = ENODEV;
	if (error == 0 && display->flip.restoring)
		error = EBUSY;
	if (error != 0) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return error;
	}

	/* A change of a cursor hidden before and after shows nothing new and makes no list. */
	if (cursor->shown || display->flip.cursor.shown) {
		display->flip.state_serial++;
		display->flip.perf.cursor_records++;
	}

	/* The newest cursor is the recorded one, shown or not. */
	display->flip.cursor = *cursor;

	/* A pipeline that is no longer ours keeps the change recorded and is left to recovery. */
	(void)publish_locked(display);

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the cursor shows from a coming refresh boundary. */
	return 0;
}

/*
 * Tells whether a cursor image may be read by the HVS or by a coming list.
 *
 * It is named while a list the hardware may read shows it or while the
 * recorded cursor (published or not) uses it; any other image may be
 * rewritten at once.
 */
bool
bcm2711_display_flip_cursor_named(
	struct bcm2711_display *display,
	uint32_t physical)
{
	unsigned long enabled;
	uint32_t slot;
	bool named;

	/* Nothing names an image before the guard exists. */
	if (!display->flip.initialized)
		return false;

	/* Lists the hardware may read and the recorded cursor are read together. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	named = false;
	if (display->flip.cursor.physical == physical)
		named = true;
	for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
		/* Only a list the HVS may still fetch keeps its image. */
		if (display->flip.slots[slot].used && display->flip.slots[slot].cursor_image == physical)
			named = true;
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the answer holds while the caller excludes other cursor changes. */
	return named;
}

/*
 * Tells how a range of storage is named: by the recorded primary (it stays
 * named until a later record), only by lists the hardware may read (an
 * adoption ends that), or not at all.
 */
unsigned
bcm2711_display_flip_frame_use(
	struct bcm2711_display *display,
	uint64_t physical,
	uint64_t bytes)
{
	unsigned long enabled;
	uint32_t slot;
	unsigned use;
	bool overlaps;

	/* Nothing names storage before the guard exists. */
	if (!display->flip.initialized)
		return BCM2711_FLIP_FRAME_FREE;

	/* The recorded primary and the lists are read together. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	use = BCM2711_FLIP_FRAME_FREE;
	for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
		/* A list the hardware may read keeps its frame until a later adoption. */
		if (!display->flip.slots[slot].used || !display->flip.slots[slot].has_frame)
			continue;
		overlaps = range_overlaps(&display->flip.slots[slot].frame, physical, bytes);
		if (overlaps)
			use = BCM2711_FLIP_FRAME_LISTED;
	}

	/* The recorded primary keeps its frame until a later record replaces it. */
	if (display->flip.primary.shown) {
		overlaps = range_overlaps(&display->flip.primary.frame, physical, bytes);
		if (overlaps)
			use = BCM2711_FLIP_FRAME_RECORDED;
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the answer holds while the caller excludes other records of this storage. */
	return use;
}

/*
 * Waits until no list the hardware may read names a range of storage.
 *
 * The caller has seen it named only by lists, and excludes records naming
 * it; an adoption of a later list ends that.  At most 100 ms.
 */
int
bcm2711_display_flip_wait_unlisted(
	struct bcm2711_display *display,
	const struct bcm2711_flip_owner *owner,
	uint64_t physical,
	uint64_t bytes)
{
	unsigned long enabled;
	uint64_t deadline;
	uint64_t observed;
	uint64_t now;
	uint32_t slot;
	bool listed;
	bool overlaps;
	int error;

	/* Nothing names storage before the guard exists. */
	if (!display->flip.initialized)
		return 0;

	/* Sleeps from adoption to adoption until no list names the storage. */
	deadline = sched_ticks() + FLIP_ADOPTION_TICKS;
	enabled = spin_lock_irqsave(&display->flip.guard);

	for (;;) {
		/* Looks for a list the hardware may read that names the storage. */
		listed = false;
		for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
			/* Only a used list with a frame can name it. */
			if (!display->flip.slots[slot].used || !display->flip.slots[slot].has_frame)
				continue;
			overlaps = range_overlaps(&display->flip.slots[slot].frame, physical, bytes);
			if (overlaps)
				listed = true;
		}

		/* No list names it: the storage may be written. */
		if (!listed) {
			error = 0;
			break;
		}

		/* The lease ended, or the lists will not move on their own. */
		error = owner_matches_locked(display, owner);
		if (error == 0 && display->flip.uncertain)
			error = EBUSY;
		if (error != 0)
			break;

		/* The hardware did not take a later list in time. */
		now = sched_ticks();
		if (now >= deadline) {
			error = ETIMEDOUT;
			break;
		}

		/* Sleeps until the next adoption or the deadline. */
		observed = waitq_sequence(&display->flip.adoption_waitq);
		(void)waitq_sleep(&display->flip.adoption_waitq, &display->flip.guard, observed, deadline, 0U);
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Reports the lease's end, uncertainty or the timeout. */
	if (error != 0)
		return error;

	/* Succeeded: no list the hardware may read names the storage. */
	return 0;
}

/*
 * Lets go of borrowed images whose replacement has been adopted.
 *
 * Called in thread context only (a release may free the allocation); while
 * the state is uncertain nothing goes, as no adoption can be trusted.
 */
void
bcm2711_display_flip_reap(
	struct bcm2711_display *display)
{
	struct bcm2711_buffer *released[BCM2711_FLIP_RETIRING];
	unsigned long enabled;
	uint32_t count;
	uint32_t index;
	uint32_t kept;

	/* Nothing waits before the guard exists. */
	if (!display->flip.initialized)
		return;

	/* Takes the images whose replacing record is shown by an adopted list. */
	count = 0;
	enabled = spin_lock_irqsave(&display->flip.guard);

	kept = 0;
	for (index = 0; index < display->flip.retiring_count; index++) {
		/* An entry still waiting moves down over the ones taken. */
		if (display->flip.uncertain || display->flip.retiring[index].serial > display->flip.adopted_serial) {
			display->flip.retiring[kept] = display->flip.retiring[index];
			kept++;
			continue;
		}

		/* The replacing record is shown: the image goes. */
		released[count] = display->flip.retiring[index].hold;
		count++;
	}

	/* The entries still waiting are the first kept ones. */
	display->flip.retiring_count = kept;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Releases each taken image outside the guard. */
	for (index = 0; index < count; index++)
		bcm2711_buffer_release(released[index]);
}

/*
 * Restores the retained boot console without changing mode or routing.
 *
 * Records stop while it runs.  A fresh matching PV interrupt is required even
 * after a late uncertain flip; the console's adoption lets every borrowed
 * image go and lowers the clock again.
 */
int
bcm2711_display_flip_restore(
	struct bcm2711_display *display)
{
	struct bcm2711_buffer *released[BCM2711_FLIP_RETIRING + 1U];
	unsigned long enabled;
	uint32_t count;
	uint32_t index;
	void *mapping;
	int error;

	/* Requires the persistent owner and the existing console CPU mapping. */
	if (!display->flip.initialized)
		return ENODEV;
	mapping = kern_pmem_to_kernel(display->screen.physical);
	if (mapping == NULL)
		return EFAULT;

	/* Stops records and publications while the console is prepared and published. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	if (!display->flip.attached) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return ENODEV;
	}

	/* One restore at a time; the display device's controller mutex makes this the rule. */
	if (display->flip.restoring) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return EBUSY;
	}

	/* restoring keeps records and IRQ publications away until the console is back. */
	display->flip.restoring = true;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Raises the provider clock before restoring a potentially heavier active list. */
	error = raise_clock(display, display->console_core_hz);
	if (error != 0) {
		enabled = spin_lock_irqsave(&display->flip.guard);
		display->flip.restoring = false;
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return error;
	}

	/* Makes current console contents visible before reinstating its original list. */
	kern_dcache_clean_range(mapping, (size_t)display->screen.size);
	kern_io_write_barrier();

	/* Allows recovery from uncertain current/next pointers only on this pipeline. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	error = check_pipeline(display, true);
	if (error != 0) {
		display->flip.uncertain = true;
		display->flip.restoring = false;
		waitq_wake_all(&display->flip.adoption_waitq);
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return error;
	}

	/* The planes go: the primary and the cursor are hidden from now on. */
	display->flip.state_serial++;
	display->flip.primary.shown = false;
	kern_memset(&display->flip.cursor, 0, sizeof(display->flip.cursor));

	/* Requeues the preserved console list and demands a fresh matching source. */
	display->flip.console_pending = true;
	publish_list(display, FLIP_CONSOLE_LIST, true);

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Waits for the console's adoption, which is the only proof every list is gone. */
	error = wait_console(display);
	if (error != 0)
		return error;

	/* Every borrowed image goes: the console list names none. */
	count = 0;
	enabled = spin_lock_irqsave(&display->flip.guard);

	if (display->flip.primary.hold != NULL) {
		released[count] = display->flip.primary.hold;
		count++;
	}

	/* The primary was hidden by the restore and borrows nothing more. */
	kern_memset(&display->flip.primary, 0, sizeof(display->flip.primary));

	/* Every image waiting for an adoption goes too. */
	for (index = 0; index < display->flip.retiring_count; index++) {
		released[count] = display->flip.retiring[index].hold;
		count++;
	}

	/* Nothing waits any more. */
	display->flip.retiring_count = 0;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Releases them outside the guard. */
	for (index = 0; index < count; index++)
		bcm2711_buffer_release(released[index]);

	/* Lowers bandwidth only after adoption, while records are still excluded. */
	lower_clock(display, display->console_core_hz);

	/* Records may come again. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.restoring = false;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: no caller-provided framebuffer remains retained. */
	return 0;
}

/*
 * Copies the state's progress and what its lists name.
 * An uninitialized display reports an unattached, empty status.
 */
void
bcm2711_display_flip_snapshot(
	struct bcm2711_display *display,
	struct bcm2711_flip_status *status)
{
	unsigned long enabled;
	uint32_t slot;

	/* Clears the caller's result without dereferencing an uninitialized guard. */
	kern_memset(status, 0, sizeof(*status));
	if (!display->flip.initialized)
		return;

	/* Copies one coherent observation under the guard. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	status->attached = display->flip.attached;
	status->uncertain = display->flip.uncertain;
	status->restoring = display->flip.restoring;
	status->active_list = display->flip.active_list;
	status->pending_count = display->flip.pending_count;
	status->retiring_count = display->flip.retiring_count;
	status->frame_sequence = display->flip.frame_sequence;
	status->state_serial = display->flip.state_serial;
	status->list_serial = display->flip.list_serial;
	status->adopted_serial = display->flip.adopted_serial;
	status->core_hz = display->flip.core_hz;
	status->clock_error = display->flip.clock_error;
	status->primary = display->flip.primary;
	for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
		/* A slot's frame means something only while the slot is used. */
		if (display->flip.slots[slot].used)
			status->used_mask |= 1U << slot;
		status->frames[slot] = display->flip.slots[slot].frame;
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the caller owns one coherent flip-state observation. */
	return;
}

/*
 * Observes the selected PV's frame with the caller's guard held.
 *
 * The current-list register says which published list the hardware took:
 * it and every older list are settled, its state number is adopted, the
 * waiters wake and changes made meanwhile go out in a new list.  Nothing is
 * released here; the woken threads do that.
 */
void
bcm2711_display_flip_vblank_locked(
	struct bcm2711_display *display,
	uint32_t current)
{
	uint32_t index;
	uint32_t found;
	uint32_t list;

	/* Counts real selected-PV frames independently of submissions and old lists. */
	display->flip.frame_sequence++;
	if (!display->flip.attached)
		return;

	/* An underrun seen in the last frame is counted once more from now on. */
	enable_underrun_locked(display);

	/* The console's adoption retires every list, even after uncertainty. */
	if (display->flip.console_pending && current == FLIP_CONSOLE_LIST) {
		display->flip.console_pending = false;
		display->flip.active_list = FLIP_CONSOLE_LIST;
		display->flip.active_slot = BCM2711_FLIP_NO_SLOT;
		display->flip.pending_count = 0;
		for (index = 0; index < BCM2711_FLIP_SLOTS; index++)
			display->flip.slots[index].used = false;
		display->flip.adopted_serial = display->flip.state_serial;
		display->flip.list_serial = display->flip.state_serial;
		display->flip.uncertain = false;
		waitq_wake_all(&display->flip.adoption_waitq);
		return;
	}

	/* Uncertain lists stay held until the console comes back. */
	if (display->flip.uncertain)
		return;

	/* Finds the published list the hardware took, if it took one. */
	found = display->flip.pending_count;
	for (index = 0; index < display->flip.pending_count; index++) {
		/* The register holds one complete list's address. */
		list = slot_list(display->flip.pending[index]);
		if (list == current)
			found = index;
	}

	/* The hardware still shows the adopted list. */
	if (found == display->flip.pending_count)
		return;

	/* Settles the adopted list and every older one, and wakes the waiters. */
	adopt_slot_locked(display, found, current);
	waitq_wake_all(&display->flip.adoption_waitq);

	/* Changes made while two lists were out go in a list of their own now. */
	(void)publish_locked(display);
}

/*
 * Counts one cursor image set (GPU_DISPLAY_CURSOR_SET) for the next flip
 * report: copied when its rows were written into a plane image, otherwise
 * shown again from an image that holds the same unchanged rows.
 */
void
bcm2711_display_flip_count_cursor(
	struct bcm2711_display *display,
	bool copied)
{
	/* The display device's plane mutex is not the guard, so the counts are atomic. */
	__atomic_fetch_add(&display->flip.perf_cursor_sets, 1U, __ATOMIC_RELAXED);
	if (copied)
		__atomic_fetch_add(&display->flip.perf_cursor_copies, 1U, __ATOMIC_RELAXED);
}

/*
 * Notes one display event of a cadence kind (WS213): its interval from the
 * previous one of the kind, and every two seconds the window's report.
 */
void
bcm2711_display_flip_note(
	struct bcm2711_display *display,
	unsigned kind)
{
	struct bcm2711_flip_cadence *cadence;
	unsigned long enabled;
	uint64_t milliseconds;
	uint64_t interval;
	uint64_t now;
	uint32_t index;
	bool report;
	bool measured;

	/* Nothing is kept without v3d.perf=1. */
	measured = perf_enabled(display);
	if (!measured || kind >= BCM2711_FLIP_CADENCE_KINDS)
		return;

	/* The interval joins the window's samples while they fit. */
	now = clock_ticks();
	report = false;
	milliseconds = 0;
	enabled = spin_lock_irqsave(&display->flip.guard);
	cadence = &display->flip.cadence[kind];
	if (cadence->last != 0 && cadence->stored < BCM2711_FLIP_CADENCE_SAMPLES) {
		interval = kern_ticks_to_rate(now - cadence->last, 1000000U);
		if (interval > UINT32_MAX)
			interval = UINT32_MAX;
		cadence->intervals[cadence->stored] = (uint32_t)interval;
		cadence->stored++;
	}
	cadence->last = now;
	cadence->count++;

	/* The first event opens the window; one caller takes a window of two seconds or more to print. */
	if (display->flip.cadence_start == 0)
		display->flip.cadence_start = now;
	milliseconds = kern_ticks_to_ms(now - display->flip.cadence_start);
	if (milliseconds >= 2000U && !display->flip.cadence_reporting) {
		display->flip.cadence_reporting = true;
		for (index = 0; index < BCM2711_FLIP_CADENCE_KINDS; index++) {
			/* The copy keeps the samples; the window starts empty, each kind keeping its last time. */
			cadence_copy[index] = display->flip.cadence[index];
			display->flip.cadence[index].count = 0;
			display->flip.cadence[index].stored = 0;
		}
		display->flip.cadence_start = now;
		report = true;
	}
	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* The copied window is sorted and printed without the guard. */
	if (!report)
		return;
	report_cadence(milliseconds);
	enabled = spin_lock_irqsave(&display->flip.guard);
	display->flip.cadence_reporting = false;
	spin_unlock_irqrestore(&display->flip.guard, enabled);
}

/*
 * Adds an adopted primary flip to the flip report and prints a window of two seconds or more (v3d.perf=1 only).
 *
 * clean and raise are the record's own stages when the caller measured them
 * itself (zero when the record added them); wait is the adoption wait.
 */
void
bcm2711_display_flip_report(
	struct bcm2711_display *display,
	uint64_t clean,
	uint64_t raise,
	uint64_t wait)
{
	struct bcm2711_flip_perf window;
	unsigned long enabled;
	uint64_t elapsed;
	uint64_t now;
	uint64_t underruns;
	uint64_t sets;
	uint64_t copies;
	uint64_t count;
	bool report;
	bool measured;

	/* Nothing is kept without v3d.perf=1. */
	measured = perf_enabled(display);
	if (!measured)
		return;

	/* Adds this flip, and takes a full window for one caller to print. */
	now = clock_ticks();
	report = false;
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.perf.flips++;
	display->flip.perf.clean += clean;
	display->flip.perf.raise += raise;
	display->flip.perf.wait += wait;
	elapsed = 0;
	if (display->flip.perf.start != 0)
		elapsed = kern_ticks_to_ms(now - display->flip.perf.start);
	if (display->flip.perf.start == 0) {
		/* The first flip opens the window; underruns are counted from the total at its start. */
		display->flip.perf.start = now;
		display->flip.perf.underruns = display->underruns;
	} else if (elapsed >= 2000U && !display->flip.perf_reporting) {
		/* The window is copied and starts again empty at this report. */
		window = display->flip.perf;
		kern_memset(&display->flip.perf, 0, sizeof(display->flip.perf));
		display->flip.perf.start = now;
		display->flip.perf.underruns = display->underruns;
		display->flip.perf_reporting = true;
		report = true;
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* A window shorter than two seconds keeps accumulating. */
	if (!report)
		return;

	/* The window's cursor images and underruns, taken with the window. */
	sets = __atomic_exchange_n(&display->flip.perf_cursor_sets, 0, __ATOMIC_RELAXED);
	copies = __atomic_exchange_n(&display->flip.perf_cursor_copies, 0, __ATOMIC_RELAXED);
	underruns = display->underruns - window.underruns;

	/* Prints microseconds per flip for each stage and the window's counts. */
	count = window.flips;
	if (count == 0)
		count = 1;
	kern_logf(
		"bcm2711: flip perf ms=%llu flips=%llu avg_us clean=%llu raise=%llu wait=%llu cursor sets=%llu copies=%llu records primary=%llu cursor=%llu lists=%llu replaced=%llu coalesced=%llu underruns=%llu\n",
		(unsigned long long)kern_ticks_to_ms(now - window.start),
		(unsigned long long)window.flips,
		(unsigned long long)(kern_ticks_to_rate(window.clean, 1000000U) / count),
		(unsigned long long)(kern_ticks_to_rate(window.raise, 1000000U) / count),
		(unsigned long long)(kern_ticks_to_rate(window.wait, 1000000U) / count),
		(unsigned long long)sets,
		(unsigned long long)copies,
		(unsigned long long)window.primary_records,
		(unsigned long long)window.cursor_records,
		(unsigned long long)window.lists,
		(unsigned long long)window.replaced,
		(unsigned long long)window.coalesced,
		(unsigned long long)underruns);

	/* Another caller may print the next window. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.perf_reporting = false;

	spin_unlock_irqrestore(&display->flip.guard, enabled);
}

/* Checks that a primary plane fits the output unscaled, in an encodable buffer away from the console. */
static int
check_primary(
	const struct bcm2711_display *display,
	const struct bcm2711_flip_primary *primary)
{
	struct drv_bcm2711_boot_screen extent;
	uint32_t words[FLIP_PLANE_WORDS];
	const struct drv_bcm2711_boot_screen *frame;
	bool overlap;
	int error;

	/* Refuses any crop, scaling or overflow of the established output. */
	frame = &primary->frame;
	if (primary->x > display->screen.width || frame->width > display->screen.width - primary->x)
		return EINVAL;
	if (primary->y > display->screen.height || frame->height > display->screen.height - primary->y)
		return EINVAL;

	/* Validates the buffer using its unscaled dimensions and native byte order. */
	extent = display->screen;
	extent.width = frame->width;
	extent.height = frame->height;
	error = encode_plane(&extent, frame, words);
	if (error != 0)
		return error;

	/* The console is never scanned out from storage that also holds a frame. */
	overlap = frames_overlap(frame, &display->screen);
	if (overlap)
		return EINVAL;

	/* Succeeded: the plane can be encoded into any later list. */
	return 0;
}

/* Generates one independently encoded unscaled RGB32 plane at the origin with fixed alpha. */
static int
encode_plane(
	const struct drv_bcm2711_boot_screen *extent,
	const struct drv_bcm2711_boot_screen *frame,
	uint32_t *words)
{
	uint32_t order;

	/* Requires the same dimensions with either independently encoded RGB byte order. */
	if (frame->width != extent->width || frame->height != extent->height)
		return EINVAL;
	if (frame->width == 0 || frame->width > 4096U)
		return EINVAL;
	if (frame->height == 0 || frame->height > 4096U)
		return EINVAL;
	if (frame->format > 1U)
		return ENOTSUP;

	/* Bounds every DMA row and keeps the bus alias representable below one GiB. */
	if ((frame->physical & 3U) != 0 || frame->physical >= 0x40000000ULL)
		return EINVAL;
	if (frame->size == 0 || frame->size > 0x40000000ULL - frame->physical)
		return EINVAL;
	if (frame->pitch < frame->width * 4U || frame->pitch > 65535U)
		return EINVAL;
	if ((frame->pitch & 3U) != 0 || (uint64_t)frame->pitch * frame->height > frame->size)
		return EINVAL;

	/* Encodes a fresh eight-word plane without inherited contexts. */
	order = 2;
	if (frame->format == 1)
		order = 3;
	words[0] = 0x48009807U | (order << 13);
	words[1] = 0;
	words[2] = 0x4000fff0U;
	words[3] = (frame->height << 16) | frame->width;
	words[4] = 0xc0c0c0c0U;
	words[5] = 0xc0000000U | (uint32_t)frame->physical;
	words[6] = 0xc0c0c0c0U;
	words[7] = frame->pitch;

	/* Succeeded: the completed words may be copied into an unused SRAM slot. */
	return 0;
}

/* Tells whether an opaque primary at the origin covers the whole output, so the console cannot show. */
static bool
console_hidden(
	const struct bcm2711_display *display,
	const struct bcm2711_flip_primary *primary)
{
	/* A hidden or translucent primary lets the console through. */
	if (!primary->shown || primary->premultiplied)
		return false;

	/* A primary away from the origin or smaller than the output leaves console pixels. */
	if (primary->x != 0 || primary->y != 0)
		return false;
	if (primary->frame.width != display->screen.width || primary->frame.height != display->screen.height)
		return false;

	/* Succeeded: no console pixel can show. */
	return true;
}

/*
 * Computes the core clock a list with this primary needs: sixty percent of
 * the planes' pixels per second at four pixels per compositor cycle, and
 * never less than the console's.
 */
static uint32_t
required_clock(
	const struct bcm2711_display *display,
	const struct bcm2711_flip_primary *primary)
{
	uint64_t load;
	uint32_t required;
	bool hidden;

	/* The primary, and the console when it shows through. */
	load = (uint64_t)primary->frame.width * primary->frame.height;
	hidden = console_hidden(display, primary);
	if (!hidden)
		load += (uint64_t)display->screen.width * display->screen.height;

	/* One output uses sixty percent of the aggregate plane cycle demand. */
	load = load * ((display->refresh_millihz + 500U) / 1000U) / 4U;
	load = load * 60U / 100U;
	required = display->console_core_hz;
	if (load > required)
		required = (uint32_t)load;

	/* Succeeded: the floor the list needs. */
	return required;
}

/*
 * Raises the transition floor for a list that needs it.
 *
 * The clock never goes down while a lease shows lists, so a record only ever
 * raises it; the mutex serializes the firmware requests, which sleep.
 */
static int
raise_clock(
	struct bcm2711_display *display,
	uint32_t required)
{
	unsigned long enabled;
	uint32_t hz;
	uint32_t current;
	int error;

	/* Refuses bandwidth outside the ceiling captured before firmware teardown. */
	if (required == 0 || required > display->max_core_hz)
		return ENOTSUP;

	/* One firmware request at a time, never under the guard. */
	mutex_lock(&display->flip.clock_mutex);

	enabled = spin_lock_irqsave(&display->flip.guard);

	current = display->flip.core_hz;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* The same temporary floor as R0, at least, and never below the clock already running. */
	hz = 500000000U;
	if (hz < required)
		hz = required;
	if (hz < current)
		hz = current;
	if (hz > display->max_core_hz)
		hz = display->max_core_hz;

	/* A clock already at the floor needs no request. */
	error = 0;
	if (hz != current)
		error = request_clock(hz);

	/* Publishes the outcome for later records and the snapshot. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.clock_error = error;
	if (error == 0)
		display->flip.core_hz = hz;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	mutex_unlock(&display->flip.clock_mutex);

	/* Refuses publication if the provider could not establish its bandwidth. */
	if (error != 0)
		return error;

	/* Succeeded: the transition clock precedes every list that needs it. */
	return 0;
}

/* Lowers the clock to the console's once the console list is adopted; a refusal keeps the higher, safe floor. */
static void
lower_clock(
	struct bcm2711_display *display,
	uint32_t hz)
{
	unsigned long enabled;
	uint32_t current;
	int error;

	/* One firmware request at a time, never under the guard. */
	mutex_lock(&display->flip.clock_mutex);

	enabled = spin_lock_irqsave(&display->flip.guard);

	current = display->flip.core_hz;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* An unchanged rate needs no request. */
	error = 0;
	if (hz != current)
		error = request_clock(hz);

	/* Publishes the clock diagnostic and the new rate. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.clock_error = error;
	if (error == 0)
		display->flip.core_hz = hz;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	mutex_unlock(&display->flip.clock_mutex);
}

/* Sends the firmware provider's exact rate request with turbo left unchanged. */
static int
request_clock(
	uint32_t hz)
{
	uint32_t values[3];
	uint32_t answered;
	int error;

	/* Leaves rounding to the same firmware provider used by R0 and Linux. */
	values[0] = 4;
	values[1] = hz;
	values[2] = 0;
	error = drv_rpi4_firmware_property(0x00038002U, values, 3, 3, &answered);
	if (error != 0)
		return error;
	if (answered < 8U || values[0] != 4)
		return EIO;

	/* Succeeded: the firmware accepted this core-clock request. */
	return 0;
}

/*
 * Checks sole ownership and unchanged timing before any list publication.
 *
 * Ordinarily the current list must be one the state knows the hardware may
 * read (the adopted one or a published one) and the next list the newest
 * published; recovery accepts any of this driver's lists on both.
 */
static int
check_pipeline(
	struct bcm2711_display *display,
	bool recovering)
{
	volatile uint8_t *hvs;
	volatile uint8_t *pv;
	uint32_t control;
	uint32_t route;
	uint32_t current;
	uint32_t next;
	uint32_t newest;
	uint32_t port;
	uint32_t slot;
	uint32_t index;
	uint32_t list;
	bool known;

	/* Requires the exact active geometry and no enabled competing HVS channel. */
	hvs = display->compositor.mapped;
	pv = display->timing[display->port].mapped;
	control = kern_mmio_read32(hvs + 0x40);
	if (control != (0x80000000U | (display->screen.width << 16) | display->screen.height))
		return EBUSY;
	control = kern_mmio_read32(hvs + 0x50);
	if ((control & 0x80000000U) != 0)
		return EBUSY;
	control = kern_mmio_read32(hvs + 0x60);
	if ((control & 0x80000000U) != 0)
		return EBUSY;

	/* Keeps the selected boot output on channel zero and the other disconnected. */
	for (port = 0; port < BCM2711_HDMI_COUNT; port++) {
		/* HDMI outputs use different mux registers, with identical two-bit fields. */
		route = kern_mmio_read32(hvs + 0x18U - port * 4U);
		route >>= 30;
		if (port == display->port) {
			/* The boot port must still feed channel zero. */
			if (route != 0)
				return EBUSY;
		} else {
			/* An independent display owner cannot share these spare SRAM slots. */
			if (route != BCM2711_NO_CHANNEL)
				return EBUSY;
		}
	}

	/* Requires ongoing pixel flow; a stopped source cannot prove buffer retirement. */
	control = kern_mmio_read32(pv);
	if ((control & 1U) == 0)
		return EBUSY;
	control = kern_mmio_read32(pv + 0x04);
	if ((control & 1U) == 0)
		return EBUSY;

	/* Recovery may overwrite only our own known lists, never a foreign owner. */
	current = kern_mmio_read32(hvs + 0x30);
	next = kern_mmio_read32(hvs + 0x20);
	if (recovering) {
		/* The console and the three slots are the only lists this component writes. */
		known = false;
		if (current == FLIP_CONSOLE_LIST)
			known = true;
		for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
			/* A slot's list address. */
			list = slot_list(slot);
			if (current == list)
				known = true;
		}

		/* A foreign current list is never overwritten. */
		if (!known)
			return EBUSY;

		/* The next list is ours too. */
		known = false;
		if (next == FLIP_CONSOLE_LIST)
			known = true;
		for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
			/* A slot's list address. */
			list = slot_list(slot);
			if (next == list)
				known = true;
		}

		/* A foreign next list is never replaced. */
		if (!known)
			return EBUSY;
	} else {
		/* The hardware shows the adopted list or one published since. */
		known = false;
		if (current == display->flip.active_list)
			known = true;
		for (index = 0; index < display->flip.pending_count; index++) {
			/* A published list the hardware may have taken already. */
			list = slot_list(display->flip.pending[index]);
			if (current == list)
				known = true;
		}

		/* Any other current list means another owner wrote the pointers. */
		if (!known)
			return EBUSY;

		/* The next pointer still holds the newest list this component published. */
		newest = display->flip.active_list;
		if (display->flip.pending_count != 0)
			newest = slot_list(display->flip.pending[display->flip.pending_count - 1U]);
		if (next != newest)
			return EBUSY;
	}

	/* Succeeded: spare SRAM and channel-zero publication belong to this owner. */
	return 0;
}

/* Tests physical intervals already bounded below one GiB by preparation or R0. */
static bool
frames_overlap(
	const struct drv_bcm2711_boot_screen *a,
	const struct drv_bcm2711_boot_screen *b)
{
	/* Rejects adjacent intervals only when they share an actual byte. */
	if (a->physical >= b->physical + b->size)
		return false;
	if (b->physical >= a->physical + a->size)
		return false;

	/* Succeeded: both descriptions refer to at least one common DMA byte. */
	return true;
}

/* Tests whether a frame shares a byte with a range of storage. */
static bool
range_overlaps(
	const struct drv_bcm2711_boot_screen *frame,
	uint64_t physical,
	uint64_t bytes)
{
	/* The range ends before the frame, or starts after it. */
	if (physical + bytes <= frame->physical)
		return false;
	if (physical >= frame->physical + frame->size)
		return false;

	/* Succeeded: they share at least one byte. */
	return true;
}

/* Checks an owner against the lease copy; the caller holds the guard. */
static int
owner_matches_locked(
	const struct bcm2711_display *display,
	const struct bcm2711_flip_owner *owner)
{
	/* Another open's lease, a different lease of the same open, or none. */
	if (display->flip.owner != owner->session || display->flip.lease != owner->lease)
		return EBUSY;

	/* A lease being released records nothing more. */
	if (display->flip.revoking)
		return EBUSY;

	/* Succeeded: the owner may change the planes. */
	return 0;
}

/*
 * Publishes a list made from the recorded state, when one can go out.
 *
 * Called with the guard held, by records and by the vblank IRQ.  No list
 * goes out while the state is uncertain, the console is coming back or the
 * newest list already shows the state.  With no list out, the new one goes
 * to a free slot; with one out, the new one replaces it (the hardware takes
 * one complete list or the other); with two out, the change waits for the
 * next adoption.
 */
static int
publish_locked(
	struct bcm2711_display *display)
{
	uint32_t slot;
	uint32_t list;
	bool fresh;
	int error;

	/* Only an attached, certain pipeline outside a restore takes lists. */
	if (!display->flip.attached || display->flip.uncertain || display->flip.restoring)
		return 0;
	if (display->flip.console_pending)
		return 0;

	/* The newest list already shows every record. */
	if (display->flip.list_serial == display->flip.state_serial)
		return 0;

	/* Two lists are out: the next adoption carries the change. */
	if (display->flip.pending_count == 2U) {
		display->flip.perf.coalesced++;
		return 0;
	}

	/* The channel and the list pointers must still be ours. */
	error = check_pipeline(display, false);
	if (error != 0) {
		display->flip.uncertain = true;
		waitq_wake_all(&display->flip.adoption_waitq);
		return error;
	}

	/* At most two slots are used now (the adopted list and one out), so one is free. */
	for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
		/* An unused slot's list is never read by the hardware. */
		if (!display->flip.slots[slot].used)
			break;
	}

	/* A fourth list cannot be out: the count above makes this impossible. */
	if (slot == BCM2711_FLIP_SLOTS)
		__builtin_trap();

	/* Writes the whole list, then points the hardware at it. */
	list = write_list_locked(display, slot);
	fresh = true;
	if (display->flip.pending_count != 0) {
		/* A replacement must not drop a vblank the list it replaces may still need. */
		fresh = false;
		display->flip.perf.replaced++;
	}

	/* The hardware follows the pointer at a coming frame start. */
	publish_list(display, list, fresh);

	/* The list is out, the newest, and shows every record so far. */
	display->flip.pending[display->flip.pending_count] = slot;
	display->flip.pending_count++;
	display->flip.list_serial = display->flip.state_serial;
	display->flip.perf.lists++;

	/* Succeeded: the hardware takes the list at a coming frame start. */
	return 0;
}

/*
 * Writes a list made from the recorded state into an unused slot.
 *
 * The list is the console (unless an opaque full-screen primary hides it),
 * the primary, the cursor and the terminator, in that order from the bottom.
 * Returns the list's address; the slot records what the list names.
 */
static uint32_t
write_list_locked(
	struct bcm2711_display *display,
	uint32_t slot)
{
	struct drv_bcm2711_boot_screen extent;
	struct bcm2711_flip_primary *primary;
	uint32_t words[FLIP_LIST_MAX_WORDS];
	uint32_t count;
	uint32_t cursor;
	uint32_t index;
	uint32_t list;
	bool hidden;
	int error;

	/* The console below everything, unless the primary covers it. */
	primary = &display->flip.primary;
	count = 0;
	hidden = console_hidden(display, primary);
	if (!hidden) {
		error = encode_plane(&display->screen, &display->screen, words);
		if (error != 0)
			__builtin_trap();
		count += FLIP_PLANE_WORDS;
	}

	/* The primary at its place, opaque or with premultiplied alpha; it was checked when recorded. */
	if (primary->shown) {
		extent = display->screen;
		extent.width = primary->frame.width;
		extent.height = primary->frame.height;
		error = encode_plane(&extent, &primary->frame, words + count);
		if (error != 0)
			__builtin_trap();
		words[count + 1U] = (primary->y << 16) | primary->x;
		if (primary->premultiplied)
			words[count + 2U] = 0x2000fff0U;
		count += FLIP_PLANE_WORDS;
	}

	/* The cursor on top, clipped to the output; it adds nothing when hidden or wholly outside. */
	cursor = cursor_words(display, words + count);
	count += cursor;

	/* The terminator follows every plane. */
	words[count] = FLIP_LIST_END;
	count++;

	/* Fresh context words go into the unused slot; the hardware does not read it yet. */
	list = slot_list(slot);
	for (index = 0; index < count; index++)
		kern_mmio_write32(display->compositor.mapped + 0x4000U + (list + index) * 4U, words[index]);

	/* The slot names the frame and the cursor image until a later adoption retires it. */
	display->flip.slots[slot].used = true;
	display->flip.slots[slot].serial = display->flip.state_serial;
	display->flip.slots[slot].has_frame = primary->shown;
	kern_memset(&display->flip.slots[slot].frame, 0, sizeof(display->flip.slots[slot].frame));
	if (primary->shown)
		display->flip.slots[slot].frame = primary->frame;
	display->flip.slots[slot].cursor_image = 0;
	if (cursor != 0)
		display->flip.slots[slot].cursor_image = display->flip.cursor.physical;

	/* Succeeded: the complete list waits in its slot. */
	return list;
}

/*
 * Points the hardware's next-list register at a complete list, with the guard held.
 *
 * A fresh publication (no list of ours out) discards a stale vblank status
 * first; a replacement leaves it, as the list it replaces may have been
 * taken at that very frame start.
 */
static void
publish_list(
	struct bcm2711_display *display,
	uint32_t list,
	bool fresh)
{
	uint32_t background;

	/* The background fill is requested once and stays on. */
	background = kern_mmio_read32(display->compositor.mapped + 0x44);
	if ((background & 0x01000000U) == 0)
		kern_mmio_write32(display->compositor.mapped + 0x44, background | 0x01000000U);

	/* A fresh publication starts from a quiet vblank source. */
	if (fresh)
		kern_mmio_write32(display->timing[display->port].mapped + 0x28, 0x80);

	/* The complete SRAM list precedes the pointer the hardware follows. */
	kern_io_write_barrier();
	kern_mmio_write32(display->compositor.mapped + 0x20, list);
	kern_io_write_barrier();

	/* Succeeded: the hardware takes the list at a coming frame start. */
	return;
}

/* Returns a list slot's address in SRAM words. */
static uint32_t
slot_list(
	uint32_t slot)
{
	/* Slots follow one another from the first private list word. */
	return FLIP_FIRST_SLOT_LIST + slot * FLIP_SLOT_WORDS;
}

/*
 * Encodes the cursor plane clipped to the output, returning its word count (0 when nothing shows).
 *
 * The plane is unscaled with per-pixel premultiplied alpha, like a
 * translucent primary; clipping at the left and top moves the start of the
 * image rather than the plane.
 */
static uint32_t
cursor_words(
	const struct bcm2711_display *display,
	uint32_t *words)
{
	const struct bcm2711_flip_cursor *cursor;
	int64_t x;
	int64_t y;
	int64_t width;
	int64_t height;
	uint32_t offset;
	uint32_t order;

	/* A hidden cursor adds no plane. */
	cursor = &display->flip.cursor;
	if (!cursor->shown)
		return 0;
	x = cursor->x;
	y = cursor->y;
	width = cursor->width;
	height = cursor->height;
	offset = 0;

	/* Columns left of the output and rows above it are skipped in the image. */
	if (x < 0) {
		if (-x >= width)
			return 0;
		offset += (uint32_t)(-x) * 4U;
		width += x;
		x = 0;
	}
	if (y < 0) {
		if (-y >= height)
			return 0;
		offset += (uint32_t)(-y) * cursor->pitch;
		height += y;
		y = 0;
	}

	/* Columns and rows beyond the right and bottom are cut off. */
	if (x >= display->screen.width || y >= display->screen.height)
		return 0;
	if (x + width > display->screen.width)
		width = display->screen.width - x;
	if (y + height > display->screen.height)
		height = display->screen.height - y;

	/* The same eight words as a frame plane, at its position and with per-pixel alpha. */
	order = 2;
	if (cursor->format == 1)
		order = 3;
	words[0] = 0x48009807U | (order << 13);
	words[1] = ((uint32_t)y << 16) | (uint32_t)x;
	words[2] = 0x2000fff0U;
	words[3] = ((uint32_t)height << 16) | (uint32_t)width;
	words[4] = 0xc0c0c0c0U;
	words[5] = 0xc0000000U | (cursor->physical + offset);
	words[6] = 0xc0c0c0c0U;
	words[7] = cursor->pitch;

	/* Succeeded: one plane follows the frame's. */
	return FLIP_PLANE_WORDS;
}

/*
 * Settles the published list at pending index found as adopted; the caller holds the guard.
 *
 * The lists older than it (the adopted one and those published before it)
 * can no longer be read, so their slots are free; the record numbers the
 * adopted list shows are adopted.
 */
static void
adopt_slot_locked(
	struct bcm2711_display *display,
	uint32_t found,
	uint32_t current)
{
	uint32_t slot;
	uint32_t index;

	/* The list adopted before and the ones published before this one are gone. */
	if (display->flip.active_slot != BCM2711_FLIP_NO_SLOT)
		display->flip.slots[display->flip.active_slot].used = false;
	for (index = 0; index < found; index++)
		display->flip.slots[display->flip.pending[index]].used = false;

	/* The adopted list is the active one; later published ones stay out. */
	slot = display->flip.pending[found];
	display->flip.active_slot = slot;
	display->flip.active_list = current;
	for (index = found + 1U; index < display->flip.pending_count; index++)
		display->flip.pending[index - found - 1U] = display->flip.pending[index];
	display->flip.pending_count -= found + 1U;

	/* A list is made from the whole state, so every record up to its number is shown or replaced. */
	if (display->flip.slots[slot].serial > display->flip.adopted_serial)
		display->flip.adopted_serial = display->flip.slots[slot].serial;
}

/*
 * Counts the next underrun of the attached channel: the HVS IRQ masks the
 * source when it counts one, so it does not recur every line; this clears
 * the reported status and unmasks it again once a frame.  The caller holds
 * the guard.
 */
static void
enable_underrun_locked(
	struct bcm2711_display *display)
{
	uint32_t control;

	/* An unmasked source needs nothing. */
	control = kern_mmio_read32(display->compositor.mapped);
	if ((control & 0x200U) != 0)
		return;

	/* Clears the stale status before unmasking the channel-zero source. */
	kern_mmio_write32(display->compositor.mapped + 0x04, 0x200);
	kern_mmio_write32(display->compositor.mapped, control | 0x200U);
}

/* Waits for the console list's adoption after a restore published it, at most 100 ms. */
static int
wait_console(
	struct bcm2711_display *display)
{
	unsigned long enabled;
	uint64_t deadline;
	uint64_t observed;
	uint64_t now;
	int error;

	/* Sleeps until the IRQ sees the console adopted, or the deadline. */
	deadline = sched_ticks() + FLIP_ADOPTION_TICKS;
	enabled = spin_lock_irqsave(&display->flip.guard);

	for (;;) {
		/* The console is the current list again. */
		if (!display->flip.console_pending) {
			error = 0;
			break;
		}

		/* The deadline passed: every image stays until a later restore succeeds. */
		now = sched_ticks();
		if (now >= deadline) {
			display->flip.uncertain = true;
			display->flip.restoring = false;
			waitq_wake_all(&display->flip.adoption_waitq);
			error = ETIMEDOUT;
			break;
		}

		/* Sleeps until the next adoption or the deadline. */
		observed = waitq_sequence(&display->flip.adoption_waitq);
		(void)waitq_sleep(&display->flip.adoption_waitq, &display->flip.guard, observed, deadline, 0U);
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Reports the missing adoption. */
	if (error != 0)
		return error;

	/* Succeeded: the hardware shows the console list. */
	return 0;
}

/* Tells whether the flip report and the cadence are kept: the boot line is read at the first use. */
static bool
perf_enabled(
	struct bcm2711_display *display)
{
	bool measured;

	/* The boot line is read once. */
	if (display->flip.cadence_state == BCM2711_FLIP_CADENCE_UNREAD) {
		display->flip.cadence_state = BCM2711_FLIP_CADENCE_OFF;
		measured = bcm2711_stage_perf_on();
		if (measured)
			display->flip.cadence_state = BCM2711_FLIP_CADENCE_ON;
	}

	/* Reports v3d.perf=1. */
	if (display->flip.cadence_state != BCM2711_FLIP_CADENCE_ON)
		return false;

	/* Succeeded: the report is kept. */
	return true;
}

/* Prints the copied cadence window: per kind, events per second and the interval minimum, median and maximum. */
static void
report_cadence(
	uint64_t milliseconds)
{
	uint64_t set[4];
	uint64_t move[4];
	uint64_t flip[4];

	/* Each kind's summary from its own samples. */
	cadence_summary(&cadence_copy[BCM2711_FLIP_CADENCE_SET], milliseconds, set);
	cadence_summary(&cadence_copy[BCM2711_FLIP_CADENCE_MOVE], milliseconds, move);
	cadence_summary(&cadence_copy[BCM2711_FLIP_CADENCE_FLIP], milliseconds, flip);
	kern_logf(
		"bcm2711: cadence ms=%llu per_s/min/med/max_us set=%llu/%llu/%llu/%llu move=%llu/%llu/%llu/%llu flip=%llu/%llu/%llu/%llu\n",
		(unsigned long long)milliseconds,
		(unsigned long long)set[0], (unsigned long long)set[1], (unsigned long long)set[2], (unsigned long long)set[3],
		(unsigned long long)move[0], (unsigned long long)move[1], (unsigned long long)move[2], (unsigned long long)move[3],
		(unsigned long long)flip[0], (unsigned long long)flip[1], (unsigned long long)flip[2], (unsigned long long)flip[3]);
}

/*
 * Summarizes one copied kind (summary: events per second, then the interval
 * minimum, median and maximum in microseconds, zero without an interval).
 * The samples are sorted in place.
 */
static void
cadence_summary(
	struct bcm2711_flip_cadence *cadence,
	uint64_t milliseconds,
	uint64_t summary[4])
{
	uint32_t *samples;
	uint32_t value;
	uint32_t index;
	uint32_t position;

	/* The rate counts every event of the window, sampled or not. */
	summary[0] = 0;
	if (milliseconds != 0)
		summary[0] = (uint64_t)cadence->count * 1000U / milliseconds;
	summary[1] = 0;
	summary[2] = 0;
	summary[3] = 0;
	if (cadence->stored == 0)
		return;

	/* Insertion sort of at most the window's sample bound. */
	samples = cadence->intervals;
	for (index = 1; index < cadence->stored; index++) {
		/* Moves each sample left past the larger ones. */
		value = samples[index];
		position = index;
		while (position > 0 && samples[position - 1U] > value) {
			samples[position] = samples[position - 1U];
			position--;
		}
		samples[position] = value;
	}
	summary[1] = samples[0];
	summary[2] = samples[cadence->stored / 2U];
	summary[3] = samples[cadence->stored - 1U];
}
