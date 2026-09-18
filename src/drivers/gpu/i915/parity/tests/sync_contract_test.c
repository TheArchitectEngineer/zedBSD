/*
 * GPU-free contract tests for the Linux-parity sync / deferred-work layer.
 * Verifies completion ordering/timeout/IRQ-race and that queue_work defers
 * (never runs inline), stays FIFO, and can be cancelled — logic only, no threads.
 */
#include <stdio.h>
#include "../osdep/sync.h"

static int g_fail;
static int g_checks;

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { printf("    FAIL: %s\n", (msg)); g_fail++; } \
} while (0)

/* tick that completes a completion on a chosen iteration (models an IRQ). */
struct tick_ctx { struct osdep_completion *c; int fire_after; int n; };
static void
tick_complete(void *p)
{
	struct tick_ctx *t = (struct tick_ctx *)p;
	t->n++;
	if (t->n >= t->fire_after)
		osdep_complete(t->c);
}

static int g_ran_count;
static void work_fn(void *ctx) { (void)ctx; g_ran_count++; }

/* cancel-during-execution helper */
struct cancel_ctx { struct osdep_workqueue *wq; struct osdep_work *victim; };
static void
work_cancel_other(void *p)
{
	struct cancel_ctx *c = (struct cancel_ctx *)p;
	osdep_cancel_work(c->wq, c->victim);
}

/* self-requeue helper: re-queues itself while running, exactly once. */
struct requeue_ctx { struct osdep_workqueue *wq; struct osdep_work *self; };
static void
work_requeue_self(void *p)
{
	struct requeue_ctx *c = (struct requeue_ctx *)p;
	if (c->self->ran_count < 2)
		osdep_queue_work(c->wq, c->self);
}

int
main(void)
{
	static struct osdep_trace trace;

	printf("== sync / deferred-work contract tests (GPU-free) ==\n");
	osdep_trace_init(&trace);

	/* SYNC-1: complete before wait -> immediate */
	printf("[SYNC-1] completion signalled before wait returns immediately\n");
	{
		struct osdep_completion c;
		osdep_completion_init(&c, &trace);
		osdep_complete(&c);
		CHECK(osdep_wait_for_completion_timeout(&c, 10u, 0, 0) == 10,
		      "already-done wait returns full budget, no block");
	}

	/* SYNC-2: timeout when never completed */
	printf("[SYNC-2] wait times out when never completed\n");
	{
		struct osdep_completion c;
		osdep_completion_init(&c, &trace);
		CHECK(osdep_wait_for_completion_timeout(&c, 5u, 0, 0) == 0, "timeout returns 0");
		CHECK(!osdep_completion_done(&c), "still not done");
	}

	/* SYNC-3: completed during wait (IRQ race) */
	printf("[SYNC-3] completion signalled mid-wait is observed\n");
	{
		struct osdep_completion c;
		struct tick_ctx t;
		int rc;
		osdep_completion_init(&c, &trace);
		t.c = &c; t.fire_after = 3; t.n = 0;
		rc = osdep_wait_for_completion_timeout(&c, 10u, tick_complete, &t);
		CHECK(rc > 0, "wait succeeds when completed mid-wait");
		CHECK(t.n == 3, "completed on the 3rd tick");
	}

	/* SYNC-4: completion is a COUNT, not a bool */
	printf("[SYNC-4] N completes satisfy N waits, then timeout\n");
	{
		struct osdep_completion c;
		osdep_completion_init(&c, &trace);
		osdep_complete(&c);
		osdep_complete(&c);
		CHECK(osdep_wait_for_completion_timeout(&c, 3u, 0, 0) > 0, "first wait consumes a permit");
		CHECK(osdep_wait_for_completion_timeout(&c, 3u, 0, 0) > 0, "second wait consumes a permit");
		CHECK(osdep_wait_for_completion_timeout(&c, 3u, 0, 0) == 0, "third wait times out (permits exhausted)");
	}

	/* SYNC-5: complete_all vs reinit */
	printf("[SYNC-5] complete_all opens permanently; reinit resets\n");
	{
		struct osdep_completion c;
		osdep_completion_init(&c, &trace);
		osdep_complete_all(&c);
		CHECK(osdep_wait_for_completion_timeout(&c, 2u, 0, 0) > 0, "complete_all: wait 1 ok");
		CHECK(osdep_wait_for_completion_timeout(&c, 2u, 0, 0) > 0, "complete_all: wait 2 ok (not consumed)");
		osdep_reinit_completion(&c);
		CHECK(!osdep_completion_done(&c), "reinit clears the state");
		CHECK(osdep_wait_for_completion_timeout(&c, 2u, 0, 0) == 0, "after reinit, wait times out");
	}

	/* WQ-1: queue_work defers (no inline run) */
	printf("[WQ-1] queue_work does not run the work inline\n");
	{
		struct osdep_workqueue wq;
		struct osdep_work w;
		g_ran_count = 0;
		osdep_workqueue_init(&wq, 1, &trace);
		osdep_work_init(&w, work_fn, 0);
		CHECK(osdep_queue_work(&wq, &w) == 1, "queued");
		CHECK(w.ran_count == 0 && g_ran_count == 0, "not run inline");
		CHECK(osdep_workqueue_pending(&wq) == 1u, "one pending");

		/* WQ-2: flush runs pending */
		osdep_flush_workqueue(&wq);
		CHECK(w.ran_count == 1 && g_ran_count == 1, "flush ran the work");
		CHECK(osdep_workqueue_pending(&wq) == 0u, "queue drained");
	}

	/* WQ-3: cancel before flush */
	printf("[WQ-3] cancel before run prevents execution\n");
	{
		struct osdep_workqueue wq;
		struct osdep_work w;
		g_ran_count = 0;
		osdep_workqueue_init(&wq, 1, &trace);
		osdep_work_init(&w, work_fn, 0);
		osdep_queue_work(&wq, &w);
		CHECK(osdep_cancel_work(&wq, &w) == 1, "cancel removed pending work");
		CHECK(w.canceled == 1, "marked canceled");
		osdep_flush_workqueue(&wq);
		CHECK(w.ran_count == 0 && g_ran_count == 0, "canceled work never ran");
	}

	/* WQ-4: ordered FIFO */
	printf("[WQ-4] ordered queue runs FIFO\n");
	{
		struct osdep_workqueue wq;
		struct osdep_work a, b, c;
		osdep_workqueue_init(&wq, 1, &trace);
		osdep_work_init(&a, work_fn, 0);
		osdep_work_init(&b, work_fn, 0);
		osdep_work_init(&c, work_fn, 0);
		osdep_queue_work(&wq, &a);
		osdep_queue_work(&wq, &b);
		osdep_queue_work(&wq, &c);
		osdep_flush_workqueue(&wq);
		CHECK(a.run_order < b.run_order && b.run_order < c.run_order, "ran in FIFO order A,B,C");
	}

	/* WQ-5: double queue is idempotent */
	printf("[WQ-5] queueing an already-queued work is a no-op\n");
	{
		struct osdep_workqueue wq;
		struct osdep_work w;
		osdep_workqueue_init(&wq, 1, &trace);
		osdep_work_init(&w, work_fn, 0);
		CHECK(osdep_queue_work(&wq, &w) == 1, "first queue ok");
		CHECK(osdep_queue_work(&wq, &w) == 0, "second queue rejected");
		CHECK(osdep_workqueue_pending(&wq) == 1u, "only one entry");
		osdep_flush_workqueue(&wq);
	}

	/* WQ-6: cancel during another work's execution */
	printf("[WQ-6] a running work can cancel a still-pending work\n");
	{
		struct osdep_workqueue wq;
		struct osdep_work a, b;
		struct cancel_ctx cc;
		g_ran_count = 0;
		osdep_workqueue_init(&wq, 1, &trace);
		cc.wq = &wq; cc.victim = &b;
		osdep_work_init(&a, work_cancel_other, &cc);
		osdep_work_init(&b, work_fn, 0);
		osdep_queue_work(&wq, &a);
		osdep_queue_work(&wq, &b);
		osdep_flush_workqueue(&wq);
		CHECK(a.ran_count == 1, "A ran");
		CHECK(b.ran_count == 0 && b.canceled == 1, "B canceled mid-flush, never ran");
	}

	/* WQ-7: self-requeue while running runs again (PENDING cleared before callback) */
	printf("[WQ-7] a running work may re-queue itself and run again\n");
	{
		struct osdep_workqueue wq;
		struct osdep_work w;
		struct requeue_ctx rc;
		osdep_workqueue_init(&wq, 1, &trace);
		osdep_work_init(&w, work_requeue_self, &rc);
		rc.wq = &wq; rc.self = &w;
		osdep_queue_work(&wq, &w);
		osdep_flush_workqueue(&wq);
		CHECK(w.ran_count == 2, "ran twice (self-requeue not dropped as duplicate)");
		CHECK(osdep_workqueue_pending(&wq) == 0u, "queue drained");
	}

	/* WQ-8: cancel_work_sync on a pending work removes it and guarantees not running */
	printf("[WQ-8] cancel_work_sync removes pending work, guarantees not running\n");
	{
		struct osdep_workqueue wq;
		struct osdep_work w;
		g_ran_count = 0;
		osdep_workqueue_init(&wq, 1, &trace);
		osdep_work_init(&w, work_fn, 0);
		osdep_queue_work(&wq, &w);
		CHECK(osdep_cancel_work_sync(&wq, &w) == 1, "sync-cancel reports it was active");
		CHECK(!osdep_work_is_running(&w), "not running on return");
		osdep_flush_workqueue(&wq);
		CHECK(w.ran_count == 0, "never ran after sync-cancel");
		/* (the truly concurrent 'cancel while running blocks' case is a kernel-backend test) */
	}

	printf("== %d checks, %d failures ==\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
