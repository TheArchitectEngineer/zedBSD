/* WS031 Linux-parity — shared kernel sync backend (see backend_sync.h). */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/waitq.h>
#include <kern/thread.h>
#include <kern/sched.h>
#include <kern/clock.h>
#include "backend_sync.h"

/* ================= completion ================= */

void
parity_kcompletion_init(struct parity_kcompletion *c, const char *name)
{
	spin_init(&c->lock, LOCK_RANK_DEVICE, name);
	waitq_init(&c->waitq, name);
	c->done = 0u;
	c->all = 0;
}

void
parity_kcomplete(struct parity_kcompletion *c)
{
	unsigned long f = spin_lock_irqsave(&c->lock);   /* IRQ-safe: callable from IRQ context */
	if (!c->all)
		c->done++;
	waitq_wake_all(&c->waitq);
	spin_unlock_irqrestore(&c->lock, f);
}

void
parity_kcomplete_all(struct parity_kcompletion *c)
{
	unsigned long f = spin_lock_irqsave(&c->lock);
	c->all = 1;
	waitq_wake_all(&c->waitq);
	spin_unlock_irqrestore(&c->lock, f);
}

void
parity_kreinit_completion(struct parity_kcompletion *c)
{
	unsigned long f = spin_lock_irqsave(&c->lock);
	c->done = 0u;
	c->all = 0;
	spin_unlock_irqrestore(&c->lock, f);
}

int
parity_kwait(struct parity_kcompletion *c, uint64_t deadline)
{
	int ret = 0;
	unsigned long f;

	f = spin_lock_irqsave(&c->lock);
	for (;;) {
		uint64_t obs;

		if (c->all) { ret = 1; break; }
		if (c->done > 0u) { c->done--; ret = 1; break; }
		if (sched_ticks() >= deadline) { ret = 0; break; }
		obs = waitq_sequence(&c->waitq);
		(void)waitq_sleep(&c->waitq, &c->lock, obs, deadline, 0u);
	}
	spin_unlock_irqrestore(&c->lock, f);
	return ret;
}

/* ================= workqueue ================= */

void
parity_kwork_init(struct parity_kwork *w, void (*fn)(void *), void *ctx)
{
	w->fn = fn;
	w->ctx = ctx;
	w->state = PARITY_KWORK_IDLE;
	w->queued = 0;
	w->ran_count = 0;
	w->run_order = 0u;
	w->wq = 0;
	waitq_init(&w->done_waitq, "parity-kwork-done");
}

/* Remove w from the ring if present; caller holds wq->lock.  Returns 1 if removed. */
static int
kwq_remove_pending(struct parity_kworkqueue *wq, struct parity_kwork *w)
{
	unsigned i, idx, j;

	if (!w->queued)
		return 0;
	for (i = 0u; i < wq->count; i++) {
		idx = (wq->head + i) % PARITY_KWQ_DEPTH;
		if (wq->slots[idx] == w) {
			for (j = i; j + 1u < wq->count; j++)
				wq->slots[(wq->head + j) % PARITY_KWQ_DEPTH] =
					wq->slots[(wq->head + j + 1u) % PARITY_KWQ_DEPTH];
			wq->tail = (wq->tail + PARITY_KWQ_DEPTH - 1u) % PARITY_KWQ_DEPTH;
			wq->count--;
			w->queued = 0;
			if (w->state == PARITY_KWORK_PENDING)
				w->state = PARITY_KWORK_IDLE;
			return 1;
		}
	}
	return 0;
}

static void
kworker_thread(void *arg)
{
	unsigned long f;   /* IRQ-safe: queue_work() may be called from an interrupt handler */
	struct parity_kworkqueue *wq = (struct parity_kworkqueue *)arg;

	f = spin_lock_irqsave(&wq->lock);
	for (;;) {
		if (wq->stop && wq->count == 0u)
			break;
		if (wq->count > 0u) {
			struct parity_kwork *w = wq->slots[wq->head];

			wq->slots[wq->head] = 0;
			wq->head = (wq->head + 1u) % PARITY_KWQ_DEPTH;
			wq->count--;
			/* PENDING cleared before the callback: a self-requeue re-queues it. */
			w->queued = 0;
			w->state = PARITY_KWORK_RUNNING;
			w->run_order = ++wq->run_seq;
			w->ran_count++;
			spin_unlock_irqrestore(&wq->lock, f);
			if (w->fn != 0)
				w->fn(w->ctx);
			f = spin_lock_irqsave(&wq->lock);
			w->state = w->queued ? PARITY_KWORK_PENDING : PARITY_KWORK_IDLE;
			/* Wake cancel_work_sync waiters blocked on this work finishing. */
			waitq_wake_all(&w->done_waitq);
		} else {
			uint64_t obs = waitq_sequence(&wq->waitq);
			(void)waitq_sleep(&wq->waitq, &wq->lock, obs, 0u, 0u);
		}
	}
	wq->worker_alive = 0;
	waitq_wake_all(&wq->waitq);
	spin_unlock_irqrestore(&wq->lock, f);
}

int
parity_kworkqueue_create(struct parity_kworkqueue *wq, const char *name)
{
	struct thread *thread;
	int rc;
	unsigned i;

	spin_init(&wq->lock, LOCK_RANK_DEVICE, name);
	waitq_init(&wq->waitq, name);
	for (i = 0u; i < PARITY_KWQ_DEPTH; i++)
		wq->slots[i] = 0;
	wq->head = wq->tail = wq->count = 0u;
	wq->run_seq = 0u;
	wq->stop = 0;
	wq->worker_alive = 1;
	wq->worker = 0;
	wq->name = name;

	rc = kthread_create(kworker_thread, wq, SCHED_PRIORITY_DEFAULT, &thread);
	if (rc != 0) {
		wq->worker_alive = 0;
		return -rc;
	}
	wq->worker = thread;
	thread->detached = 1;
	thread_start(thread);
	return 0;
}

int
parity_kqueue_work(struct parity_kworkqueue *wq, struct parity_kwork *w)
{
	unsigned long f;   /* IRQ-safe: queue_work() may be called from an interrupt handler */
	int ret;

	f = spin_lock_irqsave(&wq->lock);
	if (w->queued) {
		spin_unlock_irqrestore(&wq->lock, f);
		return 0;   /* already pending */
	}
	if (wq->count >= PARITY_KWQ_DEPTH) {
		spin_unlock_irqrestore(&wq->lock, f);
		return 0;
	}
	w->wq = wq;
	w->queued = 1;
	if (w->state == PARITY_KWORK_IDLE)
		w->state = PARITY_KWORK_PENDING;
	wq->slots[wq->tail] = w;
	wq->tail = (wq->tail + 1u) % PARITY_KWQ_DEPTH;
	wq->count++;
	waitq_wake_all(&wq->waitq);   /* wake the worker */
	ret = 1;
	spin_unlock_irqrestore(&wq->lock, f);
	return ret;
}

int
parity_kcancel_work(struct parity_kworkqueue *wq, struct parity_kwork *w)
{
	unsigned long f;   /* IRQ-safe: queue_work() may be called from an interrupt handler */
	int was_pending;

	f = spin_lock_irqsave(&wq->lock);
	was_pending = kwq_remove_pending(wq, w);
	spin_unlock_irqrestore(&wq->lock, f);
	return was_pending;
}

int
parity_kwork_is_pending(struct parity_kworkqueue *wq, struct parity_kwork *w)
{
	unsigned long f;   /* IRQ-safe: queue_work() may be called from an interrupt handler */
	int pending;

	f = spin_lock_irqsave(&wq->lock);
	pending = w->queued;
	spin_unlock_irqrestore(&wq->lock, f);
	return pending;
}

int
parity_kcancel_work_sync(struct parity_kworkqueue *wq, struct parity_kwork *w, uint64_t deadline)
{
	unsigned long f;   /* IRQ-safe: queue_work() may be called from an interrupt handler */
	int was_pending;

	f = spin_lock_irqsave(&wq->lock);
	was_pending = kwq_remove_pending(wq, w);
	/* Guarantee on return: the work is not running.  Block until it finishes. */
	for (;;) {
		uint64_t obs;

		if (w->state != PARITY_KWORK_RUNNING)
			break;
		if (sched_ticks() >= deadline)
			break;   /* safety bound; not a normal outcome */
		obs = waitq_sequence(&w->done_waitq);
		(void)waitq_sleep(&w->done_waitq, &wq->lock, obs, deadline, 0u);
	}
	spin_unlock_irqrestore(&wq->lock, f);
	return was_pending;
}

/*
 * flush_work(): block until the work has completed, WITHOUT removing it from the
 * queue.  A pending work is left to run; we wait for RUNNING -> IDLE.  Returns 1
 * if the work was pending or running, 0 if already idle (idle is not an error).
 */
int
parity_kflush_work(struct parity_kworkqueue *wq, struct parity_kwork *w, uint64_t deadline)
{
	unsigned long f;   /* IRQ-safe: queue_work() may be called from an interrupt handler */
	int was_active;

	f = spin_lock_irqsave(&wq->lock);
	was_active = (w->state != PARITY_KWORK_IDLE) ? 1 : 0;
	/* Wait for the work to leave PENDING/RUNNING; do NOT dequeue it. */
	for (;;) {
		uint64_t obs;

		if (w->state == PARITY_KWORK_IDLE)
			break;
		if (sched_ticks() >= deadline)
			break;   /* safety bound; not a normal outcome */
		obs = waitq_sequence(&w->done_waitq);
		(void)waitq_sleep(&w->done_waitq, &wq->lock, obs, deadline, 0u);
	}
	spin_unlock_irqrestore(&wq->lock, f);
	return was_active;
}

void
parity_kworkqueue_destroy(struct parity_kworkqueue *wq)
{
	unsigned long f;   /* IRQ-safe: queue_work() may be called from an interrupt handler */
	f = spin_lock_irqsave(&wq->lock);
	wq->stop = 1;
	waitq_wake_all(&wq->waitq);
	while (wq->worker_alive) {
		uint64_t obs = waitq_sequence(&wq->waitq);
		(void)waitq_sleep(&wq->waitq, &wq->lock, obs, 0u, 0u);
	}
	spin_unlock_irqrestore(&wq->lock, f);
}
