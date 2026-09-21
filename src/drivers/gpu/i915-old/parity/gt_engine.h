/*
 * WS031 Linux-parity — P6-c1: per-engine setup for execlists submission.
 *
 * What this file stands for in the reference (intel_engine_cs.c,
 * intel_execlists_submission.c):
 *
 *   engine_setup_common()             init_status_page() and the software
 *                                     execlists state
 *   intel_execlists_submission_setup()the ELSQ registers, the CSB pointers
 *                                     into the HWSP and the ccid fields
 *   enable_execlists()                HWSTAM, RING_MODE_GEN7, RING_MI_MODE,
 *                                     RING_HWS_PGA, then the error interrupt
 *   reset_csb_pointers()              RING_CONTEXT_STATUS_PTR and the software
 *                                     head, plus scrubbing the CSB entries
 *
 * Reference facts this file is built on (6.8.12, gen12/ADL-P):
 *   - The status page is ONE 4 KiB internal object, pinned in the GGTT and
 *     zeroed; RING_HWS_PGA takes its GGTT offset.
 *   - On GRAPHICS_VER >= 11 the CSB write pointer is at dword 0x2f
 *     (ICL_HWS_CSB_WRITE_INDEX), not 0x1f, and the ring holds 12 entries
 *     (GEN11_CSB_ENTRIES), not 6.
 *   - RING_MODE_GEN7 gets _MASKED_BIT_ENABLE(GEN11_GFX_DISABLE_LEGACY_MODE)
 *     on gen11+, NOT GFX_RUN_LIST_ENABLE (that is the pre-gen11 bit).
 *   - enable_error_interrupt() unmasks ONLY I915_ERROR_INSTRUCTION; CP_PRIV is
 *     left masked on purpose, because the hardware already suppresses it.
 *   - intel_mocs_init_engine() is NOT a no-op on ADL-P: global MOCS means the
 *     per-engine MOCS table is skipped, but HAS_RENDER_L3CC still re-programs
 *     the L3CC table for the RENDER engine on every resume.
 */
#ifndef PARITY_GT_ENGINE_H
#define PARITY_GT_ENGINE_H

#include <stdint.h>

struct osdep_mmio;
struct parity_engine;
struct parity_gt_mem;
struct parity_gt_object;

/* Per-engine register block (intel_engine_regs.h), relative to mmio_base. */
#define PARITY_RING_HWS_PGA(base)               ((base) + 0x80u)
#define PARITY_RING_HWSTAM(base)                ((base) + 0x98u)
#define PARITY_RING_MI_MODE(base)               ((base) + 0x9cu)
#define PARITY_RING_EIR(base)                   ((base) + 0xb0u)
#define PARITY_RING_EMR(base)                   ((base) + 0xb4u)
#define PARITY_RING_ESR(base)                   ((base) + 0xb8u)
#define PARITY_RING_MODE_GEN7(base)             ((base) + 0x29cu)
#define PARITY_RING_CONTEXT_STATUS_PTR(base)    ((base) + 0x3a0u)
#define PARITY_RING_EXECLIST_SQ_CONTENTS(base)  ((base) + 0x510u)
#define PARITY_RING_EXECLIST_CONTROL(base)      ((base) + 0x550u)

#define PARITY_GEN11_GFX_DISABLE_LEGACY_MODE    (1u << 3)
#define PARITY_STOP_RING                        (1u << 8)
#define PARITY_I915_ERROR_INSTRUCTION           (1u << 0)
#define PARITY_EL_CTRL_LOAD                     (1u << 0)
#define PARITY_MODE_IDLE                        (1u << 9)    /* RING_MI_MODE */
#define PARITY_GEN12_GFX_PREFETCH_DISABLE       (1u << 10)   /* RING_MODE_GEN7 */
#define PARITY_RING_TAIL_REG(base)              ((base) + 0x30u)
#define PARITY_RING_HEAD_REG(base)              ((base) + 0x34u)
#define PARITY_HEAD_ADDR                        0x001ffffcu
#define PARITY_TAIL_ADDR                        0x001ffff8u

/* Wa_22011802037: the GPM MSG_IDLE registers and the domain status. */
#define PARITY_MSG_IDLE_FW_MASK                 (0x1fu << 9)
#define PARITY_MSG_IDLE_FW_SHIFT                9
#define PARITY_GEN9_PWRGT_DOMAIN_STATUS         0xa2a0u

/* HWSP dword indices (intel_engine.h). */
#define PARITY_HWS_CSB_BUF0_INDEX               0x10u
#define PARITY_ICL_HWS_CSB_WRITE_INDEX          0x2fu
#define PARITY_GEN11_CSB_ENTRIES                12u
#define PARITY_GEN11_CSB_PTR_MASK               0xfu
#define PARITY_I915_GEM_HWS_PREEMPT             0x32u
#define PARITY_I915_GEM_HWS_SEQNO               0x40u
#define PARITY_I915_GEM_HWS_SCRATCH             0x80u

/* Context descriptor fields (intel_lrc.h), gen11 .. < 12.50. */
#define PARITY_GEN11_SW_CTX_ID_SHIFT            37
#define PARITY_GEN11_SW_CTX_ID_WIDTH            11
#define PARITY_GEN11_ENGINE_CLASS_SHIFT         61
#define PARITY_GEN11_ENGINE_INSTANCE_SHIFT      48

struct parity_gt_engine {
	struct parity_engine *info;      /* the P6-0 engine object */

	/* engine->default_state: the image __engines_record_defaults() saved;
	 * every context created afterwards starts from it (lrc_init_state). */
	struct parity_gt_object *default_state;

	/* init_status_page() */
	struct parity_gt_object *status_page;
	volatile uint32_t *hwsp;         /* CPU view of the status page */
	uint64_t hwsp_ggtt;              /* what RING_HWS_PGA is given */

	/* intel_engine_execlists */
	unsigned port_mask;
	uint32_t submit_reg;             /* RING_EXECLIST_SQ_CONTENTS */
	uint32_t ctrl_reg;               /* RING_EXECLIST_CONTROL */
	volatile uint64_t *csb_status;   /* &hwsp[I915_HWS_CSB_BUF0_INDEX] */
	volatile uint32_t *csb_write;    /* &hwsp[ICL_HWS_CSB_WRITE_INDEX] */
	unsigned csb_size;
	unsigned csb_head;
	uint32_t ccid;                   /* upper dword fields of the descriptor */

	/*
	 * engine->sseu = intel_sseu_from_device_info(&engine->gt->info.sseu):
	 * "use the whole device by default".  Only the two fields gen12's
	 * intel_sseu_make_rpcs() reads are kept.
	 */
	uint8_t sseu_slice_mask;
	int sseu_has_slice_pg;

	int setup_done;
	int resumed;

	/* Diagnostics. */
	int stop_cs_rc;                  /* intel_engine_stop_cs() result */
	uint32_t mi_fw_pending;          /* MSG_IDLE pending forcewakes seen */
	uint32_t esr_at_resume;          /* RING_ESR read during enable */
	unsigned enable_writes;
	unsigned csb_reset_writes;
};

struct parity_sseu;
int parity_engine_setup_common(struct parity_gt_engine *ge,
	struct parity_engine *info, struct parity_gt_mem *gm,
	const struct parity_sseu *sseu);
void parity_execlists_submission_setup(struct parity_gt_engine *ge);
void parity_execlists_enable(struct parity_gt_engine *ge, struct osdep_mmio *m);
void parity_execlists_reset_csb_pointers(struct parity_gt_engine *ge,
	struct osdep_mmio *m);
void parity_engine_release(struct parity_gt_engine *ge, struct parity_gt_mem *gm);

/* ring_set_paused(): the status page's PREEMPT dword. */
void parity_ring_set_paused(struct parity_gt_engine *ge, int state);

/*
 * intel_engine_stop_cs(): STOP_RING, then (Wa_22011802037) prefetch disable,
 * then wait for MODE_IDLE.  0, or -ETIMEDOUT when the ring is also not empty.
 */
int parity_engine_stop_cs(struct parity_gt_engine *ge, struct osdep_mmio *m);

/* intel_engine_wait_for_pending_mi_fw() (Wa_22011802037). */
void parity_engine_wait_for_pending_mi_fw(struct parity_gt_engine *ge,
	struct osdep_mmio *m);

/* execlists_reset_prepare(): pause, stop the CS, drain MI_FORCE_WAKEs. */
void parity_execlists_reset_prepare(struct parity_gt_engine *ge,
	struct osdep_mmio *m);

#endif /* PARITY_GT_ENGINE_H */
