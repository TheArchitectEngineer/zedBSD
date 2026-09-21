/*
 * WS031 Linux-parity — shared kernel sync backend.
 *
 * The REAL (waitq + spinlock + kthread) completion and workqueue that both the
 * normal driver path (later: M4 request completion / deferred work) and the
 * in-kernel concurrency tests use.  The test file only drives these; it holds no
 * private completion/worker implementation.  Follows the counting/PENDING-RUNNING
 * contract the portable osdep sync layer is unit-tested against.
 */
#ifndef PARITY_BACKEND_SYNC_H
#define PARITY_BACKEND_SYNC_H

#include <stdint.h>
#include <kern/lock.h>
#include <kern/waitq.h>

struct thread;

/* ---- completion (counting) ---- */
struct parity_kcompletion {
	struct spinlock lock;
	struct wait_queue waitq;
	unsigned done;   /* unconsumed permits */
	int all;         /* complete_all: permanent */
};

void parity_kcompletion_init(struct parity_kcompletion *c, const char *name);
void parity_kcomplete(struct parity_kcompletion *c);       /* +1 permit, wake */
void parity_kcomplete_all(struct parity_kcompletion *c);   /* open permanently, wake */
void parity_kreinit_completion(struct parity_kcompletion *c);
/* Wait up to absolute `deadline` (sched_ticks).  1 = completed (permit consumed), 0 = timeout. */
int  parity_kwait(struct parity_kcompletion *c, uint64_t deadline);

/* ---- deferred work (worker thread) ---- */
enum parity_kwork_state { PARITY_KWORK_IDLE = 0, PARITY_KWORK_PENDING, PARITY_KWORK_RUNNING };

struct parity_kwork {
	void (*fn)(void *);
	void *ctx;
	int state;                  /* enum parity_kwork_state (under wq->lock) */
	int queued;                 /* in the ring */
	int ran_count;
	uint32_t run_order;
	struct parity_kworkqueue *wq;
	struct wait_queue done_waitq;  /* for cancel_work_sync to wait on RUNNING->done */
};

#ifndef PARITY_KWQ_DEPTH
#define PARITY_KWQ_DEPTH 64u
#endif

struct parity_kworkqueue {
	struct spinlock lock;
	struct wait_queue waitq;        /* worker sleeps here for new work / stop */
	struct parity_kwork *slots[PARITY_KWQ_DEPTH];
	unsigned head, tail, count;
	uint32_t run_seq;
	int stop;
	int worker_alive;
	struct thread *worker;
	const char *name;
};

void parity_kwork_init(struct parity_kwork *w, void (*fn)(void *), void *ctx);
/* Create the queue and its worker thread.  Returns 0 / -errno. */
int  parity_kworkqueue_create(struct parity_kworkqueue *wq, const char *name);
/* Enqueue.  1 = newly queued, 0 = already pending.  A RUNNING work may re-queue. */
int  parity_kqueue_work(struct parity_kworkqueue *wq, struct parity_kwork *w);
/* cancel_work(): remove if pending; does NOT wait for a running callback.  1 if it had been pending. */
int  parity_kcancel_work(struct parity_kworkqueue *wq, struct parity_kwork *w);
/* 1 while the work sits in the queue (a RUNNING work that re-queued itself counts) */
int  parity_kwork_is_pending(struct parity_kworkqueue *wq, struct parity_kwork *w);
/* Remove if pending AND wait until it is not running.  Returns 1 if it had been pending. */
int  parity_kcancel_work_sync(struct parity_kworkqueue *wq, struct parity_kwork *w, uint64_t deadline);
/*
 * flush_work(): wait for a queued/running work to COMPLETE without cancelling
 * it (unlike cancel_work_sync).  Returns 1 if it was pending/running, 0 if it
 * was already idle -- 0 is NOT a failure.
 */
int  parity_kflush_work(struct parity_kworkqueue *wq, struct parity_kwork *w, uint64_t deadline);
/* Stop the worker and reclaim it (drains nothing new). */
void parity_kworkqueue_destroy(struct parity_kworkqueue *wq);

#endif /* PARITY_BACKEND_SYNC_H */
