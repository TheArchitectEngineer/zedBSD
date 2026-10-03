/*
 * WS031 Linux-parity — P6-c3a: what a request writes into its ring.
 *
 * This is the command stream of __engines_record_defaults() and of the
 * kernel-context switch that follows it at engine park, re-derived from the
 * 6.8.12 reference (gt/gen8_engine_cs.c, intel_workarounds.c,
 * intel_execlists_submission.c).  Nothing here submits anything: it only
 * builds dwords in a context's ring, so every word is checkable GPU-free.
 *
 * The request, in the order the reference emits it:
 *
 *   execlists_request_alloc()     emit_flush(EMIT_INVALIDATE)
 *                                 (4-level vm, so no emit_pdps)
 *   intel_engine_emit_ctx_wa()    emit_flush(BARRIER), MI_LRI(n) + pairs +
 *                                 MI_NOOP, emit_flush(BARRIER); nothing at
 *                                 all when the engine's ctx list is empty
 *   intel_renderstate_emit()      nothing: gen12 has no null renderstate
 *   i915_request_add()            emit_fini_breadcrumb
 *
 * No INITIAL breadcrumb: gen8_emit_init_breadcrumb() is called only by the
 * selftests, execbuf and the GSC path, never on this one.
 *
 * ADL-P specifics that change the stream and are asserted by the tests:
 *   - gen12_needs_ccs_aux_inv() is TRUE on every ADL-P engine, so
 *     gen12_emit_flush_rcs() runs its FLUSH block even for EMIT_INVALIDATE,
 *     and both flushes carry the AUX table invalidate with its semaphore poll.
 *   - PIPE_CONTROL_FLUSH_L3 is set only when EMIT_FLUSH was asked for
 *     (and on the fini breadcrumb), never on a pure invalidate.
 *   - Wa_1409600907 (tgl, adl-p) adds PIPE_CONTROL_DEPTH_STALL to both the
 *     render flush and the render breadcrumb.
 *   - The engines are not vGPU, so I915_ENGINE_HAS_SEMAPHORES is set and the
 *     breadcrumb tail carries gen12_emit_preempt_busywait(): an
 *     MI_SEMAPHORE_WAIT on the status page's PREEMPT dword (0x32) == 0.
 *   - A video engine's invalidate adds MI_INVALIDATE_BSD; a copy engine's adds
 *     MI_FLUSH_DW_CCS (because it needs the AUX invalidate).
 *
 * ADAPTATION (recorded): intel_ring_begin() wraps by NOOP-filling to the end
 * of the ring.  A request here is ~0.2 KiB on a fresh 4 KiB ring, so it can
 * never wrap; parity_ring_begin() REFUSES a request that would, rather than
 * carry a wrap path that nothing exercises.
 */
#ifndef PARITY_GT_REQUEST_H
#define PARITY_GT_REQUEST_H

#include <stdint.h>

struct osdep_mmio;
struct parity_gt_context;
struct parity_wa_list;

/* intel_engine_types.h */
#define PARITY_EMIT_INVALIDATE   (1u << 0)
#define PARITY_EMIT_FLUSH        (1u << 1)
#define PARITY_EMIT_BARRIER      (PARITY_EMIT_INVALIDATE | PARITY_EMIT_FLUSH)

/* intel_lrc.h: the ppHWSP scratch the flushes post their write to. */
#define PARITY_LRC_PPHWSP_SCRATCH_ADDR   (0x34u * 4u)

/* intel_engine.h: the PREEMPT dword the breadcrumb tail waits on. */
#define PARITY_I915_GEM_HWS_PREEMPT_ADDR (0x32u * 4u)
#define PARITY_I915_GEM_HWS_SEQNO_ADDR   (0x40u * 4u)

/* Commands (intel_gpu_commands.h). */
#define PARITY_GFX_OP_PIPE_CONTROL(len)  ((3u << 29) | (3u << 27) | (2u << 24) | ((len) - 2u))
#define PARITY_MI_FLUSH_DW               ((0x26u << 23) | 1u)
#define PARITY_MI_FLUSH_DW_STORE_INDEX   (1u << 21)
#define PARITY_MI_INVALIDATE_TLB         (1u << 18)
#define PARITY_MI_FLUSH_DW_CCS           (1u << 16)
#define PARITY_MI_FLUSH_DW_OP_STOREDW    (1u << 14)
#define PARITY_MI_INVALIDATE_BSD         (1u << 7)
#define PARITY_MI_FLUSH_DW_USE_GTT       (1u << 2)
#define PARITY_MI_USER_INTERRUPT         (0x02u << 23)
#define PARITY_MI_ARB_ON_OFF             (0x08u << 23)
#define PARITY_MI_ARB_ENABLE             (1u << 0)
#define PARITY_MI_ARB_CHECK              (0x05u << 23)
#define PARITY_MI_SEMAPHORE_GLOBAL_GTT   (1u << 22)

#define PARITY_PIPE_CONTROL0_HDC_PIPELINE_FLUSH       (1u << 9)
#define PARITY_PIPE_CONTROL_COMMAND_CACHE_INVALIDATE  (1u << 29)
#define PARITY_PIPE_CONTROL_TILE_CACHE_FLUSH          (1u << 28)
#define PARITY_PIPE_CONTROL_FLUSH_L3                  (1u << 27)
#define PARITY_PIPE_CONTROL_GLOBAL_GTT_IVB            (1u << 24)
#define PARITY_PIPE_CONTROL_STORE_DATA_INDEX          (1u << 21)
#define PARITY_PIPE_CONTROL_CS_STALL                  (1u << 20)
#define PARITY_PIPE_CONTROL_TLB_INVALIDATE            (1u << 18)
#define PARITY_PIPE_CONTROL_QW_WRITE                  (1u << 14)
#define PARITY_PIPE_CONTROL_DEPTH_STALL               (1u << 13)
#define PARITY_PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH (1u << 12)
#define PARITY_PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE (1u << 11)
#define PARITY_PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE  (1u << 10)
#define PARITY_PIPE_CONTROL_FLUSH_ENABLE              (1u << 7)
#define PARITY_PIPE_CONTROL_DC_FLUSH_ENABLE           (1u << 5)
#define PARITY_PIPE_CONTROL_VF_CACHE_INVALIDATE       (1u << 4)
#define PARITY_PIPE_CONTROL_CONST_CACHE_INVALIDATE    (1u << 3)
#define PARITY_PIPE_CONTROL_STATE_CACHE_INVALIDATE    (1u << 2)
#define PARITY_PIPE_CONTROL_DEPTH_CACHE_FLUSH         (1u << 0)

struct parity_gt_request {
	struct parity_gt_context *ce;
	uint32_t seqno;

	/* Where the breadcrumb lands: the timeline's HWSP slot. */
	uint32_t hwsp_ggtt;
	volatile uint32_t *hwsp_cpu;

	/* The PREEMPT dword of the engine's status page. */
	uint32_t preempt_ggtt;

	uint32_t head;      /* ring offset where the request starts */
	uint32_t tail;      /* rq->tail: after the breadcrumb, before wa_tail */
	uint32_t wa_tail;   /* rq->wa_tail: what RING_TAIL is programmed to */
	int error;
	int added;
};

/* intel_ring_begin / intel_ring_advance. */
uint32_t *parity_ring_begin(struct parity_gt_request *rq, unsigned num_dwords);
void parity_ring_advance(struct parity_gt_request *rq, uint32_t *cs);

/* gen12_emit_flush_rcs / gen12_emit_flush_xcs, dispatched on the class. */
int parity_emit_flush(struct parity_gt_request *rq, uint32_t mode);

/* intel_engine_emit_ctx_wa(): `m` reads the plain entries (forcewake held). */
int parity_emit_ctx_wa(struct parity_gt_request *rq,
	const struct parity_wa_list *wal, struct osdep_mmio *m);

/* i915_request_create() on an execlists engine: request_alloc's invalidate. */
int parity_request_create(struct parity_gt_request *rq,
	struct parity_gt_context *ce, uint32_t seqno,
	uint32_t hwsp_ggtt, volatile uint32_t *hwsp_cpu);

/* The fini breadcrumb and its tail; sets rq->tail and rq->wa_tail. */
int parity_request_add(struct parity_gt_request *rq);

/* The AUX table invalidate, shared with the indirect context batch. */
uint32_t *parity_gen12_emit_aux_table_inv(int engine_id, uint32_t *cs);

#endif /* PARITY_GT_REQUEST_H */
