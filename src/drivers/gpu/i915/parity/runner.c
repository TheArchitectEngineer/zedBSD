/*
 * WS031 Linux-parity — deferred runner (see runner.h + i915-parity.h).
 *
 *   - attach registers the device; a readiness hook records readiness.  A common
 *     launch decision, taken once under the lock, starts ONE managed runner
 *     thread once readiness holds (register-then-ready or ready-then-register).
 *   - Two modes, chosen by whether a GPU registered:
 *       SYNC_ONLY : no GPU -> run the concurrency tests, exit (never calls P0).
 *       PROBE     : GPU registered -> run the tests, then P0..P2.
 *   - Lifetime: the thread object is auto-reclaimed (detached); the runner HANDLE
 *     (state/result/device ref) is separate and outlives the thread's exit.  The
 *     result is published (under the lock) after it is filled; `done` means the
 *     probe result is finalized, not that the thread has been reclaimed.
 */
#include "../internal.h"
#include "wait.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/thread.h>
#include <kern/sched.h>
#include <drivers/i915-parity.h>
#include "parity.h"
#include "ktest.h"
#include "runner.h"

enum runner_test_status { RUN_TEST_NOT_RUN = 0, RUN_TEST_PASS, RUN_TEST_FAIL };
enum runner_probe_status { RUN_PROBE_NOT_RUN = 0, RUN_PROBE_STOPPED_AT_P2, RUN_PROBE_BLOCKED, RUN_PROBE_FAILED };

struct parity_runner_result {
	int selftest_status;              /* enum runner_test_status */
	const char *selftest_scope;
	int probe_status;                 /* enum runner_probe_status */
	const char *last_completed_op;    /* stage the probe reached */
	const char *blocked_or_failed_op;
	int cleanup_done;                 /* teardown reached */
	int published;                    /* device published (always 0 for the diagnostic) */
	int result_valid;                 /* published under the lock after being filled */
};

static struct parity_runner {
	struct spinlock lock;
	int lock_ready;
	struct i915_device *device;
	int registered;
	int ready;                        /* readiness hook fired */
	int launched;                     /* runner thread launched (once) */
	int done;                         /* probe result finalized */
	struct thread *thread;            /* not dereferenced after the thread exits */
	uint64_t thread_id;               /* for logging only */
	struct parity_runner_result result;
} g_runner;

static void
ensure_lock(void)
{
	if (!g_runner.lock_ready) {
		spin_init(&g_runner.lock, LOCK_RANK_DEVICE, "parity-runner");
		g_runner.lock_ready = 1;
	}
}

static const char *
probe_status_name(int s)
{
	switch (s) {
	case RUN_PROBE_STOPPED_AT_P2: return "STOPPED_AT_P2";
	case RUN_PROBE_BLOCKED:       return "BLOCKED";
	case RUN_PROBE_FAILED:        return "FAILED";
	default:                      return "NOT_RUN";
	}
}

static void
runner_thread(void *arg)
{
	struct i915_device *dev;
	struct parity_runner_result res;
	int ktest_rc;

	(void)arg;
	kern_logf("i915: parity runner thread begin (execution base ready)\n");

	res.selftest_status = RUN_TEST_NOT_RUN;
	res.selftest_scope = "";
	res.probe_status = RUN_PROBE_NOT_RUN;
	res.last_completed_op = "";
	res.blocked_or_failed_op = "";
	res.cleanup_done = 0;
	res.published = 0;
	res.result_valid = 0;

	/*
	 * Run the real-device attach FIRST.  The passed-through GPU idle-suspends
	 * to D3 after a short time (its config space then reads 0xffff), and the
	 * GPU-free selftest below adds enough wall-clock delay to cross that
	 * window.  The attach is device-sensitive; the selftest is not -- so the
	 * order is attach -> selftest (both still run).
	 */
	/* PROBE mode iff a GPU registered; SYNC_ONLY otherwise. */
	spin_lock(&g_runner.lock);
	dev = g_runner.device;
	spin_unlock(&g_runner.lock);

	if (dev != 0) {
		struct parity_result pr;

		(void)drv_i915_parity_attach(dev, PARITY_STAGE_P3, &pr);
		res.cleanup_done = 1;   /* parity_attach always tears down before returning */
		res.last_completed_op = pr.last_completed;   /* last COMPLETED op, not the frontier */
		switch (pr.outcome) {
		case PARITY_STOPPED: res.probe_status = RUN_PROBE_STOPPED_AT_P2; break;
		case PARITY_BLOCKED: res.probe_status = RUN_PROBE_BLOCKED; res.blocked_or_failed_op = pr.where; break;
		default:             res.probe_status = RUN_PROBE_FAILED; res.blocked_or_failed_op = pr.where; break;
		}
	}

	/* Concurrency tests through the shared sync backend (both modes). */
	ktest_rc = parity_sync_ktest();
	res.selftest_status = (ktest_rc == 0) ? RUN_TEST_PASS : RUN_TEST_FAIL;
	res.selftest_scope = "K0-K5";

	/* Publish the result under the lock AFTER it is fully filled. */
	res.result_valid = 1;
	spin_lock(&g_runner.lock);
	g_runner.result = res;
	g_runner.done = 1;
	spin_unlock(&g_runner.lock);

	kern_logf("i915: runner-result: selftest=%s selftest_scope=%s probe=%s last_op=%s "
		"blocked_at=%s cleanup=%d published=%d\n",
		res.selftest_status == RUN_TEST_PASS ? "PASS" :
			(res.selftest_status == RUN_TEST_FAIL ? "FAIL" : "NOT_RUN"),
		res.selftest_scope, probe_status_name(res.probe_status),
		res.last_completed_op[0] ? res.last_completed_op : "-",
		res.blocked_or_failed_op[0] ? res.blocked_or_failed_op : "-",
		res.cleanup_done, res.published);
	kern_logf("i915: parity runner thread end\n");
}

/* Common launch decision: take the launch reservation once, then spawn without the lock. */
static void
try_launch(void)
{
	struct thread *thread;
	int launch = 0;

	spin_lock(&g_runner.lock);
	if (g_runner.ready && !g_runner.launched) {
		g_runner.launched = 1;
		launch = 1;
	}
	spin_unlock(&g_runner.lock);
	if (!launch)
		return;

	if (kthread_create(runner_thread, 0, SCHED_PRIORITY_DEFAULT, &thread) != 0) {
		kern_logf("i915: parity runner: kthread_create failed\n");
		spin_lock(&g_runner.lock);
		g_runner.launched = 0;   /* explicit failure: allow a later retry, own nothing */
		spin_unlock(&g_runner.lock);
		return;
	}
	spin_lock(&g_runner.lock);
	g_runner.thread = thread;
	g_runner.thread_id = (uint64_t)(uintptr_t)thread;
	spin_unlock(&g_runner.lock);
	thread->detached = 1;   /* auto-reclaimed on exit; the handle tracks the result */
	thread_start(thread);
	kern_logf("i915: parity runner started (thread handle recorded)\n");
}

void
drv_i915_parity_runner_register(struct i915_device *device)
{
	ensure_lock();
	spin_lock(&g_runner.lock);
	if (!g_runner.registered) {
		g_runner.device = device;   /* held until the runner's last access */
		g_runner.registered = 1;
	}
	spin_unlock(&g_runner.lock);
	kern_logf("i915: parity runner: device registered, probe deferred to readiness\n");
	try_launch();   /* in case readiness already holds */
}

void
drv_i915_parity_runner_start(void)
{
	ensure_lock();
	spin_lock(&g_runner.lock);
	g_runner.ready = 1;
	spin_unlock(&g_runner.lock);
	try_launch();
}
