/*
 * WS031 Linux-parity — delayed work (see backend_delayed.h).  zedBSD project code.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/waitq.h>
#include <kern/thread.h>
#include <kern/sched.h>
#include <kern/clock.h>
#include "backend_delayed.h"

static void
ktimer_thread(void *arg)
{
	struct parity_ktimerq *tq = (struct parity_ktimerq *)arg;

	spin_lock(&tq->lock);
	for (;;) {
		struct parity_kdelayed *due = 0;
		uint64_t earliest = 0, now, obs;
		unsigned i;

		if (tq->stop)
			break;
		now = sched_ticks();
		for (i = 0u; i < PARITY_KDELAYED_MAX; i++) {
			struct parity_kdelayed *dw = tq->armed[i];

			if (dw == 0)
				continue;
			if (dw->deadline <= now) {
				due = dw;
				tq->armed[i] = 0;
				break;
			}
			if (earliest == 0u || dw->deadline < earliest)
				earliest = dw->deadline;
		}
		if (due != 0) {
			/*
			 * Hand over without holding tq->lock across the work queue lock:
			 * `firing` keeps cancel / queue out until the work is PENDING.
			 */
			due->armed = 0;
			due->firing = 1;
			due->fired_count++;
			spin_unlock(&tq->lock);
			(void)parity_kqueue_work(tq->wq, &due->work);
			spin_lock(&tq->lock);
			due->firing = 0;
			waitq_wake_all(&tq->waitq);
			continue;
		}
		obs = waitq_sequence(&tq->waitq);
		(void)waitq_sleep(&tq->waitq, &tq->lock, obs, earliest, 0u);   /* 0 = no deadline */
	}
	tq->alive = 0;
	waitq_wake_all(&tq->waitq);
	spin_unlock(&tq->lock);
}

int
parity_ktimerq_create(struct parity_ktimerq *tq, struct parity_kworkqueue *wq, const char *name)
{
	struct thread *thread;
	unsigned i;
	int rc;

	spin_init(&tq->lock, LOCK_RANK_DEVICE, name);
	waitq_init(&tq->waitq, name);
	for (i = 0u; i < PARITY_KDELAYED_MAX; i++)
		tq->armed[i] = 0;
	tq->wq = wq;
	tq->stop = 0;
	tq->alive = 1;
	tq->thread = 0;
	tq->name = name;
	rc = kthread_create(ktimer_thread, tq, SCHED_PRIORITY_DEFAULT, &thread);
	if (rc != 0) {
		tq->alive = 0;
		return -rc;
	}
	tq->thread = thread;
	thread->detached = 1;
	thread_start(thread);
	return 0;
}

void
parity_ktimerq_destroy(struct parity_ktimerq *tq)
{
	spin_lock(&tq->lock);
	tq->stop = 1;
	waitq_wake_all(&tq->waitq);
	while (tq->alive) {
		uint64_t obs = waitq_sequence(&tq->waitq);

		(void)waitq_sleep(&tq->waitq, &tq->lock, obs, 0u, 0u);
	}
	spin_unlock(&tq->lock);
}

void
parity_kdelayed_init(struct parity_kdelayed *dw, void (*fn)(void *), void *ctx)
{
	parity_kwork_init(&dw->work, fn, ctx);
	dw->tq = 0;
	dw->armed = 0;
	dw->firing = 0;
	dw->deadline = 0u;
	dw->armed_count = dw->fired_count = dw->cancelled_armed = dw->cancelled_pending = 0u;
}

/* with tq->lock held: wait out a hand-over in progress */
static void
wait_not_firing(struct parity_ktimerq *tq, struct parity_kdelayed *dw)
{
	while (dw->firing) {
		uint64_t obs = waitq_sequence(&tq->waitq);

		(void)waitq_sleep(&tq->waitq, &tq->lock, obs, 0u, 0u);
	}
}

/* with tq->lock held */
static int
disarm(struct parity_ktimerq *tq, struct parity_kdelayed *dw)
{
	unsigned i;

	if (!dw->armed)
		return 0;
	for (i = 0u; i < PARITY_KDELAYED_MAX; i++)
		if (tq->armed[i] == dw)
			tq->armed[i] = 0;
	dw->armed = 0;
	return 1;
}

int
parity_kdelayed_queue(struct parity_ktimerq *tq, struct parity_kdelayed *dw, unsigned delay_ms)
{
	uint64_t deadline = 0;
	unsigned i;

	spin_lock(&tq->lock);
	wait_not_firing(tq, dw);
	if (dw->armed || parity_kwork_is_pending(tq->wq, &dw->work)) {
		spin_unlock(&tq->lock);
		return 0;
	}
	/* never earlier than asked: the current tick is partly used, hence + 1 */
	if (kern_deadline_after(sched_ticks(),
	    ((uint64_t)delay_ms * KERN_CLOCK_HZ + 999u) / 1000u + 1u, &deadline) != 0) {
		spin_unlock(&tq->lock);
		return 0;
	}
	for (i = 0u; i < PARITY_KDELAYED_MAX; i++)
		if (tq->armed[i] == 0)
			break;
	if (i == PARITY_KDELAYED_MAX) {
		spin_unlock(&tq->lock);
		kern_logf("i915: parity delayed work: no free timer slot (%s)\n", tq->name);
		return 0;
	}
	dw->tq = tq;
	dw->deadline = deadline;
	dw->armed = 1;
	dw->armed_count++;
	tq->armed[i] = dw;
	waitq_wake_all(&tq->waitq);
	spin_unlock(&tq->lock);
	return 1;
}

int
parity_kdelayed_cancel(struct parity_ktimerq *tq, struct parity_kdelayed *dw)
{
	int was;

	spin_lock(&tq->lock);
	wait_not_firing(tq, dw);
	was = disarm(tq, dw);
	if (was)
		dw->cancelled_armed++;
	spin_unlock(&tq->lock);
	if (!was && parity_kcancel_work(tq->wq, &dw->work)) {
		was = 1;
		dw->cancelled_pending++;
	}
	return was;
}

int
parity_kdelayed_cancel_sync(struct parity_ktimerq *tq, struct parity_kdelayed *dw, uint64_t deadline)
{
	int was;

	spin_lock(&tq->lock);
	wait_not_firing(tq, dw);
	was = disarm(tq, dw);
	if (was)
		dw->cancelled_armed++;
	spin_unlock(&tq->lock);
	if (parity_kcancel_work_sync(tq->wq, &dw->work, deadline)) {
		was = 1;
		dw->cancelled_pending++;
	}
	return was;
}

int
parity_kdelayed_flush(struct parity_ktimerq *tq, struct parity_kdelayed *dw, uint64_t deadline)
{
	int was;

	spin_lock(&tq->lock);
	wait_not_firing(tq, dw);
	was = disarm(tq, dw);
	spin_unlock(&tq->lock);
	if (was)
		(void)parity_kqueue_work(tq->wq, &dw->work);
	return parity_kflush_work(tq->wq, &dw->work, deadline) || was;
}

int
parity_kdelayed_pending(struct parity_ktimerq *tq, struct parity_kdelayed *dw)
{
	int p;

	spin_lock(&tq->lock);
	p = dw->armed || dw->firing;
	spin_unlock(&tq->lock);
	return p || parity_kwork_is_pending(tq->wq, &dw->work);
}
