/*
 * WS031 V0 -- resident mode and the legacy-ops bridge (see legacy_shim.h, resident.h).
 *
 * MINIMAL CONNECTION, HAPPY PATH ONLY (E-127): one session at a time in practice, RCS0 only, every
 * request run to completion by the serving thread before the next one starts, completion found by
 * polling the CSB exactly as the accepted EU / draw tests do.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/waitq.h>
#include <kern/sched.h>
#include <errno.h>
#include <string.h>
#include "gt_mmio.h"
#include "gt_init.h"
#include "gt_resume.h"
#include "gt_submit.h"
#include "gt_request.h"
#include "gt_lrc.h"
#include "gt_mem.h"
#include "wait.h"
#include "osdep/mmio.h"
#include "resident.h"
#include "legacy_shim.h"
#include "lcd/parity_lcd_kernel.h"
#include "lcd/scanout.h"

#define SHIM_MI_STORE_DWORD_IMM_GEN4    ((0x20u << 23) | 2u)
#define SHIM_MI_USE_GGTT                (1u << 22)
#define SHIM_MI_BATCH_BUFFER_START_GEN8 ((0x31u << 23) | 1u)

#define SHIM_CONTEXTS     8u
#define SHIM_RING_BYTES   16384u
#define SHIM_TIMEOUT_MS   10000u
#ifndef PARITY_RESIDENT_STOP_ON_CLOSE
#define PARITY_RESIDENT_STOP_ON_CLOSE 0   /* test builds: end serving when the last session closes */
#endif
#ifndef PARITY_RESIDENT_SERVE_S
#define PARITY_RESIDENT_SERVE_S 0
#endif

/* What stands behind one legacy i915_context of the render engine. */
struct shim_context {
	struct i915_context *owner;           /* 0 = free */
	struct parity_gt_ppgtt vm;             /* only top_pd_dma is read: it names the LEGACY page tables */
	struct parity_gt_context ce;
	struct parity_gt_object *tl_page;      /* the timeline HWSP page (GGTT) */
	uint32_t tl_seqno;
	struct parity_gt_request rq;           /* one at a time: the serving thread runs them in order */
};

/* One batch a caller sleeps on: run by the serving thread like a request, reported to the caller. */
enum shim_sync_kind { SHIM_SYNC_BATCH = 0, SHIM_SYNC_PRESENT, SHIM_SYNC_RELEASE };

struct shim_sync {
	struct shim_sync *next;
	enum shim_sync_kind kind;
	/* PRESENT: the frame, read by the serving thread while the caller sleeps */
	const uint8_t *pixels;
	uint32_t width, height, stride;
	int bgra;
	struct i915_context *context;
	uint64_t batch_va;
	int done;
	int error;
};

static struct {
	struct parity_resident_ctx *ctx;
	int render_idx;
	struct shim_context contexts[SHIM_CONTEXTS];

	/* requests handed over by kick, in order; guarded by device->irq_lock */
	struct i915_request *run_head, *run_tail;
	/* batches the executor waits for (parity_shim_run_sync), in order; same lock */
	struct shim_sync *sync_head, *sync_tail;
	struct wait_queue sync_done;
	struct wait_queue work;
	int work_inited;
	int stop;

	unsigned executed, failed;
	unsigned live_contexts, contexts_ever;
	/* E-129: the display */
	int display_up;		/* the serving thread is inside the display window */
	int display_failed;	/* bringing the panel up failed once: presentation fails from then on */
	unsigned presents;
} shim;

static int
to_errno(int rc)
{
	return rc < 0 ? -rc : rc;
}

static struct shim_context *
shim_find(const struct i915_context *context)
{
	unsigned i;

	for (i = 0u; i < SHIM_CONTEXTS; i++)
		if (shim.contexts[i].owner == context)
			return &shim.contexts[i];
	return 0;
}

/* ---------------- contexts ---------------- */

int
parity_shim_lrc_create(struct i915_device *device, struct i915_engine *engine,
	struct i915_ppgtt *vm, uint32_t sw_id, struct i915_context *context)
{
	struct parity_resident_ctx *c = shim.ctx;
	struct shim_context *sc = 0;
	unsigned i;
	int rc;

	(void)device;
	memset(context, 0, sizeof(*context));
	context->engine = engine;
	context->vm = vm;
	context->sw_id = sw_id;

	if (engine->index != I915_ENGINE_RCS0) {
		/*
		 * XXX: unimplemented path -- only the render engine is connected.  The legacy ops layer
		 * makes one context per engine at open, so this is reached on every open; the context is
		 * a record only and a request on it fails in the serving thread.
		 */
		context->created = 1U;
		return 0;
	}
	if (c == 0)
		return ENODEV;

	for (i = 0u; i < SHIM_CONTEXTS; i++)
		if (shim.contexts[i].owner == 0) {
			sc = &shim.contexts[i];
			break;
		}
	if (sc == 0) {
		kern_logf("i915: resident shim: XXX no free context record (%u in use)\n", SHIM_CONTEXTS);
		return ENOMEM;
	}
	memset(sc, 0, sizeof(*sc));

	/*
	 * The address space of the session is the legacy ppgtt (plain memory, built by ppgtt.c).  The
	 * parity context reads exactly one thing from its vm: the top-level table address for PDP0.
	 */
	sc->vm.top_pd_dma = (uint64_t)vm->pml4.paddr;
	sc->vm.inited = 1;

	rc = parity_lrc_alloc(&sc->ce, &c->es->ge[shim.render_idx], &sc->vm, c->gm, SHIM_RING_BYTES, sw_id);
	if (rc != 0) {
		kern_logf("i915: resident shim: intel_context_create failed rc=%d\n", rc);
		return to_errno(rc);
	}
	sc->tl_page = parity_gt_object_create(c->gm, 4096u);
	if (sc->tl_page == 0) {
		parity_lrc_release(&sc->ce, c->gm);
		return ENOMEM;
	}
	rc = parity_gt_ggtt_bind(c->gm, sc->tl_page);
	if (rc != 0) {
		parity_gt_object_destroy(c->gm, sc->tl_page);
		parity_lrc_release(&sc->ce, c->gm);
		return to_errno(rc);
	}
	parity_lrc_init_state(&sc->ce);
	(void)parity_lrc_update_regs(&sc->ce, sc->ce.ring.tail);
	sc->tl_seqno = 0u;
	sc->owner = context;
	shim.live_contexts++;
	shim.contexts_ever++;
	context->created = 1U;
	kern_logf("i915: resident shim: context sw_id=%u lrca=%08x pml4=0x%llx ring=%u bytes\n",
		sw_id, sc->ce.lrca, (unsigned long long)sc->vm.top_pd_dma, SHIM_RING_BYTES);
	return 0;
}

void
parity_shim_lrc_destroy(struct i915_device *device, struct i915_context *context)
{
	struct parity_resident_ctx *c = shim.ctx;
	struct shim_context *sc = shim_find(context);

	(void)device;
	if (sc != 0 && c != 0) {
		/* XXX: happy path only -- the context is idle here because every request ran to its end. */
		if (sc->tl_page != 0)
			parity_gt_object_destroy(c->gm, sc->tl_page);
		parity_lrc_release(&sc->ce, c->gm);
		sc->owner = 0;
		if (shim.live_contexts != 0u)
			shim.live_contexts--;
		if (PARITY_RESIDENT_STOP_ON_CLOSE && shim.live_contexts == 0u) {
			shim.stop = 1;
			if (shim.work_inited)
				waitq_wake_all(&shim.work);
		}
	}
	context->created = 0U;
}

/* ---------------- requests ---------------- */

/* Called with device->irq_lock held: hand every queued request to the serving thread, in order. */
void
parity_shim_request_kick(struct i915_engine *engine)
{
	struct i915_request *request;

	while ((request = engine->queue_head) != NULL) {
		engine->queue_head = request->next;
		if (engine->queue_head == NULL)
			engine->queue_tail = NULL;
		request->next = NULL;
		request->seqno = engine->next_seqno;
		engine->next_seqno++;
		request->state = I915_REQUEST_ACTIVE;
		if (shim.run_tail == NULL)
			shim.run_head = request;
		else
			shim.run_tail->next = request;
		shim.run_tail = request;
	}
	if (shim.work_inited)
		waitq_wake_all(&shim.work);
}

/* One execbuf-shaped request, as the accepted EU test builds it, run to its end. */
static int
shim_run(struct i915_context *context, uint64_t batch_va, int has_batch, uint32_t label)
{
	struct parity_resident_ctx *c = shim.ctx;
	struct parity_gt_engine *ge = &c->es->ge[shim.render_idx];
	struct parity_execlists *el = &c->es->el[shim.render_idx];
	struct shim_context *sc;
	struct parity_gt_request *rq;
	uint32_t *cs;
	unsigned k, budget = SHIM_TIMEOUT_MS * 20u;
	int rc;

	if (context == NULL || context->engine == NULL ||
	    context->engine->index != I915_ENGINE_RCS0) {
		kern_logf("i915: resident shim: XXX unimplemented path: request on an engine other than RCS0\n");
		return ENOTSUP;
	}
	sc = shim_find(context);
	if (sc == 0)
		return EINVAL;

	/*
	 * XXX: the parity ring never wraps (gt_request.c refuses a request that would).  The context is
	 * idle between requests here, so the ring is simply rewound when it is nearly full.
	 */
	if (sc->ce.ring.emit + 512u > sc->ce.ring.size) {
		sc->ce.ring.head = sc->ce.ring.tail = sc->ce.ring.emit = 0u;
		(void)parity_lrc_update_regs(&sc->ce, 0u);
		kern_logf("i915: resident shim: ring rewound (idle context)\n");
	}

	rq = &sc->rq;
	memset(rq, 0, sizeof(*rq));
	sc->tl_seqno += 2u;
	rc = parity_request_create(rq, &sc->ce, sc->tl_seqno,
		(uint32_t)sc->tl_page->ggtt_offset, (volatile uint32_t *)sc->tl_page->cpu);
	if (rc != 0)
		return to_errno(rc);

	/* gen8_emit_init_breadcrumb() */
	cs = parity_ring_begin(rq, 6u);
	if (cs == 0)
		return to_errno(rq->error);
	*cs++ = SHIM_MI_STORE_DWORD_IMM_GEN4 | SHIM_MI_USE_GGTT;
	*cs++ = rq->hwsp_ggtt;
	*cs++ = 0u;
	*cs++ = rq->seqno - 1u;
	*cs++ = PARITY_MI_NOOP;
	*cs++ = PARITY_MI_ARB_CHECK;
	parity_ring_advance(rq, cs);

	/* gen8_emit_bb_start(): a PPGTT batch.  A marker (a job with no batch) is the breadcrumbs alone. */
	if (has_batch) {
		cs = parity_ring_begin(rq, 6u);
		if (cs == 0)
			return to_errno(rq->error);
		*cs++ = PARITY_MI_ARB_ON_OFF | PARITY_MI_ARB_ENABLE;
		*cs++ = SHIM_MI_BATCH_BUFFER_START_GEN8 | (1u << 8);
		*cs++ = (uint32_t)batch_va;
		*cs++ = (uint32_t)(batch_va >> 32);
		*cs++ = PARITY_MI_ARB_ON_OFF;
		*cs++ = PARITY_MI_NOOP;
		parity_ring_advance(rq, cs);
	}

	rc = parity_request_add(rq);
	if (rc != 0)
		return to_errno(rc);

	rc = parity_execlists_submit(ge, el, c->mmio, rq);
	if (rc != 0) {
		kern_logf("i915: resident shim: execlists_submit rc=%d\n", rc);
		return to_errno(rc);
	}
	for (k = 0u; k < budget; k++) {
		(void)parity_execlists_process_csb(ge, el, c->mmio);
		if (el->csb_errors != 0u)
			return EIO;
		if (parity_request_completed(rq) && !el->have_active && el->pending[0] == 0)
			return 0;
		if (parity_udelay(50u) != 0)
			return EIO;
	}
	/* XXX: unimplemented path -- a hang.  No reset, no recovery: the request fails and the log says so. */
	kern_logf("i915: resident shim: XXX request seqno=%u batch_va=0x%llx did not complete in %u ms "
		"(no recovery path; hwsp=%u last_csb=%08x:%08x)\n", label,
		(unsigned long long)batch_va, SHIM_TIMEOUT_MS,
		(unsigned)*rq->hwsp_cpu, el->last_csb_hi, el->last_csb_lo);
	return ETIMEDOUT;
}

static int
shim_execute(struct i915_request *request)
{
	return shim_run(request->context, request->batch_va, request->batch != NULL, request->seqno);
}

/*
 * Runs one batch of `context` to its end and reports how it ended.  The caller sleeps; the serving
 * thread runs the batch in the order it arrived, exactly as it runs a request.
 */
/* Queues one item for the serving thread and sleeps until it has been done. */
static int
shim_sync_do(struct i915_device *device, struct shim_sync *item)
{
	unsigned long irq;
	uint64_t observed;

	if (shim.ctx == 0 || !shim.work_inited) {
		kern_logf("i915: resident shim: XXX unimplemented path: work outside resident mode\n");
		return ENODEV;
	}
	irq = spin_lock_irqsave(&device->irq_lock);
	if (shim.sync_tail == NULL)
		shim.sync_head = item;
	else
		shim.sync_tail->next = item;
	shim.sync_tail = item;
	waitq_wake_all(&shim.work);
	while (!item->done) {
		observed = waitq_sequence(&shim.sync_done);
		(void)waitq_sleep(&shim.sync_done, &device->irq_lock, observed, sched_ticks() + 100u, 0U);
	}
	spin_unlock_irqrestore(&device->irq_lock, irq);
	return item->error;
}

/*
 * Runs one batch of `context` to its end and reports how it ended.  The caller sleeps; the serving
 * thread runs the batch in the order it arrived, exactly as it runs a request.
 */
int
parity_shim_run_sync(struct i915_device *device, struct i915_context *context, uint64_t batch_va)
{
	struct shim_sync item;

	memset(&item, 0, sizeof(item));
	item.kind = SHIM_SYNC_BATCH;
	item.context = context;
	item.batch_va = batch_va;
	return shim_sync_do(device, &item);
}

/* E-129: one frame to the panel (see resident.h). */
int
parity_shim_display_present(struct i915_device *device, const void *pixels, uint32_t width, uint32_t height,
	uint32_t stride, int bgra)
{
	struct shim_sync item;

	memset(&item, 0, sizeof(item));
	item.kind = SHIM_SYNC_PRESENT;
	item.pixels = pixels;
	item.width = width;
	item.height = height;
	item.stride = stride;
	item.bgra = bgra;
	return shim_sync_do(device, &item);
}

int
parity_shim_display_release(struct i915_device *device)
{
	struct shim_sync item;

	memset(&item, 0, sizeof(item));
	item.kind = SHIM_SYNC_RELEASE;
	return shim_sync_do(device, &item);
}

const struct parity_lcd_kernel_deps *
parity_shim_display_deps(void)
{
	return shim.ctx != 0 ? shim.ctx->lcd : 0;
}

/*
 * The frame into the buffer the display is not reading, then the flip.  XXX: CPU copy, nearest-neighbour scaling
 * by the largest whole factor that fits the panel, centred; the rest of the buffer stays black.
 */
static int
shim_present(const struct shim_sync *item)
{
	struct parity_scanout *back = parity_lcd_resident_back();
	uint32_t scale, x0, y0, x, y, sx, sy, pixel, *row;
	const uint32_t *src;

	if (back == 0)
		return EIO;
	scale = back->width / item->width < back->height / item->height ? back->width / item->width :
		back->height / item->height;
	if (scale == 0u) {
		kern_logf("i915: resident display: XXX a %ux%u frame is larger than the %ux%u panel\n", item->width,
			item->height, back->width, back->height);
		return EINVAL;
	}
	x0 = (back->width - item->width * scale) / 2u;
	y0 = (back->height - item->height * scale) / 2u;
	for (y = 0u; y < item->height * scale; y++) {
		sy = y / scale;
		src = (const uint32_t *)(const void *)(item->pixels + (uint64_t)sy * item->stride);
		row = back->cpu + (uint64_t)(y0 + y) * (back->pitch / 4u) + x0;
		for (x = 0u; x < item->width * scale; x++) {
			sx = x / scale;
			pixel = src[sx];
			/* the scanout is XRGB8888 (B G R X in memory): RGBA8888 swaps R and B, BGRA8888 is the same order */
			if (!item->bgra)
				pixel = ((pixel & 0xffu) << 16) | (pixel & 0xff00u) | ((pixel >> 16) & 0xffu);
			row[x] = pixel & 0x00ffffffu;
		}
	}
	return parity_lcd_resident_flip() == 0 ? 0 : EIO;
}

/* ---------------- recovery: entry points only ---------------- */

int
parity_shim_engine_reset(struct i915_engine *engine)
{
	/* XXX: unimplemented path.  Linux: intel_engine_reset() -> execlists reset_prepare/rewind/finish. */
	kern_logf("i915: resident shim: XXX unimplemented path: engine_reset(engine %u)\n", engine->index);
	return ENOTSUP;
}

int
parity_shim_engine_recover(struct i915_engine *engine, struct i915_session *session, int error)
{
	/* XXX: unimplemented path.  Legacy: stop the engine, fail the requests of the session, reset, resume. */
	(void)session;
	kern_logf("i915: resident shim: XXX unimplemented path: engine_recover(engine %u, error %d)\n",
		engine->index, error);
	return ENOTSUP;
}

int
parity_shim_gt_reset(struct i915_device *device)
{
	/* XXX: unimplemented path.  parity has __intel_gt_reset() (reset.c); it is not wired to a live node. */
	(void)device;
	kern_logf("i915: resident shim: XXX unimplemented path: gt_reset on a published node\n");
	return ENOTSUP;
}

/* ---------------- serving ---------------- */

#define SHIM_SERVE_STOP          0   /* told to stop */
#define SHIM_SERVE_ENTER_DISPLAY 1   /* a presentation waits and the panel is not up: the caller brings it up */
#define SHIM_SERVE_LEAVE_DISPLAY 2   /* inside the display: a release waits (it is completed after the stop) */

static uint64_t shim_started;

static void
shim_finish(struct i915_device *device, struct shim_sync *item, int error)
{
	unsigned long irq;

	irq = spin_lock_irqsave(&device->irq_lock);
	item->error = error;
	item->done = 1;
	waitq_wake_all(&shim.sync_done);
	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/* Executes queued work in arrival order until one of the SHIM_SERVE_* conditions. */
static int
shim_serve(struct i915_device *device, int in_display)
{
	struct i915_engine *engine = &device->engines[I915_ENGINE_RCS0];
	unsigned long irq;

	irq = spin_lock_irqsave(&device->irq_lock);
	for (;;) {
		struct i915_request *request;
		uint64_t observed;
		int error;

		while (shim.run_head == NULL && shim.sync_head == NULL && !shim.stop) {
			/*
			 * XXX: test builds serve for a bounded time (PARITY_RESIDENT_SERVE_S) so that the run ends
			 * through the teardown before the launcher kills qemu; 0 = serve for ever.
			 */
			if (PARITY_RESIDENT_SERVE_S != 0 &&
			    sched_ticks() - shim_started >= (uint64_t)PARITY_RESIDENT_SERVE_S * 100u) {
				shim.stop = 1;
				break;
			}
			observed = waitq_sequence(&shim.work);
			(void)waitq_sleep(&shim.work, &device->irq_lock, observed, sched_ticks() + 100u, 0U);
		}
		if (shim.sync_head != NULL) {
			struct shim_sync *item = shim.sync_head;

			/* display items that belong to the other side of the window are left at the head */
			if (item->kind == SHIM_SYNC_PRESENT && !in_display && !shim.display_failed && shim.ctx->lcd != 0) {
				spin_unlock_irqrestore(&device->irq_lock, irq);
				return SHIM_SERVE_ENTER_DISPLAY;
			}
			if (item->kind == SHIM_SYNC_RELEASE && in_display) {
				spin_unlock_irqrestore(&device->irq_lock, irq);
				return SHIM_SERVE_LEAVE_DISPLAY;
			}
			shim.sync_head = item->next;
			if (shim.sync_head == NULL)
				shim.sync_tail = NULL;
			spin_unlock_irqrestore(&device->irq_lock, irq);
			switch (item->kind) {
			case SHIM_SYNC_BATCH:
				error = shim_run(item->context, item->batch_va, 1, 0u);
				if (error == 0)
					shim.executed++;
				else
					shim.failed++;
				break;
			case SHIM_SYNC_PRESENT:
				/* in the window; outside it only when the panel could not be brought up */
				error = in_display ? shim_present(item) : EIO;
				if (error == 0)
					shim.presents++;
				break;
			default:
				error = 0;                      /* a release with the panel down: nothing to stop */
				break;
			}
			shim_finish(device, item, error);
			irq = spin_lock_irqsave(&device->irq_lock);
			continue;
		}
		if (shim.run_head == NULL && shim.stop)
			break;
		request = shim.run_head;
		shim.run_head = request->next;
		if (shim.run_head == NULL)
			shim.run_tail = NULL;
		request->next = NULL;
		spin_unlock_irqrestore(&device->irq_lock, irq);

		error = shim_execute(request);
		if (error == 0)
			shim.executed++;
		else
			shim.failed++;

		irq = spin_lock_irqsave(&device->irq_lock);
		engine->completed_seqno = request->seqno;
		request->state = I915_REQUEST_DONE;
		request->error = error;
		spin_unlock_irqrestore(&device->irq_lock, irq);

		/* legacy request.c: the callback, the count of the session, the slot and the waiters */
		drv_i915_request_complete_list(engine, request);

		irq = spin_lock_irqsave(&device->irq_lock);
	}
	spin_unlock_irqrestore(&device->irq_lock, irq);
	return SHIM_SERVE_STOP;
}

/* The display window's body (parity_lcd_kernel_resident_run calls it once the picture is up). */
static int
shim_display_window(void *ctx)
{
	shim.display_up = 1;
	(void)shim_serve(ctx, 1);
	shim.display_up = 0;
	return 0;
}


int
parity_resident_serve(struct parity_resident_ctx *ctx)
{
	struct i915_device *device = ctx->device;
	unsigned i;
	int rc;

	memset(&shim, 0, sizeof(shim));
	shim.ctx = ctx;
	shim.render_idx = -1;
	for (i = 0u; i < ctx->es->n; i++)
		if (ctx->es->ge[i].info->class == PARITY_RENDER_CLASS) {
			shim.render_idx = (int)i;
			break;
		}
	if (shim.render_idx < 0) {
		kern_logf("i915: resident: no render engine; not serving\n");
		shim.ctx = 0;
		return -ENODEV;
	}
	waitq_init(&shim.work, "i915 resident");
	waitq_init(&shim.sync_done, "i915 resident sync");
	shim.work_inited = 1;
	shim_started = sched_ticks();

	rc = drv_i915_resident_publish(device);
	if (rc != 0) {
		kern_logf("i915: resident: publish failed: %d\n", rc);
		shim.ctx = 0;
		return -rc;
	}
	kern_logf("i915: resident: GPU node published; serving (RCS0, one request at a time, CSB polling)\n");

	for (;;) {
		if (shim_serve(device, 0) != SHIM_SERVE_ENTER_DISPLAY)
			break;
		/* the first presentation: light the panel; the window serves everything until the release */
		if (parity_lcd_kernel_resident_run(ctx->lcd, shim_display_window, device) != 0 && !shim.display_failed) {
			/* XXX: no second attempt -- presentation fails from here on, the node keeps serving */
			shim.display_failed = 1;
			kern_logf("i915: resident display: the panel did not come up (or did not stop cleanly); presentation fails from now on\n");
		}
		/* a release that ended the window is at the head: shim_serve(0) completes it */
	}
	kern_logf("i915: resident: stopping (executed=%u failed=%u presented=%u)\n", shim.executed, shim.failed, shim.presents);
	rc = drv_i915_resident_unpublish(device);
	if (rc != 0)
		kern_logf("i915: resident: XXX unpublish rc=%d (sessions still open); the teardown runs anyway\n", rc);
	shim.ctx = 0;
	return 0;
}
