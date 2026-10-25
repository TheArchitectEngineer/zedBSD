/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display of a GHCI device: the display controller of the DISP2
 * capability, a unit of the GPU, as one output with one plane.  The
 * controller's mode table names the modes.
 *
 * The controller is not lit at the start: it starts scanning only when
 * the holder of the display's lease presents its first frame.  A
 * presentation copies the session's storage, by the blitter, into the
 * one of the driver's two frames that is not on the screen, flips to it
 * at the next vertical sync and returns once the flip was taken, so the
 * storage is never borrowed past the call and a wait answers at once.
 * The frame's size picks the mode among the controller's modes.  The end
 * of the lease turns the controller off and gives the frames back.
 *
 * The display's events are its topology sequence, which stays at one:
 * the controller reports no hot plug.
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

#include "ghci.h"
#include "hcr.h"

/* The display's identity: one output, of one generation, whose topology never changes. */
#define DISPLAY_ID			1U
#define DISPLAY_GENERATION		1U
#define DISPLAY_TOPOLOGY		1U

/* The controller's capability bits: the external transmitter and the TMDS output. */
#define DC_CAPS_TRANSMITTER		0x4U
#define DC_CAPS_TMDS			0x2U

/* The version of the DISP2 capability the driver knows. */
#define DISP2_VERSION			1U

/* How long the detach waits for the controller to be idle, and a presentation for its flip. */
#define DC_IDLE_MS			100U
#define FLIP_WAIT_MS			100U

/* The frame's pixels: 32 bits. */
#define PIXEL_BYTES			4U

/* The MODE registers' fields: a count in the low half, another in the high half. */
#define MODE_LOW(word)			((word) & 0xffffU)
#define MODE_HIGH(word)			((word) >> 16)

static int display_query(void *private_data, void *opaque, struct gpu_display_info *request);
static int display_mode(void *private_data, void *opaque, struct gpu_display_mode *request);
static int display_claim(void *private_data, void *opaque, struct gpu_display_claim *request);
static int display_release(void *private_data, void *opaque, const struct gpu_display_release *request);
static int display_present(void *private_data, void *opaque, void *object, struct gpu_display_present *request);
static int display_wait(void *private_data, void *opaque, struct gpu_display_wait *request);
static int display_events(void *private_data, void *opaque, uint64_t *sequence);
static int modes_read(struct ghci_device *device);
static unsigned largest_mode(const struct ghci_display *display);
static int find_mode(const struct ghci_display *display, uint32_t width, uint32_t height, unsigned *mode);
static int frames_take(struct ghci_device *device);
static void frames_give(struct ghci_device *device);
static void controller_off(struct ghci_device *device);
static void controller_on(struct ghci_device *device, unsigned mode, uint32_t format, uint32_t frame);
static int flip_wait(struct ghci_device *device, uint32_t frame);
static void lease_end(struct ghci_device *device);

/*
 * The display operations of a GHCI device with a display controller:
 * query, mode, claim, release, present, wait and the topology sequence.
 * Presentations wait under the open's admission; there is no cursor, no
 * overlay and no display control.  Immutable.
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
	NULL,
	0U,
	NULL,
	NULL
};

/*
 * Finds and starts the display controller.
 */
int
ghci_display_attach(
	struct ghci_device *device)
{
	struct ghci_display *display;
	uint32_t registers;
	uint32_t version;
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
	controller_off(device);

	/* Whether a sink is connected, as the controller was last told (1 when nobody told it). */
	display->connected = 0;
	if ((ghci_read(device, registers + GHCI_DC_CONNECTED) & 1U) != 0U)
		display->connected = 1;

	/* An external HDMI transmitter is set up now; it shows nothing until the controller scans. */
	if ((display->controller_caps & DC_CAPS_TRANSMITTER) != 0U) {
		error = ghci_adv7511_start(device, registers);
		if (error != 0)
			kern_logf("ghci: %s: HDMI transmitter not set up (%d)\n", device->name, error);
	}

	/* The display exists, without a lease. */
	display->owner = NULL;
	display->next_lease = 1U;
	display->mode = largest_mode(display);
	display->format = GHCI_DC_FMT_BGRA8;
	display->present = 1;

	/* Succeeded: the display is ready for a lease. */
	return 0;
}

/*
 * Turns the display controller off for the detach.
 */
void
ghci_display_detach(
	struct ghci_device *device)
{
	/* A device without a display has nothing to turn off. */
	if (!device->display.present)
		return;

	/* The controller dark and quiet, the lease and the frames gone. */
	mutex_lock(&device->lock);

	lease_end(device);

	mutex_unlock(&device->lock);
}

/*
 * Ends a closing session's lease; the device's lock is held.
 */
void
ghci_display_session_close(
	struct ghci_device *device,
	struct ghci_session *session)
{
	/* Only the lease's holder has anything on the display. */
	if (!device->display.present)
		return;
	if (device->display.owner != session)
		return;

	/* The lease ends with its session. */
	lease_end(device);
}

/*
 * Serves the display controller's interrupt.
 *
 * A DISP2 controller has no interrupt status of its own: its events are
 * bits of the GPU's IRQ_STATUS, which the command processor's handler
 * reads.  Reports that nothing of the controller's was pending.
 */
int
ghci_display_interrupt(
	struct ghci_device *device)
{
	(void)device;

	/* Succeeded: the controller has no status of its own to serve. */
	return 0;
}

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

	/* The output, its plane and its formats, under the device's lock. */
	mutex_lock(&device->lock);

	current = &display->modes[display->mode];
	largest = &display->modes[largest_mode(display)];
	request->display_id = DISPLAY_ID;
	request->generation = DISPLAY_GENERATION;
	request->flags = GPU_DISPLAY_FIFO;
	if (display->connected)
		request->flags |= GPU_DISPLAY_CONNECTED;
	if (display->enabled)
		request->flags |= GPU_DISPLAY_ACTIVE;
	request->plane_count = 1U;
	request->formats = GPU_DISPLAY_FORMAT_BGRA8888 | GPU_DISPLAY_FORMAT_RGBA8888;
	request->max_frame_bytes = (uint64_t)largest->width * largest->height * PIXEL_BYTES;
	request->current_width = current->width;
	request->current_height = current->height;
	request->preferred_width = largest->width;
	request->preferred_height = largest->height;
	request->max_width = largest->width;
	request->max_height = largest->height;
	request->refresh_millihz = current->refresh_millihz;

	mutex_unlock(&device->lock);

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
	unsigned index;
	int error;

	(void)opaque;

	/* Only the one output of this generation exists. */
	device = private_data;
	if (request->display_id != DISPLAY_ID)
		return ENOENT;
	if (request->generation != DISPLAY_GENERATION)
		return ESTALE;

	/* Enumeration: the modes in order. */
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

/* Gives the display's lease to a session; EBUSY while another holds it. */
static int
display_claim(
	void *private_data,
	void *opaque,
	struct gpu_display_claim *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	int error;

	/* Only plane 0 of the one output of this generation exists. */
	device = private_data;
	display = &device->display;
	if (request->display_id != DISPLAY_ID)
		return ENOENT;
	if (request->generation != DISPLAY_GENERATION)
		return ESTALE;
	if (request->plane_index != 0U)
		return EINVAL;

	/* Takes the lease when it is free, with the frames the presentations copy into. */
	mutex_lock(&device->lock);

	if (display->owner != NULL) {
		mutex_unlock(&device->lock);
		return EBUSY;
	}

	error = frames_take(device);
	if (error != 0) {
		mutex_unlock(&device->lock);
		return error;
	}

	/* The session holds a new lease with no frame yet; the controller stays dark until the first. */
	display->owner = opaque;
	display->lease = display->next_lease;
	display->next_lease++;
	display->sequence = 0U;
	request->lease = display->lease;

	mutex_unlock(&device->lock);

	/* Succeeded: the session holds the display. */
	return 0;
}

/* Ends the lease of its holder; EINVAL for another session's lease. */
static int
display_release(
	void *private_data,
	void *opaque,
	const struct gpu_display_release *request)
{
	struct ghci_device *device;
	struct ghci_display *display;

	/* Only the holder's own lease ends. */
	device = private_data;
	display = &device->display;
	mutex_lock(&device->lock);

	if (display->owner != opaque || display->lease != request->lease) {
		mutex_unlock(&device->lock);
		return EINVAL;
	}

	lease_end(device);

	mutex_unlock(&device->lock);

	/* Succeeded: the controller is dark and the lease free. */
	return 0;
}

/*
 * Shows a frame of the lease holder: copies the storage into the frame
 * not on the screen and flips to it at the next vertical sync.  Returns
 * 0 with the frame's sequence, EINVAL for another session's lease or a
 * frame the controller cannot show, or the copy's or the flip's error.
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
	struct ghci_allocation *frame;
	struct ghci_blit blit;
	uint64_t end;
	uint32_t format;
	unsigned mode;
	unsigned back;
	int error;

	device = private_data;
	display = &device->display;
	storage = object;

	/* Only whole frames of storage: blobs are not scanned. */
	if ((request->flags & GPU_DISPLAY_PRESENT_BLOB) != 0U)
		return EINVAL;

	/* The two packed formats the controller scans. */
	if (request->format == GPU_DISPLAY_FORMAT_BGRA8888) {
		format = GHCI_DC_FMT_BGRA8;
	} else if (request->format == GPU_DISPLAY_FORMAT_RGBA8888) {
		format = GHCI_DC_FMT_RGBA8;
	} else {
		return EINVAL;
	}

	/* The frame's size is one of the modes. */
	error = find_mode(display, request->width, request->height, &mode);
	if (error != 0)
		return error;

	/* Rows of whole pixels, each at least a row wide, inside the storage. */
	if ((request->stride % PIXEL_BYTES) != 0U)
		return EINVAL;
	if ((request->offset % PIXEL_BYTES) != 0U)
		return EINVAL;
	if (request->stride < request->width * PIXEL_BYTES)
		return EINVAL;

	/* The end of the last row lies inside what the storage was asked for. */
	end = request->offset + (uint64_t)request->stride * (request->height - 1U) + (uint64_t)request->width * PIXEL_BYTES;
	if (end > storage->requested)
		return EINVAL;

	/* Everything below is the lease holder's, under the device's lock. */
	mutex_lock(&device->lock);

	if (display->owner != opaque || display->lease != request->lease) {
		mutex_unlock(&device->lock);
		return EINVAL;
	}

	/* The copy into the frame that is not on the screen, by the blitter. */
	back = display->shown ^ 1U;
	if (!display->enabled)
		back = 0U;
	frame = display->frames[back];
	kern_memset(&blit, 0, sizeof(blit));
	blit.op = GHCI_BLT_COPY;
	blit.src_base = storage->gpu_address + (uint32_t)request->offset;
	blit.src_stride = request->stride;
	blit.dst_base = frame->gpu_address;
	blit.dst_stride = request->width * PIXEL_BYTES;
	blit.width = request->width;
	blit.height = request->height;
	error = ghci_blit_run(device, &blit);
	if (error != 0) {
		mutex_unlock(&device->lock);
		return error;
	}

	/* The first frame, a new mode or a new format lights the controller on the frame; later ones flip. */
	if (!display->enabled || display->mode != mode || display->format != format) {
		controller_off(device);
		controller_on(device, mode, format, frame->gpu_address);
		display->mode = mode;
		display->format = format;
	} else {
		ghci_write(device, display->registers + GHCI_DC_FB_NEXT, frame->gpu_address);
		error = flip_wait(device, frame->gpu_address);
	}

	/* A flip that was not taken leaves the frame on the screen as it was. */
	if (error != 0) {
		mutex_unlock(&device->lock);
		return error;
	}

	/* The frame is on the screen. */
	display->shown = back;
	display->sequence++;
	display->shown_time_ns = clock_ticks() * (KERN_NSEC_PER_SEC / KERN_CLOCK_HZ);
	request->sequence = display->sequence;

	mutex_unlock(&device->lock);

	/* Succeeded: the frame is shown and the storage no longer borrowed. */
	return 0;
}

/*
 * Reports the newest frame shown under the lease: a presentation returns
 * once shown, so this answers at once.  EBUSY for another session's
 * lease, EINVAL for a sequence not presented yet, EAGAIN before the first.
 */
static int
display_wait(
	void *private_data,
	void *opaque,
	struct gpu_display_wait *request)
{
	struct ghci_device *device;
	struct ghci_display *display;
	int error;

	/* Reads the lease's newest frame under the device's lock. */
	device = private_data;
	display = &device->display;
	mutex_lock(&device->lock);

	error = 0;
	if (display->owner != opaque || display->lease != request->lease) {
		error = EBUSY;
	} else if (request->sequence > display->sequence) {
		error = EINVAL;
	} else if (display->sequence == 0U) {
		error = EAGAIN;
	} else {
		request->completed_sequence = display->sequence;
		request->present_time_ns = display->shown_time_ns;
		request->generation = DISPLAY_GENERATION;
	}

	mutex_unlock(&device->lock);

	/* Reports a wait the lease does not allow. */
	if (error != 0)
		return error;

	/* Succeeded: the sequence has been shown. */
	return 0;
}

/* Reports the topology sequence: one, which no hot plug moves.  No lock, no hardware access. */
static int
display_events(
	void *private_data,
	void *opaque,
	uint64_t *sequence)
{
	(void)private_data;
	(void)opaque;

	/* Succeeded: the first and only topology. */
	*sequence = DISPLAY_TOPOLOGY;
	return 0;
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

/* Reports the index of the largest mode of the controller, by its pixels. */
static unsigned
largest_mode(
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

/* Takes the two frames of the largest mode; the device's lock is held. */
static int
frames_take(
	struct ghci_device *device)
{
	const struct ghci_display_mode *largest;
	uint64_t bytes;
	unsigned index;
	int error;

	/* Frames that are already there are kept. */
	if (device->display.frames[0] != NULL)
		return 0;

	/* Each frame holds the largest mode, in memory the controller scans. */
	largest = &device->display.modes[largest_mode(&device->display)];
	bytes = (uint64_t)largest->width * largest->height * PIXEL_BYTES;
	for (index = 0U; index < 2U; index++) {
		error = ghci_memory_alloc(device, NULL, GHCI_USE_MAPPABLE, bytes, &device->display.frames[index]);
		if (error != 0) {
			frames_give(device);
			return error;
		}
	}

	/* Succeeded: the frames are the display's. */
	return 0;
}

/* Gives the frames back once the controller no longer reads them; the device's lock is held. */
static void
frames_give(
	struct ghci_device *device)
{
	unsigned index;

	/* Each frame that was taken. */
	for (index = 0U; index < 2U; index++) {
		if (device->display.frames[index] != NULL) {
			ghci_memory_free(device, device->display.frames[index]);
			device->display.frames[index] = NULL;
		}
	}
}

/* Turns the controller off and waits until it reads no memory. */
static void
controller_off(
	struct ghci_device *device)
{
	uint32_t registers;
	int error;

	/* ENABLE off; the controller then finishes its reads. */
	registers = device->display.registers;
	ghci_write(device, registers + GHCI_DC_CTRL, 0U);
	error = ghci_poll(device, registers + GHCI_DC_STATUS, GHCI_DC_ST_IDLE, GHCI_DC_ST_IDLE, DC_IDLE_MS);
	if (error != 0)
		kern_logf("ghci: %s: display controller not idle\n", device->name);
	device->display.enabled = 0;
}

/* Programs a mode, the format and the frame of a controller that is off, and turns it on. */
static void
controller_on(
	struct ghci_device *device,
	unsigned mode,
	uint32_t format,
	uint32_t frame)
{
	const struct ghci_display_mode *timing;
	uint32_t registers;
	uint32_t control;

	timing = &device->display.modes[mode];
	registers = device->display.registers;

	/* The timing, the clock and the frame's size and rows, changed only while ENABLE is off. */
	ghci_write(device, registers + GHCI_DC_H_ACTIVE, timing->width);
	ghci_write(device, registers + GHCI_DC_H_FP, timing->h_front);
	ghci_write(device, registers + GHCI_DC_H_SYNC, timing->h_sync);
	ghci_write(device, registers + GHCI_DC_H_BP, timing->h_back);
	ghci_write(device, registers + GHCI_DC_V_ACTIVE, timing->height);
	ghci_write(device, registers + GHCI_DC_V_FP, timing->v_front);
	ghci_write(device, registers + GHCI_DC_V_SYNC, timing->v_sync);
	ghci_write(device, registers + GHCI_DC_V_BP, timing->v_back);
	ghci_write(device, registers + GHCI_DC_SYNC_POL, timing->sync_polarity);
	ghci_write(device, registers + GHCI_DC_PCLK_SEL, timing->clock_select);
	ghci_write(device, registers + GHCI_DC_FB_STRIDE, timing->width * PIXEL_BYTES);
	ghci_write(device, registers + GHCI_DC_FB_SIZE, GHCI_XY(timing->width, timing->height));

	/* With ENABLE off, FB_NEXT is taken at once. */
	ghci_write(device, registers + GHCI_DC_FB_NEXT, frame);

	/* The format and the output (HDMI when the controller drives one), then ENABLE. */
	control = GHCI_DC_CTRL_FORMAT(format);
	if ((device->display.controller_caps & (DC_CAPS_TMDS | DC_CAPS_TRANSMITTER)) != 0U)
		control |= GHCI_DC_CTRL_OUTPUT_HDMI;
	ghci_write(device, registers + GHCI_DC_CTRL, control);
	ghci_write(device, registers + GHCI_DC_CTRL, control | GHCI_DC_CTRL_ENABLE);
	device->display.enabled = 1;
}

/* Waits until the controller scans a frame (the flip was taken at a sync).  Returns 0 or ETIMEDOUT. */
static int
flip_wait(
	struct ghci_device *device,
	uint32_t frame)
{
	uint32_t scanned;
	unsigned waited;

	/* FB_BASE says which frame is scanned; it is looked at every millisecond, a bounded time. */
	for (waited = 0U; waited <= FLIP_WAIT_MS; waited++) {
		scanned = ghci_read(device, device->display.registers + GHCI_DC_FB_BASE);
		if (scanned == frame)
			return 0;

		ghci_sleep_ms(1U);
	}

	/* The controller did not take the flip. */
	kern_logf("ghci: %s: flip not taken\n", device->name);
	return ETIMEDOUT;
}

/* Ends the lease: the controller goes dark and the frames go back; the device's lock is held. */
static void
lease_end(
	struct ghci_device *device)
{
	/* Dark first, so the frames are no longer read. */
	if (device->display.enabled)
		controller_off(device);

	/* The frames, the lease and its sequence. */
	frames_give(device);
	device->display.owner = NULL;
	device->display.lease = 0U;
	device->display.sequence = 0U;
}
