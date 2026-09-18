/*
 * WS031 Linux-parity — the one-shot real-hardware EU test.
 * See eu_test.h.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include <string.h>
#include "gt_mmio.h"
#include "gt_init.h"
#include "gt_resume.h"
#include "gt_submit.h"
#include "gt_defaults.h"
#include "reset.h"
#include "wait.h"
#include "eu_test.h"
#include "osdep/mmio.h"

/* C1: SIMD8, unconditional send.hdc1 a64_untyped_write of 0xc0ffee02, then send.ts EOT. */
static const uint32_t eu_marker_cs[PARITY_EU_KERNEL_DWORDS] = {
	0x00030061u, 0x05054220u, 0x00000000u, 0xc0ffee02u,
	0x80030061u, 0x7f050220u, 0x00460005u, 0x00000000u,
	0x80030061u, 0x01264aa0u, 0x00000000u, 0x00000001u,
	0x80030161u, 0x01064aa0u, 0x00000000u, 0x00400c20u,
	0x80000101u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00030061u, 0x03260660u, 0x00000124u, 0x00000000u,
	0x00030161u, 0x03060660u, 0x00000104u, 0x00000000u,
	0x00039031u, 0x00000000u, 0xcdfa0314u, 0x019a050cu,
	0x80030131u, 0x00000004u, 0x70007f0cu, 0x00000000u,
};

/* Commands (linux/i915-commands.inc, vk/linux/3dstate-gen12.inc). */
#define MI_STORE_DWORD_IMM_GEN4   ((0x20u << 23) | 2u)
#define MI_USE_GGTT               (1u << 22)
#define MI_BATCH_BUFFER_START_GEN8 ((0x31u << 23) | 1u)
#define MI_COPY_MEM_MEM_PPGTT     0x17000003u
/* (3<<29)|(1<<27)|(1<<24)|(4<<16) = 0x69040000: Linux PIPELINE_SELECT (E-98 fix, was 0x6104). */
#define PIPELINE_SELECT_DWORD(p)  ((0x6904u << 16) | (0x13u << 8) | (1u << 4) | (p))
#define STATE_BASE_ADDRESS_HDR    ((0x6101u << 16) | (22u - 2u))
#define GEN12_MOCS(index)         ((index) << 1)
#define MOCS_UNCACHED_INDEX       3u
#define MOCS_WRITEBACK_INDEX      2u
#define PC_STALL_AT_SCOREBOARD    (1u << 1)

struct eu_batch { uint32_t *cmds; unsigned count, capacity; int overflow; };

static void
emit(struct eu_batch *b, uint32_t dw)
{
	if (b->count < b->capacity)
		b->cmds[b->count] = dw;
	else
		b->overflow = 1;
	b->count++;
}

/* i915_draw_emit_pipe_control(): the RT flush also asks for the HDC pipeline flush. */
static void
emit_pc(struct eu_batch *b, uint32_t flags)
{
	unsigned i;

	emit(b, PARITY_GFX_OP_PIPE_CONTROL(6) |
		((flags & PARITY_PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH) != 0u ?
		PARITY_PIPE_CONTROL0_HDC_PIPELINE_FLUSH : 0u));
	emit(b, flags);
	for (i = 0u; i < 4u; i++)
		emit(b, 0u);
}

static void
emit_marker(struct eu_batch *b, uint64_t va, uint32_t value)
{
	emit(b, MI_STORE_DWORD_IMM_GEN4);
	emit(b, (uint32_t)va);
	emit(b, (uint32_t)(va >> 32));
	emit(b, value);
}

static void
emit_copy(struct eu_batch *b, uint64_t dst, uint64_t src)
{
	emit(b, MI_COPY_MEM_MEM_PPGTT);
	emit(b, (uint32_t)dst);
	emit(b, (uint32_t)(dst >> 32));
	emit(b, (uint32_t)src);
	emit(b, (uint32_t)(src >> 32));
}

static void
emit_sba(struct eu_batch *b, uint64_t shared, uint64_t inst_base, uint32_t mocs)
{
	emit(b, STATE_BASE_ADDRESS_HDR);
	emit(b, 1u | (mocs << 4));                                  /* general: base 0, modify */
	emit(b, 0u);
	emit(b, mocs << 16);                                        /* stateless data-port MOCS */
	emit(b, 1u | (mocs << 4) | ((uint32_t)shared & 0xfffff000u)); /* surface state base */
	emit(b, (uint32_t)(shared >> 32));
	emit(b, 1u | (mocs << 4) | ((uint32_t)shared & 0xfffff000u)); /* dynamic state base */
	emit(b, (uint32_t)(shared >> 32));
	emit(b, 1u | (mocs << 4));                                  /* indirect: base 0 */
	emit(b, 0u);
	emit(b, 1u | (GEN12_MOCS(MOCS_WRITEBACK_INDEX) << 4) |
		((uint32_t)inst_base & 0xfffff000u));               /* instruction base */
	emit(b, (uint32_t)(inst_base >> 32));
	emit(b, 1u | (0xfffffu << 12));                             /* general size */
	emit(b, 1u | (0xfffffu << 12));                             /* dynamic size */
	emit(b, 1u | (0xfffffu << 12));                             /* indirect size */
	emit(b, 1u | (0xfffffu << 12));                             /* instruction size */
	emit(b, 1u | (mocs << 4) | ((uint32_t)shared & 0xfffff000u)); /* bindless surface */
	emit(b, (uint32_t)(shared >> 32));
	emit(b, (4096u / 64u - 1u) << 12);
	emit(b, 1u | (mocs << 4));                                  /* bindless sampler */
	emit(b, 0u);
	emit(b, 0u);
}

unsigned
parity_eu_test_build_batch(uint32_t *cmds, unsigned capacity,
	uint64_t shared_va, uint64_t inst_base, uint32_t max_threads)
{
	struct eu_batch b;
	uint32_t mocs = GEN12_MOCS(MOCS_UNCACHED_INDEX);
	uint64_t done_va = shared_va + PARITY_EU_DONE_OFF;
	unsigned i;

	b.cmds = cmds; b.count = 0u; b.capacity = capacity; b.overflow = 0;

	/* Establish 3D mode (SBA is applied in 3D: Wa_1607854226 covers ADL). */
	emit_pc(&b, PARITY_PIPE_CONTROL_CS_STALL |
		PARITY_PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PARITY_PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		PARITY_PIPE_CONTROL_DC_FLUSH_ENABLE | PARITY_PIPE_CONTROL_FLUSH_ENABLE);
	emit(&b, PIPELINE_SELECT_DWORD(0u));                         /* 3D */
	emit_sba(&b, shared_va, inst_base, mocs);
	emit_pc(&b, PARITY_PIPE_CONTROL_CS_STALL |
		PARITY_PIPE_CONTROL_STATE_CACHE_INVALIDATE | PARITY_PIPE_CONTROL_CONST_CACHE_INVALIDATE |
		PARITY_PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | PARITY_PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE);

	/* The GPU reads the IDD and the kernel back through this context's PPGTT. */
	for (i = 0u; i < 8u; i++)
		emit_copy(&b, shared_va + PARITY_EU_IDD_RB_OFF + i * 4u,
			shared_va + PARITY_EU_IDD_OFFSET + i * 4u);
	for (i = 0u; i < PARITY_EU_KERNEL_DWORDS; i++)
		emit_copy(&b, shared_va + PARITY_EU_KERNEL_RB_OFF + i * 4u,
			inst_base + PARITY_EU_KSP_OFFSET + i * 4u);

	/* 3D -> GPGPU: stalling flush (RT flush pulls in the HDC pipeline flush). */
	emit_pc(&b, PARITY_PIPE_CONTROL_CS_STALL |
		PARITY_PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PARITY_PIPE_CONTROL_DEPTH_CACHE_FLUSH);
	emit(&b, PIPELINE_SELECT_DWORD(2u));                         /* GPGPU */
	emit_pc(&b, PARITY_PIPE_CONTROL_CS_STALL | PC_STALL_AT_SCOREBOARD);

	/* MEDIA_VFE_STATE (9 dwords). */
	emit(&b, 0x70000007u);
	emit(&b, 0u);
	emit(&b, 0u);
	emit(&b, (max_threads << 16) | (2u << 8));
	emit(&b, 0u);
	emit(&b, 2u << 16);
	emit(&b, 0u);
	emit(&b, 0u);
	emit(&b, 0u);

	/* MEDIA_STATE_FLUSH before MIDL clears temporary interface-descriptor storage. */
	emit(&b, 0x70040000u);
	emit(&b, 0u);

	/* MEDIA_INTERFACE_DESCRIPTOR_LOAD (4 dwords): 32-byte IDD at the dynamic offset. */
	emit(&b, 0x70020002u);
	emit(&b, 0u);
	emit(&b, 32u);
	emit(&b, PARITY_EU_IDD_OFFSET);

	emit_marker(&b, shared_va + PARITY_EU_READY_OFF, PARITY_EU_READY_TAG);

	/* GPGPU_WALKER: one SIMD8 thread group of one thread. */
	emit(&b, 0x7105000du);
	emit(&b, 0u);          /* interface descriptor offset */
	emit(&b, 0u);          /* indirect data length */
	emit(&b, 0u);          /* indirect data start */
	emit(&b, 0u);          /* thread counters + SIMD8 */
	emit(&b, 0u);          /* thread group id starting X */
	emit(&b, 0u);
	emit(&b, 1u);          /* X dimension */
	emit(&b, 0u);          /* starting Y */
	emit(&b, 0u);
	emit(&b, 1u);          /* Y dimension */
	emit(&b, 0u);          /* starting Z */
	emit(&b, 1u);          /* Z dimension */
	emit(&b, 0x1u);        /* right execution mask */
	emit(&b, 0xffffffffu); /* bottom execution mask */

	emit(&b, 0x70040000u); /* MEDIA_STATE_FLUSH after the walker */
	emit(&b, 0u);

	/* Wa_1607156449: a stalling flush without post-sync precedes the post-sync PC. */
	emit_pc(&b, PARITY_PIPE_CONTROL_CS_STALL |
		PARITY_PIPE_CONTROL_DC_FLUSH_ENABLE | PARITY_PIPE_CONTROL_FLUSH_ENABLE);

	/* Post-sync write of the done tag into the PPGTT (raw VA). */
	emit(&b, PARITY_GFX_OP_PIPE_CONTROL(6));
	emit(&b, PARITY_PIPE_CONTROL_CS_STALL | (1u << 14));        /* PostSync = WriteImmediate */
	emit(&b, (uint32_t)done_va);
	emit(&b, (uint32_t)(done_va >> 32));
	emit(&b, PARITY_EU_DONE_TAG);
	emit(&b, 0u);

	emit_marker(&b, shared_va + PARITY_EU_CS_OFF, PARITY_EU_CS_TAG);

	emit(&b, PARITY_MI_BATCH_BUFFER_END);
	emit(&b, PARITY_MI_NOOP);
	return b.overflow ? 0u : b.count;
}

int
parity_eu_batch_check_pipeline_select(const uint32_t *cmds, unsigned n,
	struct parity_eu_pipesel_check *out)
{
	struct parity_eu_pipesel_check c;
	unsigned k;

	memset(&c, 0, sizeof(c));
	for (k = 0u; k < n; k++) {
		uint32_t dw = cmds[k];

		if (dw == 0x69041310u) {            /* fixed reference: 3D */
			if (c.n_3d++ == 0u) c.idx_3d = k;
		} else if (dw == 0x69041312u) {     /* fixed reference: GPGPU */
			if (c.n_gpgpu++ == 0u) c.idx_gpgpu = k;
		} else if ((dw >> 16) == 0x6104u || (dw >> 16) == 0x6904u) {
			if (c.n_bad++ == 0u) { c.idx_bad = k; c.bad_word = dw; }
		}
	}
	if (out != 0)
		*out = c;
	return (c.n_3d == 1u && c.n_gpgpu == 1u && c.n_bad == 0u &&
		c.idx_3d < c.idx_gpgpu) ? 0 : -EINVAL;
}

static uint64_t
fnv1a64(const void *data, size_t bytes, uint64_t h)
{
	const uint8_t *p = data;
	size_t i;

	for (i = 0u; i < bytes; i++) {
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

static int
fail(struct parity_eu_test *t, int rc, const char *where)
{
	if (t->err == 0) {
		t->err = rc;
		t->err_where = where;
	}
	t->outcome = PARITY_EU_ERROR;
	return rc;
}

/* The request has landed AND the engine has reported the context complete. */
static int
retired(struct parity_gt_request *rq, struct parity_execlists *el)
{
	return parity_request_completed(rq) && !el->have_active && el->pending[0] == 0;
}

static int
wait_retired(struct parity_eu_test *t, struct parity_gt_engine *ge,
	struct parity_execlists *el, struct parity_gt_request *rq,
	struct osdep_mmio *m, unsigned timeout_ms)
{
	unsigned budget = timeout_ms * 20u;   /* 50 us per step */
	unsigned k;

	for (k = 0u; k < budget; k++) {
		t->polls++;
		(void)parity_execlists_process_csb(ge, el, m);
		if (el->csb_errors != 0u)
			return -EIO;
		if (retired(rq, el))
			return 0;
		if (parity_udelay(50u) != 0)
			return fail(t, -EIO, "time base");
	}
	return -ETIMEDOUT;
}

int
parity_eu_test_run(struct parity_eu_test *t, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm,
	const struct parity_sseu *sseu, struct osdep_mmio *m,
	struct spinlock *uncore_lock, unsigned timeout_ms)
{
	struct parity_gt_engine *ge;
	struct parity_execlists *el;
	volatile uint32_t *page;
	uint64_t dma;
	unsigned i, bit;
	uint32_t *cs;
	int rc;

	if (t == 0 || es == 0 || vm == 0 || gm == 0 || sseu == 0 || m == 0)
		return -EINVAL;
	memset(t, 0, sizeof(*t));

	/* The render engine. */
	for (i = 0u; i < es->n; i++)
		if (es->ge[i].info->class == PARITY_RENDER_CLASS)
			break;
	if (i == es->n)
		return fail(t, -ENODEV, "no render engine");
	t->engine_idx = i;
	ge = &es->ge[i];
	el = &es->el[i];

	/* VFE MaxThreads = max_cs_threads(112) * enabled DSS - 1 (blorp, runtime popcount). */
	for (bit = 0u; bit < 16u; bit++)
		if ((sseu->subslice_mask >> bit) & 1u)
			t->dss_count++;
	if (t->dss_count == 0u)
		return fail(t, -EINVAL, "no subslice");
	t->max_threads = 112u * t->dss_count - 1u;

	/* Two user objects, softpinned into the context's vm at the L-C1 addresses. */
	t->shared = parity_gt_object_create(gm, 4096u);
	t->batch = parity_gt_object_create(gm, 4096u);
	if (t->shared == 0 || t->batch == 0)
		return fail(t, -ENOMEM, "gem_create");
	rc = parity_gt_ppgtt_alloc_range(gm, vm, PARITY_EU_SHARED_VA, 2u * 4096u);
	if (rc != 0)
		return fail(t, rc, "allocate_va_range");
	rc = parity_gt_object_page_dma(t->shared, 0u, &dma);
	if (rc == 0)
		rc = parity_gt_ppgtt_insert_page(vm, dma, PARITY_EU_SHARED_VA, 0u /* I915_CACHE_LLC -> PAT 0 */);
	if (rc == 0)
		rc = parity_gt_object_page_dma(t->batch, 0u, &dma);
	if (rc == 0)
		rc = parity_gt_ppgtt_insert_page(vm, dma, PARITY_EU_BATCH_VA, 0u);
	if (rc != 0)
		return fail(t, rc, "ppgtt_insert");

	/* The shared page: kernel at 1024, IDD at 896, markers pre-set to 0xdead0000. */
	page = (volatile uint32_t *)t->shared->cpu;
	memcpy((char *)t->shared->cpu + PARITY_EU_KSP_OFFSET, eu_marker_cs, sizeof(eu_marker_cs));
	{
		volatile uint32_t *idd = page + PARITY_EU_IDD_OFFSET / 4u;

		idd[0] = PARITY_EU_KSP_OFFSET;   /* Kernel Start Pointer */
		idd[1] = 0u; idd[2] = 1u << 20; idd[3] = 0u;
		idd[4] = 0u; idd[5] = 0u; idd[6] = 1u; idd[7] = 0u;   /* one thread per group */
	}
	page[PARITY_EU_READY_OFF / 4u] = 0xdead0000u;
	page[PARITY_EU_EU_OFF / 4u] = 0xdead0000u;
	page[PARITY_EU_DONE_OFF / 4u] = 0xdead0000u;
	page[PARITY_EU_CS_OFF / 4u] = 0xdead0000u;

	t->batch_dwords = parity_eu_test_build_batch((uint32_t *)t->batch->cpu, 1024u,
		PARITY_EU_SHARED_VA, PARITY_EU_SHARED_VA, t->max_threads);
	if (t->batch_dwords == 0u)
		return fail(t, -ENOSPC, "build_batch");
	/* Read the words back from the object that is submitted; never submit a bad select. */
	t->pipesel_rc = parity_eu_batch_check_pipeline_select((const uint32_t *)t->batch->cpu,
		t->batch_dwords, &t->pipesel);
	if (t->pipesel_rc != 0)
		return fail(t, t->pipesel_rc, "pipeline_select_verify");
	/* The fixture as submitted: batch dwords, and IDD..kernel of the shared page. */
	t->batch_hash = fnv1a64(t->batch->cpu, (size_t)t->batch_dwords * 4u, 0xcbf29ce484222325ull);
	t->fixture_hash = fnv1a64((const char *)t->shared->cpu + PARITY_EU_IDD_OFFSET,
		(PARITY_EU_KSP_OFFSET - PARITY_EU_IDD_OFFSET) + sizeof(eu_marker_cs),
		0xcbf29ce484222325ull);

	/* intel_context_create(engine) on the kernel vm; it inherits engine->default_state. */
	rc = parity_lrc_alloc(&t->ce, ge, vm, gm, 4096u, 0u);
	if (rc != 0)
		return fail(t, rc, "intel_context_create");
	t->tl_page = parity_gt_object_create(gm, 4096u);
	if (t->tl_page == 0)
		return fail(t, -ENOMEM, "intel_timeline_create");
	rc = parity_gt_ggtt_bind(gm, t->tl_page);
	if (rc != 0)
		return fail(t, rc, "intel_timeline_pin");
	parity_lrc_init_state(&t->ce);
	(void)parity_lrc_update_regs(&t->ce, t->ce.ring.tail);

	/* i915_request_create(): has_initial_breadcrumb, seqno += 2. */
	t->tl_seqno = 2u;
	rc = parity_request_create(&t->rq, &t->ce, t->tl_seqno,
		(uint32_t)t->tl_page->ggtt_offset, (volatile uint32_t *)t->tl_page->cpu);
	if (rc != 0)
		return fail(t, rc, "i915_request_create");

	/* gen8_emit_init_breadcrumb() */
	cs = parity_ring_begin(&t->rq, 6u);
	if (cs == 0)
		return fail(t, t->rq.error, "emit_init_breadcrumb");
	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = t->rq.hwsp_ggtt;
	*cs++ = 0u;
	*cs++ = t->rq.seqno - 1u;
	*cs++ = PARITY_MI_NOOP;
	*cs++ = PARITY_MI_ARB_CHECK;
	parity_ring_advance(&t->rq, cs);

	/* gen8_emit_bb_start(): arbitration on around a PPGTT batch start. */
	cs = parity_ring_begin(&t->rq, 6u);
	if (cs == 0)
		return fail(t, t->rq.error, "emit_bb_start");
	*cs++ = PARITY_MI_ARB_ON_OFF | PARITY_MI_ARB_ENABLE;
	*cs++ = MI_BATCH_BUFFER_START_GEN8 | (1u << 8);
	*cs++ = (uint32_t)PARITY_EU_BATCH_VA;
	*cs++ = (uint32_t)(PARITY_EU_BATCH_VA >> 32);
	*cs++ = PARITY_MI_ARB_ON_OFF;   /* MI_ARB_DISABLE */
	*cs++ = PARITY_MI_NOOP;
	parity_ring_advance(&t->rq, cs);

	rc = parity_request_add(&t->rq);
	if (rc != 0)
		return fail(t, rc, "i915_request_add");

	/*
	 * Immediately before submission: what the GPU will walk for the batch,
	 * the IDD, the kernel, the EU store target and the done marker, read
	 * from the tables PDP0 of this context names.
	 */
	{
		static const uint32_t off[5] = { 0u, PARITY_EU_IDD_OFFSET, PARITY_EU_KSP_OFFSET,
			PARITY_EU_EU_OFF, PARITY_EU_DONE_OFF };
		uint64_t pdp0 = ((uint64_t)t->ce.lrc_reg_state[PARITY_CTX_PDP0_UDW] << 32) |
			t->ce.lrc_reg_state[PARITY_CTX_PDP0_LDW];

		t->pdp0_matches_top = pdp0 == vm->top_pd_dma;
		for (i = 0u; i < 5u; i++) {
			uint64_t va = (i == 0u) ? PARITY_EU_BATCH_VA : PARITY_EU_SHARED_VA + off[i];

			(void)parity_gt_ppgtt_walk(vm, va, &t->walk[i]);
		}
		t->walks = 5u;
	}

	rc = parity_execlists_submit(ge, el, m, &t->rq);
	if (rc != 0)
		return fail(t, rc, "execlists_submit");
	t->submitted = 1;

	/* i915_request_wait() */
	rc = wait_retired(t, ge, el, &t->rq, m, timeout_ms);
	if (rc == 0) {
		t->completed = 1;
	} else if (rc == -ETIMEDOUT) {
		t->timed_out = 1;
	} else {
		(void)fail(t, rc, "i915_request_wait");
	}

	/* Read the page back regardless (a hang may still have left the CS markers). */
	t->ready = page[PARITY_EU_READY_OFF / 4u];
	t->eu = page[PARITY_EU_EU_OFF / 4u];
	t->done = page[PARITY_EU_DONE_OFF / 4u];
	t->cs = page[PARITY_EU_CS_OFF / 4u];
	t->idd_rb_ok = 1;
	t->kernel_rb_ok = 1;
	for (i = 0u; i < 8u; i++) {
		t->idd_rb[i] = page[PARITY_EU_IDD_RB_OFF / 4u + i];
		if (t->idd_rb[i] != page[PARITY_EU_IDD_OFFSET / 4u + i])
			t->idd_rb_ok = 0;
	}
	for (i = 0u; i < PARITY_EU_KERNEL_DWORDS; i++) {
		t->kernel_rb[i] = page[PARITY_EU_KERNEL_RB_OFF / 4u + i];
		if (t->kernel_rb[i] != eu_marker_cs[i])
			t->kernel_rb_ok = 0;
	}

	if (t->completed) {
		/* intel_context_unpin / engine park: switch to the kernel context. */
		if (el->wakeref_serial != el->serial) {
			uint32_t kseq = ++es->kernel_tl_seqno[t->engine_idx];

			rc = parity_request_create(&t->krq, &es->kernel_ce[t->engine_idx], kseq,
				(uint32_t)ge->hwsp_ggtt + PARITY_I915_GEM_HWS_SEQNO_ADDR,
				&ge->hwsp[PARITY_I915_GEM_HWS_SEQNO_ADDR / 4u]);
			if (rc == 0)
				rc = parity_request_add(&t->krq);
			el->wakeref_serial = el->serial + 1u;
			if (rc == 0)
				rc = parity_execlists_submit(ge, el, m, &t->krq);
			if (rc == 0)
				rc = wait_retired(t, ge, el, &t->krq, m, timeout_ms);
			if (rc == 0)
				t->parked = 1;
			else
				(void)fail(t, rc, "switch_to_kernel_context");
		} else {
			t->parked = 1;
		}
		t->outcome = (t->eu == PARITY_EU_STORE_TAG && t->cs == PARITY_EU_CS_TAG &&
			t->done == PARITY_EU_DONE_TAG) ? PARITY_EU_PASS : PARITY_EU_ERROR;
		if (t->outcome == PARITY_EU_ERROR && t->err == 0)
			(void)fail(t, -EIO, "markers");
		return t->err;
	}

	/* Hang (or an error): record, dump, then reset the engines like intel_gt_set_wedged. */
	if (t->timed_out)
		t->outcome = PARITY_EU_HANG;
	t->hwsp_seqno_observed = *t->rq.hwsp_cpu;
	t->ctx_ccid_hi = (uint32_t)(t->ce.lrc_desc >> 32);
	t->ctx_ccid_lo = (uint32_t)t->ce.lrc_desc;
	t->time_base_fault = (t->err == -EIO && t->err_where != 0 &&
		t->err_where[0] == 't') ? 1 : 0;
	kern_logf("i915: parity EU-TEST record: rq seqno expected=%u hwsp_observed=%u "
		"initial_breadcrumb_seen=%d | ctx sw_id=%u tag=%d lrca=%08x desc=%08x:%08x "
		"state_ggtt=0x%llx ring_ggtt=0x%llx | csb_head=%u last_csb=%08x:%08x | time_base_fault=%d\n",
		t->rq.seqno, t->hwsp_seqno_observed,
		(int32_t)(t->hwsp_seqno_observed - (t->rq.seqno - 1u)) >= 0,
		t->ce.sw_id, t->ce.tag, t->ce.lrca, t->ctx_ccid_hi, t->ctx_ccid_lo,
		(unsigned long long)t->ce.state->ggtt_offset,
		(unsigned long long)t->ce.ring.ggtt_offset,
		ge->csb_head, el->last_csb_hi, el->last_csb_lo, t->time_base_fault);
	parity_engine_dump(ge, el, m, "eu-test");
	kern_logf("i915: parity EU-TEST hang: ipehr=%08x acthd=%08x:%08x instdone=%08x fault(0xcec4)=%08x "
		"row_instdone(0xe164,raw)=%08x eu_dis(0x9134)=%08x slice_ack(0x804c)=%08x "
		"ss01_eu_ack(0x805c)=%08x ss23_eu_ack(0x8060)=%08x\n",
		osdep_mmio_read32(m, ge->info->mmio_base + 0x68u),
		osdep_mmio_read32(m, ge->info->mmio_base + 0x5cu),
		osdep_mmio_read32(m, ge->info->mmio_base + 0x74u),
		osdep_mmio_read32(m, ge->info->mmio_base + 0x6cu),
		osdep_mmio_read32(m, 0xcec4u), osdep_mmio_read32(m, 0xe164u),
		osdep_mmio_read32(m, 0x9134u), osdep_mmio_read32(m, 0x804cu),
		osdep_mmio_read32(m, 0x805cu), osdep_mmio_read32(m, 0x8060u));
	for (i = 0u; i < es->n; i++)
		parity_execlists_reset_prepare(&es->ge[i], m);
	(void)parity_gt_reset_all(uncore_lock, m, 2000u);
	t->wedged = 1;
	return t->err != 0 ? t->err : -ETIMEDOUT;
}

void
parity_eu_test_release(struct parity_eu_test *t, struct parity_gt_mem *gm)
{
	if (t == 0 || gm == 0)
		return;
	if (t->tl_page != 0) {
		parity_gt_object_destroy(gm, t->tl_page);
		t->tl_page = 0;
	}
	if (t->ce.allocated)
		parity_lrc_release(&t->ce, gm);
	if (t->batch != 0) {
		parity_gt_object_destroy(gm, t->batch);
		t->batch = 0;
	}
	if (t->shared != 0) {
		parity_gt_object_destroy(gm, t->shared);
		t->shared = 0;
	}
	/* The PPGTT tables of the range stay with the vm (freed with it). */
}

/* ---------------- MCR workaround readback (intel_gt_mcr_read per DSS) ---------------- */

#define GEN8_MCR_SELECTOR        0x0fdcu
#define GEN11_MCR_SLICE_MASK     0x78000000u
#define GEN11_MCR_SUBSLICE_MASK  0x07000000u
#define GEN11_MCR_SLICE(s)       ((((uint32_t)(s)) & 0xfu) << 27)
#define GEN11_MCR_SUBSLICE(ss)   ((((uint32_t)(ss)) & 0x7u) << 24)

/* The three RCS workaround registers the CS-side verify cannot read (E-94). */
static const uint32_t mcr_probe_regs[3] = { 0xe4f4u, 0xe18cu, 0xe48cu };
/* GEN8_ROW_CHICKEN2, GEN10_SAMPLER_MODE, GEN9_ROW_CHICKEN4 */

/*
 * rw_with_mcr_steering_fw(FW_REG_READ) for GRAPHICS_VER 11..12.5x: keep the
 * multicast bit (Wa_22013088509), set slice/subslice, read, restore the old
 * selector.  Forcewake (RENDER for 0xe000.., and the selector itself) must
 * already be held by the caller; the MCR lock is the exclusive section.
 */
static uint32_t
mcr_read_steered(struct osdep_mmio *m, uint32_t reg, unsigned group, unsigned instance,
	uint32_t *sel_before, uint32_t *sel_after)
{
	uint32_t mcr_mask = GEN11_MCR_SLICE_MASK | GEN11_MCR_SUBSLICE_MASK;
	uint32_t old, mcr, val;

	old = osdep_mmio_raw_read32(m, GEN8_MCR_SELECTOR);
	mcr = (old & ~mcr_mask) | GEN11_MCR_SLICE(group) | GEN11_MCR_SUBSLICE(instance);
	osdep_mmio_raw_write32(m, GEN8_MCR_SELECTOR, mcr);
	val = osdep_mmio_raw_read32(m, reg);
	osdep_mmio_raw_write32(m, GEN8_MCR_SELECTOR, old);
	*sel_before = old;
	*sel_after = osdep_mmio_raw_read32(m, GEN8_MCR_SELECTOR);
	return val;
}

int
parity_mcr_probe_wa(struct parity_mcr_probe *pr, struct osdep_mmio *m,
	const struct parity_wa_list *wal, const struct parity_sseu *sseu)
{
	unsigned r, ss, k;
	int rc;

	if (pr == 0 || m == 0 || wal == 0 || sseu == 0)
		return -EINVAL;
	memset(pr, 0, sizeof(*pr));
	rc = osdep_mcr_lock(m, 0u);
	pr->lock_rc = rc;
	if (rc != 0)
		return rc;
	for (r = 0u; r < 3u; r++) {
		uint32_t reg = mcr_probe_regs[r];
		uint32_t set = 0u, mask = 0u;
		int listed = 0;

		/* The workaround list's expectation for this register (merged entries). */
		for (k = 0u; k < wal->count; k++) {
			if (wal->list[k].reg != reg)
				continue;
			listed = 1;
			set |= wal->list[k].set;
			mask |= wal->list[k].read_mask;
		}
		/* group = the first enabled slice; every enabled subslice (DSS) of it. */
		for (ss = 0u; ss < 8u; ss++) {
			struct parity_mcr_probe_entry *e;

			if (((sseu->subslice_mask >> ss) & 1u) == 0u)
				continue;
			if (pr->n == PARITY_MCR_PROBE_MAX)
				break;
			e = &pr->e[pr->n++];
			e->reg = reg;
			e->group = 0u;
			e->instance = ss;
			e->listed = listed;
			e->expected_set = set;
			e->read_mask = mask;
			e->raw = mcr_read_steered(m, reg, 0u, ss, &e->selector_before,
				&e->selector_after);
			/* wa_verify(): (cur ^ set) & read; masked registers compare the low bits. */
			e->masked_mismatch = listed ? ((e->raw ^ set) & mask) : 0u;
			if (e->masked_mismatch != 0u)
				pr->mismatches++;
		}
	}
	osdep_mcr_unlock(m);
	return 0;
}
