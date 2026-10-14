/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The takeover of the firmware's head (stage P8b, nvrtx.disp=1).
 *
 * After P8a found the head the firmware (GOP) lit, the driver makes the
 * display's core channel and the head's first window channel through the
 * resource manager, writes their context DMA objects and hash-table entries
 * into the display's instance memory, and sends the head the state it is
 * armed with: the same timing, the same output resource (SOR) and control,
 * so the output keeps running.  The window then shows a surface of the
 * driver's own in the VRAM, and a flip moves it to a second surface.
 *
 * The channels, the registration of their push buffers, the instance-memory
 * objects and the order of the methods are nouveau's (the GSP display
 * channels of r535/r570, the Turing core and window classes); the head's
 * values are read back from the armed state instead of being computed from
 * a mode, which is the takeover nouveau's readout of an inherited output
 * makes possible.  Nothing is shown anywhere but on the head the firmware
 * lit (the scanout rule of plan/ws037/nvrtx-design.md, section 2).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/* The control that registers a display channel's push buffer with the resource manager. */
#define SCANOUT_CTL_PUSHBUFFER		0x20800a58U

/* The handle of the display root the channels are made under (P8a's). */
#define SCANOUT_HANDLE_ROOT		0xc5700000U

/* A display channel's push buffer: one page of 1024 words, never wrapped. */
#define SCANOUT_PUSH_BYTES		0x1000U
#define SCANOUT_PUSH_WORDS		(SCANOUT_PUSH_BYTES / 4U)

/* Where the channels' put and get are: the core's user area, and each window's page after it. */
#define SCANOUT_REG_CORE_USER		0x680000U
#define SCANOUT_REG_WINDOW_USER		0x690000U
#define SCANOUT_WINDOW_USER_STRIDE	0x1000U
#define SCANOUT_USER_PUT_AT		0x0U
#define SCANOUT_USER_GET_AT		0x4U

/* The core's armed state: the core methods' values as last released, from this register on. */
#define SCANOUT_REG_CORE_ARMED		0x688000U

/* The notifiers: 4 KiB of VRAM just after the instance memory; the core's at 0, window w's at (1 + w) * 64 + 32. */
#define SCANOUT_SYNC_AFTER_INSTANCE	0x10000U
#define SCANOUT_SYNC_BYTES		0x1000U
#define SCANOUT_CORE_NOTIFIER_AT	0x0U
#define SCANOUT_WINDOW_SLOT_BYTES	0x40U
#define SCANOUT_WINDOW_NOTIFIER_AT	0x20U
#define SCANOUT_NOTIFIER_BYTES		0x10U

/* How long a notifier is waited for, and the polling step. */
#define SCANOUT_NOTIFIER_TIMEOUT_US	2000000U
#define SCANOUT_NOTIFIER_STEP_US	100U

/* How long each test picture stays up, so it can be seen on the monitor. */
#define SCANOUT_SHOW_US			10000000U
#define SCANOUT_SHOW_STEP_US		100000U

/* The surfaces: rows in 256-byte steps, each surface on its own 1 MiB step. */
#define SCANOUT_PITCH_ALIGN		256U
#define SCANOUT_SURFACE_ALIGN		0x100000ULL
#define SCANOUT_BAND_COUNT		8U

/* The output resource a head can be driven by: the SOR's index from P8a, at most 8. */
#define SCANOUT_SOR_MAX			8U

/* The output resource's control: the heads that own it are bits 7:0. */
#define SCANOUT_SOR_OWNER_MASK		0xffU

static int scanout_read_armed(struct nvrtx_gpu *gpu);
static int scanout_surfaces(struct nvrtx_gpu *gpu);
static int scanout_paint(struct nvrtx_gpu *gpu, const struct nvrtx_disp_surface *surface, int reversed);
static int scanout_channel(struct nvrtx_gpu *gpu, uint32_t class_number, uint32_t instance, struct nvrtx_buffer *buffer, struct nvrtx_push *push);
static int scanout_bind(struct nvrtx_gpu *gpu, uint32_t chid, uint32_t handle, uint64_t start, uint64_t limit);
static int scanout_kick(struct nvrtx_gpu *gpu, struct nvrtx_push *push, uint32_t user);
static int scanout_notifier_reset(struct nvrtx_gpu *gpu, uint32_t offset);
static int scanout_notifier_wait(struct nvrtx_gpu *gpu, uint32_t offset, uint32_t least, uint32_t user, const char *what);
static int scanout_take_head(struct nvrtx_gpu *gpu);
static int scanout_show(struct nvrtx_gpu *gpu);
static int scanout_flip(struct nvrtx_gpu *gpu);
static void scanout_hold(void);
static uint32_t scanout_window_user(uint32_t window);
static uint32_t scanout_window_notifier(uint32_t window);

/*
 * Runs stage P8b: takes the firmware's head over, shows a surface and flips.
 *
 * Without a head the firmware lit, nothing is done (the scanout rule).  A
 * failure is returned; the finish's stop frees the channels that were made.
 */
int
nvrtx_scanout_start(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	int error;

	display = &gpu->display;

	/* Shows nothing anywhere the firmware did not light. */
	nvrtx_stage_mark("P8b begin");
	if (display->gop_head_found == 0) {
		nvrtx_stage_mark("P8b: no head lit by the firmware; nothing taken over");
		return 0;
	}

	/* The head's first window is the one it shows; the hardware must have it. */
	display->window = display->gop_head * 2U;
	if ((display->window_mask & (1U << display->window)) == 0U) {
		nvrtx_stage_mark("P8b fail: window %u of head %u is absent", (unsigned)display->window, (unsigned)display->gop_head);
		return ENODEV;
	}

	/* Reads the state the head and its output are armed with. */
	error = scanout_read_armed(gpu);
	if (error != 0)
		return error;

	/* Places the notifiers and the two surfaces, and paints both. */
	error = scanout_surfaces(gpu);
	if (error != 0)
		return error;

	/* Makes the core channel, then gives it the head's state and the windows' owners. */
	error = scanout_channel(gpu, NVRTX_DISP_CLASS_CORE, 0U, &display->core_buffer, &display->core_push);
	if (error != 0)
		return error;

	display->core_made = 1;
	error = scanout_take_head(gpu);
	if (error != 0)
		return error;

	/* Makes the head's window channel and shows the first surface on it. */
	error = scanout_channel(gpu, NVRTX_DISP_CLASS_WINDOW, display->window, &display->window_buffer, &display->window_push);
	if (error != 0)
		return error;

	display->window_made = 1;
	error = scanout_show(gpu);
	if (error != 0)
		return error;

	/* Leaves the first picture up, then flips to the second and leaves that up too. */
	scanout_hold();
	error = scanout_flip(gpu);
	if (error != 0)
		return error;

	scanout_hold();

	/* Succeeded: the head shows the driver's surface. */
	nvrtx_stage_mark("P8b ok: head %u shows the driver's surfaces", (unsigned)display->gop_head);
	return 0;
}

/*
 * Frees the window and core channels before the display's root; failures are only shown.
 */
void
nvrtx_scanout_stop(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	uint32_t handle;
	int failures;
	int error;

	display = &gpu->display;
	failures = 0;

	/* Frees the window channel, then the core channel. */
	if (display->window_made != 0) {
		handle = (NVRTX_DISP_CLASS_WINDOW << 16) | display->window;
		error = nvrtx_rm_free(gpu, handle);
		if (error != 0)
			failures++;
		display->window_made = 0;
	}

	if (display->core_made != 0) {
		handle = NVRTX_DISP_CLASS_CORE << 16;
		error = nvrtx_rm_free(gpu, handle);
		if (error != 0)
			failures++;
		display->core_made = 0;
	}

	/* Says how the stop went, when anything failed. */
	if (failures != 0)
		nvrtx_stage_mark("P8b: %d channel frees failed; the unload removes the rest", failures);
}

/*
 * Gives back the channels' push buffers.
 *
 * Only once the display no longer reads them: after the GSP is stopped.
 */
void
nvrtx_scanout_release(
	struct nvrtx_gpu *gpu)
{
	/* Frees each push buffer that was allocated. */
	nvrtx_buffer_free(gpu, &gpu->display.core_buffer);
	nvrtx_buffer_free(gpu, &gpu->display.window_buffer);
}

/* Reads the head's armed methods (in the copied order) and its output resource's control. */
static int
scanout_read_armed(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	uint32_t head_base;
	uint32_t index;

	display = &gpu->display;

	/* An output P8a could not name cannot be kept. */
	if (display->gop_sor >= SCANOUT_SOR_MAX) {
		nvrtx_stage_mark("P8b fail: head %u has no output resource (%x)", (unsigned)display->gop_head, (unsigned)display->gop_sor);
		return ENODEV;
	}

	/* Reads each copied method of the head. */
	head_base = SCANOUT_REG_CORE_ARMED + NVRTX_CORE_HEAD_FIRST + display->gop_head * NVRTX_CORE_HEAD_STRIDE;
	for (index = 0U; index < NVRTX_DISP_HEAD_COPIED; index++)
		display->armed[index] = nvrtx_rd32(gpu, head_base + nvrtx_disp_head_copied[index]);

	/* Reads the output resource's control: its owner heads and protocol. */
	display->sor_control = nvrtx_rd32(gpu, SCANOUT_REG_CORE_ARMED + NVRTX_CORE_SOR_SET_CONTROL + display->gop_sor * NVRTX_CORE_SOR_STRIDE);

	/* Shows the viewport, the raster and the control that will be sent back. */
	nvrtx_stage_mark("P8b head %u armed: view %08x raster %08x clock %08x, SOR %u %08x",
	    (unsigned)display->gop_head,
	    (unsigned)display->armed[NVRTX_DISP_ARMED_VIEWPORT_IN],
	    (unsigned)display->armed[NVRTX_DISP_ARMED_RASTER_SIZE],
	    (unsigned)display->armed[NVRTX_DISP_ARMED_PIXEL_CLOCK],
	    (unsigned)display->gop_sor,
	    (unsigned)display->sor_control);

	/*
	 * Sends nothing when the armed state does not describe the picture on
	 * the screen: a raster of zero or an output not owned by the head would
	 * turn the firmware's picture off.
	 */
	if (display->armed[NVRTX_DISP_ARMED_RASTER_SIZE] == 0U) {
		nvrtx_stage_mark("P8b fail: head %u has no armed raster; the picture is left alone", (unsigned)display->gop_head);
		return ENODEV;
	}

	if ((display->sor_control & SCANOUT_SOR_OWNER_MASK & (1U << display->gop_head)) == 0U) {
		nvrtx_stage_mark("P8b fail: SOR %u is not armed for head %u; the picture is left alone", (unsigned)display->gop_sor, (unsigned)display->gop_head);
		return ENODEV;
	}

	/* Succeeded: the head and its output can be sent back as they are. */
	return 0;
}

/*
 * Places the notifiers after the instance memory and the two surfaces below
 * the graphics arena's reserve, and paints the surfaces.
 */
static int
scanout_surfaces(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	struct nvrtx_disp_surface *surface;
	uint64_t span;
	uint64_t candidate;
	uint32_t region;
	uint32_t width;
	uint32_t height;
	unsigned index;
	int error;

	display = &gpu->display;

	/* The surface has the size the head's viewport takes in. */
	width = display->armed[NVRTX_DISP_ARMED_VIEWPORT_IN] & 0xffffU;
	height = display->armed[NVRTX_DISP_ARMED_VIEWPORT_IN] >> 16;
	if (width == 0U || height == 0U) {
		nvrtx_stage_mark("P8b fail: the armed viewport is empty (%08x)", (unsigned)display->armed[NVRTX_DISP_ARMED_VIEWPORT_IN]);
		return EINVAL;
	}

	/* The notifiers live in 4 KiB after the instance memory, cleared. */
	display->sync = display->instance + SCANOUT_SYNC_AFTER_INSTANCE;
	error = nvrtx_vram_zero(gpu, display->sync, SCANOUT_SYNC_BYTES);
	if (error != 0)
		return error;

	/* Takes two surfaces under the graphics arena's reserve, in the last usable region. */
	span = (uint64_t)((width * 4U + SCANOUT_PITCH_ALIGN - 1U) & ~(SCANOUT_PITCH_ALIGN - 1U)) * height;
	span = (span + SCANOUT_SURFACE_ALIGN - 1U) & ~(SCANOUT_SURFACE_ALIGN - 1U);
	error = nvrtx_mmu_vram_block(gpu->static_info, NVRTX_GR_ARENA_RESERVE + 2U * span, &candidate, &region);
	if (error != 0) {
		nvrtx_stage_mark("P8b fail: no VRAM for the surfaces (%d)", error);
		return error;
	}

	/* Describes both surfaces: the same size and pitch, one after the other, read through the VRAM object. */
	for (index = 0U; index < 2U; index++) {
		surface = &display->surfaces[index];
		surface->offset = candidate + span * index;
		surface->width = width;
		surface->height = height;
		surface->pitch = (width * 4U + SCANOUT_PITCH_ALIGN - 1U) & ~(SCANOUT_PITCH_ALIGN - 1U);
		surface->handle = NVRTX_DISP_HANDLE_VRAM;
	}

	/* Paints the bands top to bottom on the first, bottom to top on the second. */
	error = scanout_paint(gpu, &display->surfaces[0], 0);
	if (error != 0)
		return error;

	error = scanout_paint(gpu, &display->surfaces[1], 1);
	if (error != 0)
		return error;

	/* Succeeded: the surfaces are ready to be shown. */
	nvrtx_stage_mark("P8b surfaces %ux%u pitch %u at %llx and %llx",
	    (unsigned)width,
	    (unsigned)height,
	    (unsigned)display->surfaces[0].pitch,
	    (unsigned long long)display->surfaces[0].offset,
	    (unsigned long long)display->surfaces[1].offset);
	return 0;
}

/* Paints eight bands of colour across a surface, in order or reversed. */
static int
scanout_paint(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_disp_surface *surface,
	int reversed)
{
	uint32_t band;
	uint32_t colour;
	uint32_t first_row;
	uint32_t end_row;
	int error;

	/* Fills each band's rows (padding included) with its colour. */
	for (band = 0U; band < SCANOUT_BAND_COUNT; band++) {
		/* The reversed picture starts from the last colour. */
		colour = nvrtx_disp_band_color(band);
		if (reversed != 0)
			colour = nvrtx_disp_band_color(SCANOUT_BAND_COUNT - 1U - band);

		first_row = surface->height * band / SCANOUT_BAND_COUNT;
		end_row = surface->height * (band + 1U) / SCANOUT_BAND_COUNT;
		error = nvrtx_vram_fill(gpu,
		    surface->offset + (uint64_t)first_row * surface->pitch,
		    colour,
		    (size_t)(end_row - first_row) * surface->pitch);
		if (error != 0)
			return error;
	}

	/* Succeeded: the surface holds the picture. */
	return 0;
}

/*
 * Makes one display channel: its push buffer, the buffer's registration,
 * the channel object, and its two context DMA objects.
 */
static int
scanout_channel(
	struct nvrtx_gpu *gpu,
	uint32_t class_number,
	uint32_t instance,
	struct nvrtx_buffer *buffer,
	struct nvrtx_push *push)
{
	uint8_t registration[NVRTX_DISP_PUSHBUFFER_BYTES];
	uint8_t params[NVRTX_DISP_CHANNEL_BYTES];
	const uint8_t *reply;
	size_t reply_length;
	uint32_t chid;
	uint32_t handle;
	int error;

	/* One page of coherent system memory for the push buffer. */
	error = nvrtx_buffer_alloc(gpu, SCANOUT_PUSH_BYTES, buffer);
	if (error != 0)
		return error;

	nvrtx_push_init(push, (uint8_t *)buffer->dma.address, SCANOUT_PUSH_WORDS);

	/* Registers the push buffer with the resource manager, for this class and instance. */
	nvrtx_disp_pushbuffer_params(registration, buffer->dma.device_address, SCANOUT_PUSH_BYTES, class_number, instance);
	error = nvrtx_rm_control(gpu,
	    gpu->internal_client,
	    gpu->internal_subdevice,
	    SCANOUT_CTL_PUSHBUFFER,
	    registration,
	    sizeof(registration),
	    &reply,
	    &reply_length);
	if (error != 0)
		return error;

	/* Makes the channel under the display root, its put at the start. */
	handle = (class_number << 16) | instance;
	nvrtx_disp_channel_params(params, instance);
	error = nvrtx_rm_allocate(gpu, SCANOUT_HANDLE_ROOT, handle, class_number, params, sizeof(params));
	if (error != 0)
		return error;

	/* The core is channel 0 of the hash table; window w is channel 1 + w. */
	chid = NVRTX_DISP_CHID_CORE;
	if (class_number == NVRTX_DISP_CLASS_WINDOW)
		chid = NVRTX_DISP_CHID_WINDOW_FIRST + instance;

	/* Gives it the notifiers' object and the whole VRAM's. */
	error = scanout_bind(gpu, chid, NVRTX_DISP_HANDLE_SYNC, gpu->display.sync, gpu->display.sync + SCANOUT_SYNC_BYTES - 1U);
	if (error != 0)
		return error;

	error = scanout_bind(gpu, chid, NVRTX_DISP_HANDLE_VRAM, 0U, gpu->layout.vram_size - 1U);
	if (error != 0)
		return error;

	/* Succeeded: the channel takes methods. */
	nvrtx_stage_mark("P8b channel %04x.%u made", (unsigned)class_number, (unsigned)instance);
	return 0;
}

/*
 * Writes one context DMA object into the instance memory and enters it in
 * the hash table for one channel, probing past the slots already taken.
 */
static int
scanout_bind(
	struct nvrtx_gpu *gpu,
	uint32_t chid,
	uint32_t handle,
	uint64_t start,
	uint64_t limit)
{
	struct nvrtx_display_state *display;
	uint8_t object[NVRTX_DISP_CONTEXT_DMA_BYTES];
	uint8_t entry[NVRTX_DISP_RAMHT_ENTRY_BYTES];
	uint32_t slot;
	uint32_t used;
	uint32_t client;
	int taken;
	int error;

	display = &gpu->display;

	/* The objects follow the hash table. */
	if (display->object_next == 0U)
		display->object_next = NVRTX_DISP_RAMHT_BYTES;

	/* The slots taken are recorded; more than the record holds is refused. */
	if (display->ramht_count >= NVRTX_DISP_RAMHT_USED_MAX)
		return ENOSPC;

	/* Writes the object over the range. */
	nvrtx_disp_context_dma(object, start, limit);
	error = nvrtx_vram_write(gpu, display->instance + display->object_next, object, sizeof(object));
	if (error != 0)
		return error;

	/* Finds the first free slot from the handle's hash on. */
	slot = nvrtx_disp_ramht_hash(chid, handle);
	for (;;) {
		/* Looks the slot up among the taken ones. */
		taken = 0;
		for (used = 0U; used < display->ramht_count; used++) {
			if (display->ramht_slots[used] == slot)
				taken = 1;
		}

		/* A free slot ends the search; a taken one moves to the next. */
		if (taken == 0)
			break;

		slot = (slot + 1U) % NVRTX_DISP_RAMHT_ENTRIES;
	}

	/* Enters the handle with the channel, the low bits of the owning client, and the object's offset. */
	client = NVRTX_HANDLE_CLIENT;
	nvrtx_disp_ramht_entry(entry, handle, chid, client, display->object_next);
	error = nvrtx_vram_write(gpu, display->instance + (uint64_t)slot * NVRTX_DISP_RAMHT_ENTRY_BYTES, entry, sizeof(entry));
	if (error != 0)
		return error;

	/* Succeeded: the object and its slot are taken. */
	display->ramht_slots[display->ramht_count] = slot;
	display->ramht_count++;
	display->object_next += NVRTX_DISP_CONTEXT_DMA_STEP;
	return 0;
}

/* Hands the methods written so far to a channel: the put moves to the end of the buffer's words. */
static int
scanout_kick(
	struct nvrtx_gpu *gpu,
	struct nvrtx_push *push,
	uint32_t user)
{
	/* A buffer that overflowed would send a cut sequence. */
	if (push->overflow != 0) {
		nvrtx_stage_mark("P8b fail: a push buffer overflowed (%u words)", (unsigned)push->count);
		return E2BIG;
	}

	/* The words are in memory before the put moves; the put is a byte offset. */
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	nvrtx_wr32(gpu, user + SCANOUT_USER_PUT_AT, push->count * 4U);

	/* Succeeded: the channel fetches up to the put. */
	return 0;
}

/* Clears one notifier (status not begun) before the update that writes it. */
static int
scanout_notifier_reset(
	struct nvrtx_gpu *gpu,
	uint32_t offset)
{
	int error;

	/* Clears its four words. */
	error = nvrtx_vram_zero(gpu, gpu->display.sync + offset, SCANOUT_NOTIFIER_BYTES);
	if (error != 0)
		return error;

	/* Succeeded: the notifier reads not begun. */
	return 0;
}

/*
 * Waits until a notifier's status reaches least; on a timeout shows the
 * notifier and the channel's get and put.
 */
static int
scanout_notifier_wait(
	struct nvrtx_gpu *gpu,
	uint32_t offset,
	uint32_t least,
	uint32_t user,
	const char *what)
{
	uint64_t start;
	uint64_t waited;
	uint32_t word;
	uint32_t get;
	uint32_t put;
	int error;

	/* Polls until the status is reached or the time, measured, is spent. */
	start = nvrtx_now_us();
	word = 0U;
	for (;;) {
		error = nvrtx_vram_read32(gpu, gpu->display.sync + offset, &word);
		if (error != 0)
			return error;

		/* The status is the word's top two bits. */
		waited = nvrtx_now_us() - start;
		if ((word >> NVRTX_DISP_NOTIFIER_STATUS_SHIFT) >= least) {
			nvrtx_stage_mark("P8b %s notified after %u us", what, (unsigned)waited);
			return 0;
		}

		if (waited > SCANOUT_NOTIFIER_TIMEOUT_US)
			break;

		kern_usleep_range(SCANOUT_NOTIFIER_STEP_US, SCANOUT_NOTIFIER_STEP_US * 2U);
	}

	/* The notifier never came: shows where the channel stopped. */
	get = nvrtx_rd32(gpu, user + SCANOUT_USER_GET_AT);
	put = nvrtx_rd32(gpu, user + SCANOUT_USER_PUT_AT);
	nvrtx_stage_mark("P8b fail: %s notifier %08x after 2 s, get %x put %x",
	    what,
	    (unsigned)word,
	    (unsigned)get,
	    (unsigned)put);
	return ETIMEDOUT;
}

/*
 * Gives the core channel the head's armed state and the windows' owners,
 * and releases it in an update no window is interlocked with.
 *
 * nouveau's order: the core's start (its notifier object and the windows'
 * bounds), the output resource's control, the head's state, then the
 * windows' owners in an update of the core alone.
 */
static int
scanout_take_head(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	struct nvrtx_push *push;
	int error;

	display = &gpu->display;
	push = &display->core_push;

	/* The core's start: its notifier object and the bounds of every window. */
	nvrtx_disp_core_init(push, display->window_mask);

	/* The output resource and the head, with the values they are armed with. */
	nvrtx_disp_core_sor(push, display->gop_sor, display->sor_control);
	nvrtx_disp_core_head(push, display->gop_head, display->armed);

	/* Every window to its head, released by the core alone, with its notifier. */
	nvrtx_disp_core_owners(push, display->window_mask);
	error = scanout_notifier_reset(gpu, SCANOUT_CORE_NOTIFIER_AT);
	if (error != 0)
		return error;

	nvrtx_disp_core_update(push, SCANOUT_CORE_NOTIFIER_AT, 0U);
	error = scanout_kick(gpu, push, SCANOUT_REG_CORE_USER);
	if (error != 0)
		return error;

	/* Waits for the core to finish the update. */
	error = scanout_notifier_wait(gpu, SCANOUT_CORE_NOTIFIER_AT, NVRTX_DISP_NOTIFIER_FINISHED, SCANOUT_REG_CORE_USER, "core owners");
	if (error != 0)
		return error;

	/* Succeeded: the core holds the head as the firmware armed it. */
	return 0;
}

/*
 * Shows the first surface: the window's image and colour defaults
 * interlocked with a core update that turns the head's cursor and output
 * LUT off.
 */
static int
scanout_show(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	int error;

	display = &gpu->display;

	/* The window: the first surface, no input LUT, the identity matrix, opaque; held for the core. */
	nvrtx_disp_window_image(&display->window_push, &display->surfaces[0]);
	nvrtx_disp_window_defaults(&display->window_push);
	nvrtx_disp_window_update(&display->window_push, 1, display->window);
	error = scanout_kick(gpu, &display->window_push, scanout_window_user(display->window));
	if (error != 0)
		return error;

	/* The core: no cursor and no output LUT object, released together with the window. */
	nvrtx_disp_core_head_planes_off(&display->core_push, display->gop_head);
	error = scanout_notifier_reset(gpu, SCANOUT_CORE_NOTIFIER_AT);
	if (error != 0)
		return error;

	nvrtx_disp_core_update(&display->core_push, SCANOUT_CORE_NOTIFIER_AT, 1U << display->window);
	error = scanout_kick(gpu, &display->core_push, SCANOUT_REG_CORE_USER);
	if (error != 0)
		return error;

	/* Waits for the core to finish the update the window waited for. */
	error = scanout_notifier_wait(gpu, SCANOUT_CORE_NOTIFIER_AT, NVRTX_DISP_NOTIFIER_FINISHED, SCANOUT_REG_CORE_USER, "core show");
	if (error != 0)
		return error;

	/* Succeeded: the head shows the first surface. */
	nvrtx_stage_mark("P8b ok: mode set (%ux%u on SOR %u), surface A shown",
	    (unsigned)display->surfaces[0].width,
	    (unsigned)display->surfaces[0].height,
	    (unsigned)display->gop_sor);
	return 0;
}

/* Flips the window to the second surface and waits for its notifier. */
static int
scanout_flip(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	uint32_t notifier;
	int error;

	display = &gpu->display;
	notifier = scanout_window_notifier(display->window);

	/* Clears the window's notifier, then flips with it, on the window alone. */
	error = scanout_notifier_reset(gpu, notifier);
	if (error != 0)
		return error;

	nvrtx_disp_window_flip(&display->window_push, display->surfaces[1].offset, notifier);
	nvrtx_disp_window_update(&display->window_push, 0, display->window);
	error = scanout_kick(gpu, &display->window_push, scanout_window_user(display->window));
	if (error != 0)
		return error;

	/* Waits until the flip has begun (the window's notifier leaves "not begun"). */
	error = scanout_notifier_wait(gpu, notifier, 1U, scanout_window_user(display->window), "window flip");
	if (error != 0)
		return error;

	/* Succeeded: the second surface is on the head. */
	nvrtx_stage_mark("P8b ok: flip, surface B shown");
	return 0;
}

/* Keeps the current picture up for a while, so it can be seen. */
static void
scanout_hold(void)
{
	uint64_t start;
	uint64_t now;

	/* Waits, measured, in short steps. */
	start = nvrtx_now_us();
	for (;;) {
		now = nvrtx_now_us();
		if (now - start >= SCANOUT_SHOW_US)
			break;

		kern_usleep_range(SCANOUT_SHOW_STEP_US, SCANOUT_SHOW_STEP_US);
	}
}

/* Reports where one window channel's put and get are. */
static uint32_t
scanout_window_user(
	uint32_t window)
{
	/* Each window has its own page after the first. */
	return SCANOUT_REG_WINDOW_USER + window * SCANOUT_WINDOW_USER_STRIDE;
}

/* Reports where one window's notifier is in the notifiers' VRAM. */
static uint32_t
scanout_window_notifier(
	uint32_t window)
{
	/* After the core's slot, one slot for each window, the notifier in its second half. */
	return (1U + window) * SCANOUT_WINDOW_SLOT_BYTES + SCANOUT_WINDOW_NOTIFIER_AT;
}
