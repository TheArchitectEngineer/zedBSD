/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Borrowed contiguous RGB32 buffers are retired only after actual HVS adoption. */
#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <kern/dcache.h>
#include <kern/device-io.h>
#include <kern/pmem.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"
#include "drivers/platform/rpi4/rpi4-firmware.h"

/* The boot primary and two spare lists do not overlap the reserved filter. */
#define FLIP_CONSOLE_LIST 43U
#define FLIP_FIRST_LIST 64U
#define FLIP_LIST_STRIDE 16U
#define FLIP_PLANE_WORDS 9U
#define FLIP_NO_LIST 4096U

/* Two-plane lists need seventeen words; their slots cannot overlap the primaries. */
#define FLIP_COMPOSE_FIRST 128U
#define FLIP_COMPOSE_STRIDE 32U
#define FLIP_COMPOSE_WORDS 17U

static int submit_frame(struct bcm2711_display *display, const struct drv_bcm2711_boot_screen *frame, const uint32_t *words, uint32_t count, bool composed);
static int raise_clock(struct bcm2711_display *display, uint32_t required);
static int request_clock(uint32_t hz);
static void finish_adoption(struct bcm2711_display *display, uint32_t required);
static int check_pipeline(struct bcm2711_display *display, bool recovering);
static int prepare_frame(const struct drv_bcm2711_boot_screen *console, const struct drv_bcm2711_boot_screen *frame, uint32_t *words);
static bool frames_overlap(const struct drv_bcm2711_boot_screen *a, const struct drv_bcm2711_boot_screen *b);
static void publish_list(struct bcm2711_display *display, uint32_t list);
static int wait_adoption(struct bcm2711_display *display);
static void report_flips(struct bcm2711_display *display, uint64_t clean, uint64_t raise, uint64_t wait);
static uint32_t cursor_words(const struct bcm2711_display *display, uint32_t *words);
static void publish_cursor_locked(struct bcm2711_display *display);

/*
 * Initializes persistent IRQ state before installing source callback owners.
 * The enclosing display is zeroed at discovery and outlives all submissions.
 */
void
bcm2711_display_flip_init(
	struct bcm2711_display *display)
{
	/* Initializes once, so repeated IRQ preparation cannot release DMA holds. */
	if (display->flip.initialized)
		return;
	spin_init(&display->flip.guard, LOCK_RANK_DEVICE, "bcm2711-display");
	display->flip.active_list = FLIP_CONSOLE_LIST;
	display->flip.pending_list = FLIP_NO_LIST;
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
	if (display->compositor.mapped == NULL || display->compositor.size < 0x42c4U)
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

	/* Withdraws ownership only when no submission, recovery or borrowed DMA exists. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	if (display->flip.busy ||
	    display->flip.uncertain ||
	    display->flip.retained_mask != 0) {
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
 * Presents one caller-owned buffer and waits for a matching selected-PV frame.
 * Errors after publication retain all possibly referenced buffers in status.
 */
int
bcm2711_display_flip_present(
	struct bcm2711_display *display,
	const struct drv_bcm2711_boot_screen *frame)
{
	uint32_t words[FLIP_PLANE_WORDS];
	int error;

	/* Rejects unrepresentable geometry before touching ownership or hardware. */
	error = prepare_frame(&display->screen, frame, words);
	if (error != 0)
		return error;

	/* Publishes only the complete independent primary-plane image. */
	error = submit_frame(display, frame, words, FLIP_PLANE_WORDS, false);
	if (error != 0)
		return error;

	/* Succeeded: the selected PV observed actual primary adoption. */
	return 0;
}

/*
 * Composes an unscaled upper image over the retained full-screen console.
 * Positions and alpha are encoded independently; both SRAM contexts are fresh.
 */
int
bcm2711_display_flip_compose(
	struct bcm2711_display *display,
	const struct drv_bcm2711_boot_screen *frame,
	uint32_t x,
	uint32_t y,
	bool premultiplied)
{
	struct drv_bcm2711_boot_screen extent;
	uint32_t words[FLIP_COMPOSE_WORDS];
	int error;

	/* Refuses any upper-plane crop, scaling or overflow of the established output. */
	if (frame == NULL)
		return EINVAL;
	if (x > display->screen.width || frame->width > display->screen.width - x)
		return EINVAL;
	if (y > display->screen.height || frame->height > display->screen.height - y)
		return EINVAL;

	/* Regenerates the console plane instead of copying hardware-written contexts. */
	error = prepare_frame(&display->screen, &display->screen, words);
	if (error != 0)
		return error;

	/* Validates the upper buffer using its unscaled dimensions and native byte order. */
	extent = display->screen;
	extent.width = frame->width;
	extent.height = frame->height;
	error = prepare_frame(&extent, frame, words + 8);
	if (error != 0)
		return error;

	/* Replaces the lower terminator with the upper plane, followed by its own END. */
	words[9] = (y << 16) | x;
	if (premultiplied)
		words[10] = 0x2000fff0U;

	/* Publishes both completed planes while retaining the boot routes and pixel timing. */
	error = submit_frame(display, frame, words, FLIP_COMPOSE_WORDS, true);
	if (error != 0)
		return error;

	/* Succeeded: a selected-PV frame adopted both planes in their new SRAM slot. */
	return 0;
}

/*
 * Records the cursor and publishes it over the shown composed frame.
 *
 * The list is published without waiting for its adoption; a list already
 * pending, or a submitter in flight, carries the change in the next list.
 */
int
bcm2711_display_flip_cursor(
	struct bcm2711_display *display,
	const struct bcm2711_flip_cursor *cursor)
{
	unsigned long enabled;

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

	/* A change of a cursor hidden before and after shows nothing new. */
	if (cursor->shown || display->flip.cursor.shown)
		display->flip.cursor_dirty = true;
	display->flip.cursor = *cursor;
	publish_cursor_locked(display);

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the cursor shows from a coming refresh boundary. */
	return 0;
}

/*
 * Tells whether a cursor image may be read by the HVS or by a coming list.
 *
 * It is named while a retained list shows it or while the recorded cursor
 * (published or not) uses it; any other image may be rewritten at once.
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

	/* Retained lists and the recorded cursor are read together. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	named = display->flip.cursor.physical == physical;
	for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
		/* Only a list the HVS may still fetch keeps its image. */
		if ((display->flip.retained_mask & (1U << slot)) != 0 && display->flip.cursor_images[slot] == physical)
			named = true;
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the answer holds while the caller excludes other cursor changes. */
	return named;
}

/*
 * Waits until no published list is pending, so that both slots' contents are known.
 *
 * The caller holds the display's mutex, so no submitter or cursor change
 * publishes meanwhile; a list adopted now publishes no other unless the
 * cursor changed while it was pending.
 */
int
bcm2711_display_flip_settle(
	struct bcm2711_display *display)
{
	unsigned long enabled;
	uint32_t waited;
	int error;

	/* Nothing to wait for before the guard exists. */
	if (!display->flip.initialized)
		return 0;

	/* Polls adoption like a submitter, bounded the same way. */
	waited = 0;
	for (;;) {
		/* Samples the pending list with the IRQ observer excluded. */
		enabled = spin_lock_irqsave(&display->flip.guard);

		error = 0;
		if (display->flip.pending_list == FLIP_NO_LIST || display->flip.uncertain) {
			/* Settled, or left to recovery. */
		} else if (waited >= 100000U) {
			/* Both lists stay retained if adoption arrives later. */
			display->flip.uncertain = true;
			error = ETIMEDOUT;
		} else {
			error = EAGAIN;
		}

		spin_unlock_irqrestore(&display->flip.guard, enabled);

		/* Leaves once settled or out of time. */
		if (error != EAGAIN)
			break;
		kern_usleep_range(10, 10);
		waited += 10;
	}

	/* Succeeded or timed out: no list is left pending without uncertainty. */
	return error;
}

/*
 * Restores the retained boot console without changing mode or routing.
 * A fresh matching PV interrupt is required even after a late uncertain flip.
 */
int
bcm2711_display_flip_restore(
	struct bcm2711_display *display)
{
	unsigned long enabled;
	void *mapping;
	int error;

	/* Requires the persistent owner and the existing console CPU mapping. */
	if (!display->flip.initialized)
		return ENODEV;
	mapping = kern_pmem_to_kernel(display->screen.physical);
	if (mapping == NULL)
		return EFAULT;

	/* Excludes ordinary submissions while console bytes are prepared. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	if (!display->flip.attached) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return ENODEV;
	}

	/* An in-flight submitter owns both publication and its synchronous result. */
	if (display->flip.busy) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return EBUSY;
	}

	/* Keeps another caller from publishing while console cache cleaning runs. */
	display->flip.busy = true;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Raises the provider clock before restoring a potentially heavier active list. */
	error = raise_clock(display, display->console_core_hz);
	if (error != 0)
		return error;

	/* Makes current console contents visible before reinstating its original list. */
	kern_dcache_clean_range(mapping, (size_t)display->screen.size);
	kern_io_write_barrier();

	/* Allows recovery from uncertain current/next pointers only on this pipeline. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	error = check_pipeline(display, true);
	if (error != 0) {
		display->flip.uncertain = true;
		display->flip.busy = false;
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return error;
	}

	/* Requeues the preserved console list and demands a fresh matching source. */
	publish_list(display, FLIP_CONSOLE_LIST);

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Returns holds only after the selected PV proves boot-console adoption. */
	error = wait_adoption(display);
	if (error != 0)
		return error;

	/* Lowers bandwidth only after adoption, while publication is still excluded. */
	finish_adoption(display, display->console_core_hz);

	/* Succeeded: no caller-provided framebuffer remains retained. */
	return 0;
}

/*
 * Copies buffer holds and progress under the IRQ guard for the sole buffer owner.
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

	/* Copies immutable descriptors alongside the mask that defines their lifetime. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	status->attached = display->flip.attached;
	status->busy = display->flip.busy;
	status->uncertain = display->flip.uncertain;
	status->active_list = display->flip.active_list;
	status->pending_list = display->flip.pending_list;
	status->retained_mask = display->flip.retained_mask;
	status->frame_sequence = display->flip.frame_sequence;
	status->core_hz = display->flip.core_hz;
	status->clock_error = display->flip.clock_error;
	for (slot = 0; slot < BCM2711_FLIP_SLOTS; slot++) {
		/* Masked-out copies do not grant DMA ownership and may name retired memory. */
		status->frames[slot] = display->flip.frames[slot];
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the caller owns one coherent flip-state observation. */
	return;
}

/*
 * Observes the selected PV's acknowledged frame with the caller's guard held.
 * A timeout stays uncertain until actual console adoption permits all retirement.
 */
void
bcm2711_display_flip_vblank_locked(
	struct bcm2711_display *display,
	uint32_t current)
{
	uint32_t slot;
	uint32_t control;

	/* Counts real selected-PV frames independently of submissions and old lists. */
	display->flip.frame_sequence++;
	if (!display->flip.attached || display->flip.pending_list == FLIP_NO_LIST)
		return;
	if (current != display->flip.pending_list)
		return;

	/* Records adoption before marking completion visible to the synchronous caller. */
	display->flip.active_list = current;
	display->flip.pending_list = FLIP_NO_LIST;
	if (current == FLIP_CONSOLE_LIST) {
		/* Fresh console adoption makes every caller framebuffer safe to release. */
		display->flip.retained_mask = 0;
		display->flip.uncertain = false;
	} else if (!display->flip.uncertain) {
		/* Ordinary adoption releases the preceding list's borrowed storage. */
		slot = (current - FLIP_FIRST_LIST) / FLIP_LIST_STRIDE;
		if (current >= FLIP_COMPOSE_FIRST)
			slot = (current - FLIP_COMPOSE_FIRST) / FLIP_COMPOSE_STRIDE;
		display->flip.retained_mask = 1U << slot;
	}

	/* Clears stale underrun before enabling the adopted channel's source. */
	kern_mmio_write32(display->compositor.mapped + 0x04, 0x200);
	control = kern_mmio_read32(display->compositor.mapped);
	kern_mmio_write32(display->compositor.mapped, control | 0x200U);
	display->flip.completed = true;

	/* A cursor change made while this list was pending goes into the next one. */
	publish_cursor_locked(display);

	/* Succeeded: the adopted list has one completed sequence visible to waiters. */
	return;
}

/* Publishes one completely prepared primary or composed list with identical DMA holds. */
static int
submit_frame(
	struct bcm2711_display *display,
	const struct drv_bcm2711_boot_screen *frame,
	const uint32_t *words,
	uint32_t count,
	bool composed)
{
	uint32_t slot;
	uint32_t index;
	uint32_t list;
	uint32_t required;
	uint32_t cursor[8];
	uint32_t extra;
	uint64_t load;
	unsigned long enabled;
	bool overlap;
	void *mapping;
	void *console_mapping;
	uint64_t mark;
	uint64_t clean_ticks;
	uint64_t raise_ticks;
	uint64_t wait_ticks;
	uint64_t finish_ticks;
	int error;

	/* Computes the two unscaled planes at four pixels per compositor cycle. */
	required = display->console_core_hz;
	if (composed) {
		/* One output uses sixty percent of the aggregate plane cycle demand. */
		load = (uint64_t)display->screen.width * display->screen.height + (uint64_t)frame->width * frame->height;
		load = load * ((display->refresh_millihz + 500U) / 1000U) / 4U;
		load = load * 60U / 100U;
		if (load > required)
			required = (uint32_t)load;
	}

	/* Preparation completed before acquiring a mutable publication owner. */
	if (!display->flip.initialized)
		return ENODEV;
	mapping = kern_pmem_to_kernel(frame->physical);
	if (mapping == NULL)
		return EFAULT;
	console_mapping = NULL;
	if (composed) {
		/* The lower console may have received newer cached text since R0. */
		console_mapping = kern_pmem_to_kernel(display->screen.physical);
		if (console_mapping == NULL)
			return EFAULT;
	}

	/* Reserves a free SRAM slot without overwriting an active or uncertain list. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	if (!display->flip.attached) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return ENODEV;
	}

	/* An existing operation, a pending cursor list or uncertain DMA requires admission refusal. */
	if (display->flip.busy || display->flip.uncertain || display->flip.pending_list != FLIP_NO_LIST) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return EBUSY;
	}

	/* Rejects aliasing with the console and every buffer still referenced by DMA. */
	overlap = frames_overlap(frame, &display->screen);
	if (overlap) {
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return EINVAL;
	}

	/* Checks every retained interval before choosing another borrowed allocation. */
	for (index = 0; index < BCM2711_FLIP_SLOTS; index++) {
		/* Only retained descriptors name live storage; unused copies are historical. */
		if ((display->flip.retained_mask & (1U << index)) == 0)
			continue;
		overlap = frames_overlap(frame, &display->flip.frames[index]);
		if (overlap) {
			spin_unlock_irqrestore(&display->flip.guard, enabled);
			return EINVAL;
		}
	}

	/* Chooses an unheld slot while BUSY excludes another submitter or restorer. */
	slot = 0;
	if ((display->flip.retained_mask & 1U) != 0)
		slot = 1;

	/* Keeps another caller from publishing while candidate cache cleaning runs. */
	display->flip.busy = true;

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Raises bandwidth before any SRAM or next-list write, outside the IRQ guard. */
	mark = clock_ticks();
	error = raise_clock(display, required);
	raise_ticks = clock_ticks() - mark;
	if (error != 0)
		return error;

	/* Publishes both lower-console and upper-image bytes before the composed list. */
	mark = clock_ticks();
	if (console_mapping != NULL)
		kern_dcache_clean_range(console_mapping, (size_t)display->screen.size);
	if (!frame->uncached)
		kern_dcache_clean_range(mapping, (size_t)frame->size);
	kern_io_write_barrier();
	clean_ticks = clock_ticks() - mark;

	/* Rechecks ownership, then installs all words before the next-list pointer. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	/* Confirms exclusive use of the established channel and both list pointers. */
	error = check_pipeline(display, false);
	if (error != 0) {
		display->flip.uncertain = true;
		display->flip.busy = false;
		spin_unlock_irqrestore(&display->flip.guard, enabled);
		return error;
	}

	/* Writes the inactive slot completely while the current slot stays intact. */
	list = FLIP_FIRST_LIST + slot * FLIP_LIST_STRIDE;
	if (composed)
		list = FLIP_COMPOSE_FIRST + slot * FLIP_COMPOSE_STRIDE;
	for (index = 0; index + 1U < count; index++) {
		/* Context placeholders are fresh even when this retired slot is reused. */
		kern_mmio_write32(display->compositor.mapped + 0x4000U + (list + index) * 4U, words[index]);
	}

	/* A composed list keeps its planes for cursor changes and carries the cursor above them. */
	display->flip.composed[slot] = false;
	display->flip.cursor_images[slot] = 0;
	if (composed && count == FLIP_COMPOSE_WORDS) {
		kern_memcpy(display->flip.base_words[slot], words, sizeof(display->flip.base_words[slot]));
		display->flip.composed[slot] = true;
		extra = cursor_words(display, cursor);
		for (index = 0; index < extra; index++)
			kern_mmio_write32(display->compositor.mapped + 0x4000U + (list + count - 1U + index) * 4U, cursor[index]);
		if (extra != 0)
			display->flip.cursor_images[slot] = display->flip.cursor.physical;
		display->flip.cursor_dirty = false;
		count += extra;
	}

	/* The terminator follows every plane. */
	kern_mmio_write32(display->compositor.mapped + 0x4000U + (list + count - 1U) * 4U, 0x80000000U);

	/* Retains the candidate before hardware can fetch its newly published list. */
	display->flip.frames[slot] = *frame;
	display->flip.retained_mask |= 1U << slot;
	publish_list(display, list);

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Waits without holding the IRQ guard; timeout never releases borrowed DMA. */
	mark = clock_ticks();
	error = wait_adoption(display);
	wait_ticks = clock_ticks() - mark;
	if (error != 0)
		return error;

	/* The stage report is written while BUSY still excludes every other submitter. */
	mark = clock_ticks();
	report_flips(display, clean_ticks, raise_ticks, wait_ticks);

	/*
	 * A composed frame keeps the transition floor for the frames that
	 * follow; only the return to the console lowers the clock again.
	 */
	if (composed)
		required = display->flip.core_hz;

	/* Releases temporary bandwidth only after the frame has actually adopted. */
	finish_adoption(display, required);
	finish_ticks = clock_ticks() - mark;
	display->flip.perf_finish += finish_ticks;

	/* Succeeded: status now retains only the adopted framebuffer. */
	return 0;
}

/* Raises the transition floor while BUSY serializes all firmware-clock requests. */
static int
raise_clock(
	struct bcm2711_display *display,
	uint32_t required)
{
	uint32_t hz;
	unsigned long enabled;
	int error;

	/* Refuses bandwidth outside the ceiling captured before firmware teardown. */
	error = 0;
	if (required == 0 || required > display->max_core_hz)
		error = ENOTSUP;
	hz = 500000000U;
	if (hz < required)
		hz = required;
	if (hz < display->flip.core_hz)
		hz = display->flip.core_hz;
	if (hz > display->max_core_hz)
		hz = display->max_core_hz;

	/* Requests the same temporary floor as R0 before publishing any new list, unless the clock already runs at it. */
	if (error == 0 && hz != display->flip.core_hz)
		error = request_clock(hz);

	/* A failed request relinquishes admission without publishing the candidate. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.clock_error = error;
	if (error != 0) {
		display->flip.busy = false;
	} else {
		/* A timeout keeps this transition rate until confirmed console recovery. */
		display->flip.core_hz = hz;
	}

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Refuses publication if the provider could not establish its bandwidth. */
	if (error != 0)
		return error;

	/* Succeeded: the transition clock precedes every candidate register write. */
	return 0;
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

/* Retires the transition floor only after actual adoption, before releasing BUSY. */
static void
finish_adoption(
	struct bcm2711_display *display,
	uint32_t required)
{
	unsigned long enabled;
	int error;

	/* A refused reduction leaves the higher safe floor and the adopted image intact; an unchanged rate needs no request. */
	error = 0;
	if (required != display->flip.core_hz)
		error = request_clock(required);

	/* Publishes the clock diagnostic alongside permission for another submission. */
	enabled = spin_lock_irqsave(&display->flip.guard);

	display->flip.clock_error = error;
	if (error == 0)
		display->flip.core_hz = required;
	display->flip.busy = false;

	/* A cursor change made while the submitter was busy goes into a list of its own. */
	publish_cursor_locked(display);

	spin_unlock_irqrestore(&display->flip.guard, enabled);

	/* Succeeded: the adopted output state and clock outcome are visible under the flip guard. */
	return;
}

/* Checks sole ownership and unchanged timing before any list publication. */
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
	uint32_t port;

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
		/* Known private pointers are the only uncertainty this component owns. */
		if (current != FLIP_CONSOLE_LIST &&
		    current != 64U &&
		    current != 80U &&
		    current != 128U &&
		    current != 160U)
			return EBUSY;
		if (next != FLIP_CONSOLE_LIST &&
		    next != 64U &&
		    next != 80U &&
		    next != 128U &&
		    next != 160U)
			return EBUSY;
	} else {
		/* Ordinary publication requires the last completed list on both pointers. */
		if (current != display->flip.active_list || next != display->flip.active_list)
			return EBUSY;
	}

	/* Succeeded: spare SRAM and channel-zero publication belong to this owner. */
	return 0;
}

/* Generates one independently encoded unscaled RGB32 primary plus its end word. */
static int
prepare_frame(
	const struct drv_bcm2711_boot_screen *console,
	const struct drv_bcm2711_boot_screen *frame,
	uint32_t *words)
{
	uint32_t order;

	/* Requires the same dimensions with either independently encoded RGB byte order. */
	if (frame == NULL)
		return EINVAL;
	if (frame->width != console->width || frame->height != console->height)
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

	/* Encodes a fresh eight-word plane and terminator without inherited contexts. */
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
	words[8] = 0x80000000U;

	/* Succeeded: the completed words may be copied into an inactive SRAM slot. */
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

/* Publishes completion state and the next pointer with the display guard held. */
static void
publish_list(
	struct bcm2711_display *display,
	uint32_t list)
{
	uint32_t control;
	uint32_t background;

	/* Masks underrun while installing the new list and background fill request. */
	control = kern_mmio_read32(display->compositor.mapped);
	kern_mmio_write32(display->compositor.mapped, control & ~0x200U);
	background = kern_mmio_read32(display->compositor.mapped + 0x44);
	kern_mmio_write32(display->compositor.mapped + 0x44, background | 0x01000000U);
	kern_mmio_write32(display->timing[display->port].mapped + 0x28, 0x80);

	/* Arms completion before publication; the selected source uses the same guard. */
	display->flip.completed = false;
	display->flip.pending_list = list;
	kern_io_write_barrier();
	kern_mmio_write32(display->compositor.mapped + 0x20, list);
	kern_io_write_barrier();

	/* Succeeded: the complete SRAM list precedes device publication. */
	return;
}

/* Waits for IRQ-confirmed adoption, retaining all holds across uncertain timeout. */
static int
wait_adoption(
	struct bcm2711_display *display)
{
	uint32_t waited;
	unsigned long enabled;
	int error;

	/* Bounds the wait while the selected PV can retire the published list. */
	waited = 0;
	for (;;) {
		/* Samples completion and latches timeout atomically with the IRQ observer. */
		enabled = spin_lock_irqsave(&display->flip.guard);

		error = 0;
		if (display->flip.completed) {
			/* The submitter retains BUSY until its post-adoption clock operation. */
		} else if (waited >= 100000U) {
			/* Both old and new buffers remain held even if adoption arrives later. */
			display->flip.uncertain = true;
			display->flip.busy = false;
			error = ETIMEDOUT;
		} else {
			/* EAGAIN is internal to this bounded wait, never a caller outcome. */
			error = EAGAIN;
		}

		spin_unlock_irqrestore(&display->flip.guard, enabled);

		/* Leaves the wait when adoption or its deadline has a durable outcome. */
		if (error != EAGAIN)
			break;
		kern_usleep_range(10, 10);
		waited += 10;
	}

	/* A timed-out operation supplies no permission to free borrowed buffers. */
	if (error != 0)
		return error;

	/* Succeeded: a real selected-PV frame matched the requested current list. */
	return 0;
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
	/* The display device's mutex is not the report's BUSY, so the counts are atomic. */
	__atomic_fetch_add(&display->flip.perf_cursor_sets, 1U, __ATOMIC_RELAXED);
	if (copied)
		__atomic_fetch_add(&display->flip.perf_cursor_copies, 1U, __ATOMIC_RELAXED);
}

/*
 * Adds one flip's stage times to the report and prints the window every two seconds.
 *
 * The caller still holds BUSY, so no other submitter changes these totals.
 * The release step of a flip is added by the caller after its own report.
 */
static void
report_flips(
	struct bcm2711_display *display,
	uint64_t clean,
	uint64_t raise,
	uint64_t wait)
{
	uint64_t now;
	uint64_t count;
	uint64_t sets;
	uint64_t copies;

	/* Adds this flip's stages to the window. */
	display->flip.perf_flips++;
	display->flip.perf_clean += clean;
	display->flip.perf_raise += raise;
	display->flip.perf_wait += wait;

	/* The first flip opens the window that later reports measure. */
	now = clock_ticks();
	if (display->flip.perf_start == 0) {
		display->flip.perf_start = now;
		return;
	}

	/* A window shorter than two seconds keeps accumulating. */
	if (kern_ticks_to_ms(now - display->flip.perf_start) < 2000U)
		return;

	/* The window's cursor images, taken and cleared together with the window. */
	sets = __atomic_exchange_n(&display->flip.perf_cursor_sets, 0, __ATOMIC_RELAXED);
	copies = __atomic_exchange_n(&display->flip.perf_cursor_copies, 0, __ATOMIC_RELAXED);

	/* Prints microseconds per flip for each stage of the window. */
	count = display->flip.perf_flips;
	kern_logf(
		"bcm2711: flip perf ms=%llu flips=%llu avg_us clean=%llu raise=%llu wait=%llu finish=%llu cursor sets=%llu copies=%llu\n",
		(unsigned long long)kern_ticks_to_ms(now - display->flip.perf_start),
		(unsigned long long)count,
		(unsigned long long)(kern_ticks_to_rate(display->flip.perf_clean, 1000000U) / count),
		(unsigned long long)(kern_ticks_to_rate(display->flip.perf_raise, 1000000U) / count),
		(unsigned long long)(kern_ticks_to_rate(display->flip.perf_wait, 1000000U) / count),
		(unsigned long long)(kern_ticks_to_rate(display->flip.perf_finish, 1000000U) / count),
		(unsigned long long)sets,
		(unsigned long long)copies);

	/* The next window starts empty at this report. */
	display->flip.perf_start = now;
	display->flip.perf_flips = 0;
	display->flip.perf_clean = 0;
	display->flip.perf_raise = 0;
	display->flip.perf_wait = 0;
	display->flip.perf_finish = 0;
}

/*
 * Encodes the cursor plane clipped to the output, returning its word count (0 when nothing shows).
 *
 * The plane is unscaled with per-pixel premultiplied alpha, like the
 * composed upper plane; clipping at the left and top moves the start of the
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
	return 8;
}

/*
 * Publishes the shown composed frame again with the changed cursor, in the other slot, without waiting.
 *
 * Called with the guard held, by a cursor change, after an adoption and
 * when a submitter finishes.  Nothing is published while a list is pending,
 * a submitter is in flight, adoption is uncertain or no composed frame is
 * shown; the change stays recorded for the next chance or the next frame.
 */
static void
publish_cursor_locked(
	struct bcm2711_display *display)
{
	uint32_t cursor[8];
	uint32_t active;
	uint32_t slot;
	uint32_t list;
	uint32_t count;
	uint32_t index;

	/* Only a quiet, attached pipeline takes a list of its own. */
	if (!display->flip.cursor_dirty ||
	    !display->flip.attached ||
	    display->flip.busy ||
	    display->flip.uncertain ||
	    display->flip.pending_list != FLIP_NO_LIST)
		return;

	/* The shown list must be a composed one whose planes are known. */
	if (display->flip.active_list < FLIP_COMPOSE_FIRST ||
	    display->flip.active_list >= FLIP_COMPOSE_FIRST + BCM2711_FLIP_SLOTS * FLIP_COMPOSE_STRIDE)
		return;
	active = (display->flip.active_list - FLIP_COMPOSE_FIRST) / FLIP_COMPOSE_STRIDE;
	if (!display->flip.composed[active])
		return;
	slot = 1U - active;
	if ((display->flip.retained_mask & (1U << slot)) != 0)
		return;
	if (check_pipeline(display, false) != 0)
		return;

	/* The shown planes, the cursor and the terminator, in the free slot. */
	list = FLIP_COMPOSE_FIRST + slot * FLIP_COMPOSE_STRIDE;
	for (index = 0; index < 16U; index++)
		kern_mmio_write32(display->compositor.mapped + 0x4000U + (list + index) * 4U, display->flip.base_words[active][index]);
	count = cursor_words(display, cursor);
	for (index = 0; index < count; index++)
		kern_mmio_write32(display->compositor.mapped + 0x4000U + (list + 16U + index) * 4U, cursor[index]);
	kern_mmio_write32(display->compositor.mapped + 0x4000U + (list + 16U + count) * 4U, 0x80000000U);

	/* The free slot shows the same frame, which stays retained through it. */
	kern_memcpy(display->flip.base_words[slot], display->flip.base_words[active], sizeof(display->flip.base_words[slot]));
	display->flip.composed[slot] = true;
	display->flip.cursor_images[slot] = 0;
	if (count != 0)
		display->flip.cursor_images[slot] = display->flip.cursor.physical;
	display->flip.frames[slot] = display->flip.frames[active];
	display->flip.retained_mask |= 1U << slot;
	display->flip.cursor_dirty = false;
	display->flip.cursor_lists++;
	publish_list(display, list);
}
