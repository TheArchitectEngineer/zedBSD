/*
 * WS031 Linux-parity -- GPU-free checks of the LCD-G release contract (E-119): the GT TLB invalidation words and its
 * failure, the render release (every mapping back to scratch, re-verified on every call, the TLB before any object is
 * freed, ownership kept on failure), the GPU-not-done retained state up to the outer teardown, and the reclaim when the
 * display never started.  Real DMA allocations, a sentinel GGTT table, a small MMIO model.  zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <string.h>
#include <errno.h>
#include "../osdep/mmio.h"
#include "../gt_mem.h"
#include "../gt_mmio.h"
#include "../gt_resume.h"
#include "../gt_tlb.h"
#include "../eu_test.h"
#include "../../draw_fixture.h"
#include "scanout.h"
#include "parity_lcd_show.h"
#include "parity_lcd_kernel.h"
#include "lcdg_ktest.h"

#define TABLE_ENTRIES 16384u
#define SCRATCH_PTE   0x00000000dead0001ull

/* ---- a tiny MMIO model: TLB invalidation registers complete at once unless told not to ---- */
struct tlb_fake {
	uint32_t last[8];
	uint32_t wr_off[32], wr_val[32];
	unsigned n;
	int stuck;
};

static uint32_t tf_read(void *priv, uint32_t off)
{
	struct tlb_fake *f = priv;
	unsigned i;

	if (off >= 0xced8u && off <= 0xcf04u && f->stuck)
		for (i = f->n; i-- > 0u;)
			if (f->wr_off[i] == off)
				return f->wr_val[i] & 0xffffu;         /* the done bit never clears */
	return 0u;
}

static void tf_write(void *priv, uint32_t off, uint32_t val)
{
	struct tlb_fake *f = priv;

	if (f->n < 32u) {
		f->wr_off[f->n] = off;
		f->wr_val[f->n] = val;
		f->n++;
	}
}

static void tf_fw_request(void *priv, int domain, int wake) { (void)priv; (void)domain; (void)wake; }
static int tf_fw_ack(void *priv, int domain) { (void)priv; (void)domain; return 1; }

static const struct osdep_mmio_backend tf_backend = { "lcdg-ktest", tf_read, tf_write, tf_fw_request, tf_fw_ack };

static uint64_t table[TABLE_ENTRIES];
static struct parity_gt_mem gm;
static struct parity_gt_ppgtt vm;
static struct osdep_mmio m;
static struct tlb_fake tf;
static struct spinlock ulock;
static struct parity_gt_engines es;
static struct parity_engine einfo[5];
static struct parity_gt_tlb tlb;
static struct parity_fhd_render fr;
static struct parity_scanout so;

static int map_draw(struct parity_fhd_render *x, struct parity_gt_object *rt, unsigned rt_pages)
{
	static const uint64_t va[3] = { PARITY_EU_SHARED_VA, PARITY_EU_BATCH_VA, I915_TEX_FIXTURE_TEX_VA };
	struct parity_gt_object *o[3];
	uint64_t dma;
	unsigned i;
	int rc;

	memset(x, 0, sizeof(*x));
	x->t.shared = parity_gt_object_create(&gm, 4096u);
	x->t.batch = parity_gt_object_create(&gm, 4096u);
	x->tex = parity_gt_object_create(&gm, 4096u);
	if (x->t.shared == 0 || x->t.batch == 0 || x->tex == 0)
		return -ENOMEM;
	o[0] = x->t.shared; o[1] = x->t.batch; o[2] = x->tex;
	rc = parity_gt_ppgtt_alloc_range(&gm, &vm, PARITY_EU_SHARED_VA, 5u * 4096u);
	if (rc == 0)
		rc = parity_gt_ppgtt_alloc_range(&gm, &vm, I915_TEX_FHD_RT_VA, (uint64_t)rt_pages * 4096u);
	for (i = 0u; i < 3u && rc == 0; i++)
		if ((rc = parity_gt_object_page_dma(o[i], 0u, &dma)) == 0)
			rc = parity_gt_ppgtt_insert_page(&vm, dma, va[i], 0u);
	for (i = 0u; i < rt_pages && rc == 0; i++)
		if ((rc = parity_gt_object_page_dma(rt, i, &dma)) == 0 &&
		    (rc = parity_gt_ppgtt_insert_page(&vm, dma, I915_TEX_FHD_RT_VA + (uint64_t)i * 4096u, 0u)) == 0)
			x->rt_pages_mapped++;
	x->rt = rt;
	x->t.submitted = 1;
	return rc;
}

static unsigned present_ptes(unsigned rt_pages)
{
	struct parity_gt_ppgtt_walk w;
	unsigned i, n = 0u;
	uint64_t va;

	for (i = 0u; i < 3u + rt_pages; i++) {
		va = i == 0u ? PARITY_EU_SHARED_VA : i == 1u ? PARITY_EU_BATCH_VA : i == 2u ? I915_TEX_FIXTURE_TEX_VA :
			I915_TEX_FHD_RT_VA + (uint64_t)(i - 3u) * 4096u;
		if (parity_gt_ppgtt_walk(&vm, va, &w) == 0 && w.levels == 4 && w.leaf_present && !w.scratch[3])
			n++;
	}
	return n;
}

void parity_lcdg_ktest(parity_lcdg_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask)
{
	static const int cls[5] = { PARITY_RENDER_CLASS, PARITY_COPY_ENGINE_CLASS, PARITY_VIDEO_DECODE_CLASS,
		PARITY_VIDEO_DECODE_CLASS, PARITY_VIDEO_ENHANCEMENT_CLASS };
	static const int inst[5] = { 0, 0, 0, 2, 0 };
	struct parity_gt_object *rt;
	uint32_t reg, req, done;
	unsigned i, live, rrc_ok;
	int rc, rrc;

	/* ---- TLB: the reference's register table and request encodings ---- */
	check(parity_gt_tlb_engine_reg(PARITY_RENDER_CLASS, 0, &reg, &req, &done) == 0 && reg == 0xced8u && req == 1u && done == 1u &&
		parity_gt_tlb_engine_reg(PARITY_COPY_ENGINE_CLASS, 0, &reg, &req, &done) == 0 && reg == 0xcee4u && req == 1u &&
		parity_gt_tlb_engine_reg(PARITY_VIDEO_DECODE_CLASS, 2, &reg, &req, &done) == 0 && reg == 0xcedcu && req == 0x00040004u &&
		done == 4u && parity_gt_tlb_engine_reg(PARITY_VIDEO_ENHANCEMENT_CLASS, 0, &reg, &req, &done) == 0 && reg == 0xcee0u &&
		req == 0x00010001u,
		"lcdg: TLB-REGS gen12 table: RCS 0xced8 / BCS 0xcee4 plain, VCS 0xcedc / VECS 0xcee0 masked, done = BIT(instance)");
	memset(&tf, 0, sizeof(tf));
	osdep_mmio_init(&m, &tf_backend, &tf, 0, 0u, 0);
	spin_init(&ulock, LOCK_RANK_DEVICE, "lcdg-ktest-uncore");
	memset(&es, 0, sizeof(es));
	for (i = 0u; i < 5u; i++) {
		einfo[i].class = cls[i];
		einfo[i].instance = inst[i];
		es.ge[i].info = &einfo[i];
	}
	es.n = 5u;
	memset(&tlb, 0, sizeof(tlb));
	rc = parity_gt_invalidate_tlb_full(&tlb, &es, &m, &ulock);
	check(rc == 0 && tf.n == 6u && tf.wr_off[0] == 0xced8u && tf.wr_off[3] == 0xcedcu && tf.wr_val[3] == 0x00040004u &&
		tf.wr_off[5] == 0xceecu && tf.wr_val[5] == 1u && tlb.seqno == 2u && tlb.engines_invalidated == 5u,
		"lcdg: TLB-FULL every engine's request, then Wa_2207587034 (OA 0xceec), done bits back to 0, seqno += 2");
	tf.n = 0u;
	tf.stuck = 1;
	rc = parity_gt_invalidate_tlb_full(&tlb, &es, &m, &ulock);
	check(rc == -ETIMEDOUT && tlb.timeouts >= 1u && tlb.seqno == 2u,
		"lcdg: TLB-STUCK an engine that never reports done is -ETIMEDOUT, and the seqno does not advance");
	tf.stuck = 0;

	/* ---- the address space ---- */
	for (i = 0u; i < TABLE_ENTRIES; i++)
		table[i] = 0x5a5a5a5a5a5a5a5aull;
	rc = parity_gt_mem_init(&gm, dma, dma_mask, table, TABLE_ENTRIES, SCRATCH_PTE, 0);
	rc = rc == 0 ? parity_gt_ppgtt_create(&gm, &vm) : rc;
	if (rc != 0) {
		check(0, "lcdg: SETUP gt_mem / ppgtt");
		return;
	}

	/* ---- the release contract ---- */
	rt = parity_gt_object_create(&gm, 4u * 4096u);
	rc = rt != 0 ? map_draw(&fr, rt, 4u) : -ENOMEM;
	live = gm.objects_live;
	rrc = rc == 0 ? parity_fhd_render_release(&fr, &gm, &vm, &tlb, &es, &m, &ulock) : rc;
	check(rc == 0 && rrc == -EBUSY && present_ptes(4u) == 7u && gm.objects_live == live && !fr.released,
		"lcdg: REL-BUSY the GPU not shown to be done: nothing is unmapped or freed");
	fr.gpu_done = 1;
	tf.stuck = 1;
	rrc = parity_fhd_render_release(&fr, &gm, &vm, &tlb, &es, &m, &ulock);
	check(rrc == -ETIMEDOUT && fr.maps_scratch == 7u && fr.maps_total == 7u && present_ptes(4u) == 0u && gm.objects_live == live &&
		fr.tex != 0 && fr.t.shared != 0 && fr.rt == rt && !fr.released,
		"lcdg: REL-TLB all 7 PTEs back to scratch, but the TLB invalidation failed: no object is freed, ownership stays");
	tf.stuck = 0;
	rrc = parity_fhd_render_release(&fr, &gm, &vm, &tlb, &es, &m, &ulock);
	check(rrc == 0 && fr.released && fr.maps_scratch == 7u && fr.maps_total == 7u && fr.release_calls == 3u &&
		gm.objects_live == live - 3u && rt->in_use && fr.rt == 0,
		"lcdg: REL-RETRY the retry re-verifies (7 of 7, not 14): then the TLB, then the draw's 3 objects -- the target itself is not freed");
	check(parity_fhd_render_release(&fr, &gm, &vm, &tlb, &es, &m, &ulock) == 0 && fr.release_calls == 3u,
		"lcdg: REL-ONCE a further call after success changes nothing");
	parity_gt_object_destroy(&gm, rt);

	/* ---- the display never started: the owner reclaims everything ---- */
	rc = parity_gt_display_window_init(&gm, PARITY_GT_DISPLAY_PAGES);
	memset(&so, 0, sizeof(so));
	rc = rc == 0 ? parity_scanout_create(&gm, 64u, 64u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so) : rc;
	rc = rc == 0 ? parity_scanout_pin(&so, "lcdg-ktest") : rc;
	rc = rc == 0 ? map_draw(&fr, so.obj, 4u) : rc;
	fr.gpu_done = 1;
	live = gm.objects_live;
	rrc_ok = 0u;
	if (rc == 0)
		rrc_ok = (unsigned)parity_lcdg_finish(&fr, &so, 0, 0, &gm, &vm, &tlb, &es, &m, &ulock, &rrc);
	check(rc == 0 && rrc_ok == 1u && rrc == 0 && so.state == PARITY_SCANOUT_NONE && gm.objects_live == live - 4u &&
		gm.display_allocated_pages == 0u && !parity_lcd_show_retained(),
		"lcdg: FIN-NOTSHOWN display never acquired + GPU done: mappings, TLB, draw objects, then the buffer -- all reclaimed, nothing retained");

	/* ---- the GPU is not shown to be done: kept up to the outer teardown ---- */
	memset(&so, 0, sizeof(so));
	rc = parity_scanout_create(&gm, 64u, 64u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so);
	rc = rc == 0 ? parity_scanout_pin(&so, "lcdg-ktest") : rc;
	rc = rc == 0 ? map_draw(&fr, so.obj, 4u) : rc;
	fr.gpu_done = 0;                                /* submitted, never retired: a hang not shown to be over */
	live = gm.objects_live;
	rrc_ok = rc == 0 ? (unsigned)parity_lcdg_finish(&fr, &so, 0, 0, &gm, &vm, &tlb, &es, &m, &ulock, &rrc) : 9u;
	check(rrc_ok == 0u && so.state == PARITY_SCANOUT_ABANDONED && so.obj->keep && fr.tex->keep && fr.t.shared->keep &&
		fr.t.batch->keep && present_ptes(4u) == 7u && gm.objects_live == live && parity_lcd_show_gpu_retained() &&
		parity_lcd_kernel_abandoned() && parity_lcd_kernel_gpu_retained(),
		"lcdg: FIN-GPU the GPU not done: buffer, state, batch, texture kept (keep=1), mappings left, device latch set");
	{
		static struct parity_lcd_kernel_deps dd;
		static char dummy[8];
		struct parity_lcd_test_summary sum;

		dd.edp = (void *)dummy; dd.mmio = &m; dd.gm = &gm; dd.es = &es; dd.vm = &vm; dd.irq = (void *)dummy;
		rc = parity_lcd_kernel_lcdg_run(&dd);
		parity_lcd_kernel_summary(&sum);
		check(rc == -1 && sum.retained == 1 && gm.objects_live == live,
			"lcdg: FIN-REFUSE a re-run is refused before anything is allocated; the runner summary says retained=1");
	}
	parity_gt_mem_fini(&gm);
	check(gm.kept_objects >= 4u && so.obj->in_use && fr.tex->in_use && fr.t.shared->in_use && fr.t.batch->in_use &&
		parity_lcd_show_gpu_retained(),
		"lcdg: FIN-TEARDOWN the outer teardown keeps the buffer AND the objects the request may use; the latch stays");
	check(parity_lcd_show_discard_gpu_model(&gm, 1) == 0 && !parity_lcd_show_gpu_retained(),
		"lcdg: FIN-DISCARD the latch is dropped only with its memory manager finalised (GPU-free model)");
}
