/*
 * The GT power equivalence test: drives RC6 and RPS of a Gen12 and a Gen9
 * GT (the busy-time evaluation, the interrupt way, boosts, the fixed Gen9
 * frequency) over stubbed registers, queues and clock, and prints every
 * register write, log and state field, so two versions of gt-power.c can
 * be compared line by line.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gt-power.h"
#include "device-info.h"
#include "irq.h"
#include "mmio.h"
#include "workqueue.h"
#include <uapi/sysctl.h>

#ifdef GT_POWER_EQUIV_OLD
#define RC6_ENABLE drv_i915_gen11_rc6_enable
#define RC6_DISABLE drv_i915_rc6_sanitize
#define RPS_MASK_ALL drv_i915_rps_sanitize
#else
#define RC6_ENABLE drv_i915_rc6_enable
#define RC6_DISABLE drv_i915_rc6_disable
#define RPS_MASK_ALL drv_i915_rps_mask_all
#endif

static uint64_t now_ns;
static int pcode_rc;
static uint32_t cap = 0x00120a16U, info = 0x00000d00U, ddcc = 0x00000b00U;
static void (*work_fn)(void *);
static void *work_ctx;
static void (*tick_fn)(void *);
static void *tick_ctx;
static int wq_rc, tq_rc;

void kern_logf(const char *fmt, ...) { va_list ap; printf("log "); va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
void spin_init(struct spinlock *l, enum lock_rank r, const char *n) { (void)l; (void)r; printf("spin_init %s\n", n); }
unsigned long spin_lock_irqsave(struct spinlock *l) { (void)l; return 3; }
void spin_unlock_irqrestore(struct spinlock *l, unsigned long f) { (void)l; (void)f; }
uint64_t sched_ticks(void) { return 100; }
uint64_t drv_i915_perf_now(void) { return now_ns; }
uint32_t drv_i915_read32(struct i915_mmio *m, uint32_t r) { uint32_t v = r == 0x140d98U ? cap : r == 0x140190U ? info : 0x1234U; (void)m; printf("r %06x -> %08x\n", r, v); return v; }
void drv_i915_write32(struct i915_mmio *m, uint32_t r, uint32_t v) { (void)m; printf("w %06x <- %08x\n", r, v); }
void drv_i915_raw_write32(struct i915_mmio *m, uint32_t r, uint32_t v) { (void)m; printf("rw %06x <- %08x\n", r, v); }
int drv_i915_pcode_read(struct mutex *l, struct i915_mmio *m, uint32_t mbox, uint32_t *v, uint32_t *v1) { (void)l; (void)m; printf("pcode %x -> %d\n", mbox, pcode_rc); *v = ddcc; *v1 = 0; return pcode_rc; }
int drv_i915_synchronize_irq(struct i915_irq_dev *irq) { (void)irq; printf("sync_irq\n"); return 0; }
void drv_i915_gt_pm_reset_iir(struct i915_irq_dev *irq) { (void)irq; printf("pm_reset_iir\n"); }
int drv_i915_workqueue_create(struct i915_workqueue *q, const char *n) { (void)q; printf("wq_create %s -> %d\n", n, wq_rc); return wq_rc; }
void drv_i915_workqueue_destroy(struct i915_workqueue *q) { (void)q; printf("wq_destroy\n"); }
void drv_i915_work_init(struct i915_work *w, void (*f)(void *), void *c) { (void)w; work_fn = f; work_ctx = c; printf("work_init\n"); }
int drv_i915_queue_work(struct i915_workqueue *q, struct i915_work *w) { (void)q; (void)w; printf("queue_work\n"); return 1; }
int drv_i915_cancel_work_sync(struct i915_workqueue *q, struct i915_work *w, uint64_t d) { (void)q; (void)w; printf("cancel_work %llu\n", (unsigned long long)d); return 0; }
int drv_i915_timer_queue_create(struct i915_timer_queue *t, struct i915_workqueue *q, const char *n) { (void)t; (void)q; printf("tq_create %s -> %d\n", n, tq_rc); return tq_rc; }
void drv_i915_timer_queue_destroy(struct i915_timer_queue *t) { (void)t; printf("tq_destroy\n"); }
void drv_i915_delayed_work_init(struct i915_delayed_work *d, void (*f)(void *), void *c) { (void)d; tick_fn = f; tick_ctx = c; printf("delayed_init\n"); }
int drv_i915_delayed_queue(struct i915_timer_queue *t, struct i915_delayed_work *d, unsigned ms) { (void)t; (void)d; printf("delayed_queue %u\n", ms); return 1; }
int drv_i915_delayed_cancel_sync(struct i915_timer_queue *t, struct i915_delayed_work *d, uint64_t dl) { (void)t; (void)d; printf("delayed_cancel %llu\n", (unsigned long long)dl); return 0; }

static void
dump(const char *what, struct i915_rps *r)
{
	printf("%s rp0=%u rp1=%u min=%u max=%u eff=%u en=%d pc=%d soft=%u/%u boost=%u cur=%u last=%u adj=%d mode=%d thr=%u/%u clk=%u ev=%x iir=%x imr=%x ier=%x wait=%u wr=%d timer=%d fixed=%d busy=%llu/%llu/%u/%llu tick=%llu int=%u ticking=%d unp=%d tr=%d up=%u down=%u boosts=%u changes=%u runs=%u ticks=%u parks=%u unparks=%u logged=%u\n",
	    what, r->rp0_freq, r->rp1_freq, r->min_freq, r->max_freq, r->efficient_freq, r->enabled, r->pcode_ok, r->min_softlimit, r->max_softlimit, r->boost_freq, r->cur_freq, r->last_freq, r->last_adj, r->power_mode, r->up_threshold, r->down_threshold, r->clock_frequency, r->pm_events, r->pm_iir, r->pm_imr, r->pm_ier, r->waiters, r->work_ready, r->use_timer, r->fixed,
	    (unsigned long long)r->busy_total_ns, (unsigned long long)r->busy_since_ns, r->busy_runs, (unsigned long long)r->busy_seen_ns, (unsigned long long)r->tick_ns, r->interval_ms, r->ticking, r->unparking, r->timers_ready, r->up_events, r->down_events, r->boosts, r->changes, r->work_runs, r->ticks, r->parks, r->unparks, r->logged);
}

static void
tick(struct i915_rps *r, uint64_t advance, uint64_t busy)
{
	r->busy_total_ns += busy;
	now_ns += advance;
	tick_fn(tick_ctx);
	dump("tick", r);
}

int
main(void)
{
	static struct i915_rps rps;
	static struct i915_rc6 rc6;
	static struct i915_irq_dev irq;
	static struct i915_gt_info gt;
	struct i915_mmio *mmio = (struct i915_mmio *)&gt;
	struct gpu_telemetry_entry te;
	unsigned i, f;
	int adj;

	/* RC6 on Gen12 with two video engines, on Gen9, and off. */
	gt.graphics_ver = 12; gt.num_engines = 4;
	gt.engines[0].class = I915_RENDER_CLASS; gt.engines[0].mmio_base = 0x2000;
	gt.engines[1].class = I915_VIDEO_DECODE_CLASS; gt.engines[1].mmio_base = 0x1c0000; gt.engines[1].instance = 0;
	gt.engines[2].class = I915_VIDEO_DECODE_CLASS; gt.engines[2].mmio_base = 0x1d0000; gt.engines[2].instance = 2;
	gt.engines[3].class = I915_COPY_ENGINE_CLASS; gt.engines[3].mmio_base = 0x22000;
	drv_i915_rc6_init(&rc6, mmio); RC6_ENABLE(&rc6, mmio, &gt); printf("rc6 %d %x %x\n", rc6.enabled, rc6.ctl_enable, rc6.pg_enable);
	RC6_DISABLE(&rc6, mmio); gt.graphics_ver = 9; RC6_ENABLE(&rc6, mmio, &gt); printf("rc6 %d\n", rc6.enabled);
	rc6.supported = 0; RC6_ENABLE(&rc6, mmio, &gt); RC6_DISABLE(&rc6, mmio);

	/* RPS on Gen12 with the busy-time evaluation. */
	pcode_rc = 0; drv_i915_rps_init(&rps, NULL, mmio); dump("init", &rps);
	RPS_MASK_ALL(&rps, mmio);
	drv_i915_rps_enable(&rps, mmio, 19200000U); dump("enable", &rps);
	printf("start %d\n", drv_i915_rps_start(&rps, &irq, mmio)); dump("start", &rps);
	tick(&rps, 1000000, 0);
	drv_i915_rps_busy_begin(&rps); now_ns += 500000; dump("busy", &rps);
	for (i = 0; i < 12; i++)
		tick(&rps, 1000000, 990000);
	drv_i915_rps_busy_end(&rps);
	drv_i915_rps_boost_begin(&rps); work_fn(work_ctx); dump("boost", &rps); drv_i915_rps_boost_end(&rps); drv_i915_rps_boost_end(&rps);
	for (i = 0; i < 30; i++)
		tick(&rps, 4000000, i < 10 ? 3000000 : 0);
	drv_i915_rps_busy_begin(&rps); dump("unpark", &rps); tick(&rps, 1000000, 0); drv_i915_rps_busy_end(&rps);
	for (i = 0; i < 8; i++)
		tick(&rps, 2000000, 1900000);
	now_ns += 10; drv_i915_rps_telemetry_read(&rps, &te); printf("te %llu %llu %u %u %u %u\n", (unsigned long long)te.time_ns, (unsigned long long)te.busy_ns, te.req_mhz, te.min_mhz, te.max_mhz, te.valid);
	drv_i915_rps_stop(&rps); dump("stop", &rps);

	/* RPS on Gen12 the interrupt way, with the PCODE not answering. */
	memset(&rps, 0, sizeof(rps)); pcode_rc = 5; drv_i915_rps_init(&rps, NULL, mmio); rps.use_timer = 0;
	drv_i915_rps_enable(&rps, mmio, 19200000U);
	printf("start %d\n", drv_i915_rps_start(&rps, &irq, mmio)); dump("istart", &rps);
	for (i = 0; i < 10; i++) {
		irq.pm_handler(irq.pm_context, i < 6 ? 0x20U : (i == 6 ? 0x40U : 0x10U | 0x4U));
		work_fn(work_ctx); dump("iwork", &rps);
	}
	irq.pm_handler(irq.pm_context, 0x2U); dump("ignored", &rps);
	drv_i915_rps_boost_begin(&rps); work_fn(work_ctx); dump("iboost", &rps); drv_i915_rps_boost_end(&rps);
	drv_i915_rps_stop(&rps); dump("istop", &rps);

	/* A queue that is not created, a timer that is not created. */
	memset(&rps, 0, sizeof(rps)); pcode_rc = 0; drv_i915_rps_init(&rps, NULL, mmio); drv_i915_rps_enable(&rps, mmio, 24000000U);
	wq_rc = 12; printf("start %d\n", drv_i915_rps_start(&rps, &irq, mmio)); wq_rc = 0;
	tq_rc = 12; printf("start %d\n", drv_i915_rps_start(&rps, &irq, mmio)); dump("notimer", &rps); tq_rc = 0;
	drv_i915_rps_stop(&rps);

	/* Gen9: the fixed frequency. */
	memset(&rps, 0, sizeof(rps)); pcode_rc = 5; cap = 0x000c1022U; drv_i915_rps_init(&rps, NULL, mmio); drv_i915_rps_init_gen9(&rps, mmio);
	drv_i915_rps_enable(&rps, mmio, 12000000U); printf("start %d\n", drv_i915_rps_start(&rps, &irq, mmio)); dump("gen9", &rps);

	/* A range with no room. */
	memset(&rps, 0, sizeof(rps)); cap = 0x00101010U; drv_i915_rps_init(&rps, NULL, mmio); drv_i915_rps_enable(&rps, mmio, 1U); printf("start %d\n", drv_i915_rps_start(&rps, &irq, mmio)); dump("flat", &rps);

	/* The pure helpers over a sweep. */
	memset(&rps, 0, sizeof(rps)); cap = 0x00120a16U; pcode_rc = 0; drv_i915_rps_init(&rps, NULL, mmio); rps.pm_events = 0x30U;
	for (f = 0; f < 70; f += 3) {
		rps.cur_freq = f;
		for (i = 0; i < 8; i++) {
			static const uint32_t ev[8] = { 0, 0x20, 0x40, 0x10, 0x60, 0x2, 0x20, 0x10 };
			rps.last_adj = (int)i - 3;
			printf("next f=%u ev=%x b=%u -> %u", f, ev[i], i & 1U, drv_i915_rps_next_freq(&rps, ev[i], (int)(i & 1U), &adj));
			printf(" adj=%d\n", adj);
		}
		printf("mask %u %08x limits %08x int %u\n", f, drv_i915_rps_pm_mask(&rps, f), drv_i915_rps_limits(&rps, f), drv_i915_rps_pm_interval(19200000U, (uint64_t)f * 1000000U));
	}
	return 0;
}
