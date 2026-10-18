/*
 * The workaround equivalence test: builds every list for a Gen12 GT and
 * for each Gen9 platform and stepping, dumps the manifests, programs the
 * GT and resumes every engine over stubbed registers, and prints every
 * register access and log, so two versions of workarounds.c can be
 * compared line by line.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "workarounds.h"
#include "device-info.h"
#include "mmio.h"

#ifdef WA_EQUIV_OLD
#define GT_PROGRAM drv_i915_gt_init_hw_core
#else
#define GT_PROGRAM drv_i915_gt_program
#endif

void kern_logf(const char *fmt, ...) { va_list ap; printf("log "); va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
uint32_t drv_i915_read32(struct i915_mmio *m, uint32_t r) { uint32_t v = (r * 2654435761U) ^ 0x00ff00f0U; (void)m; printf("r %06x -> %08x\n", r, v); return v; }
void drv_i915_write32(struct i915_mmio *m, uint32_t r, uint32_t v) { (void)m; printf("w %06x <- %08x\n", r, v); }
void drv_i915_rc6_init(struct i915_rc6 *rc6, struct i915_mmio *m) { (void)rc6; (void)m; printf("rc6_init\n"); }
void drv_i915_rps_init(struct i915_rps *rps, struct mutex *l, struct i915_mmio *m) { (void)rps; (void)l; (void)m; printf("rps_init\n"); }
void drv_i915_rps_init_gen9(struct i915_rps *rps, struct i915_mmio *m) { (void)rps; (void)m; printf("rps_init_gen9\n"); }

static void
run(struct i915_gt_info *gt)
{
	static struct i915_gt_init gi;
	struct i915_mmio *mmio = (struct i915_mmio *)gt;
	unsigned i;

	memset(&gi, 0, sizeof(gi));
	printf("=== ver %d platform %d revision %u\n", gt->graphics_ver, gt->platform, gt->revision);
	drv_i915_gt_init_tables(&gi, gt, gt->graphics_ver, NULL, mmio);
	drv_i915_wa_list_dump(&gi.gt_wa, "gt");
	for (i = 0; i < gt->num_engines; i++) {
		drv_i915_wa_list_dump(&gi.engine_wa[i], "engine");
		drv_i915_wa_list_dump(&gi.ctx_wa[i], "ctx");
		drv_i915_wa_list_dump(&gi.whitelist[i], "whitelist");
	}
	printf("mocs n=%u uc=%u unused=%u valid=%d engine=%d", gi.mocs.n_entries, gi.mocs.uc_index, gi.mocs.unused_entries_index, gi.mocs.valid, gi.mocs.engine_mocs);
	for (i = 0; i < 64; i++)
		printf(" %x/%x", gi.mocs.control[i], gi.mocs.l3cc[i]);
	printf("\n");
	GT_PROGRAM(&gi, gt, mmio);
	for (i = 0; i < gt->num_engines + 1U; i++)
		drv_i915_engine_apply_resume_wa(&gi, gt, i, mmio);
	printf("done pat=%d gt=%u/%u/%u/%u/%u mocs=%u/%u wl=%u res=%u built=%d hw=%d\n", gi.pat_programmed, gi.gt_wa_applied.written, gi.gt_wa_applied.skipped_unchanged, gi.gt_wa_applied.verified, gi.gt_wa_applied.mismatched, gi.gt_wa_applied.not_verifiable,
	    gi.mocs_global_writes, gi.mocs_l3cc_writes, gi.whitelist_writes, gi.engines_resumed, gi.tables_built, gi.hw_programmed);
}

int
main(void)
{
	static struct i915_gt_info gt;
	static const char *names[5] = { "rcs0", "bcs0", "vcs0", "vcs1", "vecs0" };
	static const int classes[5] = { I915_RENDER_CLASS, I915_COPY_ENGINE_CLASS, I915_VIDEO_DECODE_CLASS, I915_VIDEO_DECODE_CLASS, I915_VIDEO_ENHANCEMENT_CLASS };
	static const int ids[5] = { I915_RCS0, I915_BCS0, I915_VCS0, I915_VCS2, I915_VECS0 };
	static const uint32_t bases[5] = { 0x2000, 0x22000, 0x1c0000, 0x1d0000, 0x1c8000 };
	static const unsigned gen9[][2] = { { 1, 0 }, { 1, 7 }, { 2, 0 }, { 2, 2 }, { 2, 4 }, { 2, 5 }, { 2, 6 }, { 3, 0 }, { 4, 0 } };
	unsigned i;

	gt.num_engines = 5;
	for (i = 0; i < 5; i++) {
		gt.engines[i].class = classes[i];
		gt.engines[i].id = ids[i];
		gt.engines[i].instance = i == 3 ? 2 : 0;
		gt.engines[i].mmio_base = bases[i];
		gt.engines[i].name = names[i];
	}
	gt.graphics_ver = 12; gt.platform = I915_PLATFORM_GEN12; gt.sseu.subslice_mask = 0x00f0; gt.sseu.slice_mask = 1;
	run(&gt);
	gt.engines[3].instance = 1;
	for (i = 0; i < sizeof(gen9) / sizeof(gen9[0]); i++) {
		gt.graphics_ver = 9; gt.platform = (int)gen9[i][0]; gt.revision = gen9[i][1]; gt.sseu.slice_mask = 0x6; gt.sseu.subslice_mask = 0x6;
		run(&gt);
	}
	return 0;
}
