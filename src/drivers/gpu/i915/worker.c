/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The request worker (see worker.h).
 *
 * The session operations queue under the device's IRQ lock and kick; the
 * worker takes the work in arrival order, drops the lock, runs it to its
 * end, and delivers the result.  A request is written as the reference's
 * execbuf writes one: the initial breadcrumb (gen8_emit_init_breadcrumb()),
 * the batch start (gen8_emit_bb_start()) unless the request is a marker, and
 * the final breadcrumb the request add writes.  Its end is found by
 * processing the context status buffer, as the reference's wait path does
 * when it runs the submission tasklet inline; between two looks the worker
 * sleeps until the engine's next interrupt (the final breadcrumb's user
 * interrupt, or a context switch).
 */

#include "context.h"
#include "device-info.h"
#include "display/backlight.h"
#include "display/present.h"
#include "engine.h"
#include "ggtt.h"
#include "i915.h"
#include "irq.h"
#include "memory.h"
#include "ppgtt.h"
#include "request-queue.h"
#include "request.h"
#include "session.h"
#include "submit.h"
#include "sync.h"
#include "worker.h"
#include "perf.h"
#include <kern/kcrt.h>

#include <kern/clock.h>
#include <hal/hal.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <kern/waitq.h>

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

#include "intel/commands.h"

/*
 * How many session contexts of the render engine can be live at once.
 *
 * Every open of the node is a session with one render context: the
 * compositor, each Vulkan, EGL or GLX application, and the X server of an
 * X application.  The demonstration's desktop with every application of App
 * Home open needs more than eight (ws075-p009: Gears and Browser could not
 * start); a live record costs its context image, a 16 KiB ring and a
 * timeline page, and a free one only its entry in this table.
 */
#define I915_WORKER_CONTEXTS		32U

/* The ring size of a session context. */
#define I915_WORKER_RING_BYTES		16384U

/* How long a request may run before it is failed as a hang. */
#define I915_WORKER_TIMEOUT_MS		10000U

/*
 * The longest sleep between two looks at the context status buffer, in
 * scheduler ticks.  The engine's interrupt normally ends it; the bound only
 * keeps a lost interrupt from costing more than this.
 */
#define I915_WORKER_WAIT_TICKS		1U

/* The pause between two polls of the context status buffer when no interrupt handler is attached. */
#define I915_WORKER_POLL_US		50U

/* The ring room a request needs at most; a ring with less left is rewound. */
#define I915_WORKER_REQUEST_ROOM	512U

/* How long the worker sleeps before it looks again for work it was not woken for (one second). */
#define I915_WORKER_SLEEP_TICKS		(1U * KERN_CLOCK_HZ)

/* The size of a context's timeline page. */
#define I915_WORKER_TIMELINE_BYTES	4096U

/*
 * Why a pass of the worker loop returned: a stop was asked for, a
 * presentation waits outside the display window (the caller lights the
 * panel), or the window is to be left: a release that cannot hold the last
 * picture waits inside it (it is completed after the stop), or a hold of
 * the last picture is over.
 */
#define I915_WORKER_SERVE_STOP		0
#define I915_WORKER_SERVE_ENTER_DISPLAY	1
#define I915_WORKER_SERVE_LEAVE_DISPLAY	2

/*
 * The hardware context behind one session context of the render engine.
 *
 * A record of the worker's table is free while owner is NULL.  Context
 * create fills it under the device mutex and context destroy empties it;
 * the worker uses it only while running a request of that context.
 */
struct i915_worker_context {
	/* The session context this record stands behind; NULL for a free record. */
	struct i915_context *owner;

	/*
	 * How long the engine ran this context's requests, in nanoseconds, and
	 * how many ran.  Only the worker adds to them; they are read with a
	 * debugger to see which session keeps the engine busy (ws075-p018).
	 */
	uint64_t engine_ns;
	uint32_t engine_runs;

	/*
	 * Of the synchronous batches (the executor's submits, whose caller
	 * sleeps): how long they waited in the queue before the worker took
	 * them, and how long from the queue to their end, in nanoseconds
	 * (ws075-p026).  Read the same way.
	 */
	uint64_t queue_ns;
	uint64_t round_ns;

	/*
	 * The address space the logical ring context names.  Only top_pd_dma is
	 * read: it is the top table of the session's own address space.
	 */
	struct i915_gt_ppgtt vm;

	/* The logical ring context: its image and its ring. */
	struct i915_gt_context ce;

	/* The timeline page in the GGTT the breadcrumbs land in, and the last seqno given. */
	struct i915_gt_object *tl_page;
	uint32_t tl_seqno;

	/* The one request of the context; the worker runs them one at a time. */
	struct i915_gt_request rq;
};

/*
 * One item a caller sleeps on while the worker does it: a batch, a
 * presentation or a release of the panel.
 *
 * It lives on the caller's stack from the queuing to the completion; the
 * device's IRQ lock protects its link, done and error.
 */
struct i915_worker_sync {
	/* The next queued item. */
	struct i915_worker_sync *next;

	/* What the item asks for. */
	enum i915_worker_sync_kind kind;

	/* A batch: the context it runs in, and the batch's address in its space. */
	struct i915_context *context;
	uint64_t batch_va;

	/* A presentation: the frame, read by the worker while the caller sleeps. */
	const struct i915_worker_present *present;

	/* A backlight item: what it does (I915_BACKLIGHT_*), and its value (percent, or on) in and out. */
	int backlight_op;
	uint32_t backlight_value;

	/* When the item was queued (drv_i915_perf_now()), for the context's queue and round times. */
	uint64_t queued_at;

	/* Nonzero once the worker ran the batch, and how it ended. */
	int done;
	int error;
};

/*
 * The request worker of one device.
 *
 * The device owns it from drv_i915_worker_create() to
 * drv_i915_worker_destroy().  The device's IRQ lock protects the two queues
 * and the stop flag; the context table is changed under the device mutex.
 */
struct i915_worker {
	/* The device the worker serves. */
	struct i915_device *device;

	/* The index of the render engine in the GT's engine set. */
	int render_index;

	/* The hardware contexts behind the session contexts of the render engine. */
	struct i915_worker_context contexts[I915_WORKER_CONTEXTS];

	/* The requests the kick handed over, oldest first. */
	struct i915_request *run_head;
	struct i915_request *run_tail;

	/* The request the worker is running on the engine now, or NULL (under the IRQ lock). */
	struct i915_request *current;

	/* The synchronous batches waiting to run, oldest first. */
	struct i915_worker_sync *sync_head;
	struct i915_worker_sync *sync_tail;

	/* Where synchronous callers wait for their batch, and where the worker waits for work. */
	struct wait_queue sync_done;
	struct wait_queue work;

	/* Nonzero from the publication of the node to its withdrawal: work may be accepted. */
	int serving;

	/* Nonzero once a stop was asked for: the worker finishes the queued work and withdraws the node. */
	int stop;

	/* How much work ended well and how much failed. */
	unsigned executed;
	unsigned failed;

	/* How many context records are in use, and how many were ever filled. */
	unsigned live_contexts;
	unsigned contexts_ever;

	/*
	 * How the sleeps between two looks at the context status buffer ended:
	 * woken by an engine interrupt, or run out (the bound).
	 */
	unsigned wait_interrupts;
	unsigned wait_timeouts;
};

static int i915_worker_loop(struct i915_worker *worker, int in_display);
static void i915_worker_run_request(struct i915_worker *worker, struct i915_request *request);
static void i915_worker_run_sync_item(struct i915_worker *worker, struct i915_worker_sync *item, int in_display);
static int i915_worker_queue_sync(struct i915_device *device, struct i915_worker_sync *item);
static int i915_worker_run(struct i915_worker *worker, struct i915_context *context, uint64_t batch_va, int has_batch, uint32_t label);
static int i915_worker_emit(struct i915_gt_request *rq, uint64_t batch_va, int has_batch);
static int i915_worker_wait(struct i915_worker *worker, struct i915_gt_request *rq, uint64_t batch_va, uint32_t label);
static struct i915_worker_context *i915_worker_find(struct i915_worker *worker, const struct i915_context *context);

/*
 * Creates the request worker of a device.
 *
 * The GT engines must be set up.  Returns ENODEV when the GT has no render
 * engine, ENOMEM when the worker's state cannot be allocated, and EBUSY when
 * the device already has a worker.
 */
int
drv_i915_worker_create(
	struct i915_device *device)
{
	struct i915_worker *worker;
	struct i915_gt_engines *engines;
	unsigned index;

	/* A device has one worker. */
	if (device->worker != NULL)
		return EBUSY;

	/* Allocates the worker's state. */
	worker = kern_calloc(1U, sizeof(*worker));
	if (worker == NULL)
		return ENOMEM;

	/* Finds the render engine every request runs on. */
	worker->device = device;
	worker->render_index = -1;
	engines = &device->gt.engines;
	for (index = 0U; index < engines->n; index++) {
		if (engines->ge[index].info->class == I915_RENDER_CLASS) {
			worker->render_index = (int)index;
			break;
		}
	}

	/* A GT without a render engine has nothing to serve on. */
	if (worker->render_index < 0) {
		kern_logf("i915: resident: no render engine; not serving\n");
		kern_free(worker);
		return ENODEV;
	}

	/* Prepares the two wait queues. */
	waitq_init(&worker->work, "i915 resident");
	waitq_init(&worker->sync_done, "i915 resident sync");

	device->worker = worker;

	/* Succeeded: the worker is ready to serve. */
	return 0;
}

/*
 * Publishes the GPU node, serves it until a stop is asked for, and withdraws it.
 *
 * Runs on the thread that brought the device up, which holds forcewake for
 * the whole service.  Returns once the node was withdrawn, or the error of a
 * failed publication.  A withdrawal refused because sessions are still open
 * is logged and the call still returns 0, so the device stop runs anyway.
 */
int
drv_i915_worker_serve(
	struct i915_device *device)
{
	struct i915_worker *worker;
	int outcome;
	int error;

	/* A device without a worker cannot serve. */
	worker = device->worker;
	if (worker == NULL)
		return ENODEV;

	/* Work is accepted from the moment the node can be opened. */
	worker->serving = 1;

	/* Publishes the GPU node. */
	error = drv_i915_publish(device);
	if (error != 0) {
		kern_logf("i915: resident: publish failed: %d\n", error);
		worker->serving = 0;
		return error;
	}

	kern_logf("i915: resident: GPU node published; serving (RCS0, one request at a time, woken by the engine interrupt) on cpu %u\n", (unsigned)hal_cpu_current());

	/* The panel's light as /dev/backlight/backlight0, answered by this loop; the node works without it (a failure is logged). */
	(void)drv_i915_display_backlight_register(device);

	/*
	 * Runs the queued work until a stop is asked for.  The first
	 * presentation makes the loop return: the panel is lit and the display
	 * window serves everything until the release, which is completed by the
	 * next pass of the loop.
	 */
	for (;;) {
		outcome = i915_worker_loop(worker, 0);
		if (outcome != I915_WORKER_SERVE_ENTER_DISPLAY)
			break;

		drv_i915_present_window(device);
	}

	kern_logf("i915: resident: stopping (executed=%u failed=%u presented=%u; waits woken by the engine interrupt %u, run out %u)\n",
	    worker->executed,
	    worker->failed,
	    drv_i915_present_count(device),
	    worker->wait_interrupts,
	    worker->wait_timeouts);

	/* Withdraws the node; open sessions keep it, and the device stop runs anyway. */
	error = drv_i915_unpublish(device);
	if (error != 0)
		kern_logf("i915: resident: XXX unpublish rc=%d (sessions still open); the teardown runs anyway\n", error);

	/* No work is accepted from here on. */
	worker->serving = 0;

	/* Succeeded: the service ended. */
	return 0;
}

/*
 * Asks the worker to stop serving.
 *
 * The worker finishes the work already queued and then withdraws the node;
 * drv_i915_worker_serve() returns once it has.
 */
void
drv_i915_worker_stop(
	struct i915_device *device)
{
	struct i915_worker *worker;
	unsigned long irq;

	/* A device without a worker serves nothing. */
	worker = device->worker;
	if (worker == NULL)
		return;

	/*
	 * The panel's light goes while the worker still serves: a request that
	 * holds the backlight device waits for the worker, and none is queued
	 * after this returns, so none waits for a worker that has left.
	 */
	drv_i915_display_backlight_unregister(device);

	/* Marks the stop and wakes the worker to see it. */
	irq = spin_lock_irqsave(&device->irq_lock);

	worker->stop = 1;
	waitq_wake_all(&worker->work);

	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/*
 * Wakes the worker to look at its queue and the display's hold again.
 *
 * The shutdown uses it after it ended a hold of the last picture, so the
 * worker stops the output without waiting for its next bounded sleep.
 */
void
drv_i915_worker_wake(
	struct i915_device *device)
{
	struct i915_worker *worker;
	unsigned long irq;

	/* A device without a worker has nobody to wake. */
	worker = device->worker;
	if (worker == NULL)
		return;

	/* Wakes the worker under the lock its sleep is ordered by. */
	irq = spin_lock_irqsave(&device->irq_lock);

	waitq_wake_all(&worker->work);

	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/*
 * Frees the request worker of a device.
 *
 * The worker must have stopped serving and every session context must be
 * gone; a worker that is still needed is kept and reported.
 */
void
drv_i915_worker_destroy(
	struct i915_device *device)
{
	struct i915_worker *worker;

	/* A device without a worker has nothing to free. */
	worker = device->worker;
	if (worker == NULL)
		return;

	/* A serving worker, or one that still holds contexts, is kept. */
	if (worker->serving != 0 || worker->live_contexts != 0U) {
		kern_logf("i915: worker still in use (serving=%d contexts=%u); kept\n",
		    worker->serving,
		    worker->live_contexts);
		return;
	}

	device->worker = NULL;
	kern_free(worker);
}

/*
 * Creates a session context on an engine record.
 *
 * On the render engine the worker fills a hardware context over the
 * session's address space.  The caller holds the device mutex.  Returns
 * ENODEV when the worker is not serving and ENOMEM when every context record
 * is in use.
 */
int
drv_i915_worker_context_create(
	struct i915_device *device,
	struct i915_engine *engine,
	struct i915_ppgtt *vm,
	uint32_t sw_id,
	struct i915_context *context)
{
	struct i915_worker *worker;
	struct i915_worker_context *record;
	unsigned index;
	int error;

	/* Starts the session context as a record of its engine, space and id. */
	kern_memset(context, 0, sizeof(*context));
	context->engine = engine;
	context->vm = vm;
	context->sw_id = sw_id;

	/*
	 * XXX: only the render engine is connected.  Open makes one context per
	 * engine record, so this is reached on every open; the context is a
	 * record only and a request on it fails in the worker.
	 */
	if (engine->index != I915_ENGINE_RCS0) {
		context->created = 1U;
		return 0;
	}

	/* A worker that is not serving accepts no context. */
	worker = device->worker;
	if (worker == NULL || worker->serving == 0)
		return ENODEV;

	/* Finds a free context record. */
	record = NULL;
	for (index = 0U; index < I915_WORKER_CONTEXTS; index++) {
		if (worker->contexts[index].owner == NULL) {
			record = &worker->contexts[index];
			break;
		}
	}

	/* Every record is in use. */
	if (record == NULL) {
		kern_logf("i915: resident shim: XXX no free context record (%u in use)\n", I915_WORKER_CONTEXTS);
		return ENOMEM;
	}

	kern_memset(record, 0, sizeof(*record));

	/*
	 * The address space is the session's own (plain memory, built by
	 * ppgtt.c).  The logical ring context reads exactly one thing from it:
	 * the top-level table address for PDP0.
	 */
	record->vm.top_pd_dma = (uint64_t)vm->pml4.paddr;
	record->vm.inited = 1;

	/* Allocates the image and the ring on the render engine (intel_context_create()). */
	error = drv_i915_lrc_alloc(
		&record->ce,
		&device->gt.engines.ge[worker->render_index],
		&record->vm,
		&device->gt.mem,
		I915_WORKER_RING_BYTES,
		sw_id);
	if (error != 0) {
		kern_logf("i915: resident shim: intel_context_create failed rc=%d\n", error);
		return error;
	}

	/* Allocates the timeline page the breadcrumbs land in. */
	record->tl_page = drv_i915_gt_object_create(&device->gt.mem, I915_WORKER_TIMELINE_BYTES);
	if (record->tl_page == NULL) {
		drv_i915_lrc_release(&record->ce, &device->gt.mem);
		return ENOMEM;
	}

	/* Binds the timeline page into the GGTT, where the breadcrumbs address it. */
	error = drv_i915_gt_ggtt_bind(&device->gt.mem, record->tl_page);
	if (error != 0) {
		drv_i915_gt_object_destroy(&device->gt.mem, record->tl_page);
		drv_i915_lrc_release(&record->ce, &device->gt.mem);
		return error;
	}

	/* Lays out the register state and points the image at the empty ring. */
	drv_i915_lrc_init_state(&record->ce);
	(void)drv_i915_lrc_update_regs(&record->ce, record->ce.ring.tail);

	/* The owner makes the record the session context's; the counts keep the worker alive. */
	record->tl_seqno = 0U;
	record->owner = context;
	worker->live_contexts++;
	worker->contexts_ever++;
	context->created = 1U;
	kern_logf("i915: resident shim: context sw_id=%u lrca=%08x pml4=0x%llx ring=%u bytes\n",
	    sw_id,
	    record->ce.lrca,
	    (unsigned long long)record->vm.top_pd_dma,
	    I915_WORKER_RING_BYTES);

	/* Succeeded: requests of the session context run in the hardware context. */
	return 0;
}

/*
 * Destroys a session context.
 *
 * The hardware context behind it is released while the worker serves.  The
 * caller holds the device mutex.
 */
void
drv_i915_worker_context_destroy(
	struct i915_device *device,
	struct i915_context *context)
{
	struct i915_worker *worker;
	struct i915_worker_context *record;

	/* Finds the hardware context behind the session context, if any. */
	worker = device->worker;
	record = NULL;
	if (worker != NULL)
		record = i915_worker_find(worker, context);

	/*
	 * Releases the hardware context while the worker serves.
	 *
	 * XXX: happy path only -- the context is idle here because every
	 * request ran to its end.
	 */
	if (record != NULL && worker->serving != 0) {
		/* Frees the timeline page. */
		if (record->tl_page != NULL)
			drv_i915_gt_object_destroy(&device->gt.mem, record->tl_page);

		/* Frees the image and the ring. */
		drv_i915_lrc_release(&record->ce, &device->gt.mem);

		/* NULL frees the record; the live count no longer keeps the worker. */
		record->owner = NULL;
		if (worker->live_contexts != 0U)
			worker->live_contexts--;
	}

	context->created = 0U;
}

/*
 * Hands every queued request of an engine record to the worker, in order.
 *
 * The caller holds the device's IRQ lock.  Each request is given the
 * record's next sequence number and becomes ACTIVE.
 */
void
drv_i915_worker_kick(
	struct i915_engine *engine)
{
	struct i915_worker *worker;
	struct i915_request *request;

	/* A device without a worker leaves the requests queued. */
	worker = engine->device->worker;
	if (worker == NULL) {
		kern_logf("i915: XXX request kicked with no worker; left queued\n");
		return;
	}

	/* Moves the engine queue onto the worker's run list. */
	request = engine->queue_head;
	while (request != NULL) {
		/* Takes the head request off the engine queue. */
		engine->queue_head = request->next;
		if (engine->queue_head == NULL)
			engine->queue_tail = NULL;

		request->next = NULL;

		/* ACTIVE with a sequence number: the request now belongs to the worker. */
		request->seqno = engine->next_seqno;
		engine->next_seqno++;
		request->state = I915_REQUEST_ACTIVE;

		/* Appends it to the run list. */
		if (worker->run_tail == NULL) {
			worker->run_head = request;
		} else {
			worker->run_tail->next = request;
		}

		worker->run_tail = request;
		request = engine->queue_head;
	}

	/* Wakes the worker to run them. */
	waitq_wake_all(&worker->work);
}

/*
 * Runs one batch of a context to its end and reports how it ended.
 *
 * The caller sleeps; the worker runs the batch in the order it arrived,
 * exactly as it runs a request.  Returns 0, ETIMEDOUT for a hang (no
 * recovery follows), or ENODEV when the worker is not serving.
 */
int
drv_i915_worker_run_sync(
	struct i915_device *device,
	struct i915_context *context,
	uint64_t batch_va)
{
	struct i915_worker_sync item;
	uint64_t start;
	int error;

	/* Describes the batch. */
	kern_memset(&item, 0, sizeof(item));
	item.kind = I915_WORKER_SYNC_BATCH;
	item.context = context;
	item.batch_va = batch_va;

	/* Queues the batch and sleeps until it has run; the whole round trip is timed. */
	start = drv_i915_perf_now();
	item.queued_at = start;
	error = i915_worker_queue_sync(device, &item);
	drv_i915_perf_add(&device->perf, I915_PERF_RUN, start);
	if (error != 0)
		return error;

	/* Succeeded: the batch ran to its end. */
	return 0;
}

/*
 * Queues a presentation or a release of the panel and waits until the
 * worker has done it.
 *
 * The worker enters the display window for the first presentation and
 * leaves it for the release, in the order the items arrived.  Returns 0,
 * ENODEV when the worker is not serving, or the item's error.
 */
int
drv_i915_worker_sync_display(
	struct i915_device *device,
	enum i915_worker_sync_kind kind,
	const struct i915_worker_present *present)
{
	struct i915_worker_sync item;
	int error;

	/* Describes the item. */
	kern_memset(&item, 0, sizeof(item));
	item.kind = kind;
	item.present = present;

	/* Queues it and sleeps until the worker has done it. */
	error = i915_worker_queue_sync(device, &item);
	if (error != 0)
		return error;

	/* Succeeded: the panel shows the frame, or is given back. */
	return 0;
}

/*
 * Reads or sets the panel's brightness in percent, or switches its light
 * (op: I915_BACKLIGHT_*), and waits until the worker has done it.
 *
 * Returns 0 with the brightness (or the light) in *value, EBUSY while the
 * panel is not lit by the driver (outside the display window, or HDMI in
 * its place), ENODEV when the worker is not serving, or EIO.
 */
int
drv_i915_worker_sync_backlight(
	struct i915_device *device,
	int op,
	uint32_t *value)
{
	struct i915_worker_sync item;
	int error;

	/* Describes the item. */
	kern_memset(&item, 0, sizeof(item));
	item.kind = I915_WORKER_SYNC_BACKLIGHT;
	item.backlight_op = op;
	item.backlight_value = *value;

	/* Queues it and sleeps until the worker has done it. */
	error = i915_worker_queue_sync(device, &item);
	if (error != 0)
		return error;

	/* Succeeded: the brightness (or the light) the panel has. */
	*value = item.backlight_value;
	return 0;
}

/*
 * Serves every piece of work from inside the display window.
 *
 * Runs on the worker, called back by the display window while the panel
 * is up; returns when a release reaches the head of the queue or a stop is
 * asked for.
 */
void
drv_i915_worker_serve_window(
	struct i915_device *device)
{
	/* The display window's pass of the loop; its outcome is the caller's. */
	(void)i915_worker_loop(device->worker, 1);
}

/*
 * Runs one batch of a context to its end on the worker's own thread.
 *
 * Only the worker calls it, from inside the display window (the GPU copy of
 * a presentation).  Returns 0, or the run's error.
 */
int
drv_i915_worker_run_batch(
	struct i915_device *device,
	struct i915_context *context,
	uint64_t batch_va)
{
	int error;

	/* Runs the batch as a request with a batch. */
	error = i915_worker_run(device->worker, context, batch_va, 1, 0U);
	if (error != 0)
		return error;

	/* Succeeded: the batch ran to its end. */
	return 0;
}

/*
 * Resets one engine record's engine.
 *
 * XXX: unimplemented path.  Linux: intel_engine_reset() -> execlists
 * reset_prepare/rewind/finish.  Logs and returns ENOTSUP.
 */
int
drv_i915_worker_engine_reset(
	struct i915_engine *engine)
{
	/* Names the missing path in the log. */
	kern_logf("i915: resident shim: XXX unimplemented path: engine_reset(engine %u)\n", engine->index);

	/* The reset is not implemented. */
	return ENOTSUP;
}

/*
 * Ends a session's work on one engine record (BUG-077).
 *
 * The session's requests that never reached the engine -- reserved,
 * queued, or handed to the worker but not yet taken -- fail with `error`
 * and their completions are delivered; the other sessions go on.  A
 * request of the session that the engine is running now cannot be taken
 * back without a reset: that is the XXX unimplemented path (stop the
 * engine, reset, resume), which logs and returns ENOTSUP, so the caller
 * falls back to the device fault.
 */
int
drv_i915_worker_engine_recover(
	struct i915_engine *engine,
	struct i915_session *session,
	int error)
{
	struct i915_device *device;
	struct i915_worker *worker;
	struct i915_request *retired;
	struct i915_request *request;
	struct i915_request *previous;
	struct i915_request *next;
	unsigned long irq;
	unsigned taken;
	unsigned states[6];
	unsigned index;

	/* Nothing is taken or counted yet. */
	kern_memset(states, 0, sizeof(states));
	device = engine->device;
	worker = device->worker;
	retired = NULL;
	taken = 0U;

	/* Everything is decided under the IRQ lock, which the worker takes around each request. */
	irq = spin_lock_irqsave(&device->irq_lock);

	/* A request of the session on the engine now needs the reset that is not implemented. */
	if (worker != NULL && worker->current != NULL &&
	    worker->current->session == session && worker->current->context != NULL &&
	    worker->current->context->engine == engine) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		kern_logf("i915: resident shim: XXX unimplemented path: engine_recover(engine %u, error %d) "
		    "of a session whose request seqno=%u is running\n",
		    engine->index,
		    error,
		    worker->current->seqno);
		return ENOTSUP;
	}

	/* Takes the session's requests of this engine off the worker's run list. */
	previous = NULL;
	request = NULL;
	if (worker != NULL)
		request = worker->run_head;
	while (request != NULL) {
		next = request->next;
		if (request->session != session || request->context == NULL || request->context->engine != engine) {
			previous = request;
			request = next;
			continue;
		}

		/* Unlinks it, keeping the tail right. */
		if (previous == NULL) {
			worker->run_head = next;
		} else {
			previous->next = next;
		}

		/* The tail moves back when it was the tail. */
		if (worker->run_tail == request)
			worker->run_tail = previous;

		/* It ends with the error, never having run. */
		request->state = I915_REQUEST_DONE;
		request->error = error;
		request->next = retired;
		retired = request;
		taken++;
		request = next;
	}

	/* Counts the session's slots by state for the report, then fails its queued and reserved requests. */
	for (index = 0U; index < I915_REQUEST_SLOTS; index++) {
		if (engine->slots[index].session == session && engine->slots[index].state < 6U)
			states[engine->slots[index].state]++;
	}

	/* Fails them. */
	drv_i915_request_fail(engine, session, error, &retired);

	/* The rest is done without the lock. */
	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Delivers their completions outside the lock. */
	drv_i915_request_complete_list(engine, retired);
	kern_logf("i915: engine %u: session %u isolated (%u of its requests taken from the worker; "
	    "slots queued %u active %u done %u reserved %u retained %u)\n",
	    engine->index,
	    session->identifier,
	    taken,
	    states[I915_REQUEST_QUEUED],
	    states[I915_REQUEST_ACTIVE],
	    states[I915_REQUEST_DONE],
	    states[I915_REQUEST_RESERVED],
	    states[I915_REQUEST_RETAINED]);

	/* Succeeded: the session has no work left on the engine record. */
	return 0;
}

/*
 * Resets the whole GT under a published node.
 *
 * XXX: unimplemented path.  The GT reset exists (drv_i915_gt_reset_all());
 * it is not wired to a published node.  Logs and returns ENOTSUP.
 */
int
drv_i915_worker_gt_reset(
	struct i915_device *device)
{
	UNUSED_PARAMETER(device);

	/* Names the missing path in the log. */
	kern_logf("i915: resident shim: XXX unimplemented path: gt_reset on a published node\n");

	/* The reset is not implemented. */
	return ENOTSUP;
}

/*
 * Runs queued work in arrival order until a stop is asked for and nothing
 * is left to run, or until a display item belongs to the other side of the
 * display window: a presentation outside it (the caller lights the panel)
 * or a release inside it that cannot hold the last picture (it is
 * completed after the panel stops).  Inside the window it also returns
 * when a hold of the last picture is over.  Returns one of
 * I915_WORKER_SERVE_*.
 */
static int
i915_worker_loop(
	struct i915_worker *worker,
	int in_display)
{
	struct i915_device *device;
	struct i915_request *request;
	struct i915_worker_sync *item;
	uint64_t observed;
	unsigned long irq;
	int ready;
	int holding;
	int hold_over;

	device = worker->device;

	/* Takes each piece of work under the IRQ lock and runs it with the lock dropped. */
	irq = spin_lock_irqsave(&device->irq_lock);

	for (;;) {
		/* Sleeps in bounded steps until work arrives or a stop is asked for. */
		while (worker->run_head == NULL &&
		    worker->sync_head == NULL &&
		    worker->stop == 0) {
			/* A hold of the last picture that ran out, or that the shutdown ended, leaves the window: the output is stopped. */
			if (in_display) {
				hold_over = drv_i915_present_hold_over(device);
				if (hold_over) {
					spin_unlock_irqrestore(&device->irq_lock, irq);
					return I915_WORKER_SERVE_LEAVE_DISPLAY;
				}
			}

			observed = waitq_sequence(&worker->work);
			(void)waitq_sleep(&worker->work, &device->irq_lock, observed, sched_ticks() + I915_WORKER_SLEEP_TICKS, 0U);
		}

		/* Runs a synchronous item before the next request. */
		if (worker->sync_head != NULL) {
			item = worker->sync_head;

			/* A presentation outside the window enters it, unless the panel cannot come up. */
			if ((item->kind == I915_WORKER_SYNC_PRESENT || item->kind == I915_WORKER_SYNC_PRESENT_BLOB) && !in_display) {
				ready = drv_i915_present_window_ready(device);
				if (ready) {
					spin_unlock_irqrestore(&device->irq_lock, irq);
					return I915_WORKER_SERVE_ENTER_DISPLAY;
				}
			}

			/*
			 * A release inside the window holds the last picture for the
			 * next lease (ws075-p016) and stays in the window.  When a stop
			 * is asked for or the shutdown refuses the hold, it leaves the
			 * window instead and stays at the head: it is completed after
			 * the output is stopped.
			 */
			holding = 0;
			if (item->kind == I915_WORKER_SYNC_RELEASE && in_display) {
				if (worker->stop == 0)
					holding = drv_i915_present_hold_start(device);

				if (!holding) {
					spin_unlock_irqrestore(&device->irq_lock, irq);
					return I915_WORKER_SERVE_LEAVE_DISPLAY;
				}
			}

			worker->sync_head = item->next;
			if (worker->sync_head == NULL)
				worker->sync_tail = NULL;

			spin_unlock_irqrestore(&device->irq_lock, irq);

			/* The ended lease gives up the panel buffers before its release completes. */
			if (holding)
				drv_i915_present_hold_prepare(device);

			i915_worker_run_sync_item(worker, item, in_display);

			irq = spin_lock_irqsave(&device->irq_lock);
			continue;
		}

		/* Ends once nothing is left to run and a stop was asked for. */
		if (worker->run_head == NULL && worker->stop != 0)
			break;

		/* Takes the oldest request off the run list. */
		request = worker->run_head;
		worker->run_head = request->next;
		if (worker->run_head == NULL)
			worker->run_tail = NULL;

		request->next = NULL;
		worker->current = request;

		spin_unlock_irqrestore(&device->irq_lock, irq);

		i915_worker_run_request(worker, request);

		irq = spin_lock_irqsave(&device->irq_lock);
		worker->current = NULL;
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* A stop was asked for and nothing is left to run. */
	return I915_WORKER_SERVE_STOP;
}

/* Runs one request, retires it and delivers its completion; the IRQ lock is not held. */
static void
i915_worker_run_request(
	struct i915_worker *worker,
	struct i915_request *request)
{
	struct i915_device *device;
	struct i915_engine *engine;
	unsigned long irq;
	uint64_t start;
	int error;
	int has_batch;

	device = worker->device;
	engine = &device->engines[I915_ENGINE_RCS0];

	/* A request without a batch is a marker: the breadcrumbs alone. */
	has_batch = 0;
	if (request->batch != NULL)
		has_batch = 1;

	/* A marker's wait for the worker, and the start of its run, are timed. */
	start = 0U;
	if (!has_batch && request->queued_at != 0U) {
		drv_i915_perf_add(&device->perf, I915_PERF_MARKER_WAIT, request->queued_at);
		start = drv_i915_perf_now();
	}

	/*
	 * Runs the request to its end.  The worker runs one request at a time
	 * to completion, so a marker, which only orders itself after the
	 * requests before it, is complete the moment the worker reaches it: it
	 * is not sent to the engine.
	 */
	error = 0;
	if (has_batch)
		error = i915_worker_run(worker, request->context, request->batch_va, has_batch, request->seqno);

	/* Counts how it ended. */
	if (error == 0) {
		worker->executed++;
	} else {
		worker->failed++;
	}

	/* DONE with its outcome: the request has retired and waits for its completion. */
	irq = spin_lock_irqsave(&device->irq_lock);

	engine->completed_seqno = request->seqno;
	request->state = I915_REQUEST_DONE;
	request->error = error;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Delivers the completion, drops the session's count, frees the slot and wakes the waiters. */
	drv_i915_request_complete_list(engine, request);
	if (start != 0U)
		drv_i915_perf_add(&device->perf, I915_PERF_MARKER_RUN, start);
}

/* Does one synchronous item and wakes its caller; the IRQ lock is not held. */
static void
i915_worker_run_sync_item(
	struct i915_worker *worker,
	struct i915_worker_sync *item,
	int in_display)
{
	struct i915_device *device;
	struct i915_worker_context *record;
	unsigned long irq;
	uint64_t start;
	int error;

	device = worker->device;

	/* Does what the item asks for. */
	switch (item->kind) {
	case I915_WORKER_SYNC_BATCH:
		/* Runs the batch to its end, timing it on the engine, and counts how it ended. */
		start = drv_i915_perf_now();
		error = i915_worker_run(worker, item->context, item->batch_va, 1, 0U);
		drv_i915_perf_add(&device->perf, I915_PERF_GPU, start);

		/* The context's time in the queue and to the end (ws075-p026). */
		record = i915_worker_find(worker, item->context);
		if (record != NULL && item->queued_at != 0U) {
			record->queue_ns += start - item->queued_at;
			record->round_ns += drv_i915_perf_now() - item->queued_at;
		}

		/* How it ended. */
		if (error == 0) {
			worker->executed++;
		} else {
			worker->failed++;
		}
		break;
	case I915_WORKER_SYNC_PRESENT:
		/* Inside the window; outside it only when the panel could not be brought up. */
		error = EIO;
		if (in_display)
			error = drv_i915_present_frame(device, item->present);
		break;
	case I915_WORKER_SYNC_PRESENT_BLOB:
		/* Inside the window; outside it only when the panel could not be brought up. */
		error = EIO;
		if (in_display)
			error = drv_i915_present_blob_frame(device, item->present);
		break;
	case I915_WORKER_SYNC_BACKLIGHT:
		/* The panel's light, which only the window's lit panel has. */
		error = drv_i915_display_backlight_serve(device, in_display, item->backlight_op, &item->backlight_value);
		break;
	default:
		/* A release with the panel down (nothing to stop), or held for the next lease. */
		error = 0;
		break;
	}

	/* Done with its outcome: the caller may return and its item goes away with its stack. */
	irq = spin_lock_irqsave(&device->irq_lock);

	item->error = error;
	item->done = 1;
	waitq_wake_all(&worker->sync_done);

	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/* Runs one request of a session context on the render engine to its end. */
static int
i915_worker_run(
	struct i915_worker *worker,
	struct i915_context *context,
	uint64_t batch_va,
	int has_batch,
	uint32_t label)
{
	struct i915_device *device;
	struct i915_worker_context *record;
	struct i915_gt_request *rq;
	uint64_t start;
	int error;

	device = worker->device;

	/* XXX: only the render engine is connected; a request on any other engine record fails. */
	if (context == NULL ||
	    context->engine == NULL ||
	    context->engine->index != I915_ENGINE_RCS0) {
		kern_logf("i915: resident shim: XXX unimplemented path: request on an engine other than RCS0\n");
		return ENOTSUP;
	}

	/* Finds the hardware context behind the session context. */
	record = i915_worker_find(worker, context);
	if (record == NULL)
		return EINVAL;

	/*
	 * XXX: the ring never wraps (a request that would is refused).  The
	 * context is idle between requests here, so the ring is simply rewound
	 * when it is nearly full.
	 */
	if (record->ce.ring.emit + I915_WORKER_REQUEST_ROOM > record->ce.ring.size) {
		record->ce.ring.head = 0U;
		record->ce.ring.tail = 0U;
		record->ce.ring.emit = 0U;
		(void)drv_i915_lrc_update_regs(&record->ce, 0U);
		kern_logf("i915: resident shim: ring rewound (idle context)\n");
	}

	/* Starts the request on the context's timeline; each request takes two seqnos. */
	rq = &record->rq;
	kern_memset(rq, 0, sizeof(*rq));
	record->tl_seqno += 2U;
	error = drv_i915_request_create(
		rq,
		&record->ce,
		record->tl_seqno,
		(uint32_t)record->tl_page->ggtt_offset,
		(volatile uint32_t *)record->tl_page->cpu);
	if (error != 0)
		return error;

	/* Writes the initial breadcrumb and the batch start. */
	error = i915_worker_emit(rq, batch_va, has_batch);
	if (error != 0)
		return error;

	/* Closes the request with the final breadcrumb. */
	error = drv_i915_request_add(rq);
	if (error != 0)
		return error;

	/* Submits the request through the render engine's execlists. */
	error = drv_i915_execlists_submit(
		&device->gt.engines.ge[worker->render_index],
		&device->gt.engines.el[worker->render_index],
		&device->gt.mmio,
		rq);
	if (error != 0) {
		kern_logf("i915: resident shim: execlists_submit rc=%d\n", error);
		return error;
	}

	/* The engine is busy with the request from here: RPS follows the busy time (ws075-p020). */
	drv_i915_rps_busy_begin(&device->gt.init.rps);

	/* Waits for the request to end, counting the engine's time to its context. */
	start = drv_i915_perf_now();
	error = i915_worker_wait(worker, rq, batch_va, label);
	record->engine_ns += drv_i915_perf_now() - start;
	record->engine_runs++;

	/* The engine is done with it, whichever way it ended. */
	drv_i915_rps_busy_end(&device->gt.init.rps);

	/* A request that did not end reports why. */
	if (error != 0)
		return error;

	/* Succeeded: the request ran to its end. */
	return 0;
}

/* Writes a request's initial breadcrumb and, unless it is a marker, its batch start. */
static int
i915_worker_emit(
	struct i915_gt_request *rq,
	uint64_t batch_va,
	int has_batch)
{
	uint32_t *cs;

	/* Reserves room for the initial breadcrumb (gen8_emit_init_breadcrumb()). */
	cs = drv_i915_ring_begin(rq, 6U);
	if (cs == NULL)
		return rq->error;

	/* Stores seqno - 1 in the timeline page: the request has started. */
	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = rq->hwsp_ggtt;
	*cs++ = 0U;
	*cs++ = rq->seqno - 1U;
	*cs++ = MI_NOOP;
	*cs++ = MI_ARB_CHECK;
	drv_i915_ring_advance(rq, cs);

	/* A marker (a job with no batch) is the breadcrumbs alone. */
	if (has_batch == 0)
		return 0;

	/* Reserves room for the batch start (gen8_emit_bb_start()). */
	cs = drv_i915_ring_begin(rq, 6U);
	if (cs == NULL)
		return rq->error;

	/* Starts the non-secure batch in the context's address space with arbitration on around it. */
	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	*cs++ = MI_BATCH_BUFFER_START_GEN8 | MI_BATCH_NON_SECURE_I965;
	*cs++ = (uint32_t)batch_va;
	*cs++ = (uint32_t)(batch_va >> 32);
	*cs++ = MI_ARB_ON_OFF;
	*cs++ = MI_NOOP;
	drv_i915_ring_advance(rq, cs);

	/* Succeeded: the request's commands are in the ring. */
	return 0;
}

/*
 * Waits until a submitted request has ended, or times out.
 *
 * The context status buffer is processed, and between two looks the worker
 * sleeps until the engine's next interrupt, one tick at most.  Without an
 * interrupt handler it polls every 50 microseconds instead.
 */
static int
i915_worker_wait(
	struct i915_worker *worker,
	struct i915_gt_request *rq,
	uint64_t batch_va,
	uint32_t label)
{
	struct i915_device *device;
	struct i915_gt_engine *ge;
	struct i915_execlists *el;
	uint64_t deadline;
	uint64_t observed;
	uint64_t now;
	int completed;
	int error;

	device = worker->device;
	ge = &device->gt.engines.ge[worker->render_index];
	el = &device->gt.engines.el[worker->render_index];

	/* Looks at the status buffer until the request ends, for at most the timeout. */
	deadline = sched_ticks() + KERN_MS_TO_TICKS(I915_WORKER_TIMEOUT_MS);
	for (;;) {
		/* Notes the engine interrupts so far before the look, so one during the look is not missed. */
		observed = drv_i915_irq_engine_sequence(&device->gt.irq);

		/* Applies what the engine reported. */
		(void)drv_i915_execlists_process_csb(ge, el, &device->gt.mmio);

		/* An event with nothing to apply to fails the request; the first few failures say so (BUG-077). */
		if (el->csb_errors != 0U) {
			if (worker->failed < 4U) {
				kern_logf("i915: resident shim: request seqno=%u batch_va=0x%llx fails: %u CSB errors (hwsp=%u)\n",
				    label,
				    (unsigned long long)batch_va,
				    el->csb_errors,
				    (unsigned)*rq->hwsp_cpu);
			}

			/* The request did not end. */
			return EIO;
		}

		/* Ends once the breadcrumb landed and the engine holds nothing more. */
		completed = drv_i915_request_completed(rq);
		if (completed != 0) {
			if (el->have_active == 0 && el->pending[0] == NULL)
				return 0;
		}

		/* The timeout is over. */
		now = sched_ticks();
		if (now >= deadline)
			break;

		/* Sleeps until the engine's next interrupt, or one tick at most. */
		error = drv_i915_irq_engine_wait(&device->gt.irq, observed, I915_WORKER_WAIT_TICKS);
		if (error == 0) {
			worker->wait_interrupts++;
			continue;
		}

		/* The bound ran out: a lost interrupt costs one tick, not the request. */
		if (error == ETIMEDOUT) {
			worker->wait_timeouts++;
			continue;
		}

		/* No handler: pauses before the next poll; a failed time base fails the request. */
		error = drv_i915_udelay(I915_WORKER_POLL_US);
		if (error != 0)
			return EIO;
	}

	/* XXX: unimplemented path -- a hang.  No reset, no recovery: the request fails and the log says so. */
	kern_logf("i915: resident shim: XXX request seqno=%u batch_va=0x%llx did not complete in %u ms "
	    "(no recovery path; hwsp=%u last_csb=%08x:%08x)\n",
	    label,
	    (unsigned long long)batch_va,
	    I915_WORKER_TIMEOUT_MS,
	    (unsigned)*rq->hwsp_cpu,
	    el->last_csb_hi,
	    el->last_csb_lo);

	/* The request did not end in time. */
	return ETIMEDOUT;
}

/* Queues one synchronous item, wakes the worker and sleeps until the item is done. */
static int
i915_worker_queue_sync(
	struct i915_device *device,
	struct i915_worker_sync *item)
{
	struct i915_worker *worker;
	uint64_t observed;
	unsigned long irq;
	int behind;

	/* A worker that is not serving does nothing. */
	worker = device->worker;
	if (worker == NULL || worker->serving == 0) {
		kern_logf("i915: resident shim: XXX unimplemented path: work outside resident mode\n");
		return ENODEV;
	}

	/* Queues the item and wakes the worker under the IRQ lock. */
	irq = spin_lock_irqsave(&device->irq_lock);

	/* A batch queued behind other work will not start at once (ws075-p020). */
	behind = 0;
	if (item->kind == I915_WORKER_SYNC_BATCH &&
	    (worker->sync_head != NULL ||
	     worker->run_head != NULL))
		behind = 1;

	if (worker->sync_tail == NULL) {
		worker->sync_head = item;
	} else {
		worker->sync_tail->next = item;
	}

	worker->sync_tail = item;
	waitq_wake_all(&worker->work);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* The caller waits on a batch the GT has not started: the GT frequency is boosted until it is done (Linux's waitboost). */
	if (behind)
		drv_i915_rps_boost_begin(&device->gt.init.rps);

	/* Sleeps in bounded steps until the worker marks the item done. */
	irq = spin_lock_irqsave(&device->irq_lock);

	while (item->done == 0) {
		observed = waitq_sequence(&worker->sync_done);
		(void)waitq_sleep(&worker->sync_done, &device->irq_lock, observed, sched_ticks() + I915_WORKER_SLEEP_TICKS, 0U);
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* The boost's waiter is done. */
	if (behind)
		drv_i915_rps_boost_end(&device->gt.init.rps);

	/* Reports how the item ended. */
	if (item->error != 0)
		return item->error;

	/* Succeeded: the item was done. */
	return 0;
}

/* Finds the hardware context record behind a session context, or NULL. */
static struct i915_worker_context *
i915_worker_find(
	struct i915_worker *worker,
	const struct i915_context *context)
{
	unsigned index;

	/* Scans the context table for the record the session context owns. */
	for (index = 0U; index < I915_WORKER_CONTEXTS; index++) {
		if (worker->contexts[index].owner == context)
			return &worker->contexts[index];
	}

	/* No record stands behind the session context. */
	return NULL;
}
