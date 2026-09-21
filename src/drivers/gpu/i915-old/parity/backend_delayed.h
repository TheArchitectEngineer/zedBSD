/*
 * WS031 Linux-parity — delayed work on the shared kernel sync backend.
 *
 * The contract of Linux delayed_work on top of parity_kworkqueue: a timer
 * thread sleeps on the ordinary wait queue until the earliest deadline (so the
 * existing 10 ms tick wakes it; nothing runs in the timer IRQ), then hands the
 * work to the shared worker thread, which runs the callback with no lock held.
 *
 *   IDLE -> ARMED (waiting for its deadline) -> PENDING (in the work queue)
 *        -> RUNNING -> IDLE        (a running work may be armed again)
 *
 *   queue        1 = newly armed; 0 = already armed or pending (left alone)
 *   cancel       disarms / dequeues; does NOT wait for a running callback
 *   cancel_sync  the same, then waits until the callback is not running.  The
 *                caller must not hold a lock the callback takes.
 *   flush        runs an ARMED work now (without waiting for its deadline) and
 *                waits until it has finished
 */
#ifndef PARITY_BACKEND_DELAYED_H
#define PARITY_BACKEND_DELAYED_H

#include <stdint.h>
#include "backend_sync.h"

#define PARITY_KDELAYED_MAX 16u

struct parity_ktimerq;

struct parity_kdelayed {
	struct parity_kwork work;
	struct parity_ktimerq *tq;
	int armed;                 /* waiting for `deadline` (under tq->lock) */
	int firing;                /* being handed to the work queue (under tq->lock) */
	uint64_t deadline;         /* sched_ticks() value */
	unsigned armed_count, fired_count, cancelled_armed, cancelled_pending;
};

struct parity_ktimerq {
	struct spinlock lock;
	struct wait_queue waitq;
	struct parity_kdelayed *armed[PARITY_KDELAYED_MAX];
	struct parity_kworkqueue *wq;
	int stop;
	int alive;
	struct thread *thread;
	const char *name;
};

int  parity_ktimerq_create(struct parity_ktimerq *tq, struct parity_kworkqueue *wq, const char *name);
void parity_ktimerq_destroy(struct parity_ktimerq *tq);   /* armed works are dropped: cancel them first */

void parity_kdelayed_init(struct parity_kdelayed *dw, void (*fn)(void *), void *ctx);
int  parity_kdelayed_queue(struct parity_ktimerq *tq, struct parity_kdelayed *dw, unsigned delay_ms);
int  parity_kdelayed_cancel(struct parity_ktimerq *tq, struct parity_kdelayed *dw);
int  parity_kdelayed_cancel_sync(struct parity_ktimerq *tq, struct parity_kdelayed *dw, uint64_t deadline);
int  parity_kdelayed_flush(struct parity_ktimerq *tq, struct parity_kdelayed *dw, uint64_t deadline);
/* 1 while armed or pending in the queue (not: running) */
int  parity_kdelayed_pending(struct parity_ktimerq *tq, struct parity_kdelayed *dw);

#endif /* PARITY_BACKEND_DELAYED_H */
