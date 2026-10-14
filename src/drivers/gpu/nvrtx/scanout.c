/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The takeover of the firmware's head (stage P8b, on by default, off with
 * nvrtx.disp=0).
 *
 * After P8a found the head the firmware (GOP) lit, the driver makes the
 * display's core channel and the head's first window channel through the
 * resource manager, writes their context DMA objects and hash-table entries
 * into the display's instance memory, and sends the head the state it is
 * armed with: the same timing, the same output resource (SOR) and control,
 * so the output keeps running.  The window then shows the firmware's
 * console, kept at the VRAM's start (P0 and P4m), so the console goes on
 * where it was; the GPU device (P8c) flips away from it and back.  With
 * nvrtx.disp.test=1 the window shows a surface of the driver's own instead,
 * a flip moves it to a second one, and each step waits 5 s so the monitor
 * can be watched (the R8b..R8d runs).
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
#define SCANOUT_SHOW_RESIDENT_US	3000000U
#define SCANOUT_SHOW_STEP_US		100000U

/* The surfaces: rows in 256-byte steps, each surface on its own 1 MiB step. */
#define SCANOUT_PITCH_ALIGN		256U
#define SCANOUT_SURFACE_ALIGN		0x100000ULL
#define SCANOUT_BAND_COUNT		8U

/* The output resource a head can be driven by: the SOR's index from P8a, at most 8. */
#define SCANOUT_SOR_MAX			8U

/* The most words one frame's methods take in a window's push buffer (the image, the flip and the update). */
#define SCANOUT_FRAME_WORDS_MAX		64U

/* The display's common object (P8a's) and the control that assigns an output resource to a display. */
#define SCANOUT_HANDLE_COMMON		0x00730000U
#define SCANOUT_CTL_ASSIGN_SOR		0x00731152U

/* The protocols of a TMDS output (single link A or B, dual link): the takeover drives only these. */
#define SCANOUT_PROTOCOL_TMDS_A		1U
#define SCANOUT_PROTOCOL_TMDS_B		2U
#define SCANOUT_PROTOCOL_TMDS_DUAL	5U

/* How long each step of the takeover waits after its mark, so the monitor can be watched at it. */
#define SCANOUT_STEP_US			5000000U

/* The display's error state, read when a notifier does not come: the core's exception, the other exceptions, the supervisor and the core's state. */
#define SCANOUT_REG_CORE_EXCEPTION	0x611020U
#define SCANOUT_REG_CORE_EXCEPTION_DATA	0x611024U
#define SCANOUT_REG_CORE_EXCEPTION_CODE	0x611028U
#define SCANOUT_REG_EXCEPTION_OTHER	0x611854U
#define SCANOUT_REG_SUPERVISOR		0x611c30U
#define SCANOUT_REG_CORE_STATE		0x610630U

/* The output resource's control: the heads that own it are bits 7:0. */
#define SCANOUT_SOR_OWNER_MASK		0xffU

/* The cursor channels' user areas: one page for each head (nouveau's r535_chan_user). */
#define SCANOUT_REG_CURSOR_USER		0x6d8000U
#define SCANOUT_CURSOR_USER_STRIDE	0x1000U

/* A cursor channel is written once it has four free entries, waited for at most 100 ms (nouveau's curs507a_space). */
#define SCANOUT_CURSOR_SPACE_LEAST	4U
#define SCANOUT_CURSOR_SPACE_US		100000U

/* The cursor channel's update: release the gathered state (RELEASE_ELV, nouveau's cursc37a_update). */
#define SCANOUT_CURSOR_RELEASE		0x00000001U

/* How long the core channel is given to fetch what it has before more is written over it, and the polling step. */
#define SCANOUT_DRAIN_US		100000U
#define SCANOUT_DRAIN_STEP_US		50U

/* The most words a cursor's core methods and update take (the cursor's 7, the update's 9), with a margin. */
#define SCANOUT_CURSOR_WORDS_MAX	32U

/* The heads' raster positions, whose top half counts frames (RG_DPCA): one block of 0x800 for each head. */
#define SCANOUT_REG_RG_DPCA		0x616330U
#define SCANOUT_RG_STRIDE		0x800U

static int scanout_read_armed(struct nvrtx_gpu *gpu);
static int scanout_surfaces(struct nvrtx_gpu *gpu);
static int scanout_console_surface(struct nvrtx_gpu *gpu, uint32_t width, uint32_t height);
static int scanout_surface_objects(struct nvrtx_gpu *gpu, uint64_t span);
static int scanout_paint(struct nvrtx_gpu *gpu, const struct nvrtx_disp_surface *surface, int reversed);
static int scanout_channel(struct nvrtx_gpu *gpu, uint32_t class_number, uint32_t instance, struct nvrtx_buffer *buffer, struct nvrtx_push *push, int *made);
static int scanout_bind(struct nvrtx_gpu *gpu, uint32_t chid, uint32_t handle, uint64_t start, uint64_t limit);
static int scanout_kick(struct nvrtx_gpu *gpu, struct nvrtx_push *push, uint32_t user);
static int scanout_notifier_reset(struct nvrtx_gpu *gpu, uint32_t offset);
static int scanout_notifier_wait(struct nvrtx_gpu *gpu, uint32_t offset, uint32_t least, uint32_t user, const char *what);
static int scanout_take_head(struct nvrtx_gpu *gpu);
static int scanout_core_update(struct nvrtx_gpu *gpu, uint32_t window_interlock, const char *what);
static int scanout_assign_sor(struct nvrtx_gpu *gpu);
static void scanout_step(const char *what);
static void scanout_diagnose(struct nvrtx_gpu *gpu);
static int scanout_show(struct nvrtx_gpu *gpu);
static int scanout_flip(struct nvrtx_gpu *gpu);
static void scanout_hold(uint32_t microseconds);
static uint32_t scanout_window_user(uint32_t window);
static uint32_t scanout_window_notifier(uint32_t window);
static int scanout_drained(struct nvrtx_gpu *gpu, uint32_t user, uint32_t put);
static int scanout_core_room(struct nvrtx_gpu *gpu, uint32_t words);
static int scanout_core_send_cursor(struct nvrtx_gpu *gpu);
static int scanout_cursor_write(struct nvrtx_gpu *gpu, uint32_t method, uint32_t value);

/*
 * Runs stage P8b: takes the firmware's head over and shows the console on
 * it (with the monitor test, a surface of bands and a flip).
 *
 * Without a head the firmware lit, or without a console the head can show,
 * nothing is done (the scanout rule) and display.taken stays 0.  A failure
 * is returned; the finish's stop frees the channels that were made.
 */
int
nvrtx_scanout_start(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	uint32_t hold_us;
	bool resident;
	bool test;
	int error;

	display = &gpu->display;
	test = nvrtx_stage_disp_test_wanted();

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

	/* Places the notifiers and the surface to show; a console that cannot be shown leaves the head alone. */
	error = scanout_surfaces(gpu);
	if (error == ENOTSUP)
		return 0;
	if (error != 0)
		return error;

	/* Makes the core channel, then sets the head's mode as nouveau's first commit does. */
	error = scanout_channel(gpu, NVRTX_DISP_CLASS_CORE, 0U, &display->core_buffer, &display->core_push, &display->core_made);
	if (error != 0)
		return error;

	scanout_step("1: core channel made");
	error = scanout_take_head(gpu);
	if (error != 0)
		return error;

	/* Makes the head's window channel and shows the first surface on it. */
	error = scanout_channel(gpu, NVRTX_DISP_CLASS_WINDOW, display->window, &display->window_buffer, &display->window_push, &display->window_made);
	if (error != 0)
		return error;

	error = scanout_show(gpu);
	if (error != 0)
		return error;

	/* The console is on the head again: the takeover is done. */
	if (!test) {
		display->taken = 1;
		nvrtx_stage_mark("P8b ok: head %u shows the console", (unsigned)display->gop_head);
		return 0;
	}

	/* Leaves the first picture up, then flips to the second and leaves that up too (shorter when the GPU stays up). */
	hold_us = SCANOUT_SHOW_US;
	resident = nvrtx_stage_resident_wanted();
	if (resident)
		hold_us = SCANOUT_SHOW_RESIDENT_US;
	scanout_hold(hold_us);
	error = scanout_flip(gpu);
	if (error != 0)
		return error;

	scanout_hold(hold_us);

	/* Succeeded: the head shows the driver's surface. */
	display->taken = 1;
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

	/* Frees the cursor channel (P8c) first. */
	if (display->cursor_made != 0) {
		handle = (NVRTX_DISP_CLASS_CURSOR << 16) | display->gop_head;
		error = nvrtx_rm_free(gpu, handle);
		if (error != 0)
			failures++;
		display->cursor_made = 0;
	}

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

/*
 * Shows a surface on the taken-over head's window and waits until the flip
 * has begun (the GPU device's frames, stage P8c).
 *
 * The window gets the surface's whole image state, so its size, pitch and
 * format may differ from the last one's.  When the push buffer has no room
 * left the channel is wound back to its start first (nouveau's wind).  The
 * caller serializes the frames.  Returns ETIMEDOUT when the flip does not
 * begin in 2 s (the window then still reads the surface it had or this one).
 */
int
nvrtx_scanout_frame(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_disp_surface *surface)
{
	struct nvrtx_display_state *display;
	struct nvrtx_push *push;
	uint32_t notifier;
	uint32_t user;
	int error;

	display = &gpu->display;
	push = &display->window_push;
	notifier = scanout_window_notifier(display->window);
	user = scanout_window_user(display->window);

	/*
	 * Winds the buffer back to its start when the frame might not fit
	 * before its end: a jump to the start where the get stands, and the put
	 * moved to the start, so the channel fetches the jump and stops there
	 * (the put never covers the jump, or the channel would run the old
	 * words again after it).
	 */
	if (push->count + SCANOUT_FRAME_WORDS_MAX + 1U > push->capacity) {
		nvrtx_push_jump(push, 0U);
		if (push->overflow != 0)
			return E2BIG;

		push->count = 0U;
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		nvrtx_wr32(gpu, user + SCANOUT_USER_PUT_AT, 0U);
	}

	/* Clears the window's notifier, then sends the image, the flip with the notifier, and an update of the window alone. */
	error = scanout_notifier_reset(gpu, notifier);
	if (error != 0)
		return error;

	nvrtx_disp_window_image(push, surface);
	nvrtx_disp_window_flip(push, surface->offset, notifier);
	nvrtx_disp_window_update(push, 0, display->window);
	error = scanout_kick(gpu, push, user);
	if (error != 0)
		return error;

	/* Waits until the flip has begun. */
	error = scanout_notifier_wait(gpu, notifier, 1U, user, NULL);
	if (error != 0)
		return error;

	/* Succeeded: the head shows the surface. */
	return 0;
}

/*
 * Makes the taken-over head's cursor channel (stage P8c): the resource
 * manager is told the PIO channel has no push buffer, then makes it under
 * the display root (nouveau's r535_curs_init).  Runs before the GPU device
 * is registered, while the worker is still the channel's only caller; the
 * stop frees it.  A failure leaves the display without a cursor plane.
 */
int
nvrtx_scanout_cursor_start(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	uint8_t registration[NVRTX_DISP_PUSHBUFFER_BYTES];
	uint8_t params[NVRTX_DISP_PIO_CHANNEL_BYTES];
	const uint8_t *reply;
	size_t reply_length;
	uint32_t handle;
	int error;

	display = &gpu->display;

	/* Registers the head's cursor channel without a push buffer. */
	nvrtx_disp_pio_pushbuffer_params(registration, NVRTX_DISP_CLASS_CURSOR, display->gop_head);
	error = nvrtx_rm_control(gpu,
	    gpu->internal_client,
	    gpu->internal_subdevice,
	    SCANOUT_CTL_PUSHBUFFER,
	    registration,
	    sizeof(registration),
	    &reply,
	    &reply_length);
	if (error != 0) {
		nvrtx_stage_mark("P8c: cursor channel registration failed (%d)", error);
		return error;
	}

	/* Makes the channel under the display root, the head as its instance. */
	handle = (NVRTX_DISP_CLASS_CURSOR << 16) | display->gop_head;
	nvrtx_disp_pio_channel_params(params, display->gop_head);
	error = nvrtx_rm_allocate(gpu, SCANOUT_HANDLE_ROOT, handle, NVRTX_DISP_CLASS_CURSOR, params, sizeof(params));
	if (error != 0) {
		nvrtx_stage_mark("P8c: cursor channel allocation failed (%d)", error);
		return error;
	}

	/* Succeeded: the head's cursor takes a position and an update. */
	display->cursor_made = 1;
	nvrtx_stage_mark("P8c channel %04x.%u made", (unsigned)NVRTX_DISP_CLASS_CURSOR, (unsigned)display->gop_head);
	return 0;
}

/*
 * Shows the head's cursor: the image at a VRAM offset (64 by 64 A8R8G8B8 on
 * a 256-byte boundary) with its top left at a position word.
 *
 * As nouveau's commit of a cursor plane: the cursor channel takes the
 * position and its update, the core the cursor's control, object and
 * offset, then the core's update interlocked with the cursor.  The core's
 * notifier is asked for but not waited for (core_pending is set); the
 * caller first makes sure no earlier one is out (nvrtx_scanout_core_settled).
 * Returns 0, ETIMEDOUT when a channel does not take the words, or E2BIG.
 */
int
nvrtx_scanout_cursor_show(
	struct nvrtx_gpu *gpu,
	uint64_t offset,
	uint32_t point)
{
	struct nvrtx_display_state *display;
	int error;

	display = &gpu->display;

	/* The cursor channel: the position, then its update. */
	error = scanout_cursor_write(gpu, NVRTX_CURSOR_POINT_OUT, point);
	if (error != 0)
		return error;

	error = scanout_cursor_write(gpu, NVRTX_CURSOR_UPDATE, SCANOUT_CURSOR_RELEASE);
	if (error != 0)
		return error;

	/* The core, once it has room: the cursor on, read through the VRAM object. */
	error = scanout_core_room(gpu, SCANOUT_CURSOR_WORDS_MAX);
	if (error != 0)
		return error;

	nvrtx_disp_core_cursor_set(&display->core_push, display->gop_head, NVRTX_DISP_HANDLE_VRAM, offset);

	/* The core's update with the cursor, not waited for. */
	error = scanout_core_send_cursor(gpu);
	if (error != 0)
		return error;

	/* Succeeded: the cursor shows from a coming frame on. */
	return 0;
}

/*
 * Moves the shown cursor: the cursor channel takes the position and its
 * update alone, no core update (nouveau's legacy cursor move).
 */
int
nvrtx_scanout_cursor_point(
	struct nvrtx_gpu *gpu,
	uint32_t point)
{
	int error;

	/* The position, then the update. */
	error = scanout_cursor_write(gpu, NVRTX_CURSOR_POINT_OUT, point);
	if (error != 0)
		return error;

	error = scanout_cursor_write(gpu, NVRTX_CURSOR_UPDATE, SCANOUT_CURSOR_RELEASE);
	if (error != 0)
		return error;

	/* Succeeded: the cursor moves from a coming frame on. */
	return 0;
}

/*
 * Hides the head's cursor: the core's cursor off and without its object,
 * then the core's update interlocked with the cursor (nouveau's clear of a
 * cursor plane), its notifier asked for but not waited for.  The caller
 * first makes sure no earlier one is out.
 */
int
nvrtx_scanout_cursor_clear(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	int error;

	display = &gpu->display;

	/* The core, once it has room: the cursor off. */
	error = scanout_core_room(gpu, SCANOUT_CURSOR_WORDS_MAX);
	if (error != 0)
		return error;

	nvrtx_disp_core_cursor_clear(&display->core_push, display->gop_head);

	/* The core's update with the cursor, not waited for. */
	error = scanout_core_send_cursor(gpu);
	if (error != 0)
		return error;

	/* Succeeded: the cursor is gone from a coming frame on. */
	return 0;
}

/*
 * Reports whether the core update last sent without a wait (a cursor's)
 * has finished, waiting for it at most wait_us.  Returns 0 when none is out
 * (core_pending is then clear), EBUSY while it still is, or the error of
 * reading its notifier.
 */
int
nvrtx_scanout_core_settled(
	struct nvrtx_gpu *gpu,
	uint32_t wait_us)
{
	struct nvrtx_display_state *display;
	uint64_t start;
	uint64_t waited;
	uint32_t word;
	int error;

	display = &gpu->display;

	/* Nothing is out. */
	if (display->core_pending == 0)
		return 0;

	/* Polls the core's notifier until it says finished or the time is spent. */
	start = nvrtx_now_us();
	for (;;) {
		error = nvrtx_vram_read32(gpu, display->sync + SCANOUT_CORE_NOTIFIER_AT, &word);
		if (error != 0)
			return error;

		/* The update is done. */
		if ((word >> NVRTX_DISP_NOTIFIER_STATUS_SHIFT) >= NVRTX_DISP_NOTIFIER_FINISHED)
			break;

		/* Still out after the time the caller gives it. */
		waited = nvrtx_now_us() - start;
		if (waited >= wait_us)
			return EBUSY;

		kern_usleep_range(SCANOUT_NOTIFIER_STEP_US, SCANOUT_NOTIFIER_STEP_US * 2U);
	}

	/* Succeeded: nothing of the update reads the old state any more. */
	display->core_pending = 0;
	return 0;
}

/*
 * Reports the taken-over head's frame counter: 16 bits that move by one
 * each frame the head scans out (the raster generator's RG_DPCA), read
 * without side effects.
 */
uint32_t
nvrtx_scanout_frame_counter(
	struct nvrtx_gpu *gpu)
{
	uint32_t position;

	/* The head's raster position: the line in the low half, the frame in the high half. */
	position = nvrtx_rd32(gpu, SCANOUT_REG_RG_DPCA + gpu->display.gop_head * SCANOUT_RG_STRIDE);

	/* Succeeded: the frame count. */
	return (position >> NVRTX_DISP_FRAME_COUNT_SHIFT) & NVRTX_DISP_FRAME_COUNT_MASK;
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

	/* A DisplayPort output needs a link training this driver does not do yet. */
	if (display->gop_protocol != SCANOUT_PROTOCOL_TMDS_A &&
	    display->gop_protocol != SCANOUT_PROTOCOL_TMDS_B &&
	    display->gop_protocol != SCANOUT_PROTOCOL_TMDS_DUAL) {
		nvrtx_stage_mark("P8b: protocol %u is not TMDS; the picture is left alone", (unsigned)display->gop_protocol);
		return ENODEV;
	}

	/* The mode the head is set to: the armed raster and clock, the visible size from the blank. */
	nvrtx_disp_mode_from_armed(display->armed, &display->mode);
	nvrtx_stage_mark("P8b head %u mode: %ux%u, view out %08x",
	    (unsigned)display->gop_head,
	    (unsigned)(display->mode.active & 0xffffU),
	    (unsigned)(display->mode.active >> 16),
	    (unsigned)display->armed[NVRTX_DISP_ARMED_VIEWPORT_OUT]);

	/* Succeeded: the head and its output can be sent back as they are. */
	return 0;
}

/*
 * Places the notifiers after the instance memory and the two surfaces below
 * the graphics and compute arenas' reserve, and paints the surfaces.
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
	bool test;
	int error;

	display = &gpu->display;

	/* The surface has the mode's visible size (the viewport in and out). */
	width = display->mode.active & 0xffffU;
	height = display->mode.active >> 16;
	if (width == 0U || height == 0U || width > 0x4000U || height > 0x4000U) {
		nvrtx_stage_mark("P8b fail: the armed visible size is %ux%u", (unsigned)width, (unsigned)height);
		return EINVAL;
	}

	/* The notifiers live in 4 KiB after the instance memory, cleared. */
	display->sync = display->instance + SCANOUT_SYNC_AFTER_INSTANCE;
	error = nvrtx_vram_zero(gpu, display->sync, SCANOUT_SYNC_BYTES);
	if (error != 0)
		return error;

	/* Without the monitor test the head shows the console. */
	test = nvrtx_stage_disp_test_wanted();
	if (!test) {
		error = scanout_console_surface(gpu, width, height);
		if (error != 0)
			return error;

		return 0;
	}

	/*
	 * Takes two surfaces from the VRAM allocator (P4m), mapped in the CPU's
	 * window when there is one; without the allocator, under the graphics
	 * and compute arenas' reserve in the last usable region, as before.
	 */
	span = (uint64_t)((width * 4U + SCANOUT_PITCH_ALIGN - 1U) & ~(SCANOUT_PITCH_ALIGN - 1U)) * height;
	span = (span + SCANOUT_SURFACE_ALIGN - 1U) & ~(SCANOUT_SURFACE_ALIGN - 1U);
	if (gpu->memory_state.ready != 0) {
		error = scanout_surface_objects(gpu, span);
	} else {
		error = nvrtx_mmu_vram_block(gpu->static_info, NVRTX_COMPUTE_ARENA_RESERVE + 2U * span, &candidate, &region);
		display->surface_objects[0].vram = candidate;
		display->surface_objects[1].vram = candidate + span;
	}
	if (error != 0) {
		nvrtx_stage_mark("P8b fail: no VRAM for the surfaces (%d)", error);
		return error;
	}

	/* Describes both surfaces: the same size and pitch, read through the VRAM object. */
	for (index = 0U; index < 2U; index++) {
		surface = &display->surfaces[index];
		surface->offset = display->surface_objects[index].vram;
		surface->width = width;
		surface->height = height;
		surface->pitch = (width * 4U + SCANOUT_PITCH_ALIGN - 1U) & ~(SCANOUT_PITCH_ALIGN - 1U);
		surface->handle = NVRTX_DISP_HANDLE_VRAM;
		surface->format = NVRTX_DISP_FORMAT_A8R8G8B8;
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

/*
 * Describes the firmware's console as both surfaces (the second is never
 * flipped to without the test): its VRAM at the start, its size, pitch and
 * format.  The console is shown only when it was kept (P0, P4m), has the
 * mode's visible size and a pitch the window takes (a multiple of 64
 * bytes); otherwise ENOTSUP leaves the head to the firmware.
 */
static int
scanout_console_surface(
	struct nvrtx_gpu *gpu,
	uint32_t width,
	uint32_t height)
{
	const struct nvrtx_console *console;
	struct nvrtx_disp_surface *surface;
	unsigned index;

	console = &gpu->console;

	/* Leaves the head alone without a console the window reaches. */
	if (console->mapped == 0) {
		nvrtx_stage_mark("P8b: no console kept; the head is left to the firmware");
		return ENOTSUP;
	}

	/* Leaves the head alone when the console is not the mode's picture. */
	if (console->width != width || console->height != height) {
		nvrtx_stage_mark("P8b: the console is %ux%u, the mode %ux%u; the head is left to the firmware",
		    (unsigned)console->width,
		    (unsigned)console->height,
		    (unsigned)width,
		    (unsigned)height);
		return ENOTSUP;
	}

	/* Leaves the head alone when the window cannot take the console's rows. */
	if ((console->pitch & (NVRTX_DISP_PITCH_UNIT - 1U)) != 0U) {
		nvrtx_stage_mark("P8b: the console's pitch %u is not a multiple of %u; the head is left to the firmware",
		    (unsigned)console->pitch,
		    (unsigned)NVRTX_DISP_PITCH_UNIT);
		return ENOTSUP;
	}

	/* Describes the console as both surfaces. */
	for (index = 0U; index < 2U; index++) {
		surface = &gpu->display.surfaces[index];
		surface->offset = 0U;
		surface->width = width;
		surface->height = height;
		surface->pitch = console->pitch;
		surface->handle = NVRTX_DISP_HANDLE_VRAM;
		surface->format = NVRTX_DISP_FORMAT_A8R8G8B8;
	}

	/* Succeeded: the console is the surface to show. */
	nvrtx_stage_mark("P8b console %ux%u pitch %u at the VRAM's start", (unsigned)width, (unsigned)height, (unsigned)console->pitch);
	return 0;
}

/* Takes the two surfaces' VRAM from the allocator and maps them in the CPU's window when it works. */
static int
scanout_surface_objects(
	struct nvrtx_gpu *gpu,
	uint64_t span)
{
	struct nvrtx_bo *object;
	unsigned index;
	int error;

	/* Each surface is its own object on a 1 MiB boundary. */
	for (index = 0U; index < 2U; index++) {
		object = &gpu->display.surface_objects[index];
		error = nvrtx_bo_alloc(gpu, span, SCANOUT_SURFACE_ALIGN, object);
		if (error != 0)
			return error;

		/* The CPU's view is for later frames; P8b paints through PRAMIN, so a missing window is no failure. */
		if (gpu->memory_state.bar1_ready != 0)
			(void)nvrtx_bo_map(gpu, object);
	}

	/* Succeeded: both surfaces have VRAM of their own. */
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
 *
 * *made is set as soon as the channel object exists, so the stop frees it
 * even when a later step fails.
 */
static int
scanout_channel(
	struct nvrtx_gpu *gpu,
	uint32_t class_number,
	uint32_t instance,
	struct nvrtx_buffer *buffer,
	struct nvrtx_push *push,
	int *made)
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

	*made = 1;

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
 * notifier and the channel's get and put.  what names the wait in the marks;
 * NULL (a frame of the GPU device) marks only a timeout.
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
			if (what != NULL)
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
	if (what == NULL)
		what = "frame";
	nvrtx_stage_mark("P8b fail: %s notifier %08x after 2 s, get %x put %x",
	    what,
	    (unsigned)word,
	    (unsigned)get,
	    (unsigned)put);
	return ETIMEDOUT;
}

/*
 * Sets the taken-over head's mode in the steps of nouveau's first commit.
 *
 * 1. The core's start (its notifier object and every window's bounds) is
 *    released alone first, changing no head: when its notifier does not
 *    come, the channel's objects are wrong and the head is left as it is.
 *    (nouveau releases the start with the first commit; the separate update
 *    only splits the failure.)
 * 2. The disable: the head's output LUT and cursor off, its display id
 *    cleared and its output resource released (no owner), as nouveau's
 *    flush of a modeset does before the enable.
 * 3. The resource manager assigns an output resource to the display
 *    (nouveau's acquire under the GSP).
 * 4. The enable: the display id, the output resource's control (the owner
 *    head and the protocol), the head's mode (view, raster, clock, dither,
 *    processing, output resource), then every window to its head, released
 *    by the core alone (nouveau's assignment of the windows).
 *
 * Each update waits for the core's notifier; a missing one shows the
 * display's error state and fails.
 */
static int
scanout_take_head(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	struct nvrtx_push *push;
	uint32_t control;
	int error;

	display = &gpu->display;
	push = &display->core_push;

	/* 1: the core's start, released alone. */
	nvrtx_disp_core_init(push, display->window_mask);
	error = scanout_core_update(gpu, 0U, "core start");
	if (error != 0)
		return error;

	scanout_step("2: core start released");

	/* 2: the disable of the head and its output resource. */
	nvrtx_disp_core_head_planes_off(push, display->gop_head);
	nvrtx_disp_core_display_id(push, display->gop_head, 0U);
	nvrtx_disp_core_sor(push, display->gop_sor, 0U);
	error = scanout_core_update(gpu, 0U, "core disable");
	if (error != 0)
		return error;

	scanout_step("3: head disabled");

	/* 3: the output resource the resource manager assigns to the display. */
	error = scanout_assign_sor(gpu);
	if (error != 0)
		return error;

	/* 4: the enable, then the windows' owners, in one update of the core alone. */
	control = (display->gop_protocol << NVRTX_DISP_SOR_PROTOCOL_SHIFT) | (1U << display->gop_head);
	nvrtx_disp_core_display_id(push, display->gop_head, display->gop_display_id);
	nvrtx_disp_core_sor(push, display->assigned_sor, control);
	nvrtx_disp_core_head_mode(push, display->gop_head, &display->mode);
	nvrtx_disp_core_owners(push, display->window_mask);
	error = scanout_core_update(gpu, 0U, "core owners");
	if (error != 0)
		return error;

	scanout_step("4: head enabled");

	/* Succeeded: the core drives the head with the firmware's mode. */
	return 0;
}

/*
 * Releases what the core channel gathered, with its notifier, interlocked
 * with the windows in the mask, and waits for the notifier; a missing one
 * shows the display's error state.
 */
static int
scanout_core_update(
	struct nvrtx_gpu *gpu,
	uint32_t window_interlock,
	const char *what)
{
	struct nvrtx_display_state *display;
	int error;

	display = &gpu->display;

	/* Clears the notifier, then sends the update. */
	error = scanout_notifier_reset(gpu, SCANOUT_CORE_NOTIFIER_AT);
	if (error != 0)
		return error;

	nvrtx_disp_core_update(&display->core_push, SCANOUT_CORE_NOTIFIER_AT, window_interlock);
	error = scanout_kick(gpu, &display->core_push, SCANOUT_REG_CORE_USER);
	if (error != 0)
		return error;

	/* Waits for the core to finish it. */
	error = scanout_notifier_wait(gpu, SCANOUT_CORE_NOTIFIER_AT, NVRTX_DISP_NOTIFIER_FINISHED, SCANOUT_REG_CORE_USER, what);
	if (error != 0) {
		scanout_diagnose(gpu);
		return error;
	}

	/* Succeeded: the core has released the state. */
	return 0;
}

/*
 * Has the resource manager assign an output resource to the firmware's
 * display (nouveau's acquire of a SOR under the GSP) and keeps its index.
 */
static int
scanout_assign_sor(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	uint8_t params[NVRTX_DISP_ASSIGN_SOR_BYTES];
	const uint8_t *reply;
	size_t reply_length;
	int error;

	display = &gpu->display;

	/* Asks for any SOR (none excluded) for the display. */
	nvrtx_disp_assign_sor_params(params, display->gop_display_id, 0U);
	error = nvrtx_rm_control(gpu,
	    NVRTX_HANDLE_CLIENT,
	    SCANOUT_HANDLE_COMMON,
	    SCANOUT_CTL_ASSIGN_SOR,
	    params,
	    sizeof(params),
	    &reply,
	    &reply_length);
	if (error != 0) {
		nvrtx_stage_mark("P8b fail: SOR assignment (%d)", error);
		return error;
	}

	/* The reply names the SOR whose display mask has the display. */
	display->assigned_sor = NVRTX_DISP_SOR_NONE;
	if (reply_length >= NVRTX_DISP_ASSIGN_SOR_BYTES)
		display->assigned_sor = nvrtx_disp_assign_sor_result(reply, display->gop_display_id);
	if (display->assigned_sor == NVRTX_DISP_SOR_NONE) {
		nvrtx_stage_mark("P8b fail: no SOR assigned to display %x", (unsigned)display->gop_display_id);
		return ENODEV;
	}

	/* Succeeded: the display has its SOR. */
	nvrtx_stage_mark("P8b SOR %u assigned to display %x (the firmware's was %u)",
	    (unsigned)display->assigned_sor,
	    (unsigned)display->gop_display_id,
	    (unsigned)display->gop_sor);
	return 0;
}

/* Marks one step of the takeover; with the monitor test it waits, so the monitor can be watched at it. */
static void
scanout_step(
	const char *what)
{
	bool test;

	/* Only the mark without the test. */
	test = nvrtx_stage_disp_test_wanted();
	if (!test) {
		nvrtx_stage_mark("P8b step %s", what);
		return;
	}

	/* The mark, then the wait. */
	nvrtx_stage_mark("P8b step %s (5 s)", what);
	scanout_hold(SCANOUT_STEP_US);
}

/* Shows the display's error state after a notifier did not come. */
static void
scanout_diagnose(
	struct nvrtx_gpu *gpu)
{
	/* The core channel's exception: its status (method in bits 11:0, reason in 14:12), data and code. */
	nvrtx_stage_mark("P8b core exception %08x data %08x code %08x",
	    (unsigned)nvrtx_rd32(gpu, SCANOUT_REG_CORE_EXCEPTION),
	    (unsigned)nvrtx_rd32(gpu, SCANOUT_REG_CORE_EXCEPTION_DATA),
	    (unsigned)nvrtx_rd32(gpu, SCANOUT_REG_CORE_EXCEPTION_CODE));

	/* The other exceptions, the supervisor's pending steps and the core's state. */
	nvrtx_stage_mark("P8b other %08x supervisor %08x core state %08x",
	    (unsigned)nvrtx_rd32(gpu, SCANOUT_REG_EXCEPTION_OTHER),
	    (unsigned)nvrtx_rd32(gpu, SCANOUT_REG_SUPERVISOR),
	    (unsigned)nvrtx_rd32(gpu, SCANOUT_REG_CORE_STATE));
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
	error = scanout_core_update(gpu, 1U << display->window, "core show");
	if (error != 0)
		return error;

	/* Succeeded: the head shows the first surface. */
	nvrtx_stage_mark("P8b ok: mode set (%ux%u on SOR %u), surface A shown",
	    (unsigned)display->surfaces[0].width,
	    (unsigned)display->surfaces[0].height,
	    (unsigned)display->assigned_sor);
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
scanout_hold(
	uint32_t microseconds)
{
	uint64_t start;
	uint64_t now;

	/* Waits, measured, in short steps. */
	start = nvrtx_now_us();
	for (;;) {
		now = nvrtx_now_us();
		if (now - start >= microseconds)
			break;

		kern_usleep_range(SCANOUT_SHOW_STEP_US, SCANOUT_SHOW_STEP_US);
	}
}

/*
 * Waits until a channel's get reaches a byte offset of its push buffer (its
 * put): the channel has fetched every word it was given.
 */
static int
scanout_drained(
	struct nvrtx_gpu *gpu,
	uint32_t user,
	uint32_t put)
{
	uint64_t start;
	uint64_t waited;
	uint32_t get;

	/* Polls the get until it stands at the put or the time is spent. */
	start = nvrtx_now_us();
	for (;;) {
		get = nvrtx_rd32(gpu, user + SCANOUT_USER_GET_AT);
		if (get == put)
			break;

		/* The channel did not fetch its words in time: shows where it stopped. */
		waited = nvrtx_now_us() - start;
		if (waited > SCANOUT_DRAIN_US) {
			nvrtx_stage_mark("P8c fail: channel at %x stopped, get %x put %x", (unsigned)user, (unsigned)get, (unsigned)put);
			return ETIMEDOUT;
		}

		kern_usleep_range(SCANOUT_DRAIN_STEP_US, SCANOUT_DRAIN_STEP_US * 2U);
	}

	/* Succeeded: the channel has every word. */
	return 0;
}

/*
 * Makes room for words more of the core's methods: once the core has
 * fetched everything it was given, the buffer is wound back to its start
 * when the words would not fit before its end (nouveau's wind: a jump to
 * the start where the get stands, the put moved to the start, and the
 * channel left to take the jump), so nothing unfetched is written over.
 */
static int
scanout_core_room(
	struct nvrtx_gpu *gpu,
	uint32_t words)
{
	struct nvrtx_push *push;
	int error;

	push = &gpu->display.core_push;

	/* The core has fetched up to its put. */
	error = scanout_drained(gpu, SCANOUT_REG_CORE_USER, push->count * 4U);
	if (error != 0)
		return error;

	/* The words fit before the end, with a word left for a later jump. */
	if (push->count + words + 1U <= push->capacity)
		return 0;

	/* Jumps back to the start and moves the put there. */
	nvrtx_push_jump(push, 0U);
	if (push->overflow != 0)
		return E2BIG;

	push->count = 0U;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	nvrtx_wr32(gpu, SCANOUT_REG_CORE_USER + SCANOUT_USER_PUT_AT, 0U);

	/* Waits until the core has taken the jump. */
	error = scanout_drained(gpu, SCANOUT_REG_CORE_USER, 0U);
	if (error != 0)
		return error;

	/* Succeeded: the buffer is empty from its start. */
	return 0;
}

/*
 * Sends the core's update interlocked with the head's cursor, with the
 * core's notifier asked for and not waited for: core_pending is set until
 * nvrtx_scanout_core_settled() sees it finished.
 */
static int
scanout_core_send_cursor(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_display_state *display;
	int error;

	display = &gpu->display;

	/* Clears the notifier, then sends the update. */
	error = scanout_notifier_reset(gpu, SCANOUT_CORE_NOTIFIER_AT);
	if (error != 0)
		return error;

	nvrtx_disp_core_update_cursor(&display->core_push, SCANOUT_CORE_NOTIFIER_AT, display->gop_head);
	error = scanout_kick(gpu, &display->core_push, SCANOUT_REG_CORE_USER);
	if (error != 0)
		return error;

	/* Succeeded: the update is out; its notifier will say when it is done. */
	display->core_pending = 1;
	return 0;
}

/*
 * Writes one register of the head's cursor channel once the channel has
 * room for it (nouveau's curs507a_space: four free entries within 100 ms).
 */
static int
scanout_cursor_write(
	struct nvrtx_gpu *gpu,
	uint32_t method,
	uint32_t value)
{
	uint64_t start;
	uint64_t waited;
	uint32_t user;
	uint32_t free_entries;

	/* The head's own page of the cursor channels. */
	user = SCANOUT_REG_CURSOR_USER + gpu->display.gop_head * SCANOUT_CURSOR_USER_STRIDE;

	/* Polls the free entries until there are enough or the time is spent. */
	start = nvrtx_now_us();
	for (;;) {
		free_entries = nvrtx_rd32(gpu, user + NVRTX_CURSOR_FREE) & NVRTX_CURSOR_FREE_COUNT_MASK;
		if (free_entries >= SCANOUT_CURSOR_SPACE_LEAST)
			break;

		/* The channel did not make room in time. */
		waited = nvrtx_now_us() - start;
		if (waited > SCANOUT_CURSOR_SPACE_US) {
			nvrtx_stage_mark("P8c fail: cursor channel has %u free entries", (unsigned)free_entries);
			return ETIMEDOUT;
		}

		kern_usleep_range(SCANOUT_DRAIN_STEP_US, SCANOUT_DRAIN_STEP_US * 2U);
	}

	/* Writes the register. */
	nvrtx_wr32(gpu, user + method, value);

	/* Succeeded: the channel took the word. */
	return 0;
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
