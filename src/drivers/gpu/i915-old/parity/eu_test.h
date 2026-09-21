/*
 * WS031 Linux-parity — the one-shot real-hardware EU test (explicitly released).
 *
 * The compute positive control of the big-bang investigation (C1: GPGPU_WALKER
 * dispatching one SIMD8 thread whose kernel does an unconditional A64 store of
 * 0xc0ffee02 and then send.ts EOT), executed on the GT that the parity port
 * initialised the Linux way.  The kernel, IDD, VFE state and command order are
 * the L-C1 case that completed on Linux i915 on this same GPU (E-25).  It is
 * NOT byte-identical to that replay: the batch VA differs (0x100401000 here,
 * 0x100600000 there) and this batch carries diagnostic MI_COPY_MEM_MEM
 * readbacks.  Up to E-98 the PIPELINE_SELECT words were also mis-encoded
 * (0x6104xxxx, the GPGPU_CSR_BASE_ADDRESS header); identity is established by
 * comparing the final submitted bytes, never by provenance.
 *
 * The request is shaped like an execbuf request (gem/i915_gem_execbuffer.c,
 * gt/gen8_engine_cs.c): i915_request_create (invalidate flush) ->
 * gen8_emit_init_breadcrumb (seqno-1 store, MI_ARB_CHECK) ->
 * gen8_emit_bb_start (ARB on, MI_BATCH_BUFFER_START_GEN8 into the PPGTT,
 * ARB off) -> i915_request_add (fini breadcrumb), on a fresh context of the
 * kernel vm that inherits engine->default_state (lrc_init_state).
 *
 * Outcome: PASS when the CS markers land and the EU store is visible;
 * HANG when the request does not retire within the timeout (the engines are
 * then reset the way intel_gt_set_wedged would).
 */
#ifndef PARITY_EU_TEST_H
#define PARITY_EU_TEST_H

#include <stdint.h>
#include "gt_mem.h"
#include "gt_lrc.h"
#include "gt_request.h"

struct parity_gt_engines;
struct parity_gt_ppgtt;
struct parity_sseu;
struct osdep_mmio;
struct spinlock;

#define PARITY_EU_SHARED_VA      0x100400000ull
#define PARITY_EU_BATCH_VA       0x100401000ull
#define PARITY_EU_IDD_OFFSET     896u
#define PARITY_EU_KSP_OFFSET     1024u
#define PARITY_EU_READY_OFF      0xc00u
#define PARITY_EU_EU_OFF         0xc20u     /* hard-coded in the store kernel */
#define PARITY_EU_DONE_OFF       0xc28u
#define PARITY_EU_CS_OFF         0xc30u
#define PARITY_EU_IDD_RB_OFF     0xd00u
#define PARITY_EU_KERNEL_RB_OFF  0xe00u
#define PARITY_EU_READY_TAG      0xc0ffee10u
#define PARITY_EU_STORE_TAG      0xc0ffee02u
#define PARITY_EU_DONE_TAG       0xc0ffee20u
#define PARITY_EU_CS_TAG         0xc0ffee30u
#define PARITY_EU_KERNEL_DWORDS  36u

/* The real-hardware EU test runs only when explicitly released (build-time). */
#ifndef PARITY_EU_TEST
#define PARITY_EU_TEST 0
#endif

/* The real-hardware 3D draw (PS) test: its own build flag, its own boot (no C1 before it). */
#ifndef PARITY_DRAW_TEST
#define PARITY_DRAW_TEST 0
#endif
#define PARITY_DRAW_RT_VA  0x100402000ull

/*
 * R1 (E-102): draw repeats and compute<->3D switching in ONE boot, after one
 * P0..P7, on the same request path.  Its own mode; the single EU test and the
 * single draw test stay as they are and are not run in this mode.
 */
#ifndef PARITY_R1_TEST
#define PARITY_R1_TEST 0
#endif
#define PARITY_R1_DRAW_BATCH_VA  0x100403000ull

/* T2 (E-103): the first textured draw; its own mode, its own boot (nothing submitted before it). */
#ifndef PARITY_TEX_TEST
#define PARITY_TEX_TEST 0
#endif

/* T3 (E-104): texture update, binding switch, redraw on the same and on a new context; one boot. */
#ifndef PARITY_T3_TEST
#define PARITY_T3_TEST 0
#endif

/* E-105: bilinear filtering; the T3 runner with another plan.  Its own mode, its own boot. */
#ifndef PARITY_BL_TEST
#define PARITY_BL_TEST 0
#endif

#define PARITY_EU_PASS   1
#define PARITY_EU_HANG   2
#define PARITY_EU_ERROR  3

/*
 * Strict Gen12.0 check of the PIPELINE_SELECT words of a built C1 batch,
 * against fixed reference words (not the emitter's macro).  0 when the batch
 * has exactly one 0x69041310 (3D) followed by exactly one 0x69041312 (GPGPU)
 * and no dword carrying the 0x6104 header (GPGPU_CSR_BASE_ADDRESS, which C1
 * never emits) or an unexpected 0x6904 word.
 */
struct parity_eu_pipesel_check {
	unsigned n_3d, n_gpgpu, n_bad;
	unsigned idx_3d, idx_gpgpu, idx_bad;
	uint32_t bad_word;
};
int parity_eu_batch_check_pipeline_select(const uint32_t *cmds, unsigned n,
	struct parity_eu_pipesel_check *out);

/*
 * Follow-up rounds after the first PASS (E-100): the same C1 bytes again on
 * the same context (the next requests of its timeline), then on a new context.
 * Stops at the first round that does not pass; nothing is submitted after a hang.
 */
#define PARITY_EU_ROUNDS_MAX 8
struct parity_eu_round {
	char ctx;                       /* 'A' = the first context, 'B' = a new one */
	int rc;
	int completed, parked, pass;
	uint32_t seqno, hwsp_observed;
	uint32_t ready, eu, done, cs;
	int idd_rb_ok, kernel_rb_ok;
	unsigned polls;
	uint32_t lrca, ring_head, ring_tail;
	uint32_t csb_hi, csb_lo;
};

struct parity_eu_test {
	unsigned engine_idx;
	unsigned ptes_scrubbed;          /* fixture PTEs put back to scratch at release */
	struct parity_gt_context ce;
	struct parity_gt_object *tl_page;
	uint32_t tl_seqno;
	struct parity_gt_object *shared;
	struct parity_gt_object *batch;
	unsigned batch_dwords;
	uint32_t max_threads;
	unsigned dss_count;

	struct parity_gt_request rq;
	struct parity_gt_request krq;

	/* follow-up rounds */
	struct parity_gt_context ce2;
	struct parity_gt_object *tl_page2;
	uint32_t tl_seqno2;
	struct parity_gt_request rrq;
	struct parity_eu_round round[PARITY_EU_ROUNDS_MAX];
	unsigned n_rounds, rounds_passed;
	int submitted, completed, parked, timed_out, wedged;
	unsigned polls;

	/* what the shared page says afterwards */
	uint32_t ready, eu, done, cs;
	uint32_t idd_rb[8];
	uint32_t kernel_rb[PARITY_EU_KERNEL_DWORDS];
	int idd_rb_ok, kernel_rb_ok;

	/* fixture identity: FNV-1a 64 over the batch dwords and the shared fixture */
	uint64_t batch_hash;
	uint64_t fixture_hash;
	/* PIPELINE_SELECT words read back from the object that is submitted */
	struct parity_eu_pipesel_check pipesel;
	int pipesel_rc;

	/* what the GPU walks for the fixture VAs (read from the submitted tables) */
	struct parity_gt_ppgtt_walk walk[5];
	unsigned walks;
	int pdp0_matches_top;

	/* hang record (before the reset) */
	uint32_t hwsp_seqno_observed;
	uint32_t ctx_ccid_hi, ctx_ccid_lo;
	int time_base_fault;

	int outcome;
	int err;
	const char *err_where;
};

/* MCR (multicast) workaround readback: intel_gt_mcr_read() per DSS. */
struct parity_mcr_probe_entry {
	uint32_t reg;
	unsigned group, instance;
	uint32_t raw, expected_set, read_mask, masked_mismatch;
	uint32_t selector_before, selector_after;
	int listed;                 /* the register is in the workaround list */
};
#define PARITY_MCR_PROBE_MAX 32
struct parity_mcr_probe {
	struct parity_mcr_probe_entry e[PARITY_MCR_PROBE_MAX];
	unsigned n;
	unsigned mismatches;
	int lock_rc;
};

/*
 * The big-bang RECTLIST draw (draw_fixture.h: same command list and state
 * bytes, PIPELINE_SELECT fixed) submitted the way the C1 request is: a new
 * context of the kernel vm, execbuf-shaped request, state page at
 * 0x100400000, batch at 0x100401000, render target at 0x100402000.
 */
struct parity_draw_test {
	struct parity_eu_test t;          /* context, request, objects (shared = state page), hang record */
	struct parity_gt_object *rt;
	uint32_t mocs;
	uint64_t state_hash;
	uint32_t marker_before, marker_middraw, marker_after, ps_marker;
	uint32_t px_first, px_mid, px_last;
	unsigned px_match, px_total;
	uint32_t stats_live[6];           /* hang only: live MMIO read before the reset */
	int stats_valid;
};

int parity_draw_test_run(struct parity_draw_test *d, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm, struct osdep_mmio *m,
	struct spinlock *uncore_lock, unsigned timeout_ms);
void parity_draw_test_release(struct parity_draw_test *d, struct parity_gt_mem *gm);

/* One 3D select, no GPGPU select, no 0x6104/unknown-0x6904 word: 0, else -EINVAL. */
int parity_draw_batch_check_pipeline_select(const uint32_t *cmds, unsigned n,
	struct parity_eu_pipesel_check *out);

/*
 * The textured RECTLIST draw (draw_fixture.h T1): state page 0x100400000, batch
 * 0x100401000, render target 0x100402000, texture 0x100404000, submitted the way
 * the C1 and single-colour draw requests are.
 */
#define PARITY_TEX_GUARD_BYTE 0xa5u
struct parity_tex_test;
struct parity_tex_test {
	struct parity_eu_test t;
	struct parity_gt_object *rt;
	struct parity_gt_object *tex;
	uint32_t mocs;
	uint64_t state_hash, tex_hash;
	uint32_t marker_before, marker_middraw, marker_after, ps_marker;
	unsigned px_match, px_stale, px_total;
	int first_bad_x, first_bad_y;
	uint32_t first_bad_expected, first_bad_observed;
	unsigned tex_changed_bytes;       /* texel bytes that differ from what the CPU wrote */
	unsigned guard_bad_bytes;         /* guard bytes after the 256-byte image that changed */
	uint32_t stats_live[6];
	int stats_valid;
};

/*
 * E-118: the full-HD textured draw into an object the CALLER owns (the scanout buffer): it is mapped into the PPGTT at
 * I915_TEX_FHD_RT_VA page by page (the same backing pages as its GGTT display binding), pre-filled and published by the
 * CPU, drawn by the GPU, and compared pixel by pixel after the request retired and the CPU view was invalidated.  The
 * object is never written by the CPU after the draw, and never freed here.
 */
struct parity_fhd_render {
	struct parity_eu_test t;
	struct parity_gt_object *tex;
	struct parity_gt_object *rt;           /* NOT owned */
	uint32_t mocs, rt_rss_mocs;
	uint32_t marker_before, marker_middraw, marker_after, ps_marker;
	unsigned px_match, px_stale, px_total;
	int first_bad_x, first_bad_y;
	uint32_t first_bad_expected, first_bad_observed;
	unsigned tex_changed_bytes, guard_bad_bytes;
	unsigned rt_pages, rt_pages_mapped, rt_pages_cleared;
	int rt_walk_ok;                        /* first / middle / last page: the PPGTT leaf is the object's own page */
	uint64_t rt_first_dma, rt_last_dma;
	int gpu_done;                          /* request retired and engine parked: the GPU no longer uses the target */
	/* release: every mapping, the TLB, then the objects -- re-verified from the tables on every call */
	unsigned maps_total, maps_scratch;     /* PTEs this draw wrote / found back at scratch by the LAST release call */
	int tlb_rc, released;                  /* released: mappings gone, TLB invalidated, own objects freed */
	uint64_t first_unreleased_va;          /* the first PTE still pointing at a page, 0 = none */
	unsigned release_calls;
	uint64_t image_hash;
	uint64_t rt_va;                        /* where the target is in the PPGTT */
	unsigned variant;                      /* the texture variant drawn (the expected image follows it) */
	int rt_premapped;                      /* the target's PTEs belong to a parity_fhd_rt_map, not to this draw */
};
int parity_fhd_render_run(struct parity_fhd_render *x, struct parity_gt_engines *es, struct parity_gt_ppgtt *vm,
	struct parity_gt_mem *gm, struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms,
	struct parity_gt_object *rt);
/*
 * The same draw into a target at `rt_va` (I915_TEX_FHD_RT_VA or I915_TEX_FHD_RT_B_VA) with texture `variant`.  With
 * rt_premapped the target's pages were inserted by parity_fhd_rt_map() and stay mapped after this draw's release (the
 * walk of the first / middle / last page is still checked); parity_fhd_render_run() = (I915_TEX_FHD_RT_VA, 0, 0).
 */
int parity_fhd_render_run_ex(struct parity_fhd_render *x, struct parity_gt_engines *es, struct parity_gt_ppgtt *vm,
	struct parity_gt_mem *gm, struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms,
	struct parity_gt_object *rt, uint64_t rt_va, unsigned variant, int rt_premapped);
/*
 * A render target mapped for a whole run (LCD-D: A and B, each at its own VA, drawn many times).  map: every page of the
 * object's own backing at `va`, the walk of the first / middle / last page checked.  unmap: every PTE back to scratch,
 * re-read from the tables on each call (never summed), then the GT TLB; only then released=1 and the owner may free the
 * object.  The caller must have shown the GPU done with every draw that used it.
 */
struct parity_fhd_rt_map {
	struct parity_gt_object *rt;
	uint64_t va;
	unsigned pages, mapped, scratch, unmap_calls;
	int walk_ok, tlb_rc, released;
	uint64_t first_unreleased_va;
};
struct parity_gt_tlb;
int parity_fhd_rt_map(struct parity_fhd_rt_map *b, struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm,
	struct parity_gt_object *rt, uint64_t va);
int parity_fhd_rt_unmap(struct parity_fhd_rt_map *b, struct parity_gt_ppgtt *vm, struct parity_gt_tlb *tlb,
	struct parity_gt_engines *es, struct osdep_mmio *m, struct spinlock *uncore_lock);
/* wrong pixels of the target against the expected image (no CPU write) */
uint32_t parity_fhd_render_verify(const struct parity_fhd_render *x, const uint32_t *pixels, uint32_t pitch_bytes);
/*
 * The release, in the reference's order: (the request already retired) every PTE the draw wrote -- state, batch, texture
 * and the render target -- back to scratch; the GT TLB invalidated; only then the draw's own objects are freed.  May be
 * called again after a failure: the PTEs are re-read and counted afresh each time, the ownership stays until everything
 * is done.  -EBUSY: the GPU is not shown to be done (nothing is touched).  The render target is never freed here: its
 * owner may free it once this returned 0.
 */
struct parity_gt_tlb;
int parity_fhd_render_release(struct parity_fhd_render *x, struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm,
	struct parity_gt_tlb *tlb, struct parity_gt_engines *es, struct osdep_mmio *m, struct spinlock *uncore_lock);
/* after a GPU hang that was not shown to be over: every object the request may use is kept for ever (keep = 1) */
void parity_fhd_render_keep(struct parity_fhd_render *x);
/* the VA layout table of the draw and its overlap check (GPU-free): 0 = no two ranges overlap, all aligned */
struct parity_fhd_va { const char *name; uint64_t va, len, align; };
int parity_fhd_va_layout(const struct parity_fhd_va **out, unsigned *n);

int parity_tex_test_run(struct parity_tex_test *x, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm, struct osdep_mmio *m,
	struct spinlock *uncore_lock, unsigned timeout_ms);
void parity_tex_test_release(struct parity_tex_test *x, struct parity_gt_mem *gm);

#define PARITY_R1_CTX_MAX    5
#define PARITY_R1_STEPS_MAX  16
struct parity_r1_ctx {
	struct parity_gt_context ce;
	struct parity_gt_object *tl_page;
	uint32_t seqno;
	int created;
};
struct parity_r1_step {
	char ctx;                         /* 'A'.. */
	char kind;                        /* 'D' = draw (3D), 'C' = C1 (compute) */
	int rc, completed, parked, pass;
	uint32_t lrca, seqno, hwsp_observed;
	unsigned polls;
	uint64_t batch_hash, state_hash;  /* as submitted (before the request) */
	/* draw */
	uint32_t before, middraw, after, ps_marker;
	unsigned px_match, px_stale;      /* px_stale = pixels still holding the pre-fill */
	uint32_t px_first, px_last;
	/* C1 */
	uint32_t ready, eu, done, cs;
	int idd_rb_ok, kernel_rb_ok;
};
struct parity_r1_test {
	struct parity_eu_test t;          /* shared page, C1 batch, polls, hang record */
	struct parity_gt_object *rt;
	struct parity_gt_object *dbatch;
	unsigned dbatch_dwords;
	uint32_t mocs;
	uint64_t c1_batch_hash, draw_batch_hash;
	struct parity_gt_request rq;
	struct parity_r1_ctx ctx[PARITY_R1_CTX_MAX];
	struct parity_r1_step step[PARITY_R1_STEPS_MAX];
	unsigned n_steps, n_planned, passed;
};

int parity_r1_test_run(struct parity_r1_test *r, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm, const struct parity_sseu *sseu,
	struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms);
void parity_r1_test_release(struct parity_r1_test *r, struct parity_gt_mem *gm);

#define PARITY_T3_STEPS_MAX 12
struct parity_t3_step {
	char ctx;                         /* 'A' / 'B' */
	char bind;                        /* which texture binding table entry 1 names */
	char upload;                      /* texture the CPU rewrote before this step, or '-' */
	int upload_variant;
	int expect_variant;               /* the image the bound texture holds */
	int linear;                       /* SAMPLER_STATE of this step: 0 nearest, 1 bilinear */
	unsigned differs_from_nearest;    /* expected pixels that differ from the nearest expectation */
	unsigned max_channel_diff;        /* diagnostic only: largest |observed - expected| per channel */
	int rc, completed, parked, pass;
	uint32_t lrca, seqno, hwsp_observed;
	unsigned polls;
	uint64_t state_hash, rt_hash, tex_a_hash, tex_b_hash;
	uint32_t before, middraw, after, ps_marker;
	unsigned px_match, px_stale;
	int first_bad_x, first_bad_y;
	uint32_t first_bad_expected, first_bad_observed;
	unsigned tex_changed_bytes, guard_bad_bytes;   /* over both textures */
};
struct parity_t3_test {
	struct parity_eu_test t;
	struct parity_gt_object *rt, *tex_a, *tex_b;
	uint32_t mocs;
	struct parity_gt_request rq;
	struct parity_r1_ctx ctx[2];
	int content[2];                   /* the variant each texture currently holds */
	struct parity_t3_step step[PARITY_T3_STEPS_MAX];
	unsigned n_steps, n_planned, passed;
};

int parity_t3_test_run(struct parity_t3_test *x, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm, struct osdep_mmio *m,
	struct spinlock *uncore_lock, unsigned timeout_ms);
/* Same runner, bilinear plan: nearest -> bilinear -> nearest on one context, bilinear on a new one. */
int parity_bl_test_run(struct parity_t3_test *x, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm, struct osdep_mmio *m,
	struct spinlock *uncore_lock, unsigned timeout_ms);
void parity_t3_test_release(struct parity_t3_test *x, struct parity_gt_mem *gm);

struct parity_wa_list;
struct parity_sseu;
/* Steered reads of the three engine WA registers the SRM verify skips (E-94). */
int parity_mcr_probe_wa(struct parity_mcr_probe *pr, struct osdep_mmio *m,
	const struct parity_wa_list *wal, const struct parity_sseu *sseu);

/* Builds the batch (exposed for the GPU-free word check); returns dwords. */
unsigned parity_eu_test_build_batch(uint32_t *cmds, unsigned capacity,
	uint64_t shared_va, uint64_t inst_base, uint32_t max_threads);

int parity_eu_test_run(struct parity_eu_test *t, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm,
	const struct parity_sseu *sseu, struct osdep_mmio *m,
	struct spinlock *uncore_lock, unsigned timeout_ms);

/*
 * Only after parity_eu_test_run() returned PASS: `same_ctx` more requests on
 * the first context, then `new_ctx` requests on a newly created context.
 * Returns 0 when every round passed.
 */
int parity_eu_test_repeat(struct parity_eu_test *t, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm, struct osdep_mmio *m,
	struct spinlock *uncore_lock, unsigned timeout_ms, unsigned same_ctx, unsigned new_ctx);

void parity_eu_test_release(struct parity_eu_test *t, struct parity_gt_mem *gm);

#endif /* PARITY_EU_TEST_H */
