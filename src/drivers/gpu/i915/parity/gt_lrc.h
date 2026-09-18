/*
 * WS031 Linux-parity — P6-c2a: the logical ring context image and its ring.
 *
 * What this file stands for in the reference (gt/intel_lrc.c):
 *   __lrc_alloc_state()   the context object: round_up(context_size, 4K) plus
 *                         TWO extra pages on gen12 (INDIRECT_CTX, PER_CTX_BB)
 *   intel_engine_create_ring()  the ring object (SZ_4K for a kernel context)
 *   lrc_init_state()      clear the ppHWSP, clear the wa_bb page, then build the
 *                         register state at LRC_STATE_OFFSET
 *   __lrc_init_regs()     set_offsets + init_common_regs + init_ppgtt_regs +
 *                         init_wa_bb_regs + __reset_stop_ring
 *   lrc_update_regs()     the ring registers, RPCS for render, and the descriptor
 *
 * Reference facts (6.8.12, gen12/ADL-P) this file is built on:
 *   - The image is page 0 = ppHWSP, page 1.. = register state
 *     (LRC_PPHWSP_SZ is 1, so LRC_STATE_OFFSET is one page).
 *   - wa_bb_page = round_up(context_size,4K)/4K, i.e. the first of the two
 *     extra pages; the object is TWO pages longer than context_size.
 *   - lrc_ring_mi_mode() is 0x60 on gen12 and lrc_ring_bb_offset() is 0x70.
 *   - The kernel context uses gt->vm (a 4-level ppgtt), so init_ppgtt_regs
 *     takes the ASSIGN_CTX_PML4 branch: PDP0 names the top directory and the
 *     other PDP pairs are ignored.
 *   - RING_CTL_SIZE(size) is (size - PAGE_SIZE), so a 4 KiB ring programs 0.
 *   - RPCS: gen12 sets ONLY has_slice_pg, so intel_sseu_make_rpcs() emits
 *     ENABLE | S_CNT_ENABLE | (slices << GEN11_RPCS_S_CNT_SHIFT) and neither
 *     the subslice nor the EU fields.  Leaving this at zero lets render power
 *     gating keep the EUs down, which is the classic "pipeline runs but no
 *     thread ever dispatches" hang.
 *
 * NOT YET DONE HERE (c2b): the INDIRECT_CTX and PER_CTX_BB batches that
 * lrc_update_regs() installs on gen12.  Until c2b adds them the image simply
 * carries no indirect batch (offset and size stay zero), which the hardware
 * skips; it is missing workarounds, not carrying a wrong pointer.
 */
#ifndef PARITY_GT_LRC_H
#define PARITY_GT_LRC_H

#include <stdint.h>

struct parity_gt_mem;
struct parity_gt_object;
struct parity_gt_ppgtt;
struct parity_gt_engine;

#define PARITY_LRC_PPHWSP_SZ       1u
#define PARITY_LRC_STATE_OFFSET    (PARITY_LRC_PPHWSP_SZ * 4096u)

/* Register-state dword indices (intel_lrc_reg.h, "GEN8 to GEN12"). */
#define PARITY_CTX_CONTEXT_CONTROL (0x02 + 1)
#define PARITY_CTX_RING_HEAD       (0x04 + 1)
#define PARITY_CTX_RING_TAIL       (0x06 + 1)
#define PARITY_CTX_RING_START      (0x08 + 1)
#define PARITY_CTX_RING_CTL        (0x0a + 1)
#define PARITY_CTX_BB_STATE        (0x10 + 1)
#define PARITY_CTX_TIMESTAMP       (0x22 + 1)
#define PARITY_CTX_PDP0_UDW        (0x30 + 1)
#define PARITY_CTX_PDP0_LDW        (0x32 + 1)
#define PARITY_CTX_R_PWR_CLK_STATE (0x42 + 1)

/* lrc_ring_mi_mode() / lrc_ring_bb_offset() on gen12. */
#define PARITY_LRC_MI_MODE_INDEX   0x60
#define PARITY_LRC_BB_OFFSET_INDEX 0x70

/* CTX_CONTEXT_CONTROL bits. */
#define PARITY_CTX_CTRL_INHIBIT_SYN_CTX_SWITCH        (1u << 3)
#define PARITY_CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT    (1u << 0)

/* RING_MI_MODE / RING_CTL. */
#define PARITY_LRC_STOP_RING       (1u << 8)
#define PARITY_RING_VALID          0x00000001u
#define PARITY_RING_CTL_SIZE(size) ((size) - 4096u)

/* MI commands (intel_gpu_commands.h). */
#define PARITY_MI_INSTR(op, flags)     (((op) << 23) | (flags))
#define PARITY_MI_NOOP                 PARITY_MI_INSTR(0, 0)
#define PARITY_MI_BATCH_BUFFER_END     PARITY_MI_INSTR(0x0a, 0)
#define PARITY_MI_LOAD_REGISTER_IMM(x) PARITY_MI_INSTR(0x22, 2u * (x) - 1u)
#define PARITY_MI_LRI_FORCE_POSTED     (1u << 12)
#define PARITY_MI_LRI_LRM_CS_MMIO      (1u << 19)

/* Context descriptor (intel_lrc.h). */
#define PARITY_GEN8_CTX_VALID                  (1u << 0)
#define PARITY_GEN8_CTX_PRIVILEGE              (1u << 8)
#define PARITY_GEN8_CTX_ADDRESSING_MODE_SHIFT  3
#define PARITY_INTEL_LEGACY_64B_CONTEXT        3u
#define PARITY_CTX_DESC_FORCE_RESTORE          (1u << 2)

/* GEN8_R_PWR_CLK_STATE bits. */
#define PARITY_GEN8_RPCS_ENABLE        (1u << 31)
#define PARITY_GEN8_RPCS_S_CNT_ENABLE  (1u << 18)
#define PARITY_GEN11_RPCS_S_CNT_SHIFT  12
#define PARITY_GEN11_RPCS_S_CNT_MASK   (0x3fu << PARITY_GEN11_RPCS_S_CNT_SHIFT)

/*
 * --- c2b: the INDIRECT_CTX and PER_CTX_BB batches ---
 *
 * On gen12 every context carries two extra pages: the INDIRECT_CTX batch the
 * engine runs as part of a context restore, and the PER_CTX_BB batch it runs
 * after it.  On ADL-P the indirect batch is NOT empty:
 *
 *   every engine   timestamp WA, restore scratch, AUX table invalidate
 *   render only    + CMD_BUF_CCTL WA, + state cache invalidate (Wa_18022495364,
 *                    IP 12.0..12.10)
 *
 * The AUX table invalidate ends in an MI_SEMAPHORE_WAIT that POLLS the
 * invalidate register until it reads back zero.  gen12_needs_ccs_aux_inv() is
 * true on ADL-P (each of the five engines has a valid AUX_INV register and the
 * platform has no flat CCS), so this really is emitted -- and a wrong register
 * or a missing acknowledgement here hangs the context restore, not the batch.
 */
#define PARITY_MI_LOAD_REGISTER_MEM_GEN8   PARITY_MI_INSTR(0x29, 2)
#define PARITY_MI_LOAD_REGISTER_REG        PARITY_MI_INSTR(0x2a, 1)
#define PARITY_MI_STORE_DWORD_IMM_GEN4     PARITY_MI_INSTR(0x20, 2)
#define PARITY_MI_SET_PREDICATE            PARITY_MI_INSTR(0x01, 0)
#define PARITY_MI_SEMAPHORE_WAIT_TOKEN     PARITY_MI_INSTR(0x1c, 3)
#define PARITY_MI_USE_GGTT                 (1u << 22)
#define PARITY_MI_SRM_LRM_GLOBAL_GTT       (1u << 22)
#define PARITY_MI_LRR_SOURCE_CS_MMIO       (1u << 18)
#define PARITY_MI_LRI_MMIO_REMAP_EN        (1u << 17)
#define PARITY_MI_SEMAPHORE_REGISTER_POLL  (1u << 16)
#define PARITY_MI_SEMAPHORE_POLL           (1u << 15)
#define PARITY_MI_SEMAPHORE_SAD_EQ_SDD     (4u << 12)

/* Register-state slots that name the batches (gen12 values). */
#define PARITY_LRC_RING_WA_BB_PER_CTX      0x12
#define PARITY_LRC_RING_INDIRECT_PTR       0x14
#define PARITY_LRC_RING_INDIRECT_OFFSET    0x16
#define PARITY_LRC_RING_GPR0               0x74
#define PARITY_LRC_RING_CMD_BUF_CCTL       0xb6   /* RENDER only; -1 elsewhere */
#define PARITY_GEN12_INDIRECT_CTX_OFFSET_DEFAULT 0xd
#define PARITY_PER_CTX_BB_FORCE            (1u << 2)
#define PARITY_PER_CTX_BB_VALID            (1u << 0)
#define PARITY_CACHELINE_BYTES             64u
#define PARITY_DG2_PREDICATE_RESULT_BB     2048u
#define PARITY_DG2_PREDICATE_RESULT_WA     (4096u - 8u)

/* Registers the batches touch. */
#define PARITY_GEN8_RING_CS_GPR(base, n)    ((base) + 0x600u + (n) * 8u)
#define PARITY_RING_CTX_TIMESTAMP_REG(base) ((base) + 0x3a8u)
#define PARITY_RING_CMD_BUF_CCTL_REG(base)  ((base) + 0x84u)
#define PARITY_GEN12_CS_DEBUG_MODE2         0x20d8u
#define PARITY_INSTRUCTION_STATE_CACHE_INVALIDATE (1u << 6)
#define PARITY_AUX_INV                      1u

struct parity_gt_ring {
	struct parity_gt_object *obj;
	uint32_t *vaddr;
	uint32_t size;
	uint32_t head;
	uint32_t tail;
	uint32_t emit;
	uint32_t space;
	uint64_t ggtt_offset;
};

struct parity_gt_context {
	struct parity_gt_engine *ge;
	struct parity_gt_ppgtt *vm;

	struct parity_gt_object *state;   /* the context image */
	uint32_t *lrc_reg_state;          /* state->cpu + LRC_STATE_OFFSET */
	uint32_t state_bytes;
	unsigned wa_bb_page;              /* index of the INDIRECT_CTX page */

	struct parity_gt_ring ring;

	uint32_t sw_id;                   /* SW context id for the descriptor */
	uint32_t lrca;                    /* lower dword of the descriptor */
	/*
	 * ce->lrc.desc: the union of lrca (low dword, as lrc_update_regs()
	 * returned it -- WITH CTX_DESC_FORCE_RESTORE until the first submit) and
	 * ccid (high dword, filled at schedule-in).
	 */
	uint64_t lrc_desc;
	int tag;                          /* SW context id while scheduled in, else -1 */
	unsigned reg_state_dwords;        /* how many the offset table laid out */

	/* c2b */
	uint32_t indirect_bb_ggtt;        /* GGTT address of the INDIRECT_CTX page */
	unsigned indirect_bb_dwords;      /* what the engine is told to execute */
	int per_ctx_bb_set;

	int allocated;
};

/* __lrc_alloc_state() sizing, exposed for the tests. */
uint32_t parity_lrc_state_size(uint32_t context_size, unsigned *wa_bb_page_out);

/* set_offsets(): lay the LRI headers and register offsets into the state page. */
unsigned parity_lrc_set_offsets(uint32_t *regs, const uint8_t *data,
	uint32_t mmio_base, int close);

/* The generated gen12 offset tables, for the layout tests. */
const uint8_t *parity_gen12_rcs_offsets_ref(void);
const uint8_t *parity_gen12_xcs_offsets_ref(void);

/* intel_sseu_make_rpcs() for gen12 (slice power gating only). */
uint32_t parity_sseu_make_rpcs(uint8_t slice_mask, int has_slice_pg);

int parity_lrc_alloc(struct parity_gt_context *ce, struct parity_gt_engine *ge,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm,
	uint32_t ring_size, uint32_t sw_id);
void parity_lrc_init_state(struct parity_gt_context *ce);
/* lrc_init_regs(ce, engine, inhibit): the register state only. */
void parity_lrc_init_regs(struct parity_gt_context *ce, int inhibit);
/* lrc_reset(): ring reset to emit, scrub the registers, update them. */
void parity_lrc_reset(struct parity_gt_context *ce);
uint32_t parity_lrc_update_regs(struct parity_gt_context *ce, uint32_t head);
uint32_t parity_lrc_descriptor(const struct parity_gt_context *ce);
/* gen12_get_aux_inv_reg(): 0 when the engine has none. */
uint32_t parity_lrc_aux_inv_reg(int engine_id);

void parity_lrc_release(struct parity_gt_context *ce, struct parity_gt_mem *gm);

#endif /* PARITY_GT_LRC_H */
