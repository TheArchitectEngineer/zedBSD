/*
 * The GT information equivalence test: runs drv_i915_gt_init_mmio over
 * scripted fuse and clock registers and prints every register access, log
 * and the resulting GT information, so two versions of device-info.c can
 * be compared line by line.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device-info.h"
#include "mmio.h"

static uint32_t ra[32], rv[32];
static unsigned rn;
static uint32_t qa[8], qv[8];
static unsigned qn;

static uint32_t
get(uint32_t a)
{
	unsigned i;

	for (i = 0; i < qn; i++) {
		if (qa[i] == a) {
			uint32_t v = qv[i];
			memmove(&qa[i], &qa[i + 1], (qn - i - 1) * sizeof(qa[0]));
			memmove(&qv[i], &qv[i + 1], (qn - i - 1) * sizeof(qv[0]));
			qn--;
			return v;
		}
	}
	for (i = 0; i < rn; i++)
		if (ra[i] == a)
			return rv[i];
	return 0;
}

static void set(uint32_t a, uint32_t v) { ra[rn] = a; rv[rn++] = v; }
static void queue(uint32_t a, uint32_t v) { qa[qn] = a; qv[qn++] = v; }

uint32_t drv_i915_read32(struct i915_mmio *m, uint32_t a) { uint32_t v = get(a); (void)m; printf("r %06x -> %08x\n", a, v); return v; }
void drv_i915_write32(struct i915_mmio *m, uint32_t a, uint32_t v) { (void)m; printf("w %06x <- %08x\n", a, v); }
void drv_i915_posting_read32(struct i915_mmio *m, uint32_t a) { (void)m; printf("p %06x\n", a); }
void info_equiv_log(const char *fmt, ...) { va_list ap; printf("log "); va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

static void
dump(const struct i915_gt_info *gt)
{
	unsigned i;

	printf("gt ver=%d hz=%u ns=%u valid=%d l3=%x/%d mask=%x n=%u sfc=%x/%x fault=%x seen=%d\n",
	    gt->graphics_ver, gt->clock_frequency, gt->clock_period_ns, gt->clock_valid, gt->l3bank_mask, gt->l3bank_steering,
	    gt->engine_mask, gt->num_engines, gt->vdbox_sfc_access, gt->sfc_mask, gt->fault_reg, gt->fault_valid_seen);
	printf("sseu %u/%u/%u s=%x ss=%x total=%u per=%u pg=%d\n", gt->sseu.max_slices, gt->sseu.max_subslices, gt->sseu.max_eus_per_subslice,
	    gt->sseu.slice_mask, gt->sseu.subslice_mask, gt->sseu.eu_total, gt->sseu.eu_per_subslice, gt->sseu.has_slice_pg);
	for (i = 0; i < I915_SSEU_MAX_SUBSLICES; i++)
		printf(" eu%u=%x", i, gt->sseu.eu_mask[i]);
	printf("\n");
	for (i = 0; i < gt->num_engines; i++)
		printf("engine %s id=%d c=%d i=%d base=%x mask=%x rd=%x ctx=%u caps=%x\n", gt->engines[i].name, gt->engines[i].id,
		    gt->engines[i].class, gt->engines[i].instance, gt->engines[i].mmio_base, gt->engines[i].mask,
		    gt->engines[i].reset_domain, gt->engines[i].context_size, gt->engines[i].uabi_capabilities);
}

static void
run(int ver, uint32_t platform)
{
	static struct i915_gt_info gt;

	memset(&gt, 0, sizeof(gt));
	drv_i915_gt_init_mmio(&gt, ver, platform, NULL);
	dump(&gt);
	rn = qn = 0;
}

int
main(void)
{
	uint32_t adl = (1U << I915_RCS0) | (1U << I915_BCS0) | (1U << I915_VCS0) | (1U << I915_VCS2) | (1U << I915_VECS0);
	uint32_t skl = (1U << I915_RCS0) | (1U << I915_BCS0) | (1U << I915_VCS0) | (1U << I915_VECS0);
	unsigned c;

	/* Gen12: each crystal and shift, the divide logic, fuses and faults. */
	for (c = 0; c < 8; c++) {
		set(0x00d00, (c << 3) | ((c & 3) << 1)); set(0x09138, c == 5 ? 3 : 1); set(0x0913c, 0x3f >> (c % 3)); set(0x09134, c * 0x11);
		set(0x09118, c == 4 ? 0xf : 0x2);
		run(12, adl);
	}
	set(0x0a26c, 1); set(0x044074, 0x3012); run(12, adl);
	set(0x09140, 0x1); run(12, adl);
	set(0x09140, 0x4); run(12, adl);
	set(0x09140, 0x10000); run(12, adl);
	set(0x0cec4, 0x3001); set(0x0ceb8, 0x12345); set(0x0cebc, 0x1f); set(0x020b0, 0x40); set(0x020b4, 0x3); run(12, adl);
	set(0x0cec4, 0x1); set(0x0cebc, 0x3); queue(0x020b0, 0); run(12, adl);
	run(11, adl);
	run(13, adl);
	/* Gen9: slices, subslice fuses, EU disables, the divide logic and the crystal shift, faults. */
	set(0x09120, (1U << 25)); set(0x09134, 0x01020304); run(9, skl);
	set(0x09120, (3U << 25) | (2U << 20)); set(0x09134, 0x0000ff00); set(0x09138, 0x01010101); run(9, skl);
	set(0x09120, (6U << 25) | (9U << 20)); set(0x09138, 0x3); set(0x0913c, 0x7f00); run(9, skl);
	set(0x0a26c, 2 << 1); run(9, skl);
	set(0x0a26c, 1); set(0x044074, 0x000b); run(9, skl);
	set(0x04094, 0x2001); set(0x04b10, 0x7); set(0x04b14, 0x10); run(9, skl);
	run(9, skl | (1U << (I915_VCS0 + 1)));
	return 0;
}
