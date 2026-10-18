/*
 * The interrupt equivalence test: runs install, uninstall, reset,
 * postinstall, the handler and the helpers over scripted registers and
 * prints every register access, log, wake and counter, so two versions of
 * irq.c can be compared line by line.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device-info.h"
#include "irq.h"
#include "mmio.h"
#include "sync.h"
#include <hal/hal.h>

#ifdef IRQ_EQUIV_OLD
#define REGS_CLOSE drv_i915_gen3_irq_reset
#define REGS_OPEN drv_i915_gen3_irq_init
#define REGS_CHECK drv_i915_gen3_assert_iir_is_zero
#define BANKS_SERVE drv_i915_gen11_gt_irq_handler
#else
#define REGS_CLOSE drv_i915_irq_regs_close
#define REGS_OPEN drv_i915_irq_regs_open
#define REGS_CHECK drv_i915_irq_regs_check_clear
#define BANKS_SERVE drv_i915_irq_gt_banks_serve
#endif

static uint32_t regs_addr[64], regs_val[64];
static unsigned regs_n;
static uint32_t q_addr[64], q_val[64];
static unsigned q_n;
static int attach_rc, detach_rc, udelay_rc, sleep_rc;
static hal_irq_handler_t attached;

static uint32_t
reg_get(uint32_t a)
{
	unsigned i;

	for (i = 0; i < q_n; i++) {
		if (q_addr[i] == a) {
			uint32_t v = q_val[i];
			memmove(&q_addr[i], &q_addr[i + 1], (q_n - i - 1) * sizeof(q_addr[0]));
			memmove(&q_val[i], &q_val[i + 1], (q_n - i - 1) * sizeof(q_val[0]));
			q_n--;
			return v;
		}
	}
	for (i = 0; i < regs_n; i++)
		if (regs_addr[i] == a)
			return regs_val[i];
	return 0;
}

static void set_reg(uint32_t a, uint32_t v) { regs_addr[regs_n] = a; regs_val[regs_n++] = v; }
static void queue_reg(uint32_t a, uint32_t v) { q_addr[q_n] = a; q_val[q_n++] = v; }

uint32_t drv_i915_raw_read32(struct i915_mmio *m, uint32_t a) { uint32_t v = reg_get(a); (void)m; printf("rr %06x -> %08x\n", a, v); return v; }
void drv_i915_raw_write32(struct i915_mmio *m, uint32_t a, uint32_t v) { (void)m; printf("rw %06x <- %08x\n", a, v); }
uint32_t drv_i915_read32(struct i915_mmio *m, uint32_t a) { uint32_t v = reg_get(a); (void)m; printf("r %06x -> %08x\n", a, v); return v; }
void drv_i915_write32(struct i915_mmio *m, uint32_t a, uint32_t v) { (void)m; printf("w %06x <- %08x\n", a, v); }
void drv_i915_posting_read32(struct i915_mmio *m, uint32_t a) { (void)m; printf("p %06x\n", a); }
int drv_i915_udelay(unsigned us) { printf("udelay %u -> %d\n", us, udelay_rc); return udelay_rc; }
void spin_init(struct spinlock *l, int rank, const char *name) { (void)l; printf("spin_init %d %s\n", rank, name); }
unsigned long spin_lock_irqsave(struct spinlock *l) { l->held = 1; printf("lock\n"); return 7; }
void spin_unlock_irqrestore(struct spinlock *l, unsigned long irq) { l->held = 0; printf("unlock %lu\n", irq); }
void waitq_init(struct wait_queue *q, const char *name) { (void)q; printf("waitq_init %s\n", name); }
uint64_t waitq_sequence(struct wait_queue *q) { (void)q; printf("waitq_sequence\n"); return 42; }
int waitq_sleep(struct wait_queue *q, struct spinlock *l, uint64_t o, uint64_t d, unsigned f) { (void)q; (void)l; printf("waitq_sleep %llu %llu %u -> %d\n", (unsigned long long)o, (unsigned long long)d, f, sleep_rc); return sleep_rc; }
void waitq_wake_all(struct wait_queue *q) { (void)q; printf("wake_all\n"); }
uint64_t sched_ticks(void) { return 1000; }
int hal_irq_attach_msi(int irq, hal_irq_handler_t h, void *a) { (void)a; printf("attach %d -> %d\n", irq, attach_rc); if (attach_rc == 0) attached = h; return attach_rc; }
int hal_irq_detach_msi_sync(int irq, hal_irq_handler_t h, void *a) { (void)a; printf("detach %d same=%d -> %d\n", irq, h == attached, detach_rc); return detach_rc; }
void hal_irq_send_eoi(hal_irq_ack_t ack) { printf("eoi %p\n", ack); }
void irq_equiv_log(const char *fmt, ...) { va_list ap; printf("log "); va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

static int ctx;

static void d_set(void *c, int e) { printf("d set_irqs_enabled %d %d\n", c == &ctx, e); }
static void d_check(void *c) { printf("d uninstall_check %d\n", c == &ctx); }
static void d_reset(void *c) { printf("d reset %d\n", c == &ctx); }
static void d_post(void *c) { printf("d postinstall %d\n", c == &ctx); }
static void d_handle(void *c, uint32_t m) { printf("d handle %d %08x\n", c == &ctx, m); }
static void d_gse(void *c) { printf("d gse %d\n", c == &ctx); }
static const struct i915_irq_display_ops dops = { d_set, d_check, d_reset, d_post, d_handle, d_gse };
static void pm(void *c, uint32_t v) { printf("pm %d %08x\n", c == &ctx, v); }

static struct i915_gt_info gt;
static struct i915_irq_dev irq;

static void
dump(void)
{
	unsigned i;
	const unsigned *u = (const unsigned *)&irq.irq_count;

	printf("C en=%d,%d att=%d masks=%08x,%08x,%08x", irq.irqs_enabled, irq.irq_enabled, irq.handler_attached, irq.gt_irqs, irq.gt_dmask, irq.gt_smask);
	(void)u;
	printf(" n=%u,%u,%u mc=%08x gu=%08x gse=%u gt=%u disp=%u", irq.irq_count, irq.irq_none_count, irq.irq_handled_count, irq.last_master_ctl, irq.last_gu_misc_iir, irq.gse_count, irq.gt_irq_count, irq.display_irq_count);
	for (i = 0; i < 2; i++)
		printf(" b%u=%u/%08x", i, irq.gt_bank_acks[i], irq.last_gt_intr_dw[i]);
	printf(" id=%u,%u eng=%u oth=%u unk=%u u=%u cs=%u e=%u s=%u lid=%08x", irq.gt_identity_reads, irq.gt_identity_invalid, irq.gt_engine_intrs, irq.gt_other_intrs, irq.gt_unknown_class, irq.gt_user_intr, irq.gt_ctx_switch_intr, irq.gt_error_intr, irq.gt_semaphore_intr, irq.last_gt_identity);
	printf(" ent=%u,%u wk=%u pm=%u sync=%u,%u,%u wr=%u,%u reach=%d,%d\n", irq.handler_entries, irq.handler_exits, irq.engine_wakeups, irq.gt_pm_intrs, irq.sync_calls, irq.sync_timeouts, irq.sync_time_faults, irq.reset_writes, irq.postinstall_writes, irq.reached_postinstall, irq.reached_master_enable);
}

static void
setup(int ver, int display, int submission)
{
	regs_n = q_n = 0;
	attach_rc = detach_rc = udelay_rc = sleep_rc = 0;
	attached = NULL;
	memset(&gt, 0, sizeof(gt));
	memset(&irq, 0, sizeof(irq));
	gt.graphics_ver = ver;
	gt.num_engines = 5;
	gt.engines[0].class = I915_RENDER_CLASS;
	gt.engines[1].class = I915_VIDEO_DECODE_CLASS;
	gt.engines[2].class = I915_VIDEO_DECODE_CLASS;
	gt.engines[2].instance = ver == 9 ? 1 : 2;
	gt.engines[3].class = I915_VIDEO_ENHANCEMENT_CLASS;
	gt.engines[4].class = I915_COPY_ENGINE_CLASS;
	irq.gt = ver < 0 ? NULL : &gt;
	irq.msi_irq = 33;
	irq.submission = submission;
	if (display) {
		irq.display_ops = &dops;
		irq.display_context = &ctx;
	}
}

static void
fire(void)
{
	printf("-- fire\n");
	if (attached != NULL)
		attached(33, (hal_irq_ack_t)0x55, &irq);
	dump();
}

#define ID(c, i, b) (0x80000000U | ((uint32_t)(c) << 16) | ((uint32_t)(i) << 20) | (b))

int
main(void)
{
	int ver;
	static const int vers[] = { 12, 9, -1 };
	unsigned v;

	for (v = 0; v < 3; v++) {
		ver = vers[v];
		printf("=== ver %d\n", ver);

		/* Install with and without a display table, both backends; a failed attach. */
		setup(ver, 1, I915_SUBMISSION_EXECLISTS); printf("= %d\n", drv_i915_irq_install(&irq)); dump();
		setup(ver, 0, I915_SUBMISSION_GUC); printf("= %d\n", drv_i915_irq_install(&irq)); dump();
		setup(ver, 1, I915_SUBMISSION_EXECLISTS); attach_rc = 3; printf("= %d\n", drv_i915_irq_install(&irq)); dump();
		drv_i915_irq_uninstall(&irq); dump();
		/* A stale IIR seen by the postinstall check. */
		setup(ver, 1, I915_SUBMISSION_EXECLISTS); set_reg(0x444f8, 0x5); set_reg(0x44328, 0x9); printf("= %d\n", drv_i915_irq_install(&irq)); dump();

		/* The handler on a clean install. */
		setup(ver, 1, I915_SUBMISSION_EXECLISTS); (void)drv_i915_irq_install(&irq);
		irq.pm_handler = pm; irq.pm_context = &ctx;
		/* disabled */
		irq.irqs_enabled = 0; fire(); irq.irqs_enabled = 1;
		/* spurious */
		fire();
		if (ver == 9) {
			uint32_t m = 0x44200;
			uint32_t bits[] = { 1U << 0, 1U << 1, 1U << 2, 1U << 3, 1U << 4, 1U << 6, 1U << 7, 1U << 16, 1U << 22, 0x80000000U | 0xffU, 0xffffffffU };
			unsigned i;
			for (i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
				queue_reg(m, bits[i]);
				queue_reg(0x44308, 0x01110111U);
				queue_reg(0x44318, 0x01010101U);
				queue_reg(0x44328, 0x00000070U);
				queue_reg(0x44338, 0x00000100U);
				fire();
				q_n = 0;
			}
			irq.pm_handler = NULL; queue_reg(m, 1U << 4); queue_reg(0x44328, 0x7); fire(); q_n = 0;
			queue_reg(m, 1U << 0); fire(); q_n = 0;
		} else {
			uint32_t m = 0x190010;
			/* bank 0: rcs user, bcs ctx switch, gtpm; identity sequences. */
			queue_reg(m, 0x1); queue_reg(0x190018, (1U << 0) | (1U << 16) | (1U << 20));
			queue_reg(0x190060, 0); queue_reg(0x190060, ID(0, 0, 0x1));
			queue_reg(0x190060, ID(4, 1, 0x70));
			queue_reg(0x190060, ID(4, 2, 0x1));
			fire(); q_n = 0;
			/* bank 1 + display + gu misc gse */
			queue_reg(m, 0x2 | (1U << 16) | (1U << 29)); queue_reg(0x19001c, 0x3);
			queue_reg(0x190064, ID(1, 0, 0x100)); queue_reg(0x190064, ID(1, 2, 0x8 | 0x800));
			queue_reg(0x444f8, 1U << 27);
			fire(); q_n = 0;
			/* unknown class, invalid identity, unknown instance, no bits */
			queue_reg(m, 0x1); queue_reg(0x190018, 0x7);
			queue_reg(0x190060, ID(6, 0, 0x1));
			queue_reg(0x190060, ID(1, 3, 0x1));
			queue_reg(0x190060, ID(0, 0, 0x0));
			fire(); q_n = 0;
			queue_reg(m, 0x1); queue_reg(0x190018, 0x1); fire(); q_n = 0;
			/* gu misc without gse, display only */
			queue_reg(m, 1U << 29); queue_reg(0x444f8, 0x4); fire(); q_n = 0;
			queue_reg(m, 1U << 16); fire(); q_n = 0;
			queue_reg(m, 0x1); queue_reg(0x190018, 0); fire(); q_n = 0;
			printf("-- banks\n"); queue_reg(0x190018, 1U << 3); queue_reg(0x190060, ID(3, 0, 0x8)); BANKS_SERVE(&irq, 0x3); dump(); q_n = 0;
			/* pm reset: pending twice, then always pending */
			printf("-- pm reset\n"); queue_reg(0x190018, 1U << 16); queue_reg(0x190018, 1U << 16); queue_reg(0x190060, ID(4, 1, 0x10)); queue_reg(0x190060, ID(4, 1, 0x10));
			drv_i915_gt_pm_reset_iir(&irq); dump(); q_n = 0;
			set_reg(0x190018, 1U << 16); set_reg(0x190060, ID(4, 1, 0x10)); drv_i915_gt_pm_reset_iir(&irq); dump(); regs_n = 0;
		}

		/* engine wait and sequence */
		printf("seq %llu\n", (unsigned long long)drv_i915_irq_engine_sequence(&irq));
		printf("wait %d\n", drv_i915_irq_engine_wait(&irq, 5, 10));
		sleep_rc = EAGAIN; printf("wait %d\n", drv_i915_irq_engine_wait(&irq, 5, 10));
		sleep_rc = ETIMEDOUT; printf("wait %d\n", drv_i915_irq_engine_wait(&irq, 5, 10));
		/* synchronize: balanced, a stuck invocation, a time base failure */
		printf("sync %d\n", drv_i915_synchronize_irq(&irq));
		irq.handler_entries++; udelay_rc = 0; printf("sync %d\n", drv_i915_synchronize_irq(&irq));
		udelay_rc = 5; printf("sync %d\n", drv_i915_synchronize_irq(&irq)); irq.handler_exits++; udelay_rc = 0;
		/* the register helpers */
		printf("-- helpers\n"); REGS_CLOSE(&irq, 0x100, 0x104, 0x108); set_reg(0x204, 3); REGS_OPEN(&irq, 0x200, 0xfff0, 0x208, 0xf, 0x204); REGS_CHECK(&irq, 0x300); dump(); regs_n = 0;
		/* reset and postinstall again, uninstall with a failing detach, uninstall again */
		drv_i915_irq_reset(&irq); drv_i915_irq_postinstall(&irq); dump();
		detach_rc = 9; drv_i915_irq_uninstall(&irq); dump();
		drv_i915_irq_uninstall(&irq); dump();
		printf("wait %d\n", drv_i915_irq_engine_wait(&irq, 5, 10));
	}
	return 0;
}
