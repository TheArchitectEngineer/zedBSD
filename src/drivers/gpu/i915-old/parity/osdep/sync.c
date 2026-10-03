/* WS031 Linux-parity OS adaptation layer — synchronization & deferred work (see sync.h). */
#include "sync.h"

static void
tr(struct osdep_trace *t, uint16_t op, const char *what, uint64_t a0, uint64_t a1)
{
	if (t != 0)
		osdep_trace_emit(t, 0u, op, what, a0, a1);
}

/* ---- completion (counting) ---- */

void
osdep_completion_init(struct osdep_completion *c, struct osdep_trace *trace)
{
	c->done = 0u;
	c->all = 0;
	c->trace = trace;
}

void
osdep_reinit_completion(struct osdep_completion *c)
{
	c->done = 0u;
	c->all = 0;
	tr(c->trace, OSDEP_TR_NOTE, "reinit_completion", 0u, 0u);
}

void
osdep_complete(struct osdep_completion *c)
{
	if (!c->all)
		c->done++;
	tr(c->trace, OSDEP_TR_NOTE, "complete", c->done, 0u);
}

void
osdep_complete_all(struct osdep_completion *c)
{
	c->all = 1;
	tr(c->trace, OSDEP_TR_NOTE, "complete_all", 0u, 0u);
}

int
osdep_completion_done(const struct osdep_completion *c)
{
	return c->all || c->done > 0u;
}

static int
try_consume(struct osdep_completion *c)
{
	if (c->all)
		return 1;             /* permanent: never consumed */
	if (c->done > 0u) {
		c->done--;            /* consume exactly one permit */
		return 1;
	}
	return 0;
}

int
osdep_wait_for_completion_timeout(struct osdep_completion *c, unsigned max_iters,
				  void (*tick)(void *), void *ctx)
{
	unsigned i;

	if (try_consume(c)) {
		tr(c->trace, OSDEP_TR_NOTE, "wait_already_done", 0u, 0u);
		return (int)max_iters;
	}
	for (i = 0u; i < max_iters; i++) {
		if (tick != 0)
			tick(ctx);
		if (try_consume(c)) {
			tr(c->trace, OSDEP_TR_NOTE, "wait_completed", (uint64_t)(max_iters - i - 1u), 0u);
			return (int)(max_iters - i - 1u) + 1;
		}
	}
	tr(c->trace, OSDEP_TR_FAIL, "wait_timeout", 0u, 0u);
	return 0;
}

/* ---- deferred work ---- */

void
osdep_work_init(struct osdep_work *w, void (*fn)(void *), void *ctx)
{
	w->fn = fn;
	w->ctx = ctx;
	w->state = OSDEP_WORK_IDLE;
	w->queued = 0;
	w->ran_count = 0;
	w->canceled = 0;
	w->run_order = 0u;
}

void
osdep_workqueue_init(struct osdep_workqueue *wq, int ordered, struct osdep_trace *trace)
{
	unsigned i;

	for (i = 0u; i < OSDEP_WQ_DEPTH; i++)
		wq->slots[i] = 0;
	wq->head = wq->tail = wq->count = 0u;
	wq->ordered = ordered;
	wq->running = 0;
	wq->run_seq = 0u;
	wq->trace = trace;
}

int
osdep_queue_work(struct osdep_workqueue *wq, struct osdep_work *w)
{
	/* Already pending: idempotent.  A RUNNING work is NOT pending -> may re-queue. */
	if (w->queued) {
		tr(wq->trace, OSDEP_TR_NOTE, "queue_work_already_pending", (uint64_t)(uintptr_t)w, 0u);
		return 0;
	}
	if (wq->count >= OSDEP_WQ_DEPTH)
		return 0;

	w->queued = 1;
	w->canceled = 0;
	if (w->state == OSDEP_WORK_IDLE)
		w->state = OSDEP_WORK_PENDING;
	/* if RUNNING, it stays RUNNING but is now also queued again (re-run later). */
	wq->slots[wq->tail] = w;
	wq->tail = (wq->tail + 1u) % OSDEP_WQ_DEPTH;
	wq->count++;
	/* Deferred; a successful queue publishes the caller's prior writes (release). */
	tr(wq->trace, OSDEP_TR_WORK_ENQ, "queue_work", (uint64_t)(uintptr_t)w, wq->count);
	return 1;
}

static int
remove_pending(struct osdep_workqueue *wq, struct osdep_work *w)
{
	unsigned i, idx, j;

	if (!w->queued)
		return 0;
	for (i = 0u; i < wq->count; i++) {
		idx = (wq->head + i) % OSDEP_WQ_DEPTH;
		if (wq->slots[idx] == w) {
			for (j = i; j + 1u < wq->count; j++)
				wq->slots[(wq->head + j) % OSDEP_WQ_DEPTH] =
					wq->slots[(wq->head + j + 1u) % OSDEP_WQ_DEPTH];
			wq->tail = (wq->tail + OSDEP_WQ_DEPTH - 1u) % OSDEP_WQ_DEPTH;
			wq->count--;
			w->queued = 0;
			if (w->state == OSDEP_WORK_PENDING)
				w->state = OSDEP_WORK_IDLE;
			return 1;
		}
	}
	return 0;
}

int
osdep_cancel_work(struct osdep_workqueue *wq, struct osdep_work *w)
{
	if (remove_pending(wq, w)) {
		w->canceled = 1;
		tr(wq->trace, OSDEP_TR_WORK_CANCEL, "cancel_work", (uint64_t)(uintptr_t)w, wq->count);
		return 1;
	}
	return 0;
}

int
osdep_cancel_work_sync(struct osdep_workqueue *wq, struct osdep_work *w)
{
	int was_active = (w->state != OSDEP_WORK_IDLE) || w->queued;

	(void)remove_pending(wq, w);
	w->canceled = 1;
	/*
	 * Guarantee on return: the work is not running.  The real backend blocks on
	 * the running flag here; the single-threaded host flush cannot be mid-callback
	 * when called from outside flush, so the guarantee already holds.
	 */
	tr(wq->trace, OSDEP_TR_WORK_CANCEL, "cancel_work_sync", (uint64_t)(uintptr_t)w,
	   (uint64_t)(w->state == OSDEP_WORK_RUNNING));
	return was_active;
}

void
osdep_flush_workqueue(struct osdep_workqueue *wq)
{
	wq->running = 1;
	while (wq->count > 0u) {
		struct osdep_work *w = wq->slots[wq->head];
		wq->slots[wq->head] = 0;
		wq->head = (wq->head + 1u) % OSDEP_WQ_DEPTH;
		wq->count--;

		/* Pending cleared BEFORE the callback: a self-requeue inside fn re-queues it. */
		w->queued = 0;
		w->state = OSDEP_WORK_RUNNING;
		tr(wq->trace, OSDEP_TR_WORK_BEGIN, "work", (uint64_t)(uintptr_t)w, 0u);
		w->run_order = ++wq->run_seq;
		w->ran_count++;
		if (w->fn != 0)
			w->fn(w->ctx);
		/* If fn re-queued it, it is PENDING again; otherwise IDLE. */
		if (!w->queued)
			w->state = OSDEP_WORK_IDLE;
		else
			w->state = OSDEP_WORK_PENDING;
		tr(wq->trace, OSDEP_TR_WORK_END, "work", (uint64_t)(uintptr_t)w, w->run_order);
	}
	wq->running = 0;
}

unsigned
osdep_workqueue_pending(const struct osdep_workqueue *wq)
{
	return wq->count;
}

int
osdep_work_is_running(const struct osdep_work *w)
{
	return w->state == OSDEP_WORK_RUNNING;
}
