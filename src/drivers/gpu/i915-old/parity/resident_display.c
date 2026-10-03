/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * WS031 E-129: the panel as the display of the resident GPU node (-DPARITY_RESIDENT_DISPLAY=1).
 *
 * The node's display operations (include/drivers/gpu-display.h) for one output -- the eDP panel the parity
 * probe drives -- with one full-output plane and the COPIED presentation route: libvulkan copies each
 * completed frame into ordinary storage (GPU_RESOURCE_WRITE) and presents that storage.  The serving thread
 * shows it (legacy_shim.c): the first presentation lights the panel through the LCD-C body, every later one
 * is a synchronous flip, the release stops the panel through the reference's stop path.
 *
 * MINIMAL CONNECTION, HAPPY PATH ONLY:
 *   - XXX: one output, one plane, one lease at a time; the panel's own mode only.  A requested mode smaller
 *     than the panel is accepted and shown scaled by a whole factor and centred (the rest black); the
 *     display is never re-timed.
 *   - E-130: the SHARED route: the application's frame is a blob the rendering connection exported and the
 *     display connection imported; each presentation is a GPU copy (scaled by a whole factor, centred) of that
 *     blob into the panel's back buffer, then the flip.  No CPU touches pixels.  The COPIED route stays as the
 *     fallback the constraints still offer.
 *   - XXX: presentation is synchronous: it returns once the flip has completed, so a wait never waits.
 *   - XXX: no hot-plug, no topology events: the panel exists from the first query to the unpublish.
 */

#include "../internal.h"
#include "resident.h"
#include "resident_display.h"
#include "lcd/parity_lcd_kernel.h"
#include "../vk/gfx.h"

#include <drivers/gpu.h>
#include <drivers/gpu-display.h>
#include <drivers/gpu-scanout.h>
#include <kern/klog.h>
#include <kern/pmem.h>
#include <kern/sched.h>
#include <kern/clock.h>

#include <errno.h>
#include <string.h>

#define RD_DISPLAY_ID		1U
#define RD_GENERATION		1U
#define RD_MAX_FRAME_BYTES	(64ULL << 20)

static struct {
	struct mutex mutex;
	int inited;
	void *owner;			/* the session holding the lease; NULL = free */
	uint64_t lease;
	uint64_t next_lease;
	uint64_t sequence;		/* completed presentations of the current lease */
	uint64_t present_tick;
	int active;			/* the panel shows this node's frames */
} rd;

static void
rd_init(void)
{
	if (rd.inited)
		return;
	(void)mutex_init(&rd.mutex, LOCK_RANK_DEVICE, "i915-resident-display");
	rd.next_lease = 1U;
	rd.inited = 1;
}

static int
rd_panel(uint32_t *width, uint32_t *height, uint32_t *refresh)
{
	const struct parity_lcd_kernel_deps *deps = parity_shim_display_deps();

	if (deps == 0 || parity_lcd_kernel_panel_mode(deps, width, height, refresh) != 0)
		return ENXIO;
	return 0;
}

/* ---------------- scanout: identity and constraints ---------------- */

static int
rd_device_query(void *device, void *session, struct gpu_device_info *request)
{
	(void)device;
	(void)session;
	request->roles = GPU_DEVICE_RENDER | GPU_DEVICE_DISPLAY;
	request->companion_id = 0U;
	return 0;
}

static int
rd_constraints(void *device, void *session, struct gpu_scanout_constraints *request)
{
	(void)device;
	(void)session;
	if (request->display_id != RD_DISPLAY_ID)
		return ENOENT;
	if (request->generation != RD_GENERATION)
		return ESTALE;
	request->flags = GPU_SCANOUT_SHARED | GPU_SCANOUT_COPY;
	request->formats = GPU_DISPLAY_FORMAT_BGRA8888 | GPU_DISPLAY_FORMAT_RGBA8888;
	request->stride_alignment = 64U;	/* a linear render-target / sampler surface */
	request->offset_alignment = 64U;
	request->placement = 0U;
	request->max_dma_address = 0U;
	return 0;
}

/* ---------------- display ---------------- */

static int
rd_query(void *device, void *session, struct gpu_display_info *request)
{
	uint32_t width, height, refresh;
	int error;

	(void)device;
	(void)session;
	error = rd_panel(&width, &height, &refresh);
	request->count = error == 0 ? 1U : 0U;
	if (request->index == GPU_DISPLAY_COUNT_ONLY)
		return 0;
	if (error != 0 || request->index != 0U)
		return EINVAL;

	request->display_id = RD_DISPLAY_ID;
	request->generation = RD_GENERATION;
	request->flags = GPU_DISPLAY_CONNECTED | GPU_DISPLAY_FIFO;
	if (rd.active)
		request->flags |= GPU_DISPLAY_ACTIVE;
	request->plane_count = 1U;
	request->formats = GPU_DISPLAY_FORMAT_BGRA8888 | GPU_DISPLAY_FORMAT_RGBA8888;
	request->max_frame_bytes = RD_MAX_FRAME_BYTES;
	request->current_width = width;
	request->current_height = height;
	request->preferred_width = width;
	request->preferred_height = height;
	request->max_width = width;
	request->max_height = height;
	request->refresh_millihz = refresh;
	{
		const struct parity_lcd_kernel_deps *deps = parity_shim_display_deps();
		uint32_t mm[2];

		if (parity_lcd_kernel_panel_size_mm(deps, &mm[0], &mm[1]) == 0) {
			request->physical_width_mm = mm[0];
			request->physical_height_mm = mm[1];
		}
	}
	memcpy(request->name, "eDP panel", sizeof("eDP panel"));
	return 0;
}

static int
rd_mode(void *device, void *session, struct gpu_display_mode *request)
{
	uint32_t width, height, refresh;
	int error;

	(void)device;
	(void)session;
	if (request->display_id != RD_DISPLAY_ID)
		return ENOENT;
	if (request->generation != RD_GENERATION)
		return ESTALE;
	error = rd_panel(&width, &height, &refresh);
	if (error != 0)
		return error;

	if (request->operation == GPU_DISPLAY_MODE_ENUMERATE) {
		/* one native mode: the panel's */
		request->count = 1U;
		if (request->index == GPU_DISPLAY_COUNT_ONLY)
			return 0;
		if (request->index != 0U)
			return EINVAL;
		request->width = width;
		request->height = height;
		request->refresh_millihz = refresh;
		return 0;
	}

	/*
	 * A smaller frame at the panel's refresh is shown scaled into the panel's own timing (see the header): the
	 * display is never re-timed.  XXX: another refresh rate is refused rather than pretended.
	 */
	if (request->width > width || request->height > height)
		return EINVAL;
	if (request->refresh_millihz == 0U)
		request->refresh_millihz = refresh;
	if (request->refresh_millihz != refresh) {
		kern_logf("i915: resident display: XXX mode %ux%u@%u mHz refused: the panel runs at %u mHz only\n",
			request->width, request->height, request->refresh_millihz, refresh);
		return EINVAL;
	}
	return 0;
}

static int
rd_claim(void *device, void *session, struct gpu_display_claim *request)
{
	(void)device;
	if (request->display_id != RD_DISPLAY_ID)
		return ENOENT;
	if (request->generation != RD_GENERATION)
		return ESTALE;
	if (request->plane_index != 0U)
		return EINVAL;
	rd_init();
	mutex_lock(&rd.mutex);
	if (rd.owner != NULL) {
		mutex_unlock(&rd.mutex);
		return EBUSY;
	}
	rd.owner = session;
	rd.lease = rd.next_lease++;
	rd.sequence = 0U;
	request->lease = rd.lease;
	mutex_unlock(&rd.mutex);
	kern_logf("i915: resident display: lease %llu claimed\n", (unsigned long long)request->lease);
	return 0;
}

/* The lease ends; if the panel shows this node's frames it is stopped first. Called with rd.mutex held. */
static int
rd_release_locked(struct i915_device *device)
{
	int error;

	error = 0;
	if (rd.active) {
		error = parity_shim_display_release(device);
		rd.active = 0;
	}
	kern_logf("i915: resident display: lease %llu released after %llu frame(s) (stop %s)\n",
		(unsigned long long)rd.lease, (unsigned long long)rd.sequence, error == 0 ? "done" : "FAILED");
	rd.owner = NULL;
	rd.lease = 0U;
	return error;
}

static int
rd_release(void *device, void *session, const struct gpu_display_release *request)
{
	int error;

	rd_init();
	mutex_lock(&rd.mutex);
	if (rd.owner != session || rd.lease != request->lease) {
		mutex_unlock(&rd.mutex);
		return EINVAL;
	}
	error = rd_release_locked(device);
	mutex_unlock(&rd.mutex);
	return error;
}

/* The GPU copy of one shared frame into a panel buffer (built on the serving thread, run there). */
struct rd_blit {
	struct i915_vk_session *vk;
	struct gfx_surface src;
	const uint32_t *cpu;
};

const uint32_t *
parity_shim_blit_source(void *ctx, uint32_t *width, uint32_t *height, uint32_t *pitch)
{
	struct rd_blit *blit = ctx;

	*width = blit->src.width;
	*height = blit->src.height;
	*pitch = blit->src.pitch;
	return blit->cpu;
}

static int
rd_blit_build(void *ctx, uint64_t dst_va, uint32_t width, uint32_t height, uint32_t pitch, uint64_t *batch_va)
{
	struct rd_blit *blit = ctx;
	struct gfx_surface dst;
	struct gfx_rect src_rect, dst_rect;
	uint32_t scale;

	dst.va = dst_va;
	dst.width = width;
	dst.height = height;
	dst.pitch = pitch;
	dst.format = VK_FORMAT_B8G8R8A8_UNORM;		/* the panel's XRGB8888 */
	scale = width / blit->src.width < height / blit->src.height ? width / blit->src.width : height / blit->src.height;
	if (scale == 0U)
		return EINVAL;
	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.w = blit->src.width;
	src_rect.h = blit->src.height;
	dst_rect.w = blit->src.width * scale;
	dst_rect.h = blit->src.height * scale;
	dst_rect.x = (int32_t)((width - dst_rect.w) / 2U);
	dst_rect.y = (int32_t)((height - dst_rect.h) / 2U);
	return i915_vk_gfx_rect_build(blit->vk, &dst, &dst_rect, &blit->src, &src_rect, NULL, 0, batch_va);
}

static int
rd_present(void *device, void *session, void *object, struct gpu_display_present *request)
{
	struct i915_gem_object *storage;
	const uint8_t *pixels;
	uint32_t width, height, refresh;
	int error;

	storage = object;
	rd_init();
	mutex_lock(&rd.mutex);
	if (rd.owner != session || rd.lease != request->lease) {
		mutex_unlock(&rd.mutex);
		return EINVAL;
	}
	if (request->generation != RD_GENERATION) {
		mutex_unlock(&rd.mutex);
		return ESTALE;
	}
	error = rd_panel(&width, &height, &refresh);
	if (error == 0 && (request->width > width || request->height > height))
		error = EINVAL;
	if (error != 0) {
		mutex_unlock(&rd.mutex);
		return error;
	}

	/* E-130: the shared route -- the GPU copies the imported blob into the panel's back buffer */
	if ((request->flags & GPU_DISPLAY_PRESENT_BLOB) != 0U) {
		struct i915_session *owner = session;
		struct rd_blit blit;

		blit.vk = owner->vk;
		blit.src.va = storage->va != 0U ? storage->va + request->offset : 0U;
		blit.src.width = request->width;
		blit.src.height = request->height;
		blit.src.pitch = request->stride;
		blit.cpu = (const uint32_t *)((const uint8_t *)kern_pmem_to_kernel(storage->run.paddr) + request->offset);
		blit.src.format = request->format == GPU_PIXEL_BGRA8888 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
		/* the kernels and the session's objects are made here, not on the serving thread */
		error = blit.vk == NULL || blit.src.va == 0U ? EINVAL : i915_vk_gfx_rect_prepare(blit.vk);
		if (error == 0)
			error = parity_shim_display_present_blob(device, &owner->contexts[I915_ENGINE_RCS0], owner->vm,
				rd_blit_build, &blit);
		goto presented;
	}

	/* the core checked the extent against the storage; the storage is ordinary managed RAM */
	pixels = (const uint8_t *)kern_pmem_to_kernel(storage->run.paddr) + request->offset;
	error = parity_shim_display_present(device, pixels, request->width, request->height, request->stride,
		request->format == GPU_PIXEL_BGRA8888);
presented:
	if (error == 0) {
		rd.active = 1;
		rd.sequence++;
		rd.present_tick = sched_ticks();
		request->sequence = rd.sequence;
		if (rd.sequence == 1U)
			kern_logf("i915: resident display: first frame %ux%u (stride %u; %s; %s) shown on the %ux%u panel\n",
				request->width, request->height, request->stride,
				(request->flags & GPU_DISPLAY_PRESENT_BLOB) != 0U ? "shared, GPU copy" : "copied, CPU copy",
				request->format == GPU_PIXEL_BGRA8888 ? "BGRA" : "RGBA", width, height);
	}
	mutex_unlock(&rd.mutex);
	return error;
}

static int
rd_wait(void *device, void *session, struct gpu_display_wait *request)
{
	(void)device;
	rd_init();
	mutex_lock(&rd.mutex);
	if (rd.owner != session || rd.lease != request->lease || request->sequence > rd.sequence) {
		mutex_unlock(&rd.mutex);
		return EINVAL;
	}
	if (rd.sequence == 0U) {
		mutex_unlock(&rd.mutex);
		return EAGAIN;
	}
	/* presentation is synchronous: the newest sequence has completed already */
	request->completed_sequence = rd.sequence;
	request->present_time_ns = rd.present_tick * (KERN_NSEC_PER_SEC / KERN_CLOCK_HZ);
	request->generation = RD_GENERATION;
	mutex_unlock(&rd.mutex);
	return 0;
}

static int
rd_events(void *device, void *session, uint64_t *sequence)
{
	(void)device;
	(void)session;
	*sequence = 1U;			/* XXX: no topology change is ever published */
	return 0;
}

const struct drv_gpu_display_ops drv_i915_resident_display_ops = {
	rd_query, rd_mode, rd_claim, rd_release, rd_present, rd_wait, rd_events
};

const struct drv_gpu_scanout_ops drv_i915_resident_scanout_ops = {
	rd_device_query, rd_constraints, NULL
};

/* A session that closes with the lease still held gives it back (the panel is stopped first). */
void
drv_i915_resident_display_close(struct i915_device *device, void *session)
{
	if (!rd.inited)
		return;
	mutex_lock(&rd.mutex);
	if (rd.owner == session) {
		kern_logf("i915: resident display: the lease owner closed without releasing it\n");
		(void)rd_release_locked(device);
	}
	mutex_unlock(&rd.mutex);
}
