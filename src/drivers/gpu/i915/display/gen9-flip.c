/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The flip display of a Gen9 node (WS237, stage 1).
 *
 * The display controller of Gen9 and Gen9.5 keeps the mode the firmware
 * set: nothing here enables a pipe, a port, a clock or a power well.  At
 * the binding of the boot framebuffer display (bootfb.c) the firmware's
 * display registers are read without writing any of them; when exactly one
 * pipe is lit and its first plane scans out a linear XRGB8888 surface with
 * no rotation, offset, asynchronous flip, PSR or DC state in the way, two
 * buffers of that plane's extent are made in the GGTT's display window.
 *
 * A presentation then waits until the flip before it has latched (the
 * plane's live surface is that flip's buffer), has the GPU copy the frame
 * into the other buffer, and writes that buffer's address into the plane's
 * surface register.  The write arms the plane's double-buffered registers,
 * which take effect at the start of the next vertical blank, so a frame is
 * never shown while it is being drawn and every frame is shown for at
 * least one refresh.  The plane's flip-done interrupt wakes the waiter;
 * the waiter also reads the live surface every tick, so a missing
 * interrupt makes the waits coarser but never wrong.
 *
 * The end of a lease points the plane back at the firmware's surface, so
 * the text console is on the screen again.  A flip that does not latch
 * within I915_GEN9_FLIP_LATCH_MS gives the flips up for the rest of the
 * boot: the firmware's surface is written back and bootfb.c copies every
 * frame into the firmware's framebuffer as it did before.
 *
 * Every function but the interrupt hook runs under the display's lease
 * mutex (bootfb.c), or before any session exists (the start).
 *
 * XXX: one device; the plane's control, stride and watermarks are the
 * firmware's, so only a mode the firmware set is shown.
 */

#include "internal.h"
#include "gen9-display.h"
#include "gen9-flip.h"
#include "scanout.h"
#include <kern/kcrt.h>

#include "../i915.h"
#include "../ggtt.h"
#include "../memory.h"
#include "../mmio.h"
#include "../perf.h"
#include "../ppgtt.h"
#include "../session.h"
#include "../sync.h"
#include "../render/blit.h"
#include "../intel/display-regs-gen9.h"

#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu-display.h>
#include <kern/clock.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/sched.h>

#include <uapi/errno.h>
#include <stddef.h>

/* How many frames one timing line of the log sums up. */
#define I915_SKL_FLIP_TIMING_FRAMES	600U

/* Nanoseconds in a microsecond, for the timing lines. */
#define I915_SKL_FLIP_NS_PER_US		1000U

/* The refresh range a measured timing is believed in (20 Hz to 250 Hz). */
#define I915_SKL_FLIP_REFRESH_MIN	20000U
#define I915_SKL_FLIP_REFRESH_MAX	250000U

/* The largest side the scanout buffers take. */
#define I915_SKL_FLIP_MAX_SIDE		8192U

/* The bytes of a GGTT page, and the multiple of pages the display window is claimed in. */
#define I915_SKL_FLIP_PAGE_BYTES	4096U
#define I915_SKL_FLIP_WINDOW_UNIT	32U

/*
 * The flip display of the node.
 *
 * Made by the start before any session exists and kept until the display
 * stops.  From then on its fields change only under the display's lease
 * mutex, except the interrupt count and the completion, which the
 * interrupt handler touches.
 */
struct i915_skl_flip {
	/* Nonzero while the flips are used: the readout allowed them and both buffers are pinned. */
	int ready;

	/* What the firmware's display is, and the plane's raw stride the firmware set. */
	struct i915_skl_readout readout;
	uint32_t firmware_stride;

	/* The device's registers and GT memory. */
	struct i915_mmio *mmio;
	struct i915_gt_mem *gm;

	/* The two buffers; the plane shows the firmware's surface, the armed buffer, or the other one. */
	struct i915_scanout buffer[2];
	int made[2];

	/* The address space that maps both buffers, and where; NULL while none does. */
	struct i915_ppgtt *map_vm;
	uint64_t map_va[2];

	/* The buffer the next frame is drawn into, and the surface last written (0: none since the claim). */
	unsigned back;
	uint32_t armed;

	/* The tick the newest flip was seen latched at. */
	uint64_t latched_tick;

	/* Nonzero while the plane's flip-done interrupt is enabled; the handler acknowledges it only then. */
	volatile int irq_on;

	/* Completed by the flip-done interrupt; a waiter sleeps on it a tick at a time. */
	struct i915_completion done;

	/* Flip-done interrupts seen, flips armed, and the frames' copy and wait times since the last timing line. */
	volatile uint64_t irq_count;
	uint64_t flips;
	uint64_t gpu_ns;
	uint64_t wait_ns;
	uint32_t timed;
};

/*
 * The flip display of the one device.
 *
 * Zero means no flips: the start fills it when the readout allows them,
 * and the stop or the display's end empties it again.
 */
static struct i915_skl_flip i915_skl_flip_state;

static void i915_skl_read(struct i915_mmio *mmio, struct i915_skl_registers *registers);
static void i915_skl_read_transcoder(struct i915_mmio *mmio, struct i915_skl_registers *registers, unsigned transcoder);
static void i915_skl_read_pipe(struct i915_mmio *mmio, struct i915_skl_registers *registers, unsigned pipe);
static void i915_skl_log_readout(const struct i915_skl_registers *registers, const struct i915_skl_readout *readout, const char *fb_match);
static int i915_skl_decode_plane(const struct i915_skl_registers *registers, struct i915_skl_readout *readout);
static void i915_skl_decode_refresh(const struct i915_skl_registers *registers, struct i915_skl_readout *readout);
static int i915_skl_transcoder_pipe(const struct i915_skl_registers *registers, unsigned transcoder, unsigned *pipe);
static int i915_skl_make_buffers(struct i915_skl_flip *flip);
static void i915_skl_free_buffers(struct i915_skl_flip *flip);
static int i915_skl_map(struct i915_device *device, struct i915_ppgtt *vm);
static void i915_skl_unmap(struct i915_device *device);
static int i915_skl_latch(struct i915_skl_flip *flip, uint32_t surf);
static void i915_skl_arm(struct i915_skl_flip *flip, uint32_t surf, uint32_t stride);
static int i915_skl_restore(struct i915_skl_flip *flip);
static void i915_skl_irq_enable(struct i915_skl_flip *flip);
static void i915_skl_irq_disable(struct i915_skl_flip *flip);
static void i915_skl_time(struct i915_skl_flip *flip, uint64_t gpu_ns, uint64_t wait_ns, uint32_t width, uint32_t height);
static uint32_t i915_skl_plane_reg(const struct i915_skl_flip *flip, uint32_t pipe_a_reg);
static const char *i915_skl_transcoder_name(unsigned transcoder);

/*
 * Decodes the firmware's display registers and decides whether the flips
 * can be used.
 *
 * Returns 0 when they can, with every field of the readout filled; else
 * EINVAL with readout->reason naming the first condition that failed and
 * the fields found so far.
 */
int
drv_i915_skl_readout_decode(
	const struct i915_skl_registers *registers,
	struct i915_skl_readout *readout)
{
	unsigned transcoder;
	unsigned pipe;
	unsigned lit_transcoder;
	int error;

	kern_memset(readout, 0, sizeof(*readout));
	readout->refresh_millihz = I915_GEN9_FLIP_REFRESH_ASSUMED;

	/* Pipe A, the eDP transcoder and the first DDI live in power well 1; without it nothing is lit. */
	if ((registers->power & I915_SKL_PW_1_STATE) == 0U) {
		readout->reason = "pw1-off";
		return EINVAL;
	}

	/* A DC state lets the DMC power the display down between frames, which this driver does not manage. */
	if ((registers->dc_state & I915_SKL_DC_STATE_MASK) != 0U) {
		readout->reason = "dc-state";
		return EINVAL;
	}

	/* Counts the pipes a running transcoder feeds and whose first plane is on, keeping the last one. */
	lit_transcoder = 0U;
	for (transcoder = 0U; transcoder < I915_SKL_TRANSCODERS; transcoder++) {
		error = i915_skl_transcoder_pipe(registers, transcoder, &pipe);
		if (error != 0)
			continue;

		/* A pipe whose registers were not read, or whose first plane is off, shows nothing of the firmware's. */
		if (!registers->pipe_read[pipe])
			continue;
		if ((registers->plane_ctl[pipe] & I915_SKL_PLANE_CTL_ENABLE) == 0U)
			continue;

		readout->lit_pipes++;
		readout->pipe = pipe;
		lit_transcoder = transcoder;
	}

	/* The flips take over exactly the one output the firmware shows. */
	if (readout->lit_pipes == 0U) {
		readout->reason = "no-lit-pipe";
		return EINVAL;
	}
	if (readout->lit_pipes > 1U) {
		readout->reason = "several-lit-pipes";
		return EINVAL;
	}

	readout->transcoder = lit_transcoder;

	/* The plane must scan out a surface the driver's buffers can stand in for. */
	error = i915_skl_decode_plane(registers, readout);
	if (error != 0)
		return error;

	/* A compressor caching the firmware's surface would need the invalidations this driver does not make. */
	if ((registers->fbc_ctl & I915_SKL_DPFC_CTL_EN) != 0U) {
		readout->reason = "fbc";
		return EINVAL;
	}

	/* A self-refreshing eDP panel would not see the flips without the PSR exits this driver does not make. */
	if (lit_transcoder == I915_SKL_TRANSCODER_EDP && (registers->psr_ctl & I915_SKL_EDP_PSR_ENABLE) != 0U) {
		readout->reason = "psr";
		return EINVAL;
	}

	/* The refresh the display reports, measured from the timing when it can be. */
	i915_skl_decode_refresh(registers, readout);

	/* Succeeded: the flips can stand in for the firmware's surface. */
	return 0;
}

/*
 * Reports the display window pages two buffers of the given size need.
 *
 * Each buffer takes its pages, a guard on each side and up to an
 * alignment's worth of skipped pages; the sum is rounded up to the
 * window's unit.
 */
unsigned
drv_i915_skl_flip_window_pages(
	uint32_t buffer_bytes)
{
	unsigned buffer_pages;
	unsigned pages;

	/* One buffer's pages, guards and alignment slack. */
	buffer_pages = (unsigned)((buffer_bytes + I915_SKL_FLIP_PAGE_BYTES - 1U) / I915_SKL_FLIP_PAGE_BYTES);
	pages = buffer_pages + 2U * I915_GEN9_FLIP_GUARD_PAGES + I915_GEN9_FLIP_ALIGN_PAGES;

	/* Two buffers, in whole units of the window's bitmap. */
	pages = 2U * pages;
	pages = (pages + I915_SKL_FLIP_WINDOW_UNIT - 1U) / I915_SKL_FLIP_WINDOW_UNIT * I915_SKL_FLIP_WINDOW_UNIT;

	/* Succeeded: the window holds both buffers wherever their alignment falls. */
	return pages;
}

/*
 * Reports the pixel clock in kHz of a DP link: link M times the port
 * clock over link N, rounded up, or zero when N is zero.
 */
uint32_t
drv_i915_skl_dp_pixel_khz(
	uint32_t link_m,
	uint32_t link_n,
	uint32_t port_khz)
{
	uint64_t product;
	uint64_t pixel;

	/* A link without an N carries no pixels the driver can count. */
	if (link_n == 0U)
		return 0U;

	/* The data rate scaled by M over N, rounded up as the reference does. */
	product = (uint64_t)link_m * (uint64_t)port_khz;
	pixel = (product + (uint64_t)link_n - 1U) / (uint64_t)link_n;

	/* A clock beyond 32 bits is not a timing the plane runs. */
	if (pixel > 0xffffffffULL)
		return 0U;

	/* Succeeded: the pixel clock. */
	return (uint32_t)pixel;
}

/*
 * Reports the port clock in kHz of a DPLL_CTRL1 link rate (the link
 * clock times two), or zero for a rate not known.
 */
uint32_t
drv_i915_skl_port_khz(
	unsigned link_rate)
{
	uint32_t link_khz;

	/* The link clock each rate names. */
	switch (link_rate) {
	case I915_SKL_LINK_RATE_810:
		link_khz = 81000U;
		break;
	case I915_SKL_LINK_RATE_1080:
		link_khz = 108000U;
		break;
	case I915_SKL_LINK_RATE_1350:
		link_khz = 135000U;
		break;
	case I915_SKL_LINK_RATE_1620:
		link_khz = 162000U;
		break;
	case I915_SKL_LINK_RATE_2160:
		link_khz = 216000U;
		break;
	case I915_SKL_LINK_RATE_2700:
		link_khz = 270000U;
		break;
	default:
		link_khz = 0U;
		break;
	}

	/* Succeeded: the port clock is twice the link clock. */
	return link_khz * 2U;
}

/*
 * Reads the firmware's display and makes the two buffers.
 *
 * fb_physical is the loader's framebuffer, compared with the plane's
 * surface for the log only.  Returns 0 when the flips are used, ENOTSUP
 * when the boot line or the default asks for copies, EINVAL when the
 * firmware's display cannot be flipped, or the window's or the buffers'
 * error.  Every outcome is logged.
 */
int
drv_i915_gen9_flip_start(
	struct i915_device *device,
	uint64_t fb_physical)
{
	struct i915_skl_flip *flip;
	struct i915_skl_registers registers;
	struct i915_gt *gt;
	const char *fb_match;
	unsigned pages;
	int level;
	int error;

	flip = &i915_skl_flip_state;
	gt = &device->gt;
	kern_memset(flip, 0, sizeof(*flip));

	/* Only a Gen9 node's firmware display is read. */
	if (gt->graphics_ver != 9U)
		return ENOTSUP;

	/* The boot line, or the default, chooses copies or flips. */
	level = drv_i915_gen9_display_level();
	if (level < I915_GEN9_DISPLAY_FLIP) {
		kern_logf("i915: gen9 display: copy (i915.gen9.display=%s)\n", drv_i915_gen9_display_name(level));
		return ENOTSUP;
	}

	/* Reads what the firmware left, without writing a register. */
	kern_memset(&registers, 0, sizeof(registers));
	i915_skl_read(&gt->mmio, &registers);

	/* Decides whether the plane can be flipped. */
	error = drv_i915_skl_readout_decode(&registers, &flip->readout);

	/* Says whether the loader's framebuffer is the plane's surface (it is not in a virtual machine). */
	fb_match = "other";
	if (fb_physical >= gt->gmadr_base && fb_physical - gt->gmadr_base == (uint64_t)flip->readout.surf)
		fb_match = "same";

	i915_skl_log_readout(&registers, &flip->readout, fb_match);

	/* Reports a display the flips cannot stand in for. */
	if (error != 0) {
		kern_logf("i915: gen9 display: flip not used (%s); the copy display is used\n", flip->readout.reason);
		kern_memset(flip, 0, sizeof(*flip));
		return error;
	}

	/* The device's registers and GT memory the flips use. */
	flip->mmio = &gt->mmio;
	flip->gm = &gt->mem;
	flip->firmware_stride = registers.plane_stride[flip->readout.pipe];

	/* Claims a display window for two buffers; one claimed before is used as it is. */
	pages = drv_i915_skl_flip_window_pages(flip->readout.stride_bytes * flip->readout.height);
	error = drv_i915_gt_display_window_init(flip->gm, pages);
	if (error != 0 && error != EBUSY) {
		kern_logf("i915: gen9 display: flip not used (the display window of %u pages could not be claimed: %d); the copy display is used\n",
		    pages,
		    error);
		kern_memset(flip, 0, sizeof(*flip));
		return error;
	}

	/* Makes and pins the two buffers. */
	error = i915_skl_make_buffers(flip);
	if (error != 0) {
		kern_logf("i915: gen9 display: flip not used (the buffers could not be made: %d); the copy display is used\n", error);
		i915_skl_free_buffers(flip);
		kern_memset(flip, 0, sizeof(*flip));
		return error;
	}

	/* The waiter's completion, which the flip-done interrupt signals. */
	drv_i915_completion_init(&flip->done, "i915 gen9 flip");

	/* The flips are used from the first presentation of a lease on. */
	flip->ready = 1;

	kern_logf("i915: gen9 display: flip on pipe %c, buffers at 0x%08x and 0x%08x (GGTT), the firmware's surface 0x%08x is restored on release\n",
	    'A' + (int)flip->readout.pipe,
	    (uint32_t)flip->buffer[0].surf,
	    (uint32_t)flip->buffer[1].surf,
	    flip->readout.surf);

	/* Succeeded: the node's desktop is shown by flips. */
	return 0;
}

/*
 * Reports the extent and the refresh the flips show: the firmware's
 * plane's.
 */
void
drv_i915_gen9_flip_extent(
	uint32_t *width,
	uint32_t *height,
	uint32_t *refresh_millihz)
{
	struct i915_skl_flip *flip;

	flip = &i915_skl_flip_state;

	/* The plane's extent and the timing's refresh. */
	*width = flip->readout.width;
	*height = flip->readout.height;
	*refresh_millihz = flip->readout.refresh_millihz;
}

/*
 * Readies the buffers and the flip interrupt for a new lease.
 *
 * Both buffers are cleared to black, so a frame smaller than the plane
 * leaves black around it, and the plane keeps showing the firmware's
 * surface until the lease's first presentation.
 */
void
drv_i915_gen9_flip_claim(
	struct i915_device *device)
{
	struct i915_skl_flip *flip;
	unsigned index;

	UNUSED_PARAMETER(device);

	flip = &i915_skl_flip_state;

	/* Without the flips there is nothing to ready. */
	if (!flip->ready)
		return;

	/* Clears both buffers and makes the black visible to the display engine. */
	for (index = 0U; index < 2U; index++) {
		kern_memset(flip->buffer[index].cpu, 0, flip->buffer[index].size);
		drv_i915_scanout_publish(&flip->buffer[index]);
	}

	/* The first frame goes into buffer 0, with no flip armed before it. */
	flip->back = 0U;
	flip->armed = 0U;

	/* Lets the plane's flip-done interrupt wake the waits. */
	i915_skl_irq_enable(flip);
}

/*
 * Draws a frame into the buffer the plane does not show and flips to it.
 *
 * The flip armed by the previous presentation must latch first, so the
 * buffer drawn into is shown by nothing.  The flip armed here latches at
 * the next vertical blank; the next presentation, or a wait, waits for
 * it.  Returns 0, EINVAL for a session without an executor or a frame not
 * in its address space, ETIMEDOUT when the previous flip did not latch,
 * or the mapping's or the copy's error.
 */
int
drv_i915_gen9_flip_present(
	struct i915_device *device,
	void *session,
	void *object,
	const struct gpu_display_present *request)
{
	struct i915_skl_flip *flip;
	struct i915_session *owner;
	struct i915_gem_object *storage;
	struct i915_scanout *back;
	struct i915_gfx_surface src;
	struct i915_gfx_surface dst;
	struct i915_gfx_rect rect;
	uint64_t started;
	uint64_t waited;
	uint64_t copied;
	int error;

	flip = &i915_skl_flip_state;
	owner = session;
	storage = object;

	/* Without the flips, or without an executor or a frame in the session's space, nothing can be flipped. */
	if (!flip->ready)
		return EINVAL;
	if (owner->vk == NULL)
		return EINVAL;
	if (storage->va == 0U)
		return EINVAL;
	if (request->width == 0U || request->height == 0U)
		return EINVAL;

	/* Makes both buffers addressable in the session's space. */
	error = i915_skl_map(device, owner->vm);
	if (error != 0)
		return error;

	/* Waits until the plane shows the flip armed before, so the other buffer is free. */
	started = drv_i915_perf_now();
	if (flip->armed != 0U) {
		error = i915_skl_latch(flip, flip->armed);
		if (error != 0)
			return error;
	}

	waited = drv_i915_perf_now();

	/* The frame as the GPU sees it, in the order it was presented. */
	src.va = storage->va + request->offset;
	src.width = request->width;
	src.height = request->height;
	src.pitch = request->stride;
	src.format = VK_FORMAT_R8G8B8A8_UNORM;
	if (request->format == GPU_PIXEL_BGRA8888)
		src.format = VK_FORMAT_B8G8R8A8_UNORM;
	src.tiled = 0U;

	/* The buffer the plane does not show, in the plane's byte order. */
	back = &flip->buffer[flip->back];
	dst.va = flip->map_va[flip->back];
	dst.width = back->width;
	dst.height = back->height;
	dst.pitch = back->pitch;
	dst.format = VK_FORMAT_B8G8R8A8_UNORM;
	if (flip->readout.rgbx)
		dst.format = VK_FORMAT_R8G8B8A8_UNORM;
	dst.tiled = 0U;

	/* The frame's top-left part the plane has room for, unscaled. */
	rect.x = 0;
	rect.y = 0;
	rect.w = request->width;
	if (rect.w > back->width)
		rect.w = back->width;
	rect.h = request->height;
	if (rect.h > back->height)
		rect.h = back->height;

	/* Copies it and waits until the copy has reached memory. */
	error = drv_i915_gfx_rect(owner->vk, &dst, &rect, &src, &rect, NULL, 0);
	if (error != 0)
		return error;

	copied = drv_i915_perf_now();

	/* Points the plane at the buffer (its pin keeps the address within 32 bits); it latches at the next vertical blank. */
	i915_skl_arm(flip, (uint32_t)back->surf, back->stride_units);
	flip->armed = (uint32_t)back->surf;
	flip->back ^= 1U;
	flip->flips++;

	/* How long the wait and the copy took, in the log now and then. */
	i915_skl_time(flip, copied - waited, waited - started, rect.w, rect.h);

	/* Succeeded: the frame's flip is armed. */
	return 0;
}

/*
 * Waits until the newest flip has latched and reports the tick it was
 * seen latched at.
 *
 * Returns 0 (also when no flip is armed: the last latch is reported), or
 * ETIMEDOUT when the flip did not latch.
 */
int
drv_i915_gen9_flip_wait(
	uint64_t *latched_tick)
{
	struct i915_skl_flip *flip;
	int error;

	flip = &i915_skl_flip_state;

	/* Waits for the armed flip, if any. */
	if (flip->ready && flip->armed != 0U) {
		error = i915_skl_latch(flip, flip->armed);
		if (error != 0)
			return error;
	}

	/* Succeeded: the tick the newest flip latched at. */
	*latched_tick = flip->latched_tick;
	return 0;
}

/*
 * Gives the plane back the firmware's surface at the end of a lease.
 *
 * The buffers stay pinned for the next lease; their mapping in the
 * session's space goes.  Returns 0, or ETIMEDOUT when the surface did not
 * come back: that is logged and the flips are given up.
 */
int
drv_i915_gen9_flip_release(
	struct i915_device *device)
{
	struct i915_skl_flip *flip;
	int error;

	flip = &i915_skl_flip_state;

	/* Without the flips the plane never left the firmware's surface. */
	if (!flip->ready)
		return 0;

	/* Shows the firmware's surface again, once it is on the plane. */
	error = i915_skl_restore(flip);

	/* The waits need the interrupt no longer, and the session's space no longer names the buffers. */
	i915_skl_irq_disable(flip);
	i915_skl_unmap(device);

	/* A plane that kept a buffer of ours ends the flips; the buffers are kept. */
	if (error != 0) {
		kern_logf("i915: gen9 display: the firmware's surface 0x%08x did not come back; the flips end\n", flip->readout.surf);
		flip->ready = 0;
		return error;
	}

	kern_logf("i915: gen9 display: %llu flip(s), %llu flip-done interrupt(s); the firmware's surface is back\n",
	    (unsigned long long)flip->flips,
	    (unsigned long long)flip->irq_count);

	/* Succeeded: the firmware's surface is on the screen. */
	return 0;
}

/*
 * Gives the flips up for the rest of the boot after a flip did not latch.
 *
 * The firmware's surface is written back, the interrupt is disabled and
 * the mapping goes; the buffers are kept, since the plane may still read
 * one of them.
 */
void
drv_i915_gen9_flip_stop(
	struct i915_device *device)
{
	struct i915_skl_flip *flip;
	uint32_t live;
	int error;

	flip = &i915_skl_flip_state;

	/* Without the flips there is nothing to give up. */
	if (!flip->ready)
		return;

	/* Writes the firmware's surface back and gives it the same time to latch. */
	live = drv_i915_raw_read32(flip->mmio, i915_skl_plane_reg(flip, I915_SKL_PLANE_SURFLIVE_1_A)) & I915_SKL_PLANE_SURF_ADDR_MASK;
	error = i915_skl_restore(flip);

	/* No more waits, and no more mapping in the session's space. */
	i915_skl_irq_disable(flip);
	i915_skl_unmap(device);
	flip->ready = 0;

	kern_logf("i915: gen9 display: flip to 0x%08x did not latch in %u ms (live 0x%08x); the firmware's surface is %s and the copy display is used from now on\n",
	    flip->armed,
	    I915_GEN9_FLIP_LATCH_MS,
	    live,
	    (error == 0) ? "back" : "NOT back");
}

/*
 * Frees the buffers once the plane is shown not to read them.
 *
 * A buffer the plane still shows, or may show, is abandoned instead: its
 * pages and GGTT entries are kept for ever.
 */
void
drv_i915_gen9_flip_fini(
	struct i915_device *device)
{
	struct i915_skl_flip *flip;
	uint32_t live;
	unsigned index;

	UNUSED_PARAMETER(device);

	flip = &i915_skl_flip_state;

	/* Nothing was made. */
	if (flip->mmio == NULL)
		return;

	/* The interrupt goes first. */
	i915_skl_irq_disable(flip);

	/* Keeps a buffer the plane shows; frees the others. */
	live = drv_i915_raw_read32(flip->mmio, i915_skl_plane_reg(flip, I915_SKL_PLANE_SURFLIVE_1_A)) & I915_SKL_PLANE_SURF_ADDR_MASK;
	for (index = 0U; index < 2U; index++) {
		if (flip->made[index] && flip->buffer[index].surf == (uint64_t)live) {
			kern_logf("i915: gen9 display: XXX buffer %u is still on the plane at stop; it is kept\n", index);
			drv_i915_scanout_abandon(&flip->buffer[index]);
			flip->made[index] = 0;
		}
	}

	i915_skl_free_buffers(flip);

	/* Forgets the flips. */
	kern_memset(flip, 0, sizeof(*flip));
}

/*
 * Acknowledges the flip's pipe interrupt and wakes the waiter.
 *
 * Runs in the interrupt handler with the master interrupt control off.
 * Only the flip pipe's sources are read; the handler does nothing while
 * the interrupt is not enabled.
 */
void
drv_i915_gen9_flip_irq(
	uint32_t master_ctl)
{
	struct i915_skl_flip *flip;
	uint32_t pipe_bit;
	uint32_t iir_reg;
	uint32_t iir;

	flip = &i915_skl_flip_state;

	/* Only an enabled flip interrupt is served. */
	if (!flip->irq_on)
		return;

	/* The master control names the flip pipe's sources. */
	pipe_bit = I915_SKL_MASTER_DE_PIPE_A << flip->readout.pipe;
	if ((master_ctl & pipe_bit) == 0U)
		return;

	/* Reads and acknowledges what the pipe reports. */
	iir_reg = I915_SKL_DE_PIPE_IIR_A + flip->readout.pipe * I915_SKL_DE_PIPE_IRQ_STRIDE;
	iir = drv_i915_raw_read32(flip->mmio, iir_reg);
	if (iir == 0U)
		return;

	drv_i915_raw_write32(flip->mmio, iir_reg, iir);

	/* A flip latched: the waiter reads the live surface now. */
	if ((iir & I915_SKL_DE_PIPE_PLANE1_FLIP_DONE) != 0U) {
		flip->irq_count++;
		drv_i915_complete(&flip->done);
	}
}

/*
 * Reads the firmware's display registers, each only while its power well
 * is up.  Nothing is written.
 */
static void
i915_skl_read(
	struct i915_mmio *mmio,
	struct i915_skl_registers *registers)
{
	unsigned transcoder;
	unsigned pipe;

	/* The power wells decide what else can be read. */
	registers->power = drv_i915_raw_read32(mmio, I915_SKL_PWR_WELL_CTL2);
	if ((registers->power & I915_SKL_PW_1_STATE) == 0U)
		return;

	/* The DC states, the CD clock, the DPLL controls and the compressor, which power well 1 keeps. */
	registers->dc_state = drv_i915_raw_read32(mmio, I915_SKL_DC_STATE_EN);
	registers->cdclk = drv_i915_raw_read32(mmio, I915_SKL_CDCLK_CTL);
	registers->dpll_ctrl1 = drv_i915_raw_read32(mmio, I915_SKL_DPLL_CTRL1);
	registers->dpll_ctrl2 = drv_i915_raw_read32(mmio, I915_SKL_DPLL_CTRL2);
	registers->fbc_ctl = drv_i915_raw_read32(mmio, I915_SKL_DPFC_CONTROL);

	/* The eDP transcoder and pipe A live in power well 1. */
	i915_skl_read_transcoder(mmio, registers, I915_SKL_TRANSCODER_EDP);
	registers->psr_ctl = drv_i915_raw_read32(mmio, I915_SKL_EDP_PSR_CTL);
	i915_skl_read_pipe(mmio, registers, 0U);

	/* Transcoders A to C and pipes B and C live in power well 2. */
	if ((registers->power & I915_SKL_PW_2_STATE) == 0U)
		return;

	for (transcoder = 0U; transcoder < I915_SKL_TRANSCODER_EDP; transcoder++)
		i915_skl_read_transcoder(mmio, registers, transcoder);

	for (pipe = 1U; pipe < I915_SKL_PIPES; pipe++)
		i915_skl_read_pipe(mmio, registers, pipe);
}

/* Reads one transcoder's DDI function, configuration, totals and DP link M and N. */
static void
i915_skl_read_transcoder(
	struct i915_mmio *mmio,
	struct i915_skl_registers *registers,
	unsigned transcoder)
{
	uint32_t offset;

	/* The eDP transcoder's registers sit at their own places; A to C are a stride apart. */
	if (transcoder == I915_SKL_TRANSCODER_EDP) {
		registers->ddi_func[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_TRANS_DDI_FUNC_CTL_EDP);
		registers->transconf[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_TRANSCONF_EDP);
		registers->htotal[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_TRANS_HTOTAL_EDP);
		registers->vtotal[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_TRANS_VTOTAL_EDP);
		registers->link_m[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_LINK_M1_EDP);
		registers->link_n[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_LINK_N1_EDP);
	} else {
		offset = transcoder * I915_SKL_TRANSCODER_STRIDE;
		registers->ddi_func[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_TRANS_DDI_FUNC_CTL_A + offset);
		registers->transconf[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_TRANSCONF_A + offset);
		registers->htotal[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_TRANS_HTOTAL_A + offset);
		registers->vtotal[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_TRANS_VTOTAL_A + offset);
		registers->link_m[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_LINK_M1_A + offset);
		registers->link_n[transcoder] = drv_i915_raw_read32(mmio, I915_SKL_LINK_N1_A + offset);
	}

	registers->transcoder_read[transcoder] = 1;
}

/* Reads one pipe's source size, first plane and scalers. */
static void
i915_skl_read_pipe(
	struct i915_mmio *mmio,
	struct i915_skl_registers *registers,
	unsigned pipe)
{
	uint32_t offset;
	uint32_t scaler_offset;

	offset = pipe * I915_SKL_PIPE_STRIDE;
	scaler_offset = pipe * I915_SKL_PS_PIPE_STRIDE;

	/* The pipe's source size. */
	registers->pipe_src[pipe] = drv_i915_raw_read32(mmio, I915_SKL_PIPESRC_A + offset);

	/* The first plane's control, layout and surfaces. */
	registers->plane_ctl[pipe] = drv_i915_raw_read32(mmio, I915_SKL_PLANE_CTL_1_A + offset);
	registers->plane_stride[pipe] = drv_i915_raw_read32(mmio, I915_SKL_PLANE_STRIDE_1_A + offset);
	registers->plane_pos[pipe] = drv_i915_raw_read32(mmio, I915_SKL_PLANE_POS_1_A + offset);
	registers->plane_size[pipe] = drv_i915_raw_read32(mmio, I915_SKL_PLANE_SIZE_1_A + offset);
	registers->plane_surf[pipe] = drv_i915_raw_read32(mmio, I915_SKL_PLANE_SURF_1_A + offset);
	registers->plane_offset[pipe] = drv_i915_raw_read32(mmio, I915_SKL_PLANE_OFFSET_1_A + offset);
	registers->plane_surflive[pipe] = drv_i915_raw_read32(mmio, I915_SKL_PLANE_SURFLIVE_1_A + offset);

	/* The pipe's two scalers. */
	registers->scaler[pipe][0] = drv_i915_raw_read32(mmio, I915_SKL_PS_1A_CTRL + scaler_offset);
	registers->scaler[pipe][1] = drv_i915_raw_read32(mmio, I915_SKL_PS_2A_CTRL + scaler_offset);

	registers->pipe_read[pipe] = 1;
}

/* Logs the registers read and what the readout made of them. */
static void
i915_skl_log_readout(
	const struct i915_skl_registers *registers,
	const struct i915_skl_readout *readout,
	const char *fb_match)
{
	unsigned pipe;
	unsigned transcoder;

	/* The raw registers, for the photograph of a failure. */
	kern_logf("i915: gen9 display: PWR_WELL_CTL2=0x%08x DC_STATE_EN=0x%08x CDCLK_CTL=0x%08x DPLL_CTRL1=0x%08x DPLL_CTRL2=0x%08x PSR_CTL_EDP=0x%08x DPFC_CONTROL=0x%08x\n",
	    registers->power,
	    registers->dc_state,
	    registers->cdclk,
	    registers->dpll_ctrl1,
	    registers->dpll_ctrl2,
	    registers->psr_ctl,
	    registers->fbc_ctl);

	/* Every transcoder that was read. */
	for (transcoder = 0U; transcoder < I915_SKL_TRANSCODERS; transcoder++) {
		if (!registers->transcoder_read[transcoder])
			continue;

		kern_logf("i915: gen9 display: transcoder %s DDI_FUNC_CTL=0x%08x TRANSCONF=0x%08x HTOTAL=0x%08x VTOTAL=0x%08x LINK_M1=0x%08x LINK_N1=0x%08x\n",
		    i915_skl_transcoder_name(transcoder),
		    registers->ddi_func[transcoder],
		    registers->transconf[transcoder],
		    registers->htotal[transcoder],
		    registers->vtotal[transcoder],
		    registers->link_m[transcoder],
		    registers->link_n[transcoder]);
	}

	/* Every pipe that was read. */
	for (pipe = 0U; pipe < I915_SKL_PIPES; pipe++) {
		if (!registers->pipe_read[pipe])
			continue;

		kern_logf("i915: gen9 display: pipe %c PIPESRC=0x%08x PLANE_CTL=0x%08x STRIDE=0x%08x POS=0x%08x SIZE=0x%08x SURF=0x%08x OFFSET=0x%08x SURFLIVE=0x%08x PS=0x%08x/0x%08x\n",
		    'A' + (int)pipe,
		    registers->pipe_src[pipe],
		    registers->plane_ctl[pipe],
		    registers->plane_stride[pipe],
		    registers->plane_pos[pipe],
		    registers->plane_size[pipe],
		    registers->plane_surf[pipe],
		    registers->plane_offset[pipe],
		    registers->plane_surflive[pipe],
		    registers->scaler[pipe][0],
		    registers->scaler[pipe][1]);
	}

	/* What the readout found: a pipe lit by the firmware, or none. */
	if (readout->lit_pipes == 0U) {
		kern_logf("i915: gen9 display: readout: no pipe lit by the firmware\n");
		return;
	}

	kern_logf("i915: gen9 display: readout pipe %c transcoder %s %ux%u stride %u %s surf 0x%08x refresh %u mHz (%s, pixel %u kHz) scaler %s fb=%s\n",
	    'A' + (int)readout->pipe,
	    i915_skl_transcoder_name(readout->transcoder),
	    readout->width,
	    readout->height,
	    readout->stride_bytes,
	    readout->rgbx ? "XBGR8888" : "XRGB8888",
	    readout->surf,
	    readout->refresh_millihz,
	    readout->refresh_measured ? "measured" : "assumed",
	    readout->pixel_khz,
	    readout->scaled ? "on" : "off",
	    fb_match);
}

/*
 * Decodes the lit pipe's first plane into the readout and checks that the
 * driver's buffers can stand in for its surface.
 *
 * Returns 0, or EINVAL with the reason.
 */
static int
i915_skl_decode_plane(
	const struct i915_skl_registers *registers,
	struct i915_skl_readout *readout)
{
	uint32_t control;
	uint32_t format;
	uint32_t tiling;
	uint32_t live;
	unsigned pipe;

	pipe = readout->pipe;
	control = registers->plane_ctl[pipe];

	/* The plane's extent, row bytes, byte order and surface, as found. */
	readout->width = (registers->plane_size[pipe] & I915_SKL_PLANE_SIZE_FIELD_MASK) + 1U;
	readout->height = ((registers->plane_size[pipe] >> I915_SKL_PLANE_SIZE_HEIGHT_SHIFT) & I915_SKL_PLANE_SIZE_FIELD_MASK) + 1U;
	readout->stride_bytes = (registers->plane_stride[pipe] & I915_SKL_PLANE_STRIDE_MASK) * I915_SKL_PLANE_STRIDE_UNIT;
	readout->rgbx = 0;
	if ((control & I915_SKL_PLANE_CTL_ORDER_RGBX) != 0U)
		readout->rgbx = 1;
	readout->surf = registers->plane_surf[pipe] & I915_SKL_PLANE_SURF_ADDR_MASK;

	/* A scaler on the pipe stretches the plane to the panel; the flips keep it as the firmware set it. */
	if ((registers->scaler[pipe][0] & I915_SKL_PS_SCALER_EN) != 0U)
		readout->scaled = 1;
	if ((registers->scaler[pipe][1] & I915_SKL_PS_SCALER_EN) != 0U)
		readout->scaled = 1;

	/* The buffers are 32-bit RGB of either order. */
	format = (control >> I915_SKL_PLANE_CTL_FORMAT_SHIFT) & I915_SKL_PLANE_CTL_FORMAT_MASK;
	if (format != I915_SKL_PLANE_CTL_FORMAT_XRGB_8888) {
		readout->reason = "format";
		return EINVAL;
	}

	/* The buffers are linear. */
	tiling = (control >> I915_SKL_PLANE_CTL_TILED_SHIFT) & I915_SKL_PLANE_CTL_TILED_MASK;
	if (tiling != I915_SKL_PLANE_CTL_TILED_LINEAR) {
		readout->reason = "tiled";
		return EINVAL;
	}

	/* The frames are drawn upright. */
	if ((control & I915_SKL_PLANE_CTL_ROTATE_MASK) != 0U) {
		readout->reason = "rotated";
		return EINVAL;
	}

	/* An asynchronous flip would not wait for the vertical blank. */
	if ((control & I915_SKL_PLANE_CTL_ASYNC_FLIP) != 0U) {
		readout->reason = "async";
		return EINVAL;
	}

	/* The plane reads each buffer from its first pixel. */
	if (registers->plane_offset[pipe] != 0U) {
		readout->reason = "offset";
		return EINVAL;
	}

	/* The buffers take a size the scanout buffers can have, and the rows the plane reads fit its stride. */
	if (readout->width > I915_SKL_FLIP_MAX_SIDE || readout->height > I915_SKL_FLIP_MAX_SIDE) {
		readout->reason = "size";
		return EINVAL;
	}
	if (readout->stride_bytes < readout->width * 4U) {
		readout->reason = "stride";
		return EINVAL;
	}

	/* A linear surface of display 9 starts on a 256 KiB boundary. */
	if ((readout->surf % I915_SKL_LINEAR_ALIGNMENT) != 0U) {
		readout->reason = "surf-align";
		return EINVAL;
	}

	/* A flip of the firmware's own that has not latched would race the first of the driver's. */
	live = registers->plane_surflive[pipe] & I915_SKL_PLANE_SURF_ADDR_MASK;
	if (live != readout->surf) {
		readout->reason = "surf-pending";
		return EINVAL;
	}

	/* Succeeded: the plane can be pointed at the driver's buffers. */
	return 0;
}

/*
 * Measures the refresh of the lit transcoder from its DP timing: the pixel
 * clock from the link M and N and the DPLL's port clock, over the totals.
 *
 * A transcoder that is not DP, a DDI clock not chosen by DPLL_CTRL2, an
 * HDMI-mode DPLL or a refresh out of range leaves the assumed 60 Hz.
 */
static void
i915_skl_decode_refresh(
	const struct i915_skl_registers *registers,
	struct i915_skl_readout *readout)
{
	unsigned transcoder;
	uint32_t ddi_func;
	uint32_t mode;
	uint32_t port;
	uint32_t dpll;
	uint32_t rate;
	uint32_t port_khz;
	uint32_t htotal;
	uint32_t vtotal;
	uint64_t millihz;

	transcoder = readout->transcoder;
	ddi_func = registers->ddi_func[transcoder];

	/* Only a DP transcoder's clock follows from its link M and N. */
	mode = (ddi_func >> I915_SKL_TRANS_DDI_MODE_SHIFT) & I915_SKL_TRANS_DDI_MODE_MASK;
	if (mode != I915_SKL_TRANS_DDI_MODE_DP_SST && mode != I915_SKL_TRANS_DDI_MODE_DP_MST)
		return;

	/* The DDI port's clock comes from the DPLL DPLL_CTRL2 selects, when it overrides the default. */
	port = (ddi_func >> I915_SKL_TRANS_DDI_PORT_SHIFT) & I915_SKL_TRANS_DDI_PORT_MASK;
	if ((registers->dpll_ctrl2 & (1U << (port * I915_SKL_DPLL_CTRL2_BITS))) == 0U)
		return;

	dpll = (registers->dpll_ctrl2 >> (port * I915_SKL_DPLL_CTRL2_BITS + I915_SKL_DPLL_CTRL2_SELECT_SHIFT)) & I915_SKL_DPLL_CTRL2_SELECT_MASK;

	/* An HDMI-mode DPLL has no fixed link rate. */
	if ((registers->dpll_ctrl1 & (1U << (dpll * I915_SKL_DPLL_CTRL1_BITS + I915_SKL_DPLL_CTRL1_HDMI_SHIFT))) != 0U)
		return;

	/* The DPLL's link rate gives the port clock. */
	rate = (registers->dpll_ctrl1 >> (dpll * I915_SKL_DPLL_CTRL1_BITS + I915_SKL_DPLL_CTRL1_RATE_SHIFT)) & I915_SKL_DPLL_CTRL1_RATE_MASK;
	port_khz = drv_i915_skl_port_khz(rate);
	if (port_khz == 0U)
		return;

	/* The pixel clock the link carries. */
	readout->pixel_khz = drv_i915_skl_dp_pixel_khz(registers->link_m[transcoder] & I915_SKL_LINK_MN_MASK, registers->link_n[transcoder] & I915_SKL_LINK_MN_MASK, port_khz);
	if (readout->pixel_khz == 0U)
		return;

	/* The frame's totals. */
	htotal = ((registers->htotal[transcoder] >> I915_SKL_TIMING_TOTAL_SHIFT) & I915_SKL_TIMING_FIELD_MASK) + 1U;
	vtotal = ((registers->vtotal[transcoder] >> I915_SKL_TIMING_TOTAL_SHIFT) & I915_SKL_TIMING_FIELD_MASK) + 1U;

	/* The frames a second, in millihertz, rounded to the nearest. */
	millihz = ((uint64_t)readout->pixel_khz * 1000000U + ((uint64_t)htotal * vtotal) / 2U) / ((uint64_t)htotal * vtotal);

	/* A refresh no panel runs at means a register was misread. */
	if (millihz < I915_SKL_FLIP_REFRESH_MIN || millihz > I915_SKL_FLIP_REFRESH_MAX)
		return;

	readout->refresh_millihz = (uint32_t)millihz;
	readout->refresh_measured = 1;
}

/*
 * Finds the pipe a running transcoder feeds.
 *
 * Returns 0 with the pipe, or ENOENT for a transcoder that was not read,
 * is off, or names no pipe.
 */
static int
i915_skl_transcoder_pipe(
	const struct i915_skl_registers *registers,
	unsigned transcoder,
	unsigned *pipe)
{
	uint32_t input;

	/* A transcoder not read, without a DDI function, or not running feeds nothing. */
	if (!registers->transcoder_read[transcoder])
		return ENOENT;
	if ((registers->ddi_func[transcoder] & I915_SKL_TRANS_DDI_FUNC_ENABLE) == 0U)
		return ENOENT;
	if ((registers->transconf[transcoder] & I915_SKL_TRANSCONF_ENABLE) == 0U)
		return ENOENT;
	if ((registers->transconf[transcoder] & I915_SKL_TRANSCONF_STATE) == 0U)
		return ENOENT;

	/* Transcoders A to C feed their own pipes. */
	if (transcoder != I915_SKL_TRANSCODER_EDP) {
		*pipe = transcoder;
		return 0;
	}

	/* The eDP transcoder feeds the pipe its input select names. */
	input = (registers->ddi_func[transcoder] >> I915_SKL_TRANS_DDI_EDP_INPUT_SHIFT) & I915_SKL_TRANS_DDI_EDP_INPUT_MASK;
	if (input == I915_SKL_TRANS_DDI_EDP_INPUT_A_ON || input == I915_SKL_TRANS_DDI_EDP_INPUT_A_ONOFF) {
		*pipe = 0U;
	} else if (input == I915_SKL_TRANS_DDI_EDP_INPUT_B_ONOFF) {
		*pipe = 1U;
	} else if (input == I915_SKL_TRANS_DDI_EDP_INPUT_C_ONOFF) {
		*pipe = 2U;
	} else {
		return ENOENT;
	}

	/* Succeeded: the pipe the transcoder feeds. */
	return 0;
}

/* Makes and pins the two buffers of the plane's extent; the caller frees what was made on failure. */
static int
i915_skl_make_buffers(
	struct i915_skl_flip *flip)
{
	unsigned index;
	int error;

	/* Each buffer: its backing and its GGTT range. */
	for (index = 0U; index < 2U; index++) {
		error = drv_i915_scanout_create(flip->gm, flip->readout.width, flip->readout.height, I915_FOURCC_XRGB8888, I915_MOD_LINEAR, &flip->buffer[index]);
		if (error != 0)
			return error;

		flip->made[index] = 1;

		error = drv_i915_scanout_pin(&flip->buffer[index], "gen9 flip");
		if (error != 0)
			return error;
	}

	/* Succeeded: both buffers have a surface address. */
	return 0;
}

/* Unpins and destroys the buffers made, leaving the abandoned ones alone. */
static void
i915_skl_free_buffers(
	struct i915_skl_flip *flip)
{
	unsigned index;
	int error;

	/* Each buffer made: the GGTT range, then the backing. */
	for (index = 0U; index < 2U; index++) {
		if (!flip->made[index])
			continue;

		/* A buffer that was never pinned has no range to give back. */
		if (flip->buffer[index].state >= I915_SCANOUT_PINNED) {
			error = drv_i915_scanout_unpin(&flip->buffer[index]);
			if (error != 0) {
				kern_logf("i915: gen9 display: XXX buffer %u could not be unpinned: %d; it is kept\n", index, error);
				continue;
			}
		}

		error = drv_i915_scanout_destroy(&flip->buffer[index]);
		if (error != 0)
			kern_logf("i915: gen9 display: XXX buffer %u could not be destroyed: %d\n", index, error);

		flip->made[index] = 0;
	}
}

/*
 * Maps both buffers uncached into a presenting session's address space.
 *
 * One address space holds them at a time, from the first presentation of
 * a lease to its end: the same one again is a no-op, another is refused
 * with EBUSY.  Returns 0, or the address space's error.
 */
static int
i915_skl_map(
	struct i915_device *device,
	struct i915_ppgtt *vm)
{
	struct i915_skl_flip *flip;
	struct i915_gt_object *object;
	uint64_t va;
	uint64_t dma;
	unsigned index;
	unsigned page;
	int error;

	flip = &i915_skl_flip_state;

	/* The buffers are already in this address space. */
	if (flip->map_vm == vm)
		return 0;

	/* One presenting address space at a time. */
	if (flip->map_vm != NULL)
		return EBUSY;

	/* Maps each buffer under the device mutex, which guards the address spaces. */
	mutex_lock(&device->mutex);

	error = 0;
	for (index = 0U; index < 2U && error == 0; index++) {
		object = flip->buffer[index].obj;

		/* Reserves a range as large as the buffer; ranges are never reused. */
		error = drv_i915_ppgtt_va_alloc(vm, (uint64_t)object->pages * I915_SKL_FLIP_PAGE_BYTES, &va);
		if (error != 0)
			break;

		/* Maps every page uncached, in order, so the GPU's writes reach the memory the plane reads. */
		for (page = 0U; error == 0 && page < object->pages; page++) {
			error = drv_i915_gt_object_page_dma(object, page, &dma);
			if (error != 0) {
				error = EIO;
				break;
			}

			error = drv_i915_ppgtt_insert_uncached(vm, va + (uint64_t)page * I915_SKL_FLIP_PAGE_BYTES, dma, 1U);
		}

		flip->map_va[index] = va;
	}

	/* A buffer that could not be mapped is pointed back at scratch with the ones before it. */
	if (error != 0) {
		while (index > 0U) {
			index--;
			if (flip->map_va[index] != 0U)
				drv_i915_ppgtt_clear(vm, flip->map_va[index], flip->buffer[index].obj->pages);
			flip->map_va[index] = 0U;
		}

		mutex_unlock(&device->mutex);
		return error;
	}

	/* The address space holds the buffers from here on. */
	flip->map_vm = vm;

	mutex_unlock(&device->mutex);

	/* Succeeded: the GPU can draw into both buffers. */
	return 0;
}

/*
 * Unmaps both buffers from the address space that holds them.
 *
 * XXX: the session address space never reuses a range, so the ranges are
 * only cleared, not given back.
 */
static void
i915_skl_unmap(
	struct i915_device *device)
{
	struct i915_skl_flip *flip;
	unsigned index;

	flip = &i915_skl_flip_state;

	/* No address space holds the buffers. */
	if (flip->map_vm == NULL)
		return;

	/* Points both ranges back at scratch under the device mutex. */
	mutex_lock(&device->mutex);

	for (index = 0U; index < 2U; index++) {
		drv_i915_ppgtt_clear(flip->map_vm, flip->map_va[index], flip->buffer[index].obj->pages);
		flip->map_va[index] = 0U;
	}

	mutex_unlock(&device->mutex);

	/* No address space holds the buffers from here on. */
	flip->map_vm = NULL;
}

/*
 * Waits until the plane's live surface is the given one.
 *
 * Sleeps a tick at a time; the flip-done interrupt ends a sleep early.
 * Returns 0 with the latch's tick recorded, or ETIMEDOUT after
 * I915_GEN9_FLIP_LATCH_MS.
 */
static int
i915_skl_latch(
	struct i915_skl_flip *flip,
	uint32_t surf)
{
	uint64_t deadline;
	uint64_t now;
	uint32_t live_reg;
	uint32_t live;

	live_reg = i915_skl_plane_reg(flip, I915_SKL_PLANE_SURFLIVE_1_A);

	/* Reads the live surface until it is the one asked for, or the time is up. */
	deadline = sched_ticks() + ((uint64_t)I915_GEN9_FLIP_LATCH_MS * KERN_CLOCK_HZ + 999U) / 1000U + 1U;
	for (;;) {
		live = drv_i915_raw_read32(flip->mmio, live_reg) & I915_SKL_PLANE_SURF_ADDR_MASK;
		if (live == surf)
			break;

		/* The time is up: the flip did not latch. */
		now = sched_ticks();
		if (now >= deadline)
			return ETIMEDOUT;

		/* Sleeps until the next tick or the flip-done interrupt. */
		(void)drv_i915_wait_for_completion(&flip->done, now + 1U);
	}

	/* Succeeded: the plane shows the surface from this tick on. */
	flip->latched_tick = sched_ticks();
	return 0;
}

/*
 * Points the plane at a surface: the stride first when it differs from
 * the one the plane has, then the surface, whose write arms both for the
 * next vertical blank.
 */
static void
i915_skl_arm(
	struct i915_skl_flip *flip,
	uint32_t surf,
	uint32_t stride)
{
	uint32_t stride_reg;
	uint32_t surf_reg;
	uint32_t current_stride;

	stride_reg = i915_skl_plane_reg(flip, I915_SKL_PLANE_STRIDE_1_A);
	surf_reg = i915_skl_plane_reg(flip, I915_SKL_PLANE_SURF_1_A);

	/* A stale completion would only end the next sleep early; it starts empty for this flip. */
	drv_i915_reinit_completion(&flip->done);

	/* The stride, only when the buffer's is not the plane's. */
	current_stride = drv_i915_raw_read32(flip->mmio, stride_reg);
	if (current_stride != stride)
		drv_i915_raw_write32(flip->mmio, stride_reg, stride);

	/* The surface, which arms the plane's registers. */
	drv_i915_raw_write32(flip->mmio, surf_reg, surf);
	drv_i915_posting_read32(flip->mmio, surf_reg);
}

/*
 * Points the plane back at the firmware's surface and stride, and waits
 * for the latch.
 *
 * Returns 0, or ETIMEDOUT when the plane did not take the firmware's
 * surface back.
 */
static int
i915_skl_restore(
	struct i915_skl_flip *flip)
{
	int error;

	/* Nothing of the driver's was ever armed since the claim. */
	if (flip->armed == 0U)
		return 0;

	/* The firmware's surface and stride, armed together. */
	i915_skl_arm(flip, flip->readout.surf, flip->firmware_stride);

	/* Waits until the plane shows it. */
	error = i915_skl_latch(flip, flip->readout.surf);
	if (error != 0)
		return error;

	/* No buffer of the driver's is armed or shown. */
	flip->armed = 0U;
	flip->back = 0U;

	/* Succeeded: the firmware's surface is on the screen. */
	return 0;
}

/*
 * Enables the plane's flip-done interrupt on the flip pipe: clears what is
 * pending, enables the source and unmasks it.  The other display sources
 * stay as the device start left them (masked and disabled).
 */
static void
i915_skl_irq_enable(
	struct i915_skl_flip *flip)
{
	uint32_t imr_reg;
	uint32_t iir_reg;
	uint32_t ier_reg;
	uint32_t value;

	imr_reg = I915_SKL_DE_PIPE_IMR_A + flip->readout.pipe * I915_SKL_DE_PIPE_IRQ_STRIDE;
	iir_reg = I915_SKL_DE_PIPE_IIR_A + flip->readout.pipe * I915_SKL_DE_PIPE_IRQ_STRIDE;
	ier_reg = I915_SKL_DE_PIPE_IER_A + flip->readout.pipe * I915_SKL_DE_PIPE_IRQ_STRIDE;

	/* Already enabled for this lease. */
	if (flip->irq_on)
		return;

	/* Clears a stale flip done, twice because the IIR can hold two. */
	drv_i915_raw_write32(flip->mmio, iir_reg, I915_SKL_DE_PIPE_PLANE1_FLIP_DONE);
	drv_i915_raw_write32(flip->mmio, iir_reg, I915_SKL_DE_PIPE_PLANE1_FLIP_DONE);

	/* The handler acknowledges the source from here on. */
	flip->irq_on = 1;

	/* Enables the source, then unmasks it. */
	value = drv_i915_raw_read32(flip->mmio, ier_reg);
	drv_i915_raw_write32(flip->mmio, ier_reg, value | I915_SKL_DE_PIPE_PLANE1_FLIP_DONE);
	value = drv_i915_raw_read32(flip->mmio, imr_reg);
	drv_i915_raw_write32(flip->mmio, imr_reg, value & ~I915_SKL_DE_PIPE_PLANE1_FLIP_DONE);
	drv_i915_posting_read32(flip->mmio, imr_reg);
}

/* Masks and disables the plane's flip-done interrupt and clears what is pending. */
static void
i915_skl_irq_disable(
	struct i915_skl_flip *flip)
{
	uint32_t imr_reg;
	uint32_t iir_reg;
	uint32_t ier_reg;
	uint32_t value;

	imr_reg = I915_SKL_DE_PIPE_IMR_A + flip->readout.pipe * I915_SKL_DE_PIPE_IRQ_STRIDE;
	iir_reg = I915_SKL_DE_PIPE_IIR_A + flip->readout.pipe * I915_SKL_DE_PIPE_IRQ_STRIDE;
	ier_reg = I915_SKL_DE_PIPE_IER_A + flip->readout.pipe * I915_SKL_DE_PIPE_IRQ_STRIDE;

	/* Not enabled. */
	if (!flip->irq_on)
		return;

	/* Masks the source, then disables it. */
	value = drv_i915_raw_read32(flip->mmio, imr_reg);
	drv_i915_raw_write32(flip->mmio, imr_reg, value | I915_SKL_DE_PIPE_PLANE1_FLIP_DONE);
	drv_i915_posting_read32(flip->mmio, imr_reg);
	value = drv_i915_raw_read32(flip->mmio, ier_reg);
	drv_i915_raw_write32(flip->mmio, ier_reg, value & ~I915_SKL_DE_PIPE_PLANE1_FLIP_DONE);

	/* Clears what is pending, twice because the IIR can hold two. */
	drv_i915_raw_write32(flip->mmio, iir_reg, I915_SKL_DE_PIPE_PLANE1_FLIP_DONE);
	drv_i915_raw_write32(flip->mmio, iir_reg, I915_SKL_DE_PIPE_PLANE1_FLIP_DONE);

	/* The handler leaves the source alone from here on. */
	flip->irq_on = 0;
}

/*
 * Sums the copy and wait times and logs them: the first frame alone, then
 * every I915_SKL_FLIP_TIMING_FRAMES frames the means, in microseconds.
 */
static void
i915_skl_time(
	struct i915_skl_flip *flip,
	uint64_t gpu_ns,
	uint64_t wait_ns,
	uint32_t width,
	uint32_t height)
{
	/* The first frame tells how long one copy takes. */
	if (flip->flips == 1U) {
		kern_logf("i915: gen9 display: frame 1 %ux%u gpu_copy_us=%llu flip-done irqs %llu\n",
		    width,
		    height,
		    (unsigned long long)(gpu_ns / I915_SKL_FLIP_NS_PER_US),
		    (unsigned long long)flip->irq_count);
		return;
	}

	/* The later frames are summed. */
	flip->gpu_ns += gpu_ns;
	flip->wait_ns += wait_ns;
	flip->timed++;

	/* Not yet a whole period. */
	if (flip->timed < I915_SKL_FLIP_TIMING_FRAMES)
		return;

	/* The period's means, and a new period. */
	kern_logf("i915: gen9 display: %llu flips, the last %u at gpu_copy_us=%llu latch_wait_us=%llu each, flip-done irqs %llu\n",
	    (unsigned long long)flip->flips,
	    flip->timed,
	    (unsigned long long)(flip->gpu_ns / flip->timed / I915_SKL_FLIP_NS_PER_US),
	    (unsigned long long)(flip->wait_ns / flip->timed / I915_SKL_FLIP_NS_PER_US),
	    (unsigned long long)flip->irq_count);
	flip->gpu_ns = 0U;
	flip->wait_ns = 0U;
	flip->timed = 0U;
}

/* Reports a first-plane register of the flip pipe, given its pipe A offset. */
static uint32_t
i915_skl_plane_reg(
	const struct i915_skl_flip *flip,
	uint32_t pipe_a_reg)
{
	/* The pipe's registers are a stride apart. */
	return pipe_a_reg + flip->readout.pipe * I915_SKL_PIPE_STRIDE;
}

/* Reports a transcoder's name for the log. */
static const char *
i915_skl_transcoder_name(
	unsigned transcoder)
{
	/* The eDP transcoder, then A to C by their index. */
	if (transcoder == I915_SKL_TRANSCODER_EDP)
		return "EDP";
	if (transcoder == 0U)
		return "A";
	if (transcoder == 1U)
		return "B";

	/* Succeeded: the last one. */
	return "C";
}
