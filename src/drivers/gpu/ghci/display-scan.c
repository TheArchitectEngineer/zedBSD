/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The scan-out side of a GHCI device's display: the display controller of
 * the DISP2 capability, a unit of the GPU that the ring drives.
 *
 * While the command processor runs, only the ring changes what the
 * controller shows: DISP_SET writes its registers and FLIP makes an image
 * a plane's pending one, taken at the next vertical sync (plane 0) or once
 * the cursor's image is read (plane 1).  At the take the GPU writes the
 * flip's record, {value, FRAME_COUNT}, into the record page (64 bytes
 * past the fence word of the fence page, 8 bytes a plane) and raises
 * IRQ_STATUS.FLIP.  The host writes the scan-out registers itself only
 * while the processor does not run: at the attach, during a reset and at
 * the detach of a device that no longer runs.
 *
 * The driver places a plane's next flip only once the last one's record
 * has come, so the controller's one pending flip per plane never makes
 * the ring wait; the presents in between wait in a queue of two.  A flip
 * value is 31 bits (bit 31 of a record says the flip was cancelled), and
 * the record page is cleared before each flip, so a record names its
 * flip without any wrap.
 *
 * A lease ends by marking it ended; the controller is then turned dark by
 * the ring, and the memory it scanned is let go once the cancelled
 * records and the dark packet's fence have come.  The service thread
 * reads the records, counts the scan-out errors, watches the connection
 * and finishes ended leases.  A reset of the command processor loses the
 * flips still in the ring; the driver counts them again from TAKEN0,
 * TAKEN1 and STATUS while the processor is idle, and places the lost
 * ones again.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/poll.h>
#include <uapi/errno.h>

#include "display-scan.h"
#include "ghci.h"
#include "hcr.h"
#include "packets.h"

/* The record page: the 64 bytes past the fence word, 8 bytes a plane. */
#define RECORD_OFFSET			64U
#define RECORD_BYTES			8U

/* The controller's capability bits: the external transmitter and the TMDS output. */
#define DC_CAPS_TRANSMITTER		0x4U
#define DC_CAPS_TMDS			0x2U

/* The version of the DISP2 capability the driver knows. */
#define DISP2_VERSION			1U

/*
 * How long the driver waits for the controller: to be idle after it went
 * dark, for a record still to be written after a reset, and for an ended
 * lease to let go at the detach.
 */
#define DC_IDLE_MS			100U
#define RECORD_WAIT_MS			100U
#define DETACH_WAIT_MS			200U

/* How often a waiter looks at the record page when no interrupt comes. */
#define FLIP_POLL_MS			10U

/* How often the service thread asks the transmitter whether a sink is there. */
#define HOTPLUG_PERIOD_MS		1000U

/*
 * The scan-out errors: past ERROR_LIMIT error interrupts in a second the
 * interrupt is masked for the rest of that second, and READ_ERROR_RUNS
 * rounds in a row with a read error turn the controller dark.
 */
#define ERROR_PERIOD_MS			1000U
#define ERROR_LIMIT			30U
#define READ_ERROR_RUNS			3U

/* The STATUS bits that count as scan-out errors (sticky, write 1 to clear). */
#define DC_ERRORS			(GHCI_DC_ST_UNDERFLOW | GHCI_DC_ST_READ_ERROR | GHCI_DC_ST_RECORD_ERROR | GHCI_DC_ST_BAD_WRITE)

/* The largest flip value: 31 bits. */
#define FLIP_VALUE_MAX			0x7fffffffU

/* The MODE registers' fields: a count in the low half, another in the high half. */
#define MODE_LOW(word)			((word) & 0xffffU)
#define MODE_HIGH(word)			((word) >> 16)

/* The dwords of the run that lights the controller (and flips the cursor). */
#define LIGHT_PAIRS			12U
#define LIGHT_DWORDS			(3U + (1U + 2U * LIGHT_PAIRS) + GHCI_FLIP_PACKET_DWORDS + 3U + GHCI_FLIP_PACKET_DWORDS)

static int modes_read(struct ghci_device *device);
static void image_clear(struct ghci_display_image *image);
static uint32_t control_word(struct ghci_device *device, uint32_t format);
static uint32_t record_address(struct ghci_device *device, unsigned plane);
static volatile uint64_t *record_slot(struct ghci_device *device, unsigned plane);
static uint32_t value_take(struct ghci_display *display);
static void records_take(struct ghci_device *device);
static void taken(struct ghci_device *device, unsigned plane, int cancelled);
static void queue_kick(struct ghci_device *device);
static void queue_push_front(struct ghci_display *display, const struct ghci_display_image *image);
static void queue_pop(struct ghci_display *display);
static int light(struct ghci_device *device, const struct ghci_display_image *image);
static int flip_primary(struct ghci_device *device, const struct ghci_display_image *image);
static int flip_cursor(struct ghci_device *device, int surface);
static void cursor_kick(struct ghci_device *device);
static void dark(struct ghci_device *device);
static void dark_by_host(struct ghci_device *device);
static void flips_forget(struct ghci_device *device);
static void errors_serve(struct ghci_device *device);
static void irq_mask_set(struct ghci_device *device, int masked);
static int connection_serve(struct ghci_device *device);
static void topology_move(struct ghci_device *device);

/*
 * Finds the display controller and sets it dark, before the command
 * processor runs.
 */
int
ghci_display_attach(
	struct ghci_device *device)
{
	struct ghci_display *display;
	uint32_t registers;
	uint32_t version;
	uint32_t connected_word;
	int error;

	/* A GPU without a display controller in its window has no display. */
	display = &device->display;
	display->present = 0;
	if ((device->caps & GHCI_CAPS_DISP) == 0U)
		return 0;

	/*
	 * The controller's registers, from the DISP2 capability: a controller
	 * that is a unit of the GPU, which the ring drives.  A DISP capability
	 * (a controller the host drives by itself) is not served here.
	 */
	error = ghci_extcap_find(ghci_read_context, device, device->extcap, device->register_bytes, GHCI_XCAP_DISP2, &registers, &version);
	if (error != 0)
		return error;

	/* The capability's window of 256 bytes lies inside the register window. */
	if (device->register_bytes < GHCI_DC2_WINDOW)
		return EINVAL;
	if (registers > device->register_bytes - GHCI_DC2_WINDOW)
		return EINVAL;

	/* Refuses a version of the capability the driver does not know. */
	if (version != DISP2_VERSION) {
		kern_logf("ghci: %s: DISP2 version %u is not known\n", device->name, (unsigned)version);
		return ENOTSUP;
	}

	/* The controller's identity and what it can do. */
	display->registers = registers;
	display->controller_id = ghci_read(device, registers + GHCI_DC_ID);
	display->controller_version = ghci_read(device, registers + GHCI_DC_VERSION);
	display->controller_caps = ghci_read(device, registers + GHCI_DC_CAPS);

	/* The modes, from the controller's own table. */
	error = modes_read(device);
	if (error != 0)
		return error;

	/*
	 * The controller starts reset and dark.  The host writes it directly
	 * here, before the command processor runs: once it runs, the ring
	 * alone changes what the controller shows.
	 */
	ghci_write(device, registers + GHCI_DC_CTRL, GHCI_DC_CTRL_RESET);
	ghci_write(device, registers + GHCI_DC_CTRL, control_word(device, GHCI_DC_FMT_BGRA8));
	error = ghci_poll(device, registers + GHCI_DC_STATUS, GHCI_DC_ST_IDLE, GHCI_DC_ST_IDLE, DC_IDLE_MS);
	if (error != 0)
		kern_logf("ghci: %s: display controller not idle\n", device->name);
	ghci_write(device, registers + GHCI_DC_STATUS, DC_ERRORS);

	/* Whether a sink is connected, as the controller was last told (1 when nobody told it). */
	connected_word = ghci_read(device, registers + GHCI_DC_CONNECTED);
	display->connected = 0;
	if ((connected_word & 1U) != 0U)
		display->connected = 1;

	/* An external HDMI transmitter is set up now; it shows nothing until the controller scans. */
	if ((display->controller_caps & DC_CAPS_TRANSMITTER) != 0U) {
		error = ghci_adv7511_start(device, registers);
		if (error != 0)
			kern_logf("ghci: %s: HDMI transmitter not set up (%d)\n", device->name, error);
	}

	/* The display exists, dark and without a lease, at its first generation and topology. */
	display->generation = GHCI_DISPLAY_FIRST_GENERATION;
	display->topology = 1U;
	display->hotplug_tick = clock_ticks();
	display->owner = NULL;
	display->next_lease = 1U;
	display->ending = 0;
	display->enabled = 0;
	display->mode = ghci_scan_largest_mode(display);
	display->format = GHCI_DC_FMT_BGRA8;
	display->stride = 0U;
	image_clear(&display->shown);
	display->flips[0].placed = 0;
	display->flips[1].placed = 0;
	display->queue_count = 0U;
	display->next_value = 1U;
	display->drop_count = 0U;
	display->cursor_shown = -1;
	display->cursor_pending = -1;
	display->cursor_image = 0;
	display->cursor_copying = 0;
	display->copy_frame_busy = -1;
	display->error_tick = clock_ticks();
	display->present = 1;

	/* Succeeded: the display is ready for a lease. */
	return 0;
}

/*
 * Turns the display controller off and gives its memory back for the
 * detach.
 */
void
ghci_display_detach(
	struct ghci_device *device)
{
	struct ghci_display *display;
	uint64_t deadline;
	uint64_t mark;
	uint64_t now;
	unsigned index;
	int done;

	/* A device without a display has nothing to turn off. */
	display = &device->display;
	if (!display->present)
		return;

	/* A lease still there ends; the controller goes dark either way. */
	mutex_lock(&display->lock);

	if (!display->ending || display->dark_placed < 0)
		ghci_scan_lease_end(device);

	mutex_unlock(&display->lock);

	/* Waits a bounded time for the dark packet and the cancelled records. */
	deadline = clock_ticks() + kern_ms_to_ticks(DETACH_WAIT_MS);
	for (;;) {
		mark = ghci_scan_mark(device);
		mutex_lock(&display->lock);

		done = ghci_scan_lease_done(device);

		mutex_unlock(&display->lock);

		/* Ends once everything was let go, or at the deadline. */
		now = clock_ticks();
		if (done)
			break;
		if (now >= deadline)
			break;

		ghci_scan_sleep(device, mark, deadline);
	}

	/* Whatever did not answer is forgotten, the controller turned off by the host if need be. */
	mutex_lock(&display->lock);

	if (!done) {
		kern_logf("ghci: %s: display did not go dark in time\n", device->name);
		dark_by_host(device);
		flips_forget(device);
		ghci_scan_drop(device, display->shown.memory);
		image_clear(&display->shown);
		display->ending = 0;
	}
	display->present = 0;

	mutex_unlock(&display->lock);

	/* The memory the display held and its own buffers go back. */
	ghci_scan_flush(device);
	mutex_lock(&device->lock);

	for (index = 0U; index < 2U; index++) {
		if (display->frames[index] != NULL)
			ghci_memory_free(device, display->frames[index]);
		display->frames[index] = NULL;
		if (display->surfaces[index] != NULL)
			ghci_memory_free(device, display->surfaces[index]);
		display->surfaces[index] = NULL;
	}
	if (display->staging != NULL)
		ghci_memory_free(device, display->staging);
	display->staging = NULL;

	mutex_unlock(&device->lock);
}

/*
 * Ends a closing session's lease; the device's lock is held.  Only the
 * mark is made here: the service thread turns the controller dark.
 */
void
ghci_display_session_close(
	struct ghci_device *device,
	struct ghci_session *session)
{
	struct ghci_display *display;
	int ended;

	/* Only the lease's holder has anything on the display. */
	display = &device->display;
	if (!display->present)
		return;

	/* The lease ends with its session; nothing waits for the ring here. */
	mutex_lock(&display->lock);

	ended = 0;
	if (display->owner == session) {
		display->owner = NULL;
		display->lease = 0U;
		display->ending = 1;
		display->dark_placed = -1;
		ended = 1;
	}

	mutex_unlock(&display->lock);

	/* The service thread turns the controller dark and lets the memory go. */
	if (ended)
		ghci_service_wake(device, GHCI_WORK_DISPLAY);
}

/*
 * Does the display's part of a round of the service thread: the records,
 * the end of a lease, the scan-out errors and the connection.
 */
void
ghci_display_service(
	struct ghci_device *device,
	unsigned work)
{
	struct ghci_display *display;
	uint64_t adopted;
	int changed;

	/* A device without a display has nothing to serve. */
	display = &device->display;
	if (!display->present)
		return;

	/* Everything under the display's lock; nothing here waits for the ring or a record. */
	mutex_lock(&display->lock);

	adopted = display->adopted;

	/* A lease a closed session ended goes dark now, and is finished once it let go. */
	if (display->ending && display->dark_placed < 0)
		ghci_scan_lease_end(device);
	if (display->ending)
		(void)ghci_scan_lease_done(device);

	/*
	 * The scan-out errors: after an error interrupt (GHCI_WORK_DISPLAY in
	 * work), and every round, so a masked second ends on time.
	 */
	(void)work;
	errors_serve(device);

	/* The connection, and the records and what waits for the controller. */
	changed = connection_serve(device);
	ghci_scan_update(device);
	if (display->adopted != adopted)
		changed = 1;

	mutex_unlock(&display->lock);

	/* The memory let go of, and the waiters on the display's events. */
	ghci_scan_flush(device);
	if (changed)
		poll_notify();
}

/*
 * Takes the display's lock before a reset of the command processor, so
 * nothing places a flip while the processor is reset.
 */
void
ghci_display_recover_begin(
	struct ghci_device *device)
{
	/* The lock exists for every device, with a display or not. */
	mutex_lock(&device->display.lock);
}

/*
 * Counts the display's flips again while the reset processor is idle and
 * the host may write the controller; cp_lock and the display's lock are
 * held.
 */
void
ghci_display_recover_count(
	struct ghci_device *device)
{
	struct ghci_display *display;
	struct ghci_display_flip *flip;
	uint32_t status;
	uint32_t taken_value;
	uint32_t pending;
	unsigned plane;
	int error;

	/* A device without a display has nothing to count. */
	display = &device->display;
	if (!display->present)
		return;

	/* A record still to be written goes out while the processor is idle. */
	error = ghci_poll(device, display->registers + GHCI_DC_STATUS, GHCI_DC_ST_RECORD, 0U, RECORD_WAIT_MS);
	if (error != 0)
		kern_logf("ghci: %s: a flip's record was not written after the reset\n", device->name);

	/* The records that came. */
	records_take(device);

	/* An ended lease's controller goes dark now, by the host: the dark packet may have been lost. */
	if (display->ending) {
		dark_by_host(device);
		flips_forget(device);
	}

	/* Each flip still placed is in the slot, was taken without its record, or was lost with the ring. */
	status = ghci_read(device, display->registers + GHCI_DC_STATUS);
	for (plane = 0U; plane < 2U; plane++) {
		flip = &display->flips[plane];
		if (!flip->placed)
			continue;

		/* A flip in the controller's slot is taken later; its record comes once the ring runs. */
		pending = GHCI_DC_ST_PENDING0;
		if (plane == 1U)
			pending = GHCI_DC_ST_PENDING1;
		if ((status & pending) != 0U)
			continue;

		/* A flip the controller took, whose record write failed, is known by TAKEN0 or TAKEN1. */
		if (plane == 0U) {
			taken_value = ghci_read(device, display->registers + GHCI_DC_TAKEN0);
		} else {
			taken_value = ghci_read(device, display->registers + GHCI_DC_TAKEN1);
		}
		if ((taken_value & ~GHCI_FLIP_CANCELLED) == flip->value) {
			taken(device, plane, (taken_value & GHCI_FLIP_CANCELLED) != 0U);
			continue;
		}

		/* A flip lost with the ring is placed again: a lost lighting lights again. */
		flip->placed = 0;
		if (plane == 0U) {
			if (flip->light)
				display->enabled = 0;
			queue_push_front(display, &flip->image);
		} else {
			display->cursor_pending = flip->surface;
		}
	}

	/* A fault hides the cursor (its image is kept); the connection is read again by the service thread. */
	ghci_write(device, display->registers + GHCI_DC_CURSOR_CTRL, 0U);
	ghci_write(device, display->registers + GHCI_DC_STATUS, DC_ERRORS);
}

/*
 * Places again what the reset lost, when the GPU came back, and lets the
 * display's lock go.
 */
void
ghci_display_recover_end(
	struct ghci_device *device,
	int running)
{
	struct ghci_display *display;

	/* The ring runs again: the records it cleared are read, and the lost flips placed. */
	display = &device->display;
	if (display->present && running)
		ghci_scan_update(device);

	mutex_unlock(&display->lock);

	/* The memory let go of, and the connection read again. */
	if (display->present) {
		ghci_scan_flush(device);
		ghci_service_wake(device, GHCI_WORK_DISPLAY);
	}
}

/*
 * Reports the index of the controller's largest mode, by its pixels.
 */
unsigned
ghci_scan_largest_mode(
	const struct ghci_display *display)
{
	uint64_t pixels;
	uint64_t largest_pixels;
	unsigned largest;
	unsigned index;

	/* The first of the modes with the most pixels. */
	largest = 0U;
	largest_pixels = 0U;
	for (index = 0U; index < display->mode_count; index++) {
		pixels = (uint64_t)display->modes[index].width * display->modes[index].height;
		if (pixels > largest_pixels) {
			largest = index;
			largest_pixels = pixels;
		}
	}

	/* Succeeded: the largest mode. */
	return largest;
}

/*
 * Reports whether the ring can take the display's packets now.
 */
int
ghci_scan_ring_usable(
	struct ghci_device *device)
{
	/* A processor that stopped or was lost runs no packet. */
	if (!device->running)
		return 0;
	if (device->lost)
		return 0;

	/* Succeeded: the ring runs. */
	return 1;
}

/*
 * Reports the bytes between the rows of a frame of a width: whole pixels,
 * rounded up to the memory line, as the controller scans them.
 */
uint32_t
ghci_scan_stride(
	struct ghci_device *device,
	uint32_t width)
{
	uint32_t line;
	uint32_t bytes;

	/* The memory line, at least a pixel. */
	line = device->line_bytes;
	if (line < GHCI_DISPLAY_PIXEL_BYTES)
		line = GHCI_DISPLAY_PIXEL_BYTES;

	/* The row's pixels, rounded up to whole lines. */
	bytes = width * GHCI_DISPLAY_PIXEL_BYTES;
	bytes = (bytes + line - 1U) / line * line;

	/* Reports the stride. */
	return bytes;
}

/*
 * Reports the time a frame is shown, on the scheduler's clock in
 * nanoseconds.
 */
uint64_t
ghci_scan_now_ns(void)
{
	uint64_t ticks;

	/* The clock's ticks, in nanoseconds. */
	ticks = clock_ticks();

	/* Reports the time. */
	return ticks * (KERN_NSEC_PER_SEC / KERN_CLOCK_HZ);
}

/*
 * Reads the records the controller wrote and places what waits for the
 * planes; the lock is held.
 */
void
ghci_scan_update(
	struct ghci_device *device)
{
	/* The records, then the next present and the cursor's flip. */
	records_take(device);
	queue_kick(device);
	cursor_kick(device);
}

/*
 * Records an image of plane 0 for the controller, after every one
 * recorded before.
 */
int
ghci_scan_record(
	struct ghci_device *device,
	const struct ghci_display_image *image,
	int fifo)
{
	struct ghci_display *display;
	struct ghci_display_image *last;

	/*
	 * A mailbox present replaces the newest present still waiting: the
	 * replaced one counts as adopted once its replacement is.
	 */
	display = &device->display;
	if (!fifo && display->queue_count != 0U) {
		last = &display->queue[display->queue_count - 1U];
		ghci_scan_drop(device, last->memory);
		*last = *image;
		queue_kick(device);
		return 0;
	}

	/* A FIFO present waits for room in the queue. */
	if (display->queue_count >= GHCI_DISPLAY_QUEUE_DEPTH)
		return EAGAIN;

	/* The present waits behind the others, and goes to the ring when the plane's slot is free. */
	display->queue[display->queue_count] = *image;
	display->queue_count++;
	queue_kick(device);

	/* Succeeded: the present is recorded. */
	return 0;
}

/*
 * Shows a cursor surface: flips it now when the controller is lit and
 * plane 1 has no flip placed, after the next lighting or record otherwise.
 */
void
ghci_scan_cursor(
	struct ghci_device *device,
	int surface)
{
	/* The surface waits for its flip, which goes now when it can. */
	device->display.cursor_pending = surface;
	cursor_kick(device);
}

/*
 * Marks the lease ended and has the controller go dark: the presents
 * waiting are dropped, the cursor hidden and forgotten, and the dark
 * packet placed (by the host when the ring does not run).
 */
void
ghci_scan_lease_end(
	struct ghci_device *device)
{
	struct ghci_display *display;
	unsigned index;

	/* The lease is gone; no claim succeeds until the end is finished. */
	display = &device->display;
	display->owner = NULL;
	display->lease = 0U;
	display->ending = 1;
	display->uncertain = 0;

	/* The presents that waited are not shown. */
	for (index = 0U; index < display->queue_count; index++)
		ghci_scan_drop(device, display->queue[index].memory);
	display->queue_count = 0U;

	/* The cursor is hidden at once and its image forgotten. */
	ghci_write(device, display->registers + GHCI_DC_CURSOR_CTRL, 0U);
	display->cursor_image = 0;
	display->cursor_pending = -1;

	/* The controller goes dark, which cancels every pending flip. */
	display->dark_placed = 0;
	dark(device);
}

/*
 * Reports whether an ended lease has let go of everything: the dark
 * packet ran and every placed flip's record came.
 */
int
ghci_scan_lease_done(
	struct ghci_device *device)
{
	struct ghci_display *display;
	int usable;
	int done;

	/* A lease that did not end has nothing to finish. */
	display = &device->display;
	if (!display->ending)
		return 1;

	/* The dark packet has not been placed yet: the service thread places it. */
	if (display->dark_placed < 0)
		return 0;

	/* A ring that no longer runs answers nothing: its flips are forgotten. */
	usable = ghci_scan_ring_usable(device);
	if (!usable) {
		dark_by_host(device);
		flips_forget(device);
	}

	/* The cancelled records of the flips placed. */
	records_take(device);
	if (display->flips[0].placed)
		return 0;
	if (display->flips[1].placed)
		return 0;

	/* The dark packet, whose fence passes once the controller is idle. */
	if (display->dark_placed > 0) {
		done = ghci_cp_done(device, display->dark_ticket.fence, display->dark_ticket.generation);
		if (!done)
			return 0;
	}

	/* The controller no longer scans the image it showed: its memory goes. */
	ghci_scan_drop(device, display->shown.memory);
	image_clear(&display->shown);
	display->cursor_shown = -1;
	display->ending = 0;
	display->dark_placed = 0;

	/* Succeeded: the display may be leased again. */
	return 1;
}

/*
 * Lets go of held memory later, once the display's lock is let go: the
 * device's lock, which ghci_memory_unhold needs, comes before the
 * display's.
 */
void
ghci_scan_drop(
	struct ghci_device *device,
	struct ghci_allocation *memory)
{
	struct ghci_display *display;

	/* A driver's frame is not held. */
	if (memory == NULL)
		return;

	/* The list is long enough for every hold the display has at once. */
	display = &device->display;
	if (display->drop_count >= GHCI_DISPLAY_DROPS) {
		kern_logf("ghci: %s: display holds more memory than it can let go of\n", device->name);
		return;
	}

	/* The hold goes at the next flush. */
	display->drops[display->drop_count] = memory;
	display->drop_count++;
}

/*
 * Lets go of the memory dropped meanwhile.
 */
void
ghci_scan_flush(
	struct ghci_device *device)
{
	struct ghci_display *display;
	struct ghci_allocation *drops[GHCI_DISPLAY_DROPS];
	unsigned count;
	unsigned index;

	/* The device's lock, then the display's to take the list. */
	display = &device->display;
	mutex_lock(&device->lock);
	mutex_lock(&display->lock);

	count = display->drop_count;
	for (index = 0U; index < count; index++)
		drops[index] = display->drops[index];
	display->drop_count = 0U;

	mutex_unlock(&display->lock);

	/* Each hold goes; memory its owner gave back waits for the newest fence. */
	for (index = 0U; index < count; index++)
		ghci_memory_unhold(device, drops[index]);

	mutex_unlock(&device->lock);

	/* The deferred memory goes back from the service thread. */
	if (count != 0U)
		ghci_service_wake(device, GHCI_WORK_RECLAIM);
}

/*
 * Reports the wake-up sequence of the flip waiters, read before a wait
 * looks at the records so no interrupt between is missed.
 */
uint64_t
ghci_scan_mark(
	struct ghci_device *device)
{
	unsigned long irq;
	uint64_t mark;

	/* The sequence, read with the interrupt's lock. */
	irq = spin_lock_irqsave(&device->irq_lock);

	mark = waitq_sequence(&device->display.flip_wake);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Reports the sequence. */
	return mark;
}

/*
 * Sleeps until a flip's interrupt, a short poll or the deadline.
 */
void
ghci_scan_sleep(
	struct ghci_device *device,
	uint64_t mark,
	uint64_t deadline)
{
	uint64_t wake;
	unsigned long irq;

	/* The next look comes after a short poll at the latest. */
	wake = clock_ticks() + kern_ms_to_ticks(FLIP_POLL_MS);
	if (wake > deadline)
		wake = deadline;

	/* Sleeps unless an interrupt came since the mark. */
	irq = spin_lock_irqsave(&device->irq_lock);

	(void)waitq_sleep(&device->display.flip_wake, &device->irq_lock, mark, wake, 0U);

	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/*
 * Reads the controller's mode table into the display.  Returns 0, or
 * ENOTSUP when the controller names no mode the driver can use.
 */
static int
modes_read(
	struct ghci_device *device)
{
	struct ghci_display *display;
	struct ghci_display_mode *mode;
	uint32_t registers;
	uint32_t count;
	uint32_t word;
	uint32_t flags;
	uint64_t clock_khz;
	uint64_t total;
	uint32_t index;

	display = &device->display;
	registers = display->registers;

	/* The number of modes the controller names, at most what the display keeps. */
	count = GHCI_DC_CAPS_MODES(display->controller_caps);
	if (count > GHCI_DISPLAY_MODES_MAX)
		count = GHCI_DISPLAY_MODES_MAX;

	/* Reads each mode through MODE_INDEX; a mode without a size or a clock is skipped. */
	display->mode_count = 0U;
	for (index = 0U; index < count; index++) {
		mode = &display->modes[display->mode_count];
		ghci_write(device, registers + GHCI_DC_MODE_INDEX, index);

		/* The horizontal timing: active pixels and sync, then the porches. */
		word = ghci_read(device, registers + GHCI_DC_MODE_H0);
		mode->width = MODE_LOW(word);
		mode->h_sync = MODE_HIGH(word);
		word = ghci_read(device, registers + GHCI_DC_MODE_H1);
		mode->h_front = MODE_LOW(word);
		mode->h_back = MODE_HIGH(word);

		/* The vertical timing, in lines. */
		word = ghci_read(device, registers + GHCI_DC_MODE_V0);
		mode->height = MODE_LOW(word);
		mode->v_sync = MODE_HIGH(word);
		word = ghci_read(device, registers + GHCI_DC_MODE_V1);
		mode->v_front = MODE_LOW(word);
		mode->v_back = MODE_HIGH(word);

		/* The sync polarities (SYNC_POL's two bits) and the clock's PCLK_SEL value. */
		flags = ghci_read(device, registers + GHCI_DC_MODE_FLAGS);
		mode->sync_polarity = flags & 3U;
		mode->clock_select = GHCI_DC_MODE_PCLK_SEL(flags);

		/* A mode without a size or a clock cannot be shown. */
		clock_khz = ghci_read(device, registers + GHCI_DC_MODE_CLOCK);
		if (mode->width == 0U ||
		    mode->height == 0U ||
		    clock_khz == 0U)
			continue;

		/* The pixel clocks of one whole frame, the blanking included. */
		total = (uint64_t)(mode->width + mode->h_front + mode->h_sync + mode->h_back) *
		    (mode->height + mode->v_front + mode->v_sync + mode->v_back);

		/* The refresh the clock gives over the frame's totals, in millihertz, rounded. */
		mode->refresh_millihz = (uint32_t)((clock_khz * 1000000U + total / 2U) / total);
		display->mode_count++;
	}

	/* A controller without a usable mode has no display. */
	if (display->mode_count == 0U)
		return ENOTSUP;

	/* Succeeded: the modes are known. */
	return 0;
}

/* Empties an image record: no present, no memory, no frame. */
static void
image_clear(
	struct ghci_display_image *image)
{
	kern_memset(image, 0, sizeof(*image));
	image->copy_frame = -1;
}

/* Reports CTRL's word for a format with ENABLE off: the format and the output (HDMI when there is one). */
static uint32_t
control_word(
	struct ghci_device *device,
	uint32_t format)
{
	uint32_t control;

	/* The format, and HDMI when the controller drives a TMDS link or a transmitter. */
	control = GHCI_DC_CTRL_FORMAT(format);
	if ((device->display.controller_caps & (DC_CAPS_TMDS | DC_CAPS_TRANSMITTER)) != 0U)
		control |= GHCI_DC_CTRL_OUTPUT_HDMI;

	/* Reports the word. */
	return control;
}

/* Reports the GPU address of a plane's record. */
static uint32_t
record_address(
	struct ghci_device *device,
	unsigned plane)
{
	return device->fence_page->gpu_address + RECORD_OFFSET + RECORD_BYTES * plane;
}

/* Reports the processor's view of a plane's record, read as one 64-bit word. */
static volatile uint64_t *
record_slot(
	struct ghci_device *device,
	unsigned plane)
{
	return (volatile uint64_t *)(device->fence_page->view + RECORD_OFFSET + RECORD_BYTES * plane);
}

/* Takes the next flip value: 1 to 2^31 - 1, never 0, so a cleared record names no flip. */
static uint32_t
value_take(
	struct ghci_display *display)
{
	uint32_t value;

	/* The value, and the next one after it with the wrap past 31 bits. */
	value = display->next_value;
	display->next_value++;
	if (display->next_value > FLIP_VALUE_MAX)
		display->next_value = 1U;

	/* Reports the value. */
	return value;
}

/* Reads the record page for each placed flip and takes the flips whose records came. */
static void
records_take(
	struct ghci_device *device)
{
	struct ghci_display *display;
	struct ghci_display_flip *flip;
	uint64_t word;
	uint32_t value;
	unsigned plane;

	/* Records exist only once the ring and its fence page do. */
	display = &device->display;
	if (device->fence_page == NULL)
		return;

	/* Each plane's record, read in one access so its two words are not torn. */
	for (plane = 0U; plane < 2U; plane++) {
		flip = &display->flips[plane];
		if (!flip->placed)
			continue;

		/* The record names the flip by its value, with bit 31 saying it was cancelled. */
		word = *record_slot(device, plane);
		value = (uint32_t)word;
		if ((value & ~GHCI_FLIP_CANCELLED) != flip->value)
			continue;

		taken(device, plane, (value & GHCI_FLIP_CANCELLED) != 0U);
	}
}

/*
 * Takes a plane's flip: shown, or cancelled.
 *
 * A shown image of plane 0 replaces the one shown before, whose memory
 * goes, and its present (with every lower one) is adopted.  A cancelled
 * image is let go of without being shown.  A cursor surface shown
 * becomes the controller's; a cancelled one is flipped again once the
 * controller is lit.
 */
static void
taken(
	struct ghci_device *device,
	unsigned plane,
	int cancelled)
{
	struct ghci_display *display;
	struct ghci_display_flip *flip;

	display = &device->display;
	flip = &display->flips[plane];
	flip->placed = 0;

	/* The cursor's plane. */
	if (plane == 1U) {
		if (cancelled) {
			/* Not read: the surface is flipped again while the lease keeps its image. */
			if (display->cursor_image && display->cursor_pending < 0)
				display->cursor_pending = flip->surface;
		} else {
			/* The controller has read the surface and shows it from now. */
			display->cursor_shown = flip->surface;
		}
		return;
	}

	/* A cancelled image is not shown; its memory goes. */
	if (cancelled) {
		ghci_scan_drop(device, flip->image.memory);
		return;
	}

	/* The image is on the screen; the one before is no longer scanned. */
	ghci_scan_drop(device, display->shown.memory);
	display->shown = flip->image;
	display->shown_time_ns = ghci_scan_now_ns();

	/* The present is adopted, with every lower one (the ones it replaced included). */
	if (flip->image.sequence > display->adopted)
		display->adopted = flip->image.sequence;
}

/* Places the oldest present waiting, when plane 0 has no flip placed: a flip, or a new lighting. */
static void
queue_kick(
	struct ghci_device *device)
{
	struct ghci_display *display;
	struct ghci_display_image *head;
	int relight;
	int usable;
	int error;

	/* Nothing waits, the plane's slot is in use, the lease is ending, or the ring does not run. */
	display = &device->display;
	usable = ghci_scan_ring_usable(device);
	if (display->queue_count == 0U)
		return;
	if (display->flips[0].placed)
		return;
	if (display->ending)
		return;
	if (!usable)
		return;

	/* An image of another mode, format or stride than the one lit lights the controller again. */
	head = &display->queue[0];
	relight = 0;
	if (!display->enabled)
		relight = 1;
	if (head->mode != display->mode ||
	    head->format != display->format ||
	    head->stride != display->stride)
		relight = 1;

	/* The run that lights the controller on the image, or one flip. */
	if (relight) {
		error = light(device, head);
	} else {
		error = flip_primary(device, head);
	}
	if (error != 0) {
		kern_logf("ghci: %s: present not placed (%d)\n", device->name, error);
		return;
	}

	/* The present has left the queue: its flip is placed. */
	queue_pop(display);
}

/* Puts an image back at the head of the queue, for a flip a reset lost. */
static void
queue_push_front(
	struct ghci_display *display,
	const struct ghci_display_image *image)
{
	unsigned index;

	/* The queue has room for one more than its depth: the flip the slot had. */
	if (display->queue_count >= GHCI_DISPLAY_QUEUE)
		return;

	/* The others move back by one. */
	for (index = display->queue_count; index > 0U; index--)
		display->queue[index] = display->queue[index - 1U];
	display->queue[0] = *image;
	display->queue_count++;
}

/* Takes the oldest present out of the queue. */
static void
queue_pop(
	struct ghci_display *display)
{
	unsigned index;

	/* The others move forward by one. */
	for (index = 1U; index < display->queue_count; index++)
		display->queue[index - 1U] = display->queue[index];
	display->queue_count--;
}

/*
 * Places the run that lights the controller on an image: CTRL off (the
 * processor waits until the controller is idle), the mode's timing, the
 * stride and size, the flip (taken at once while ENABLE is 0), CTRL with
 * ENABLE, and the cursor's flip when one waits; then the driver's fence.
 */
static int
light(
	struct ghci_device *device,
	const struct ghci_display_image *image)
{
	struct ghci_display *display;
	const struct ghci_display_mode *timing;
	struct ghci_ticket ticket;
	uint32_t packets[LIGHT_DWORDS];
	uint32_t pairs[2U * LIGHT_PAIRS];
	uint32_t control;
	uint32_t value;
	uint32_t cursor_value;
	uint32_t dwords;
	uint32_t written;
	int cursor;
	int error;

	display = &device->display;
	timing = &display->modes[image->mode];
	control = control_word(device, image->format);

	/* CTRL with ENABLE off, waiting for the controller's idle. */
	pairs[0] = GHCI_DC_CTRL;
	pairs[1] = control;
	error = ghci_packet_disp_set(GHCI_TAG_DRIVER, 1, pairs, 1U, &packets[0], &written);
	if (error != 0)
		return error;
	dwords = written;

	/* The timing, the sync polarity, the pixel clock and the image's rows and size. */
	pairs[0] = GHCI_DC_H_ACTIVE;
	pairs[1] = timing->width;
	pairs[2] = GHCI_DC_H_FP;
	pairs[3] = timing->h_front;
	pairs[4] = GHCI_DC_H_SYNC;
	pairs[5] = timing->h_sync;
	pairs[6] = GHCI_DC_H_BP;
	pairs[7] = timing->h_back;
	pairs[8] = GHCI_DC_V_ACTIVE;
	pairs[9] = timing->height;
	pairs[10] = GHCI_DC_V_FP;
	pairs[11] = timing->v_front;
	pairs[12] = GHCI_DC_V_SYNC;
	pairs[13] = timing->v_sync;
	pairs[14] = GHCI_DC_V_BP;
	pairs[15] = timing->v_back;
	pairs[16] = GHCI_DC_SYNC_POL;
	pairs[17] = timing->sync_polarity;
	pairs[18] = GHCI_DC_PCLK_SEL;
	pairs[19] = timing->clock_select;
	pairs[20] = GHCI_DC_FB_STRIDE;
	pairs[21] = image->stride;
	pairs[22] = GHCI_DC_FB_SIZE;
	pairs[23] = GHCI_XY(timing->width, timing->height);
	error = ghci_packet_disp_set(GHCI_TAG_DRIVER, 0, pairs, LIGHT_PAIRS, &packets[dwords], &written);
	if (error != 0)
		return error;
	dwords += written;

	/* The image's flip, taken at once while ENABLE is off; its record clears first. */
	value = value_take(display);
	*record_slot(device, 0U) = 0U;
	ghci_packet_flip(GHCI_TAG_DRIVER, 0, 0U, image->base, record_address(device, 0U), value, &packets[dwords]);
	dwords += GHCI_FLIP_PACKET_DWORDS;

	/* CTRL with ENABLE: the first frame starts at the next vsync. */
	pairs[0] = GHCI_DC_CTRL;
	pairs[1] = control | GHCI_DC_CTRL_ENABLE;
	error = ghci_packet_disp_set(GHCI_TAG_DRIVER, 0, pairs, 1U, &packets[dwords], &written);
	if (error != 0)
		return error;
	dwords += written;

	/* The cursor's surface that waited for the lighting, when plane 1 has no flip placed. */
	cursor = 0;
	cursor_value = 0U;
	if (display->cursor_pending >= 0 && !display->flips[1].placed) {
		cursor = 1;
		cursor_value = value_take(display);
		*record_slot(device, 1U) = 0U;
		ghci_packet_flip(GHCI_TAG_DRIVER, 0, 1U, display->surfaces[display->cursor_pending]->gpu_address, record_address(device, 1U), cursor_value, &packets[dwords]);
		dwords += GHCI_FLIP_PACKET_DWORDS;
	}

	/* The run, with the driver's fence after it. */
	error = ghci_cp_submit(device, NULL, packets, dwords, &ticket);
	if (error != 0)
		return error;

	/* The controller is lit on the image's mode, format and stride once the run is parsed. */
	display->enabled = 1;
	display->mode = image->mode;
	display->format = image->format;
	display->stride = image->stride;
	display->flips[0].placed = 1;
	display->flips[0].value = value;
	display->flips[0].light = 1;
	display->flips[0].image = *image;

	/* The cursor's flip is placed too. */
	if (cursor) {
		display->flips[1].placed = 1;
		display->flips[1].value = cursor_value;
		display->flips[1].light = 0;
		display->flips[1].surface = display->cursor_pending;
		display->cursor_pending = -1;
	}

	/* Succeeded: the run is in the ring. */
	return 0;
}

/* Places one flip of plane 0 to an image of the mode lit, without a fence. */
static int
flip_primary(
	struct ghci_device *device,
	const struct ghci_display_image *image)
{
	struct ghci_display *display;
	uint32_t packet[GHCI_FLIP_PACKET_DWORDS];
	uint32_t value;
	int error;

	/* The flip, whose record clears first. */
	display = &device->display;
	value = value_take(display);
	*record_slot(device, 0U) = 0U;
	ghci_packet_flip(GHCI_TAG_DRIVER, 0, 0U, image->base, record_address(device, 0U), value, packet);
	error = ghci_cp_submit_unfenced(device, packet, GHCI_FLIP_PACKET_DWORDS);
	if (error != 0)
		return error;

	/* The plane's slot is the image's until its record comes. */
	display->flips[0].placed = 1;
	display->flips[0].value = value;
	display->flips[0].light = 0;
	display->flips[0].image = *image;

	/* Succeeded: the flip is in the ring. */
	return 0;
}

/* Places one flip of plane 1 to a cursor surface, without a fence. */
static int
flip_cursor(
	struct ghci_device *device,
	int surface)
{
	struct ghci_display *display;
	uint32_t packet[GHCI_FLIP_PACKET_DWORDS];
	uint32_t value;
	int error;

	/* The flip, whose record clears first. */
	display = &device->display;
	value = value_take(display);
	*record_slot(device, 1U) = 0U;
	ghci_packet_flip(GHCI_TAG_DRIVER, 0, 1U, display->surfaces[surface]->gpu_address, record_address(device, 1U), value, packet);
	error = ghci_cp_submit_unfenced(device, packet, GHCI_FLIP_PACKET_DWORDS);
	if (error != 0)
		return error;

	/* The cursor's slot is the surface's until its record comes. */
	display->flips[1].placed = 1;
	display->flips[1].value = value;
	display->flips[1].light = 0;
	display->flips[1].surface = surface;

	/* Succeeded: the flip is in the ring. */
	return 0;
}

/* Places the cursor surface that waits, when the controller is lit and plane 1 has no flip placed. */
static void
cursor_kick(
	struct ghci_device *device)
{
	struct ghci_display *display;
	int usable;
	int error;

	/* Nothing waits, the controller is dark or not yet lit, the slot is in use, or the ring does not run. */
	display = &device->display;
	usable = ghci_scan_ring_usable(device);
	if (display->cursor_pending < 0)
		return;
	if (!display->enabled)
		return;
	if (display->flips[1].placed)
		return;
	if (display->ending)
		return;
	if (!usable)
		return;

	/* The surface's flip. */
	error = flip_cursor(device, display->cursor_pending);
	if (error != 0) {
		kern_logf("ghci: %s: cursor flip not placed (%d)\n", device->name, error);
		return;
	}

	/* The surface no longer waits. */
	display->cursor_pending = -1;
}

/*
 * Turns the controller dark through the ring: CTRL off, the processor
 * waiting for the controller's idle, then the driver's fence (by the host
 * when the ring does not run).
 */
static void
dark(
	struct ghci_device *device)
{
	struct ghci_display *display;
	uint32_t packets[3U];
	uint32_t pairs[2];
	uint32_t written;
	int usable;
	int error;

	/* A ring that does not run leaves the controller to the host. */
	display = &device->display;
	usable = ghci_scan_ring_usable(device);
	if (!usable) {
		dark_by_host(device);
		flips_forget(device);
		display->dark_placed = 0;
		return;
	}

	/* CTRL with ENABLE off and the wait for the idle; every pending flip is taken as cancelled. */
	pairs[0] = GHCI_DC_CTRL;
	pairs[1] = control_word(device, display->format);
	error = ghci_packet_disp_set(GHCI_TAG_DRIVER, 1, pairs, 1U, packets, &written);
	if (error == 0)
		error = ghci_cp_submit(device, NULL, packets, written, &display->dark_ticket);
	if (error != 0) {
		kern_logf("ghci: %s: display not turned off by the ring (%d)\n", device->name, error);
		dark_by_host(device);
		flips_forget(device);
		display->dark_placed = 0;
		return;
	}

	/* The controller is dark once the packet runs; its fence says when. */
	display->enabled = 0;
	display->dark_placed = 1;
}

/*
 * Turns the controller dark by the host, which may write it only while
 * the command processor does not run, and waits until it reads no memory.
 */
static void
dark_by_host(
	struct ghci_device *device)
{
	struct ghci_display *display;
	int error;

	/* ENABLE off; the controller then finishes its reads. */
	display = &device->display;
	ghci_write(device, display->registers + GHCI_DC_CTRL, control_word(device, display->format));
	error = ghci_poll(device, display->registers + GHCI_DC_STATUS, GHCI_DC_ST_IDLE, GHCI_DC_ST_IDLE, DC_IDLE_MS);
	if (error != 0)
		kern_logf("ghci: %s: display controller not idle\n", device->name);
	display->enabled = 0;
}

/* Forgets the flips placed whose records will not come: their images are let go of, not shown. */
static void
flips_forget(
	struct ghci_device *device)
{
	struct ghci_display *display;

	/* Plane 0's image goes; the cursor's surface is free again. */
	display = &device->display;
	if (display->flips[0].placed)
		ghci_scan_drop(device, display->flips[0].image.memory);
	display->flips[0].placed = 0;
	display->flips[1].placed = 0;
}

/*
 * Counts the scan-out errors the controller reports and clears them: too
 * many in a second mask the error interrupt for the rest of it, a read
 * error in round after round turns the controller dark (the image it
 * scans is bad), and a refused register write is the driver's own error.
 */
static void
errors_serve(
	struct ghci_device *device)
{
	struct ghci_display *display;
	uint32_t status;
	uint32_t errors;
	uint64_t period;
	uint64_t now;

	/* A new second unmasks the interrupt and starts the count again. */
	display = &device->display;
	now = clock_ticks();
	period = kern_ms_to_ticks(ERROR_PERIOD_MS);
	if (now - display->error_tick >= period) {
		display->error_tick = now;
		display->error_count = 0U;
		if (display->error_masked)
			irq_mask_set(device, 0);
	}

	/* The sticky errors, cleared once read. */
	status = ghci_read(device, display->registers + GHCI_DC_STATUS);
	errors = status & DC_ERRORS;
	if (errors == 0U) {
		display->read_errors = 0U;
		return;
	}
	ghci_write(device, display->registers + GHCI_DC_STATUS, errors);

	/* Too many errors this second: the interrupt stays off until the next. */
	display->error_count++;
	if (display->error_count > ERROR_LIMIT && !display->error_masked)
		irq_mask_set(device, 1);

	/* An underflow means the memory did not keep up; it is counted only. */
	if ((errors & GHCI_DC_ST_UNDERFLOW) != 0U)
		display->underflows++;

	/* A register write the controller refused is a fault of the driver. */
	if ((errors & GHCI_DC_ST_BAD_WRITE) != 0U)
		kern_logf("ghci: %s: display controller refused a register write\n", device->name);

	/* A record the GPU could not write. */
	if ((errors & GHCI_DC_ST_RECORD_ERROR) != 0U)
		kern_logf("ghci: %s: a flip's record was not written\n", device->name);

	/* A read error round after round: the image is bad, and the controller goes dark until the next present. */
	if ((errors & GHCI_DC_ST_READ_ERROR) == 0U) {
		display->read_errors = 0U;
		return;
	}
	display->read_errors++;
	if (display->read_errors < READ_ERROR_RUNS)
		return;
	if (!display->enabled || display->flips[0].placed || display->ending)
		return;
	kern_logf("ghci: %s: display reads fail; turning it off\n", device->name);
	display->read_errors = 0U;
	dark(device);
}

/* Masks or unmasks the scan-out error interrupt, under the interrupt's lock. */
static void
irq_mask_set(
	struct ghci_device *device,
	int masked)
{
	unsigned long irq;

	/* The mask the handler restores, and the register, changed together. */
	irq = spin_lock_irqsave(&device->irq_lock);

	if (masked) {
		device->irq_mask &= ~GHCI_IRQ_DISP_ERROR;
	} else if (device->irq_mask != 0U) {
		device->irq_mask |= GHCI_IRQ_DISP_ERROR;
	}
	ghci_write(device, GHCI_IRQ_ENABLE, device->irq_mask);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* The display remembers which way it is. */
	device->display.error_masked = masked;
}

/*
 * Watches the connection: the transmitter's hot-plug line is read once a
 * second and told to the controller (CONNECTED), whose change raises
 * HOTPLUG; a change of CONNECTED changes the output's connection, and a
 * new connection is a new generation with the transmitter set up again.
 * Returns 1 when the output changed.
 */
static int
connection_serve(
	struct ghci_device *device)
{
	struct ghci_display *display;
	uint64_t period;
	uint64_t now;
	uint32_t connected_word;
	int sensed;
	int connected;
	int error;

	/* The transmitter's line, once a second, when the controller does not sense the sink itself. */
	display = &device->display;
	now = clock_ticks();
	period = kern_ms_to_ticks(HOTPLUG_PERIOD_MS);
	if ((display->controller_caps & DC_CAPS_TRANSMITTER) != 0U &&
	    (display->controller_caps & GHCI_DC_CAPS_SENSE) == 0U &&
	    now - display->hotplug_tick >= period) {
		display->hotplug_tick = now;
		error = ghci_adv7511_sensed(device, display->registers, &sensed);
		connected_word = ghci_read(device, display->registers + GHCI_DC_CONNECTED) & 1U;
		if (error == 0 && connected_word != (uint32_t)sensed)
			ghci_write(device, display->registers + GHCI_DC_CONNECTED, (uint32_t)sensed);
	}

	/* What the controller says now. */
	connected_word = ghci_read(device, display->registers + GHCI_DC_CONNECTED);
	connected = 0;
	if ((connected_word & 1U) != 0U)
		connected = 1;
	if (connected == display->connected)
		return 0;

	/* A new connection is a new generation of the output, whose transmitter is set up again. */
	display->connected = connected;
	if (connected) {
		display->generation++;
		if ((display->controller_caps & DC_CAPS_TRANSMITTER) != 0U) {
			error = ghci_adv7511_start(device, display->registers);
			if (error != 0)
				kern_logf("ghci: %s: HDMI transmitter not set up again (%d)\n", device->name, error);
		}
	}

	/* The output's topology changed. */
	kern_logf("ghci: %s: display %s\n", device->name, connected ? "connected" : "disconnected");
	topology_move(device);

	/* Succeeded: the output changed. */
	return 1;
}

/* Moves the output's event sequence, which the events operation reads under the interrupt's lock. */
static void
topology_move(
	struct ghci_device *device)
{
	unsigned long irq;

	/* The sequence only grows; a reader sees the change at its next look. */
	irq = spin_lock_irqsave(&device->irq_lock);

	device->display.topology++;

	spin_unlock_irqrestore(&device->irq_lock, irq);
}
