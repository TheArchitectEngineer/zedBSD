/*
 * The GT reset equivalence test: runs drv_i915_gt_reset_all and
 * drv_i915_gt_reset_engine over scripted registers and prints every
 * register access, wait, forcewake, lock, delay and log, so two versions of
 * the reset code can be compared line by line.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/lock.h>
#include "device-info.h"
#include "mmio.h"
#include "reset.h"
#include "sync.h"
#include <kern/lock.h>

static uint32_t regs_addr[64], regs_val[64];
static unsigned regs_n;
static uint32_t q_addr[16], q_val[16];
static unsigned q_n;
static int wait_err[32];
static unsigned wait_i;
static int fw_fail_domain;
static int domain_of;
static int udelay_err;

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

uint32_t drv_i915_raw_read32(struct i915_mmio *m, uint32_t a) { uint32_t v = reg_get(a); (void)m; printf("r %06x -> %08x\n", a, v); return v; }
void drv_i915_raw_write32(struct i915_mmio *m, uint32_t a, uint32_t v) { (void)m; printf("w %06x <- %08x\n", a, v); }
int drv_i915_wait_reg(struct i915_mmio *m, uint32_t reg, uint32_t mask, uint32_t value, unsigned fast_us, unsigned slow_ms, uint32_t *last)
{
	int e = wait_err[wait_i++];

	(void)m;
	if (last != NULL)
		*last = 0;
	printf("wait %06x %08x %08x %u %u -> %d\n", reg, mask, value, fast_us, slow_ms, e);
	return e;
}
int drv_i915_forcewake_get(struct i915_mmio *m, int d) { (void)m; printf("fw get %d\n", d); return d == fw_fail_domain ? 5 : 0; }
int drv_i915_forcewake_put(struct i915_mmio *m, int d) { (void)m; printf("fw put %d\n", d); return 0; }
int drv_i915_mmio_domain_of(const struct i915_mmio *m, uint32_t off) { (void)m; printf("domain_of %06x\n", off); return domain_of; }
int drv_i915_udelay(unsigned us) { printf("udelay %u -> %d\n", us, udelay_err); return udelay_err; }
unsigned long spin_lock_irqsave(struct spinlock *l) { l->held = 1; printf("lock\n"); return 7; }
void spin_unlock_irqrestore(struct spinlock *l, unsigned long irq) { l->held = 0; printf("unlock %lu\n", irq); }
void reset_equiv_log(const char *fmt, ...) { va_list ap; printf("log "); va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

static struct i915_gt_info gt;

static void
reset(int ver)
{
	regs_n = q_n = wait_i = 0;
	memset(wait_err, 0, sizeof(wait_err));
	fw_fail_domain = -100;
	domain_of = 4;
	udelay_err = 0;
	memset(&gt, 0, sizeof(gt));
	gt.graphics_ver = ver;
	gt.num_engines = 5;
	gt.engines[0] = (struct i915_engine_info){ .class = I915_RENDER_CLASS, .instance = 0, .mmio_base = 0x2000, .reset_domain = 0, .name = "rcs0" };
	gt.engines[1] = (struct i915_engine_info){ .class = I915_VIDEO_DECODE_CLASS, .instance = 0, .mmio_base = 0x1c0000, .reset_domain = 1U << 5, .name = "vcs0" };
	gt.engines[2] = (struct i915_engine_info){ .class = I915_VIDEO_DECODE_CLASS, .instance = 2, .mmio_base = 0x1d0000, .reset_domain = 1U << 7, .name = "vcs2" };
	gt.engines[3] = (struct i915_engine_info){ .class = I915_VIDEO_ENHANCEMENT_CLASS, .instance = 0, .mmio_base = 0x1c8000, .reset_domain = 1U << 13, .name = "vecs0" };
	gt.engines[4] = (struct i915_engine_info){ .class = I915_COPY_ENGINE_CLASS, .instance = 0, .mmio_base = 0x22000, .reset_domain = 1U << 3, .name = "bcs0" };
	gt.vdbox_sfc_access = 0x5U;
}

#define ENG(i) printf("= %d\n", drv_i915_gt_reset_engine(&lock, NULL, &gt, &gt.engines[i], 2000))

int
main(void)
{
	struct spinlock lock = { 0 };
	unsigned i;

	/* The whole GT: ok, every attempt times out, a timeout then ok, a settle fault, forcewake failures. */
	reset(12); printf("= %d\n", drv_i915_gt_reset_all(&lock, NULL, 2000));
	reset(12); for (i = 0; i < 6; i++) wait_err[i] = 110; printf("= %d\n", drv_i915_gt_reset_all(&lock, NULL, 2000));
	reset(12); wait_err[1] = 110; printf("= %d\n", drv_i915_gt_reset_all(&lock, NULL, 2000));
	reset(12); udelay_err = 5; printf("= %d\n", drv_i915_gt_reset_all(&lock, NULL, 2000));
	reset(12); fw_fail_domain = I915_FORCEWAKE_RENDER; printf("= %d\n", drv_i915_gt_reset_all(&lock, NULL, 2000));
	reset(12); fw_fail_domain = I915_FORCEWAKE_GT; printf("= %d\n", drv_i915_gt_reset_all(&lock, NULL, 2000));

	/* One engine: no reset domain, the copy engine with the engine already ready, and not ready. */
	reset(12); ENG(0);
	reset(12); set_reg(0x220d0, 2); ENG(4);
	reset(12); ENG(4);
	reset(12); wait_err[0] = 110; ENG(4);
	reset(12); set_reg(0x220d0, 4); ENG(4);
	reset(12); domain_of = -1; ENG(4);
	reset(12); fw_fail_domain = 4; ENG(4);
	reset(12); fw_fail_domain = I915_FORCEWAKE_GT; ENG(4);
	/* A decode engine whose own pipe uses the converter: held, and kept in use. */
	reset(12); set_reg(0x1c0890, 1); ENG(1);
	/* ... the lock not acknowledged while the converter went free, and while still in use. */
	reset(12); queue_reg(0x1c0890, 1); wait_err[1] = 110; ENG(1);
	reset(12); set_reg(0x1c0890, 1); wait_err[1] = 110; ENG(1);
	/* The HEVC pipe uses it: through the enhancement partner, kept free, and taken over. */
	reset(12); set_reg(0x1c2914, 1); ENG(1);
	reset(12); set_reg(0x1c2914, 1); set_reg(0x1ca014, 1); ENG(1);
	reset(12); set_reg(0x1c2914, 1); queue_reg(0x1ca014, 1); wait_err[1] = 110; ENG(1);
	/* Nobody uses it; a decode engine without converter access; a partner fused off. */
	reset(12); ENG(1);
	reset(12); gt.vdbox_sfc_access = 0x1U; set_reg(0x1d0890, 1); ENG(2);
	reset(12); gt.num_engines = 3; set_reg(0x1d2914, 1); ENG(2);
	reset(12); set_reg(0x1d2914, 1); gt.engines[3].instance = 1; ENG(2);
	/* An enhancement engine using its own converter, and Gen9. */
	reset(12); set_reg(0x1ca014, 1); ENG(3);
	reset(9); set_reg(0x1ca014, 1); ENG(3);
	/* The pulse times out, and the settle time faults. */
	reset(12); set_reg(0x1c0890, 1); wait_err[2] = 110; ENG(1);
	reset(12); set_reg(0x1c0890, 1); udelay_err = 5; ENG(1);
	return 0;
}
