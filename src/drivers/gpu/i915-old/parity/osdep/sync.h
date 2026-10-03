/*
 * WS031 Linux-parity OS adaptation layer — synchronization & deferred work.
 *
 * rev2 (M2 §1.3): the contract now matches the Linux APIs it stands for, not a
 * simplified mock:
 *
 *   - completion carries a COUNT, not a bool.  complete() adds one permit that a
 *     wait consumes; complete_all() opens it permanently; reinit resets it.  N
 *     completes satisfy N waits.
 *   - work has distinct PENDING and RUNNING states.  Pending is cleared before
 *     the callback runs, so a work may legitimately re-queue itself while running
 *     and run again (self-requeue is not dropped as a duplicate).
 *   - cancel_work removes a still-pending work; cancel_work_sync additionally
 *     does not report "not running" until the callback has finished (the real
 *     backend blocks; the host model exposes the running flag so a single-thread
 *     test can assert the distinction, and the kernel-backend test exercises the
 *     concurrent case).
 *   - ordered vs concurrent queues are distinct; FIFO is only promised on ordered.
 *
 * Execution stays deferred: the portable layer queues; a driver worker (real
 * backend) or a test drains.  queue_work never becomes a direct call, and a
 * successful queue publishes the caller's prior writes to the worker (the real
 * backend inserts the release/acquire; the model documents the requirement).
 */
#ifndef PARITY_OSDEP_SYNC_H
#define PARITY_OSDEP_SYNC_H

#include <stdint.h>
#include "trace.h"

/* ---- completion (counting) ---- */
struct osdep_completion {
	unsigned done;        /* number of unconsumed permits */
	int all;              /* complete_all: permanently signalled */
	struct osdep_trace *trace;
};

void osdep_completion_init(struct osdep_completion *c, struct osdep_trace *trace);
void osdep_reinit_completion(struct osdep_completion *c);
void osdep_complete(struct osdep_completion *c);       /* +1 permit */
void osdep_complete_all(struct osdep_completion *c);   /* open permanently */
int  osdep_completion_done(const struct osdep_completion *c);

/*
 * Wait up to max_iters.  A permit already available is consumed at once (no tick).
 * Otherwise each iteration calls tick(ctx) (models IRQ/worker) and re-checks.
 * On success consumes one permit (unless complete_all) and returns iters left+1
 * (>0); returns 0 on timeout.
 */
int osdep_wait_for_completion_timeout(struct osdep_completion *c, unsigned max_iters,
				      void (*tick)(void *), void *ctx);

/* ---- deferred work ---- */
enum osdep_work_state {
	OSDEP_WORK_IDLE = 0,
	OSDEP_WORK_PENDING,   /* queued, not yet executing */
	OSDEP_WORK_RUNNING    /* callback executing (pending already cleared) */
};

struct osdep_work {
	void (*fn)(void *);
	void *ctx;
	int state;            /* enum osdep_work_state */
	int queued;           /* currently in the ring */
	int ran_count;        /* number of completed executions */
	int canceled;         /* last cancel removed it while pending */
	uint32_t run_order;   /* last execution order, for FIFO checks */
};

#ifndef OSDEP_WQ_DEPTH
#define OSDEP_WQ_DEPTH 64u
#endif

struct osdep_workqueue {
	struct osdep_work *slots[OSDEP_WQ_DEPTH];
	unsigned head, tail, count;
	int ordered;          /* FIFO guaranteed only when set */
	int running;          /* inside flush */
	uint32_t run_seq;
	struct osdep_trace *trace;
};

void osdep_work_init(struct osdep_work *w, void (*fn)(void *), void *ctx);
void osdep_workqueue_init(struct osdep_workqueue *wq, int ordered, struct osdep_trace *trace);

/*
 * Enqueue.  Returns 1 if newly queued, 0 if already pending (idempotent).  A
 * RUNNING work is NOT pending, so it may be re-queued and will run again.
 */
int osdep_queue_work(struct osdep_workqueue *wq, struct osdep_work *w);
/* Remove if still pending.  Returns 1 if removed, 0 otherwise. */
int osdep_cancel_work(struct osdep_workqueue *wq, struct osdep_work *w);
/*
 * cancel_work_sync: remove if pending AND guarantee the target is not running on
 * return.  Returns 1 if it had been pending/running, 0 if already idle.  In the
 * host model the flush is single-threaded, so a work is never running when this
 * is called from outside flush; the real backend blocks on the running flag.
 */
int osdep_cancel_work_sync(struct osdep_workqueue *wq, struct osdep_work *w);
/* Run pending work to completion (FIFO on an ordered queue). */
void osdep_flush_workqueue(struct osdep_workqueue *wq);
unsigned osdep_workqueue_pending(const struct osdep_workqueue *wq);
int osdep_work_is_running(const struct osdep_work *w);

#endif /* PARITY_OSDEP_SYNC_H */
