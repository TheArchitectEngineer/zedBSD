/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display operations of a GHCI device: one output, the display
 * controller of the DISP2 capability, with its primary plane and, when
 * the controller has one, a cursor plane (display-scan.c drives the
 * controller through the ring).
 *
 * A present records a frame and returns; the core then waits for its
 * adoption (DRV_GPU_DISPLAY_UNGATED), so presents, waits and the cursor
 * do not queue behind each other on an open.  A frame is shown one of two
 * ways, chosen once for a swapchain by libvulkan from the constraints:
 *
 *  - shared (a blob the session presents): the controller scans the
 *    session's memory where it is, which the display holds until the
 *    controller no longer scans it;
 *  - copied (ordinary storage): the blitter copies the storage into the
 *    one of the driver's two frames that is not on the screen, and the
 *    storage is not borrowed past the call.
 *
 * The frame's size picks the mode among the controller's modes; a frame
 * of another mode, format or stride than the one lit lights the
 * controller again.  The end of the lease turns the controller dark and
 * lets its images go.
 *
 * The display's events are its topology sequence, which a change of the
 * connection moves; a new connection is a new generation of the output.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu-display.h>
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-display.h>
#include <uapi/gpu-scanout.h>

#include "display-scan.h"
#include "ghci.h"
#include "hcr.h"

/* How long a release waits for the controller to go dark and let its images go. */
#define RELEASE_WAIT_MS			1000U

/* The longest a display wait waits, in nanoseconds: one second. */
#define WAIT_MAX_NS			1000000000ULL

/* The cursor's image: 64 x 64 pixels of 4 bytes, rows 256 bytes apart. */
#define CURSOR_STRIDE			(GHCI_DC_CURSOR_SIZE * GHCI_DISPLAY_PIXEL_BYTES)
#define CURSOR_BYTES			(CURSOR_STRIDE * GHCI_DC_CURSOR_SIZE)

static int display_query(void *private_data, void *opaque, struct gpu_display_info *request);
static int display_mode(void *private_data, void *opaque, struct gpu_display_mode *request);
static int display_claim(void *private_data, void *opaque, struct gpu_display_claim *request);
static int display_release(void *private_data, void *opaque, const struct gpu_display_release *request);
static int display_present(void *private_data, void *opaque, void *object, struct gpu_display_present *request);
static int display_wait(void *private_data, void *opaque, struct gpu_display_wait *request);
static int display_events(void *private_data, void *opaque, uint64_t *sequence);
static int display_cursor_set(void *private_data, void *opaque, void *object, const struct gpu_display_cursor_image *request);
static int display_cursor_move(void *private_data, void *opaque, const struct gpu_display_cursor_move *request);
static int display_cursor_hide(void *private_data, void *opaque, const struct gpu_display_cursor_hide *request);
static int display_adoption_wait(void *private_data, void *opaque, uint64_t lease, uint64_t *sequence, uint64_t *adopted);
static int scanout_query_device(void *private_data, void *opaque, struct gpu_device_info *request);
static int scanout_constraints(void *private_data, void *opaque, struct gpu_scanout_constraints *request);

/*
 * The display operations of a GHCI device whose controller has no cursor
 * plane: query, mode, claim, release, the ungated present and wait with
 * the adoption wait, and the topology sequence.  There is no overlay and
 * no display control.  Immutable.
 */
const struct drv_gpu_display_ops ghci_display_ops = {
	display_query,
	display_mode,
	display_claim,
	display_release,
	display_present,
	display_wait,
	display_events,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	display_adoption_wait,
	DRV_GPU_DISPLAY_UNGATED,
	NULL,
	NULL
};

/*
 * The display operations of a GHCI device whose controller has a cursor
 * plane: the same, and the cursor's three operations.  Immutable.
 */
const struct drv_gpu_display_ops ghci_display_cursor_ops = {
	display_query,
	display_mode,
	display_claim,
	display_release,
	display_present,
	display_wait,
	display_events,
	NULL,
	NULL,
	display_cursor_set,
	display_cursor_move,
	display_cursor_hide,
	display_adoption_wait,
	DRV_GPU_DISPLAY_UNGATED,
	NULL,
	NULL
};

/*
 * The scan-out query of a GHCI device with a display: its roles and the
 * constraints of the shared and copied routes.  The renderer and the
 * display are one device, so an import is the ordinary share import.
 * Immutable.
 */
const struct drv_gpu_scanout_ops ghci_scanout_ops = {
	scanout_query_device,
	scanout_constraints,
	NULL
};

static int lease_check(struct ghci_display *display, void *opaque, uint64_t lease, uint64_t generation);
static int find_mode(const struct ghci_display *display, uint32_t width, uint32_t height, unsigned *mode);
static int buffers_take(struct ghci_device *device);
static int present_shared(struct ghci_device *device, void *opaque, struct ghci_allocation *storage, unsigned mode, uint32_t format, struct gpu_display_present *request);
static int present_copied(struct ghci_device *device, void *opaque, struct ghci_allocation *storage, unsigned mode, uint32_t format, struct gpu_display_present *request);
static void memory_let_go(struct ghci_device *device, struct ghci_allocation *memory);
static int cursor_surface_free(const struct ghci_display *display);
static void cursor_convert(const uint8_t *staging, uint8_t *surface, uint32_t width, uint32_t height, int swap);

/* Describes the one output. */
static int
display_query(
	void *private_data,
	void *opaque,
	struct gpu_display_info *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	const struct ghci_display_mode *current;
	const struct ghci_display_mode *largest;

	(void)opaque;

	/* One output. */
	device = private_data;
	display = &device->display;
	request->count = 1U;

	/* Only the count was asked for. */
	if (request->index == GPU_DISPLAY_COUNT_ONLY)
		return 0;

	/* Only output 0 exists. */
	if (request->index != 0U)
		return EINVAL;

	/* The output, its plane and its formats, under the display's lock. */
	mutex_lock(&display->lock);

	current = &display->modes[display->mode];
	largest = &display->modes[ghci_scan_largest_mode(display)];
	request->display_id = GHCI_DISPLAY_ID;
	request->generation = display->generation;
	request->flags = GPU_DISPLAY_FIFO | GPU_DISPLAY_BLOB;
	if (display->connected)
		request->flags |= GPU_DISPLAY_CONNECTED;
	if (display->enabled)
		request->flags |= GPU_DISPLAY_ACTIVE;
	if ((display->controller_caps & GHCI_DC_CAPS_CURSOR) != 0U)
		request->flags |= GPU_DISPLAY_HAS_CURSOR;
	request->plane_count = 1U;
	request->formats = GPU_DISPLAY_FORMAT_BGRA8888 | GPU_DISPLAY_FORMAT_RGBA8888;
	request->max_frame_bytes = (uint64_t)ghci_scan_stride(device, largest->width) * largest->height;
	request->current_width = current->width;
	request->current_height = current->height;
	request->preferred_width = largest->width;
	request->preferred_height = largest->height;
	request->max_width = largest->width;
	request->max_height = largest->height;
	request->refresh_millihz = current->refresh_millihz;

	mutex_unlock(&display->lock);

	/* The monitor's size is not read (no EDID through the controller). */
	request->physical_width_mm = 0U;
	request->physical_height_mm = 0U;
	kern_memset(request->name, 0, sizeof(request->name));
	kern_memcpy(request->name, "ghci display", sizeof("ghci display"));

	/* Succeeded: the output is described. */
	return 0;
}

/* Enumerates or checks the controller's modes. */
static int
display_mode(
	void *private_data,
	void *opaque,
	struct gpu_display_mode *request)
{
	struct ghci_device *device;
	const struct ghci_display_mode *mode;
	uint64_t generation;
	unsigned index;
	int error;

	(void)opaque;

	/* The output's generation now. */
	device = private_data;
	mutex_lock(&device->display.lock);

	generation = device->display.generation;

	mutex_unlock(&device->display.lock);

	/* Only the one output of this generation exists. */
	if (request->display_id != GHCI_DISPLAY_ID)
		return ENOENT;
	if (request->generation != generation)
		return ESTALE;

	/* Enumeration: the modes in order (the table never changes after the attach). */
	if (request->operation == GPU_DISPLAY_MODE_ENUMERATE) {
		request->count = device->display.mode_count;
		if (request->index == GPU_DISPLAY_COUNT_ONLY)
			return 0;
		if (request->index >= device->display.mode_count)
			return EINVAL;

		mode = &device->display.modes[request->index];
		request->width = mode->width;
		request->height = mode->height;
		request->refresh_millihz = mode->refresh_millihz;
		return 0;
	}

	/* Refuses an operation that does not exist. */
	if (request->operation != GPU_DISPLAY_MODE_VALIDATE)
		return EINVAL;

	/* Validation: one of the modes, at its refresh (0 asks for it). */
	error = find_mode(&device->display, request->width, request->height, &index);
	if (error != 0)
		return error;

	/* The refresh, when one is asked for, must be the mode's. */
	mode = &device->display.modes[index];
	if (request->refresh_millihz != 0U && request->refresh_millihz != mode->refresh_millihz)
		return EINVAL;

	/* Succeeded: the mode exists, at its refresh. */
	request->refresh_millihz = mode->refresh_millihz;
	return 0;
}

/* Gives the display's lease to a session; EBUSY while another holds it or a lease is still ending. */
static int
display_claim(
	void *private_data,
	void *opaque,
	struct gpu_display_claim *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	int error;

	/* Only plane 0 of the one output exists. */
	device = private_data;
	display = &device->display;
	if (request->display_id != GHCI_DISPLAY_ID)
		return ENOENT;
	if (request->plane_index != 0U)
		return EINVAL;

	/* The frames a copied present fills and the cursor's surfaces, under the device's lock. */
	mutex_lock(&device->lock);

	error = buffers_take(device);
	if (error != 0) {
		mutex_unlock(&device->lock);
		return error;
	}

	/* Takes the lease when it is free and the last one has finished ending. */
	mutex_lock(&display->lock);

	error = 0;
	if (request->generation != display->generation) {
		error = ESTALE;
	} else if (display->owner != NULL || display->ending) {
		error = EBUSY;
	} else {
		/* The session holds a new lease with no frame yet; the controller stays as it is until the first. */
		display->owner = opaque;
		display->lease = display->next_lease;
		display->next_lease++;
		display->sequence = 0U;
		display->adopted = 0U;
		display->uncertain = 0;
		display->cursor_image = 0;
		request->lease = display->lease;

		/* The idle frame is not lit again for this connection once a lease has held it. */
		display->idle_generation = display->generation;
	}

	mutex_unlock(&display->lock);
	mutex_unlock(&device->lock);

	/* Reports a lease that could not be taken. */
	if (error != 0)
		return error;

	/* Succeeded: the session holds the display. */
	return 0;
}

/*
 * Ends the lease of its holder: the controller goes dark and lets its
 * images go before the call returns (or, past a bounded time, soon
 * after).  EINVAL for another session's lease.
 */
static int
display_release(
	void *private_data,
	void *opaque,
	const struct gpu_display_release *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	uint64_t deadline;
	uint64_t mark;
	uint64_t now;
	int done;

	/* Only the holder's own lease ends. */
	device = private_data;
	display = &device->display;
	mutex_lock(&display->lock);

	if (display->owner != opaque || display->lease != request->lease) {
		mutex_unlock(&display->lock);
		return EINVAL;
	}

	ghci_scan_lease_end(device);

	mutex_unlock(&display->lock);

	/* Waits a bounded time for the dark packet and the cancelled records. */
	deadline = clock_ticks() + kern_ms_to_ticks(RELEASE_WAIT_MS);
	for (;;) {
		mark = ghci_scan_mark(device);
		mutex_lock(&display->lock);

		done = ghci_scan_lease_done(device);

		mutex_unlock(&display->lock);

		/* Ends once everything was let go, or at the deadline (the service thread finishes it). */
		ghci_scan_flush(device);
		now = clock_ticks();
		if (done)
			break;
		if (now >= deadline)
			break;

		ghci_scan_sleep(device, mark, deadline);
	}

	/* A controller slow to go dark is finished by the service thread. */
	if (!done) {
		kern_logf("ghci: %s: display lease ended before the controller went dark\n", device->name);
		ghci_service_wake(device, GHCI_WORK_DISPLAY);
	}

	/* Succeeded: the lease is free (once the controller is dark, for a new claim). */
	return 0;
}

/*
 * Records a frame of the lease holder and returns its number; the core
 * then waits for its adoption.  Returns 0, EINVAL for a frame the
 * controller cannot show, EBUSY for another session's lease or an
 * uncertain output, ESTALE for an old generation, ENXIO when
 * disconnected, or the copy's error.
 */
static int
display_present(
	void *private_data,
	void *opaque,
	void *object,
	struct gpu_display_present *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	struct ghci_allocation *storage;
	uint64_t end;
	uint32_t format;
	unsigned mode;
	int error;

	device = private_data;
	display = &device->display;
	storage = object;

	/* The two packed formats the controller scans. */
	if (request->format == GPU_PIXEL_BGRA8888) {
		format = GHCI_DC_FMT_BGRA8;
	} else if (request->format == GPU_PIXEL_RGBA8888) {
		format = GHCI_DC_FMT_RGBA8;
	} else {
		return EINVAL;
	}

	/* The frame's size is one of the modes (the table never changes after the attach). */
	error = find_mode(display, request->width, request->height, &mode);
	if (error != 0)
		return error;

	/* Rows of whole pixels, each at least a row wide, inside the storage. */
	if ((request->stride % GHCI_DISPLAY_PIXEL_BYTES) != 0U)
		return EINVAL;
	if ((request->offset % GHCI_DISPLAY_PIXEL_BYTES) != 0U)
		return EINVAL;
	if (request->stride < request->width * GHCI_DISPLAY_PIXEL_BYTES)
		return EINVAL;

	/* The end of the last row lies inside what the storage was asked for. */
	end = request->offset + (uint64_t)request->stride * (request->height - 1U) + (uint64_t)request->width * GHCI_DISPLAY_PIXEL_BYTES;
	if (end > storage->requested)
		return EINVAL;

	/* A blob is scanned where it is; ordinary storage is copied. */
	if ((request->flags & GPU_DISPLAY_PRESENT_BLOB) != 0U) {
		error = present_shared(device, opaque, storage, mode, format, request);
	} else {
		error = present_copied(device, opaque, storage, mode, format, request);
	}
	if (error != 0)
		return error;

	/* Succeeded: the frame is recorded with its number in the request. */
	return 0;
}

/*
 * Reports the newest frame adopted under the lease, waiting up to the
 * request's time (at most a second) for its sequence.  EBUSY for another
 * session's lease, EINVAL for a sequence not presented yet, EAGAIN when
 * the time is zero and the sequence is not adopted, ETIMEDOUT, ENXIO when
 * disconnected.
 */
static int
display_wait(
	void *private_data,
	void *opaque,
	struct gpu_display_wait *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	uint64_t timeout_ns;
	uint64_t deadline;
	uint64_t mark;
	uint64_t now;
	int error;

	/* The wait ends at most a second from now. */
	device = private_data;
	display = &device->display;
	timeout_ns = request->timeout_ns;
	if (timeout_ns > WAIT_MAX_NS)
		timeout_ns = WAIT_MAX_NS;
	deadline = clock_ticks() + kern_ms_to_ticks((timeout_ns + 999999U) / 1000000U);

	/* Looks at the records, sleeping between looks, until the sequence is adopted. */
	for (;;) {
		mark = ghci_scan_mark(device);
		mutex_lock(&display->lock);

		ghci_scan_update(device);
		error = EAGAIN;
		if (display->owner != opaque || display->lease != request->lease) {
			error = EBUSY;
		} else if (!display->connected) {
			error = ENXIO;
		} else if (request->sequence > display->sequence) {
			error = EINVAL;
		} else if (display->adopted != 0U && display->adopted >= request->sequence) {
			error = 0;
			request->completed_sequence = display->adopted;
			request->present_time_ns = display->shown_time_ns;
			request->generation = display->generation;
		}

		mutex_unlock(&display->lock);

		/* Ends at an answer, at once for a zero time, or at the deadline. */
		ghci_scan_flush(device);
		now = clock_ticks();
		if (error != EAGAIN)
			break;
		if (timeout_ns == 0U)
			break;
		if (now >= deadline) {
			error = ETIMEDOUT;
			break;
		}

		ghci_scan_sleep(device, mark, deadline);
	}

	/* Reports a wait the lease does not allow, or one that ran out. */
	if (error != 0)
		return error;

	/* Succeeded: the sequence has been adopted. */
	return 0;
}

/* Reports the topology sequence, under the interrupt's lock only.  No hardware access. */
static int
display_events(
	void *private_data,
	void *opaque,
	uint64_t *sequence)
{
	struct ghci_device *device;
	unsigned long irq;

	(void)opaque;

	/* The sequence a change of the connection moves. */
	device = private_data;
	irq = spin_lock_irqsave(&device->irq_lock);

	*sequence = device->display.topology;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: the sequence is read. */
	return 0;
}

/*
 * Sets the cursor's image from session storage: the blitter copies the
 * rows into the driver's staging, and the processor puts them in a free
 * cursor surface (red and blue swapped for RGBA8888, the rest of the
 * 64 x 64 image cleared), which is then flipped.  EBUSY when both
 * surfaces are in use (the call may succeed a refresh later).
 */
static int
display_cursor_set(
	void *private_data,
	void *opaque,
	void *object,
	const struct gpu_display_cursor_image *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	struct ghci_allocation *storage;
	struct ghci_blit blit;
	int surface;
	int swap;
	int error;

	device = private_data;
	display = &device->display;
	storage = object;

	/* A free surface of the lease's display, and the staging for the copy. */
	mutex_lock(&display->lock);

	error = lease_check(display, opaque, request->lease, request->generation);
	surface = -1;
	if (error == 0) {
		surface = cursor_surface_free(display);
		if (surface < 0 || display->cursor_copying)
			error = EBUSY;
	}
	if (error == 0)
		display->cursor_copying = 1;

	mutex_unlock(&display->lock);

	/* Reports a lease that does not allow the change, or no free surface. */
	if (error != 0)
		return error;

	/* The rows into the staging, by the blitter, outside the display's lock. */
	kern_memset(&blit, 0, sizeof(blit));
	blit.op = GHCI_BLT_COPY;
	blit.src_base = storage->gpu_address + (uint32_t)request->offset;
	blit.src_stride = request->stride;
	blit.dst_base = display->staging->gpu_address;
	blit.dst_stride = CURSOR_STRIDE;
	blit.width = request->width;
	blit.height = request->height;
	error = ghci_blit_run(device, &blit);

	/* The surface takes the rows and is flipped, while the lease is still the caller's. */
	mutex_lock(&display->lock);

	display->cursor_copying = 0;
	if (error == 0)
		error = lease_check(display, opaque, request->lease, request->generation);
	if (error == 0) {
		swap = 0;
		if (request->format == GPU_PIXEL_RGBA8888)
			swap = 1;
		cursor_convert(display->staging->view, display->surfaces[surface]->view, request->width, request->height, swap);
		display->cursor_image = 1;
		ghci_scan_cursor(device, surface);
	}

	mutex_unlock(&display->lock);

	/* Reports a copy that failed or a lease that ended meanwhile. */
	if (error != 0)
		return error;

	/* Succeeded: the image shows from the controller's next read of it. */
	return 0;
}

/* Shows the cursor at a place, from the next frame; EINVAL before an image was set. */
static int
display_cursor_move(
	void *private_data,
	void *opaque,
	const struct gpu_display_cursor_move *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	uint32_t position;
	int error;

	/* The cursor's registers are the host's in every state of the processor. */
	device = private_data;
	display = &device->display;
	mutex_lock(&display->lock);

	error = lease_check(display, opaque, request->lease, request->generation);
	if (error == 0 && !display->cursor_image)
		error = EINVAL;
	if (error == 0) {
		position = ((uint32_t)request->x & 0xffffU) | ((uint32_t)request->y << 16);
		ghci_write(device, display->registers + GHCI_DC_CURSOR_POS, position);
		ghci_write(device, display->registers + GHCI_DC_CURSOR_CTRL, 1U);
	}

	mutex_unlock(&display->lock);

	/* Reports a lease that does not allow the move. */
	if (error != 0)
		return error;

	/* Succeeded: the controller takes the place at the start of the next frame. */
	return 0;
}

/* Hides the cursor from the next frame; its image is kept. */
static int
display_cursor_hide(
	void *private_data,
	void *opaque,
	const struct gpu_display_cursor_hide *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	int error;

	/* The cursor's register is the host's in every state of the processor. */
	device = private_data;
	display = &device->display;
	mutex_lock(&display->lock);

	error = lease_check(display, opaque, request->lease, request->generation);
	if (error == 0)
		ghci_write(device, display->registers + GHCI_DC_CURSOR_CTRL, 0U);

	mutex_unlock(&display->lock);

	/* Reports a lease that does not allow the change. */
	if (error != 0)
		return error;

	/* Succeeded: the controller hides the cursor from the next frame. */
	return 0;
}

/*
 * Waits for the adoption of a recorded present (the ungated core's second
 * step), holding no pin and no lock while it sleeps.  sequence holds the
 * record's number, which is also the number the caller sees; adopted,
 * when given, receives the highest adopted number.  EBUSY when the lease
 * ended, EIO when the GPU no longer runs, ETIMEDOUT when the adoption did
 * not come (the lease's later presents then fail with EBUSY).
 */
static int
display_adoption_wait(
	void *private_data,
	void *opaque,
	uint64_t lease,
	uint64_t *sequence,
	uint64_t *adopted)
{
	struct ghci_device *device;
	struct ghci_display *display;
	uint64_t deadline;
	uint64_t mark;
	uint64_t now;
	int usable;
	int error;

	/* The adoption comes within a bounded time. */
	device = private_data;
	display = &device->display;
	deadline = clock_ticks() + kern_ms_to_ticks(GHCI_DISPLAY_ADOPTION_MS);

	/* Looks at the records, sleeping between looks, until the record is adopted. */
	for (;;) {
		mark = ghci_scan_mark(device);
		mutex_lock(&display->lock);

		ghci_scan_update(device);
		usable = ghci_scan_ring_usable(device);
		error = EAGAIN;
		if (display->owner != opaque || display->lease != lease) {
			error = EBUSY;
		} else if (display->adopted >= *sequence) {
			error = 0;
		} else if (display->uncertain) {
			error = ETIMEDOUT;
		} else if (!usable) {
			error = EIO;
		}
		if (adopted != NULL)
			*adopted = display->adopted;

		mutex_unlock(&display->lock);

		/* Ends at an answer, or at the deadline: the output is then uncertain. */
		ghci_scan_flush(device);
		now = clock_ticks();
		if (error != EAGAIN)
			break;
		if (now >= deadline) {
			mutex_lock(&display->lock);

			display->uncertain = 1;

			mutex_unlock(&display->lock);

			kern_logf("ghci: %s: present %llu not adopted in time\n", device->name, (unsigned long long)*sequence);
			error = ETIMEDOUT;
			break;
		}

		ghci_scan_sleep(device, mark, deadline);
	}

	/* Reports a lease that ended, a lost GPU or an adoption that did not come. */
	if (error != 0)
		return error;

	/* Succeeded: the record is shown, or replaced by one that is. */
	return 0;
}

/* Reports the device's roles: a renderer when it runs commands, and a display. */
static int
scanout_query_device(
	void *private_data,
	void *opaque,
	struct gpu_device_info *request)
{
	struct ghci_device *device;

	(void)opaque;

	/* The display always; the renderer with the command operation. */
	device = private_data;
	request->roles = GPU_DEVICE_DISPLAY;
	if ((device->ops.capabilities & GPU_CAP_COMMAND) != 0U)
		request->roles |= GPU_DEVICE_RENDER;

	/* Succeeded: the roles are known (no companion: the device renders for itself). */
	return 0;
}

/*
 * Reports the constraints of the output's generation: the shared and
 * copied routes, the two packed formats, rows and images on the memory
 * line, and no placement (every blob of a GPU with local memory is in
 * local memory, which the controller scans).
 */
static int
scanout_constraints(
	void *private_data,
	void *opaque,
	struct gpu_scanout_constraints *request)
{
	struct ghci_device *device;
	uint64_t generation;
	uint32_t line;

	(void)opaque;

	/* The output's generation now. */
	device = private_data;
	mutex_lock(&device->display.lock);

	generation = device->display.generation;

	mutex_unlock(&device->display.lock);

	/* Only the one output of this generation exists. */
	if (request->display_id != GHCI_DISPLAY_ID)
		return ENOENT;
	if (request->generation != generation)
		return ESTALE;

	/* The memory line the controller reads, a power of two. */
	line = ghci_scan_stride(device, 1U);

	/* Succeeded: the routes and their layout. */
	request->flags = GPU_SCANOUT_SHARED | GPU_SCANOUT_COPY;
	request->formats = GPU_DISPLAY_FORMAT_BGRA8888 | GPU_DISPLAY_FORMAT_RGBA8888;
	request->stride_alignment = line;
	request->offset_alignment = line;
	request->placement = 0U;
	request->max_dma_address = 0U;
	return 0;
}

/*
 * Checks that a request names this open's lease of the output's current
 * generation, connected and not uncertain.  Returns 0, EBUSY, ESTALE or
 * ENXIO; the display's lock is held.
 */
static int
lease_check(
	struct ghci_display *display,
	void *opaque,
	uint64_t lease,
	uint64_t generation)
{
	/* Another open's lease, none, or one whose adoption did not come. */
	if (display->owner != opaque)
		return EBUSY;
	if (display->lease != lease)
		return EBUSY;
	if (display->uncertain)
		return EBUSY;

	/* An old generation of the output, or one without a sink. */
	if (generation != display->generation)
		return ESTALE;
	if (!display->connected)
		return ENXIO;

	/* Succeeded: the lease may change the output. */
	return 0;
}

/* Finds the mode of a size among the controller's.  Returns 0 or EINVAL. */
static int
find_mode(
	const struct ghci_display *display,
	uint32_t width,
	uint32_t height,
	unsigned *mode)
{
	unsigned index;

	/* Looks at each mode the controller has. */
	for (index = 0U; index < display->mode_count; index++) {
		if (display->modes[index].width != width)
			continue;
		if (display->modes[index].height != height)
			continue;

		*mode = index;
		return 0;
	}

	/* No mode has the size. */
	return EINVAL;
}

/*
 * Takes the driver's buffers once: two frames of the largest mode that a
 * copied present fills, which only the GPU writes (local memory on a
 * discrete GPU, GHCI section 11.3), and, with a cursor plane, two cursor
 * surfaces and the staging of their copies, which the processor converts
 * (host memory, which the controller reads by DMA).
 * They stay until the detach.  The device's lock is held.
 */
static int
buffers_take(
	struct ghci_device *device)
{
	struct ghci_display *display;
	const struct ghci_display_mode *largest;
	uint64_t bytes;
	unsigned index;
	int error;

	/* Each frame holds the largest mode, with rows on the memory line. */
	display = &device->display;
	largest = &display->modes[ghci_scan_largest_mode(display)];
	bytes = (uint64_t)ghci_scan_stride(device, largest->width) * largest->height;
	for (index = 0U; index < 2U; index++) {
		if (display->frames[index] != NULL)
			continue;

		error = ghci_memory_alloc(device, NULL, GHCI_USE_DEVICE, bytes, &display->frames[index]);
		if (error != 0)
			return error;
	}

	/* A controller without a cursor plane needs no surface. */
	if ((display->controller_caps & GHCI_DC_CAPS_CURSOR) == 0U)
		return 0;

	/* The two cursor surfaces. */
	for (index = 0U; index < 2U; index++) {
		if (display->surfaces[index] != NULL)
			continue;

		error = ghci_memory_alloc(device, NULL, GHCI_USE_MAPPABLE, CURSOR_BYTES, &display->surfaces[index]);
		if (error != 0)
			return error;
	}

	/* The staging the blitter copies a session's rows into. */
	if (display->staging == NULL) {
		error = ghci_memory_alloc(device, NULL, GHCI_USE_MAPPABLE, CURSOR_BYTES, &display->staging);
		if (error != 0)
			return error;
	}

	/* Succeeded: the buffers are the display's. */
	return 0;
}

/*
 * Records a shared present: the blob's memory, held by the display, is
 * scanned where it is.  The image lies in memory the controller scans
 * (local memory on a GPU that has it), its first row and its stride on
 * the memory line.  A FIFO present waits for room in the queue.
 */
static int
present_shared(
	struct ghci_device *device,
	void *opaque,
	struct ghci_allocation *storage,
	unsigned mode,
	uint32_t format,
	struct gpu_display_present *request)
{
	struct ghci_display *display;
	struct ghci_display_image image;
	struct ghci_allocation *memory;
	uint32_t line;
	uint64_t deadline;
	uint64_t mark;
	uint64_t now;
	int usable;
	int fifo;
	int error;

	/* The memory the blob shows: its own, or the allocation a Vulkan memory blob shows. */
	display = &device->display;
	memory = storage;
	if (storage->backing != NULL)
		memory = storage->backing;

	/* The controller scans local memory only, on a GPU that has it. */
	if (device->local_size != 0U && memory->kind == GHCI_MEMORY_HOST)
		return EINVAL;

	/* The first row and the stride are on the memory line. */
	line = ghci_scan_stride(device, 1U);
	if (((storage->gpu_address + (uint32_t)request->offset) % line) != 0U)
		return EINVAL;
	if ((request->stride % line) != 0U)
		return EINVAL;

	/* The image the controller scans. */
	kern_memset(&image, 0, sizeof(image));
	image.base = storage->gpu_address + (uint32_t)request->offset;
	image.stride = request->stride;
	image.format = format;
	image.mode = mode;
	image.memory = memory;
	image.copy_frame = -1;
	fifo = 0;
	if ((request->flags & GPU_DISPLAY_PRESENT_FIFO) != 0U)
		fifo = 1;

	/* The display holds the memory until the controller no longer scans it. */
	mutex_lock(&device->lock);

	ghci_memory_hold(memory);

	mutex_unlock(&device->lock);

	/* Records the image, waiting for room in the queue when it is full. */
	deadline = clock_ticks() + kern_ms_to_ticks(GHCI_DISPLAY_ADOPTION_MS);
	for (;;) {
		mark = ghci_scan_mark(device);
		mutex_lock(&display->lock);

		ghci_scan_update(device);
		error = lease_check(display, opaque, request->lease, request->generation);
		usable = ghci_scan_ring_usable(device);
		if (error == 0 && !usable)
			error = EIO;
		if (error == 0 && display->copy_frame_busy >= 0)
			error = EAGAIN;
		if (error == 0) {
			image.sequence = display->sequence + 1U;
			error = ghci_scan_record(device, &image, fifo);
		}
		if (error == 0) {
			display->sequence = image.sequence;
			request->sequence = image.sequence;
		}

		mutex_unlock(&display->lock);

		/* Ends once recorded or refused; a full queue is waited for, a bounded time. */
		ghci_scan_flush(device);
		now = clock_ticks();
		if (error != EAGAIN)
			break;
		if (now >= deadline) {
			error = EBUSY;
			break;
		}

		ghci_scan_sleep(device, mark, deadline);
	}

	/* A present that was not recorded lets the memory go. */
	if (error != 0) {
		memory_let_go(device, memory);
		return error;
	}

	/* Succeeded: the image is recorded. */
	return 0;
}

/*
 * Records a copied present: once every present before it was placed and
 * plane 0 has no flip placed, the blitter copies the storage into the
 * driver's frame not on the screen, which is then recorded.  The storage
 * is not borrowed after the call.
 */
static int
present_copied(
	struct ghci_device *device,
	void *opaque,
	struct ghci_allocation *storage,
	unsigned mode,
	uint32_t format,
	struct gpu_display_present *request)
{
	struct ghci_display *display;
	struct ghci_display_image image;
	struct ghci_blit blit;
	uint64_t deadline;
	uint64_t mark;
	uint64_t now;
	int usable;
	int frame;
	int error;

	/* Waits until the frame not on the screen is free: nothing waits and no flip is placed. */
	display = &device->display;
	deadline = clock_ticks() + kern_ms_to_ticks(GHCI_DISPLAY_ADOPTION_MS);
	frame = -1;
	for (;;) {
		mark = ghci_scan_mark(device);
		mutex_lock(&display->lock);

		ghci_scan_update(device);
		error = lease_check(display, opaque, request->lease, request->generation);
		usable = ghci_scan_ring_usable(device);
		if (error == 0 && !usable)
			error = EIO;
		if (error == 0) {
			error = EAGAIN;
			if (display->queue_count == 0U &&
			    !display->flips[0].placed &&
			    display->copy_frame_busy < 0)
				error = 0;
		}
		if (error == 0) {
			/* The frame the controller does not scan; it is the copy's until recorded. */
			frame = 0;
			if (display->shown.copy_frame == 0)
				frame = 1;
			display->copy_frame_busy = frame;
		}

		mutex_unlock(&display->lock);

		/* Ends once a frame is the copy's or the present is refused, or at the deadline. */
		ghci_scan_flush(device);
		now = clock_ticks();
		if (error != EAGAIN)
			break;
		if (now >= deadline) {
			error = EBUSY;
			break;
		}

		ghci_scan_sleep(device, mark, deadline);
	}

	/* Reports a present that may not be recorded. */
	if (error != 0)
		return error;

	/* The copy into the frame, by the blitter, outside the display's lock. */
	kern_memset(&blit, 0, sizeof(blit));
	blit.op = GHCI_BLT_COPY;
	blit.src_base = storage->gpu_address + (uint32_t)request->offset;
	blit.src_stride = request->stride;
	blit.dst_base = display->frames[frame]->gpu_address;
	blit.dst_stride = ghci_scan_stride(device, request->width);
	blit.width = request->width;
	blit.height = request->height;
	error = ghci_blit_run(device, &blit);

	/* The frame is recorded while the lease is still the caller's. */
	mutex_lock(&display->lock);

	display->copy_frame_busy = -1;
	if (error == 0)
		error = lease_check(display, opaque, request->lease, request->generation);
	if (error == 0) {
		kern_memset(&image, 0, sizeof(image));
		image.sequence = display->sequence + 1U;
		image.base = display->frames[frame]->gpu_address;
		image.stride = blit.dst_stride;
		image.format = format;
		image.mode = mode;
		image.memory = NULL;
		image.copy_frame = frame;
		error = ghci_scan_record(device, &image, 1);
	}
	if (error == 0) {
		display->sequence = image.sequence;
		request->sequence = image.sequence;
	}

	mutex_unlock(&display->lock);

	/* Reports a copy that failed, or a lease that ended meanwhile. */
	ghci_scan_flush(device);
	if (error != 0)
		return error;

	/* Succeeded: the copied frame is recorded. */
	return 0;
}

/* Lets go of memory the display held for a present that was not recorded. */
static void
memory_let_go(
	struct ghci_device *device,
	struct ghci_allocation *memory)
{
	/* The hold goes under the device's lock; memory its owner gave back waits for the newest fence. */
	mutex_lock(&device->lock);

	ghci_memory_unhold(device, memory);

	mutex_unlock(&device->lock);

	/* The deferred memory goes back from the service thread. */
	ghci_service_wake(device, GHCI_WORK_RECLAIM);
}

/* Reports a cursor surface neither shown nor flipped, or -1 when both are in use. */
static int
cursor_surface_free(
	const struct ghci_display *display)
{
	int surface;

	/* The first surface the controller does not have and no flip names. */
	for (surface = 0; surface < 2; surface++) {
		if (surface == display->cursor_shown)
			continue;
		if (display->flips[1].placed && display->flips[1].surface == surface)
			continue;

		return surface;
	}

	/* Both are the controller's for now. */
	return -1;
}

/*
 * Puts a cursor image's rows from the staging into a surface: BGRA8
 * pixels (red and blue swapped from RGBA8 when swap is set), the part
 * past the image's width and height cleared.
 */
static void
cursor_convert(
	const uint8_t *staging,
	uint8_t *surface,
	uint32_t width,
	uint32_t height,
	int swap)
{
	const uint8_t *source;
	uint8_t *target;
	uint32_t row;
	uint32_t column;

	/* Each pixel of the 64 x 64 surface. */
	for (row = 0U; row < GHCI_DC_CURSOR_SIZE; row++) {
		for (column = 0U; column < GHCI_DC_CURSOR_SIZE; column++) {
			source = &staging[row * CURSOR_STRIDE + column * GHCI_DISPLAY_PIXEL_BYTES];
			target = &surface[row * CURSOR_STRIDE + column * GHCI_DISPLAY_PIXEL_BYTES];

			/* Outside the image: transparent black. */
			if (row >= height || column >= width) {
				target[0] = 0U;
				target[1] = 0U;
				target[2] = 0U;
				target[3] = 0U;
				continue;
			}

			/* The pixel, in the controller's BGRA order. */
			if (swap) {
				target[0] = source[2];
				target[1] = source[1];
				target[2] = source[0];
				target[3] = source[3];
			} else {
				target[0] = source[0];
				target[1] = source[1];
				target[2] = source[2];
				target[3] = source[3];
			}
		}
	}
}
