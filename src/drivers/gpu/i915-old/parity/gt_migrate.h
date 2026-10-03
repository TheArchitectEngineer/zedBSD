/*
 * WS031 Linux-parity — P6-c6: intel_migrate_init().
 *
 * The reference (gt/intel_migrate.c, gt/intel_engine_cs.c, 6.8.12):
 *   intel_migrate_init(&gt->migrate, gt)
 *     ce = pinned_context(gt)
 *       engine = first_copy_engine(gt)        the first BCS instance
 *       vm = migrate_vm(gt)
 *         i915_ppgtt_create(gt)               a NEW ppgtt (scratch tower + top)
 *         per copy engine instance i, base = i << 32:
 *           sz = 2 * CHUNK_SZ (8 MiB chunks: source window, destination window)
 *           d.offset = base + sz              the PTE window follows the two
 *           sz += (sz >> 12) * sizeof(u64)    one PTE per page of the windows
 *           allocate_va_range(base, sz)       page tables for the whole range
 *           foreach PT in [base, d.offset):   insert_pte(): map the PT page
 *             insert_page(dma(pt), d.offset,  ITSELF at d.offset, uncached, so
 *                         I915_CACHE_NONE)    the GPU can rewrite the PTEs of
 *             d.offset += PAGE_SIZE           the windows in line with a blit
 *       ce = intel_engine_create_pinned_context(engine, vm, SZ_512K,
 *                                              I915_GEM_HWS_MIGRATE, "migrate")
 *         intel_context_create(engine); CONTEXT_BARRIER_BIT; timeline on the
 *         engine's own status page at 0x42 * 4; ring 512 KiB; ce->vm = vm;
 *         intel_context_pin(): lrc_alloc + lrc_init_state + lrc_update_regs,
 *         perma-pinned
 *     m->context = ce
 *   The return value is NOT checked by intel_gt_init(): without a migrate
 *   context the TTM move path falls back to memcpy.
 *
 * ADL-P: one copy engine (bcs0), no 64K pages (HAS_64K_PAGES is false), so the
 * layout is the classic [0, 8M) src / [8M, 16M) dst / [16M, 16M + 32K) PTE.
 * Nothing is submitted here: the context exists, pinned, for later copies.
 *
 * ADAPTATIONS (recorded):
 *   - The ring (512 KiB) exceeds the DMA vector cap of this kernel, so an
 *     object above that cap is one coherent allocation (gt_mem.c).
 *   - allocate_va_range's page-table stash is created on demand from the
 *     fixed object pool rather than pre-allocated and mapped under ww locks.
 */
#ifndef PARITY_GT_MIGRATE_H
#define PARITY_GT_MIGRATE_H

#include <stdint.h>
#include "gt_mem.h"
#include "gt_lrc.h"

struct parity_gt_engines;

#define PARITY_MIGRATE_CHUNK_SZ        (8u << 20)      /* CHUNK_SZ = SZ_8M */
#define PARITY_MIGRATE_RING_BYTES      (512u << 10)    /* SZ_512K */
#define PARITY_I915_GEM_HWS_MIGRATE    (0x42u * 4u)

struct parity_gt_migrate {
	struct parity_gt_ppgtt vm;
	struct parity_gt_context ce;
	int has_engine;
	unsigned engine_idx;

	/* The pinned timeline: the engine's status page at HWS_MIGRATE. */
	uint32_t hwsp_ggtt;
	volatile uint32_t *hwsp_cpu;
	uint32_t tl_seqno;

	/* migrate_vm() accounting */
	uint64_t window_bytes;    /* 2 * CHUNK_SZ */
	uint64_t pte_window;      /* d.offset at the start: base + 2 * CHUNK_SZ */
	unsigned exposed_pts;     /* PTs mapped into the PTE window */

	int err;
	const char *err_where;
	int inited;
};

int parity_intel_migrate_init(struct parity_gt_migrate *m,
	struct parity_gt_engines *es, struct parity_gt_mem *gm);
void parity_intel_migrate_fini(struct parity_gt_migrate *m,
	struct parity_gt_mem *gm);

#endif /* PARITY_GT_MIGRATE_H */
