/*
 * WS031 Linux-parity — GPU-free kernel checks with REAL threads, locks and ticks:
 *   1. the delayed-work backend (backend_delayed.c) on its own;
 *   2. the eDP first stage with the kernel env's real mutexes + timer + worker
 *      (parity_dp_kernel.c) driving the register model (dp_fake_hw.c) -- the delayed
 *      VDD-off running by itself, a re-acquisition before the deadline, and a stop
 *      while the timer waits and while the worker body is running.
 * zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/waitq.h>
#include <kern/sched.h>
#include <kern/clock.h>
#include <string.h>
#include "parity_edp.h"
#include "parity_dp_kernel.h"
#include "dp_fake_hw.h"
#include "dp_fixture_latitude5330.h"
#include "edp_ktest.h"

static uint64_t ticks_after_ms(unsigned ms)
{
	uint64_t d = 0;

	(void)kern_deadline_after(sched_ticks(), (uint64_t)ms * KERN_CLOCK_HZ / 1000u + 1u, &d);
	return d;
}

/* ---- 1. delayed work ---- */
static struct {
	struct parity_kcompletion started, release, done;
	int block;              /* the body waits for `release` */
	unsigned runs;
	uint64_t ran_at;
} body;

static void body_fn(void *arg)
{
	(void)arg;
	body.runs++;
	body.ran_at = sched_ticks();
	parity_kcomplete(&body.started);
	if (body.block)
		(void)parity_kwait(&body.release, ticks_after_ms(5000u));
	parity_kcomplete(&body.done);
}

static void delayed_work_checks(parity_edp_ktest_check check)
{
	static struct parity_kworkqueue wq;
	static struct parity_ktimerq tq;
	static struct parity_kdelayed dw;
	uint64_t t0;
	int r1, r2, r3;

	memset(&body, 0, sizeof(body));
	parity_kcompletion_init(&body.started, "dw-started");
	parity_kcompletion_init(&body.release, "dw-release");
	parity_kcompletion_init(&body.done, "dw-done");
	if (parity_kworkqueue_create(&wq, "ktest-dw-wq") != 0 || parity_ktimerq_create(&tq, &wq, "ktest-dw-timer") != 0) {
		check(0, "dwork: DW-SETUP worker and timer threads start");
		return;
	}
	parity_kdelayed_init(&dw, body_fn, 0);

	t0 = sched_ticks();
	r1 = parity_kdelayed_queue(&tq, &dw, 50u);
	r2 = parity_kdelayed_queue(&tq, &dw, 50u);
	r3 = parity_kdelayed_pending(&tq, &dw);
	check(r1 == 1 && r2 == 0 && r3 == 1 && body.runs == 0u,
		"dwork: DW-QUEUE newly queued once; a second queue while armed is refused; nothing ran yet");
	r1 = parity_kwait(&body.done, ticks_after_ms(1000u));
	check(r1 == 1 && body.runs == 1u && body.ran_at - t0 >= 5u && dw.fired_count == 1u,
		"dwork: DW-FIRE the body runs by itself on the worker, not before the 50 ms (5 ticks) asked for");

	(void)parity_kdelayed_queue(&tq, &dw, 100u);
	r1 = parity_kdelayed_cancel(&tq, &dw);
	r2 = parity_kwait(&body.done, ticks_after_ms(250u));
	check(r1 == 1 && r2 == 0 && body.runs == 1u && parity_kdelayed_pending(&tq, &dw) == 0,
		"dwork: DW-CANCEL a cancelled reservation never runs");

	/* re-arm before the deadline: the deadline counts from the LAST queue */
	(void)parity_kdelayed_queue(&tq, &dw, 100u);
	(void)parity_kwait(&body.done, ticks_after_ms(50u));
	(void)parity_kdelayed_cancel(&tq, &dw);
	t0 = sched_ticks();
	(void)parity_kdelayed_queue(&tq, &dw, 100u);
	r1 = parity_kwait(&body.done, ticks_after_ms(1000u));
	check(r1 == 1 && body.runs == 2u && body.ran_at - t0 >= 10u,
		"dwork: DW-REARM cancel + queue moves the deadline: the old one does not fire");

	/* cancel_sync while the body is running waits for it (the completions count: start from none) */
	parity_kreinit_completion(&body.started);
	parity_kreinit_completion(&body.done);
	body.block = 1;
	(void)parity_kdelayed_queue(&tq, &dw, 0u);
	r1 = parity_kwait(&body.started, ticks_after_ms(1000u));
	r2 = parity_kdelayed_cancel(&tq, &dw);            /* not pending any more: running */
	parity_kcomplete(&body.release);
	(void)parity_kdelayed_cancel_sync(&tq, &dw, ticks_after_ms(3000u));
	r3 = parity_kwait(&body.done, ticks_after_ms(10u));  /* must ALREADY be complete */
	check(r1 == 1 && r2 == 0 && r3 == 1 && body.runs == 3u,
		"dwork: DW-CANCEL-SYNC a plain cancel does not wait for a running body; cancel_sync returns only after it finished");
	body.block = 0;

	/* flush runs an armed work now */
	parity_kreinit_completion(&body.started);
	parity_kreinit_completion(&body.done);
	(void)parity_kdelayed_queue(&tq, &dw, 60000u);
	t0 = sched_ticks();
	r1 = parity_kdelayed_flush(&tq, &dw, ticks_after_ms(3000u));
	check(r1 == 1 && body.runs == 4u && sched_ticks() - t0 < 100u && parity_kdelayed_pending(&tq, &dw) == 0,
		"dwork: DW-FLUSH an armed work is run now and waited for, without waiting for its deadline");

	parity_ktimerq_destroy(&tq);
	parity_kworkqueue_destroy(&wq);
	check(tq.alive == 0 && wq.worker_alive == 0, "dwork: DW-STOP both threads end");
}

/* ---- 2. eDP with real locks / timer / worker on the register model ---- */
static struct dp_fake_hw hw;
static struct parity_dp_kernel k;
static struct parity_dp_env env;
static struct parity_edp_result res;
static struct parity_kcompletion in_pp_write;
static void (*model_write)(void *ctx, uint32_t reg, uint32_t value);
static int slow_vdd_off;

/* the model is single-threaded by design; here two threads reach it, serialised by the PPS
 * lock except for the power references -- which the hybrid keeps in the model under this lock */
static struct spinlock model_lock;

static struct parity_dp_env model_env;          /* the model's own hooks (ctx = &hw) */

static uint32_t hy_read32(void *ctx, uint32_t reg)
{
	(void)ctx;
	return model_env.read32(&hw, reg);
}

static void hy_write32(void *ctx, uint32_t reg, uint32_t value)
{
	(void)ctx;
	if (slow_vdd_off && reg == 0xC7204u && (value & 8u) == 0u && (hw.pp_control & 8u) != 0u) {
		/* the worker body is now INSIDE the VDD-off write, holding the PPS lock */
		parity_kcomplete(&in_pp_write);
		parity_dp_kernel_sleep_us(&k, 150000u);
	}
	model_write(&hw, reg, value);
}

static int hy_wait_reg(void *ctx, uint32_t reg, uint32_t mask, uint32_t value, unsigned fast_us,
	unsigned slow_ms, uint32_t *out)
{
	(void)ctx;
	return model_env.wait_reg(&hw, reg, mask, value, fast_us, slow_ms, out);
}

static void hy_sleep_us(void *ctx, unsigned us)     { (void)ctx; model_env.sleep_us(&hw, us); }
static uint64_t hy_now_ms(void *ctx)                { (void)ctx; return model_env.now_ms(&hw); }

static int hy_power_get(void *ctx, int domain)
{
	int rc;

	(void)ctx;
	spin_lock(&model_lock);
	rc = model_env.power_get(&hw, domain);
	spin_unlock(&model_lock);
	return rc;
}

static void hy_power_put(void *ctx, int domain)
{
	(void)ctx;
	spin_lock(&model_lock);
	model_env.power_put(&hw, domain);
	spin_unlock(&model_lock);
}

static void hy_power_put_async(void *ctx, int domain)
{
	(void)ctx;
	spin_lock(&model_lock);
	model_env.power_put_async(&hw, domain);
	spin_unlock(&model_lock);
}

static void hybrid_fresh(void)
{
	dp_fake_init(&hw, dp_fixture_dpcd_000, dp_fixture_dpcd_100, dp_fixture_dpcd_700,
		dp_fixture_edid, sizeof(dp_fixture_edid));
	dp_fake_bind_env(&hw, &model_env);
	model_write = model_env.write32;
	memset(&env, 0, sizeof(env));
	parity_dp_kernel_bind_sync(&k, &env);        /* real locks + real delayed work; ctx = &k */
	env.read32 = hy_read32;
	env.write32 = hy_write32;
	env.wait_reg = hy_wait_reg;
	env.sleep_us = hy_sleep_us;                  /* the model's clock: panel delays cost no real time */
	env.now_ms = hy_now_ms;
	env.power_get = hy_power_get;
	env.power_put = hy_power_put;
	env.power_put_async = hy_power_put_async;
	slow_vdd_off = 0;
}

static int hybrid_released(void)
{
	int ok = env.power_refs[0] == 0 && env.power_refs[1] == 0 && (hw.pp_control & 8u) == 0u &&
		res.vdd_wakeref_held == 0 && res.vdd_work_pending == 0 && res.power_put_underflows == 0u &&
		env.lock_errors == 0u;

	dp_fake_flush_async(&hw);
	return ok && hw.refs_core == 0 && hw.refs_aux == 0;
}

static unsigned wait_vdd(int want_on, unsigned limit_ms)
{
	unsigned waited = 0u;

	while (((hw.pp_control & 8u) != 0u) != (want_on != 0) && waited < limit_ms) {
		parity_dp_kernel_sleep_us(&k, 20000u);
		waited += 20u;
	}
	return waited;
}

static void edp_concurrency_checks(parity_edp_ktest_check check)
{
	struct parity_edp_config cfg;
	uint8_t b[2];
	unsigned waited, ran;
	int rc, end;
	long n;

	memset(&k, 0, sizeof(k));
	spin_init(&model_lock, LOCK_RANK_DEVICE, "ktest-dp-model");
	parity_kcompletion_init(&in_pp_write, "ktest-pp-write");
	if (parity_dp_kernel_sync_start(&k) != 0) {
		check(0, "edp-sync: SYNC-SETUP locks, worker and timer start");
		return;
	}
	/* the shortest delays the reference's rules give: T11_T12 1 -> +100 ms -> round up = 200 ms; x5 = 1 s */
	memset(&cfg, 0, sizeof(cfg));
	cfg.rawclk_khz = 19200u;
	cfg.t1_t3 = 100u; cfg.t8 = 10u; cfg.t9 = 10u; cfg.t10 = 100u; cfg.t11_t12 = 1u;
	cfg.log_level = -1;

	/* (a) the delayed off runs by itself, from the tick, on the worker thread */
	hybrid_fresh();
	rc = parity_edp_begin(&env, &cfg, &res);
	rc = rc == 0 ? parity_edp_init_late(&cfg, &res) : rc;
	check(rc == 0 && res.delay_power_cycle_ms == 200 && res.vdd_on_hw == 1 && res.vdd_work_pending == 1 &&
		res.vdd_wakeref_held == 1 && res.power_refs_aux == 1,
		"edp-sync: SYNC-RESERVE after late init the panel is KEPT: VDD on, its AUX reference held, off reserved in 1 s");
	waited = wait_vdd(0, 3000u);
	parity_edp_snapshot(&res);
	check(waited >= 800u && waited < 3000u && res.vdd_on_hw == 0 && res.vdd_wakeref_held == 0 &&
		res.power_refs_aux == 0 && hw.refs_aux == 0 && k.vdd_off_work.work.ran_count == 1 &&
		k.vdd_off_work.fired_count == 1u && env.lock_errors == 0u,
		"edp-sync: SYNC-AUTO-OFF nobody drives it: ~1 s later the worker took the PPS lock, forced VDD off and returned the reference");

	/* (b) re-acquisition before the deadline */
	n = parity_edp_dpcd_read(0x000u, b, 2u);
	ran = (unsigned)k.vdd_off_work.work.ran_count;
	parity_dp_kernel_sleep_us(&k, 600000u);
	n = n == 2 ? parity_edp_dpcd_read(0x000u, b, 2u) : n;
	parity_dp_kernel_sleep_us(&k, 600000u);          /* 1.2 s after the first reservation */
	check(n == 2 && (hw.pp_control & 8u) != 0u && (unsigned)k.vdd_off_work.work.ran_count == ran &&
		k.vdd_off_work.cancelled_armed >= 1u,
		"edp-sync: SYNC-REACQUIRE a read before the deadline cancels the reservation; the old deadline passes and VDD stays on");
	waited = wait_vdd(0, 3000u);
	check(waited < 3000u && (unsigned)k.vdd_off_work.work.ran_count == ran + 1u,
		"edp-sync: SYNC-REACQUIRE-OFF the new reservation then fires by itself");

	/* (c1) stop while the timer is waiting */
	n = parity_edp_dpcd_read(0x000u, b, 2u);
	parity_edp_snapshot(&res);
	rc = res.vdd_work_pending;
	end = parity_edp_end(&res);
	check(n == 2 && rc == 1 && end == 0 && hybrid_released() && parity_kdelayed_pending(&k.tq, &k.vdd_off_work) == 0,
		"edp-sync: SYNC-STOP-WAITING stop with the off still reserved: cancelled synchronously, VDD forced off, references back");

	/* (c2) stop while the worker body is running (inside the VDD-off write, holding the PPS lock) */
	hybrid_fresh();
	rc = parity_edp_begin(&env, &cfg, &res);
	rc = rc == 0 ? parity_edp_init_late(&cfg, &res) : rc;
	slow_vdd_off = 1;
	n = parity_kwait(&in_pp_write, ticks_after_ms(3000u));
	end = parity_edp_end(&res);                       /* cancel_sync must wait for the body, then take the lock */
	check(rc == 0 && n == 1 && end == 0 && hybrid_released() && hw.vdd_off_events == 1u &&
		k.vdd_off_work.work.state == PARITY_KWORK_IDLE,
		"edp-sync: SYNC-STOP-RUNNING stop while the body is mid-write: no deadlock, no double off, no access after the end");
	slow_vdd_off = 0;

	/* a body dequeued after the end finds nothing live */
	parity_edp_work_run(PARITY_DP_WORK_VDD_OFF);
	check(hw.vdd_off_events == 1u, "edp-sync: SYNC-LATE-BODY a body that runs after the end touches nothing");

	parity_dp_kernel_sync_stop(&k);
	check(k.tq.alive == 0 && k.wq.worker_alive == 0, "edp-sync: SYNC-STOP threads end");
}

void parity_edp_sync_ktest(parity_edp_ktest_check check)
{
	delayed_work_checks(check);
	edp_concurrency_checks(check);
}
