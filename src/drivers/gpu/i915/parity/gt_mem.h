/*
 * WS031 Linux-parity — P6-c0: the GT's own memory objects and address spaces.
 *
 * What the reference does here, and what this file stands for:
 *
 *   intel_gt_init_scratch()   a 4 KiB internal object pinned into the GGTT
 *   kernel_vm()               INTEL_PPGTT(ADL-P) > ALIASING, so the kernel
 *                             context gets its OWN ppgtt (i915_ppgtt_create),
 *                             not a reference to the GGTT vm
 *   gen8_init_scratch()       scratch[0] (a data page) and then one filler page
 *                             per level up to vm->top, each filled with the
 *                             level below's encoded address
 *   gen8_alloc_top_pd()       the top page directory (PML4 on 4-level), filled
 *                             with scratch[top]'s encode
 *
 * ADL-P facts this file is built on (re-derived from the 6.8.12 reference, NOT
 * carried over from the big-bang transcription):
 *   - 4-level ppgtt: ppgtt_size = 48 -> vm->top = 3, top count = 512 entries.
 *   - vm->has_read_only is FALSE on gen11/gen12 (HSDES#1807136187), so the
 *     PTE_READ_ONLY branch of gen12_pte_encode never fires here.
 *   - TGL_CACHELEVEL: I915_CACHE_NONE maps to PAT index 3, which is UC in the
 *     PPAT this driver programs (WB, WC, WT, UC, WB, WB, WB, WB).  PAT index 3
 *     sets GEN12_PPGTT_PTE_PAT0 (bit 3) and PAT1 (bit 4).
 *   - gen8_pde_encode(addr, I915_CACHE_NONE) = addr | PRESENT | RW | PPAT_UNCACHED
 *     where PPAT_UNCACHED is _PAGE_PWT | _PAGE_PCD (bits 3 and 4).
 *
 * ADAPTATION (recorded, not hidden): the reference hands out GGTT space from a
 * drm_mm range allocator over the whole GGTT.  The parity layer performs no
 * dynamic allocation, so GGTT space for the driver's own objects comes from a
 * FIXED window of PARITY_GT_GGTT_PAGES pages taken at the TOP of the GGTT --
 * the same end intel_gt_init_scratch() asks for with PIN_HIGH, and the end the
 * BIOS framebuffer (which sits at the bottom, in stolen) does not occupy.  The
 * objects themselves come from a fixed pool for the same reason.
 */
#ifndef PARITY_GT_MEM_H
#define PARITY_GT_MEM_H

#include <stdint.h>
#include <drivers/dma.h>
#include "osdep/dma.h"

struct drv_dma_device;
struct drv_dma_vector;
struct osdep_mmio;

#define PARITY_GT_PAGE_BYTES     4096u
#define PARITY_GT_PTES_PER_PAGE  (PARITY_GT_PAGE_BYTES / 8u)

/* GPU VA window reserved at the top of the GGTT for this driver's objects. */
#define PARITY_GT_GGTT_PAGES     256u                        /* 1 MiB */
#define PARITY_GT_GGTT_WORDS     (PARITY_GT_GGTT_PAGES / 32u)

/* Object pool: HWSP/ring/LRC per engine, plus scratch and the ppgtt pages. */
#define PARITY_GT_MAX_OBJECTS    64u

/* gen12 ppgtt page-table entry bits (intel_gtt.h). */
#define PARITY_GEN8_PAGE_PRESENT_B  (((uint64_t)1) << 0)
#define PARITY_GEN8_PAGE_RW_B       (((uint64_t)1) << 1)
#define PARITY_GEN12_PTE_PAT0       (((uint64_t)1) << 3)
#define PARITY_GEN12_PTE_PAT1       (((uint64_t)1) << 4)
#define PARITY_GEN12_PTE_PAT2       (((uint64_t)1) << 7)
/* PPAT_UNCACHED = _PAGE_PWT | _PAGE_PCD */
#define PARITY_PPAT_UNCACHED        ((((uint64_t)1) << 3) | (((uint64_t)1) << 4))

/* TGL_CACHELEVEL: I915_CACHE_NONE -> PAT index 3. */
#define PARITY_PAT_INDEX_CACHE_NONE 3u

/* 4-level ppgtt on ADL-P (ppgtt_size = 48). */
#define PARITY_PPGTT_TOP         3
#define PARITY_PPGTT_TOP_COUNT   512u

/*
 * One driver-internal GT object: backing pages with a CPU-contiguous view and a
 * DMA mapping, optionally bound into the GGTT.  This is the parity stand-in for
 * (drm_i915_gem_object + i915_vma) for the objects the GT itself owns.
 */
struct parity_gt_object {
	struct drv_dma_vector *vec;
	/*
	 * ADAPTATION: the DMA vector caps at DRV_DMA_VECTOR_MAX_SIZE (64 KiB);
	 * a larger object (the 512 KiB migrate ring) is one coherent
	 * allocation instead.  Either way the pages are page aligned.
	 */
	struct drv_dma_buffer big;
	int contiguous;
	void *cpu;                  /* CPU-contiguous view of the backing pages */
	uint32_t bytes;
	unsigned pages;
	unsigned ggtt_page;         /* first page index in the GGTT table */
	uint64_t ggtt_offset;       /* the GPU VA the engine uses; valid when bound */
	int bound;
	int in_use;
};

struct parity_gt_mem {
	struct drv_dma_device *dma;
	uint64_t dma_mask;

	/* The GGTT PTE table window P2 mapped (BAR0 upper half). */
	volatile uint8_t *table;
	unsigned entries;           /* PTE slots in the window */
	uint64_t scratch_pte;       /* what a free entry holds (from P2) */
	struct osdep_mmio *m;       /* for GFX_FLSH_CNTL_GEN6 */

	unsigned window_first;      /* first page of the driver window */
	unsigned window_pages;
	uint32_t bitmap[PARITY_GT_GGTT_WORDS];
	unsigned allocated_pages;

	struct parity_gt_object objects[PARITY_GT_MAX_OBJECTS];
	unsigned objects_live;

	/* Diagnostics (never a substitute for a return contract). */
	unsigned pte_writes;
	unsigned flushes;
	unsigned obj_alloc_fail;
	unsigned ggtt_alloc_fail;
	int inited;
};

/* A page table allocated below the top directory (allocate_va_range). */
#define PARITY_PPGTT_MAX_TABLES  16u
struct parity_gt_ppgtt_table {
	struct parity_gt_object *obj;
	struct parity_gt_object *parent;   /* the directory holding its entry */
	unsigned idx;                      /* entry index in the parent */
	int lvl;                           /* 2 = PDP, 1 = PD, 0 = PT */
	uint64_t dma;
};

/* i915_ppgtt: scratch tower, the top directory, and any allocated tables. */
struct parity_gt_ppgtt {
	struct parity_gt_object *scratch[PARITY_PPGTT_TOP + 1];
	uint64_t scratch_encode[PARITY_PPGTT_TOP + 1];
	struct parity_gt_object *top_pd;
	uint64_t top_pd_dma;
	unsigned top_count;
	int top;
	int inited;

	struct parity_gt_ppgtt_table tables[PARITY_PPGTT_MAX_TABLES];
	unsigned n_tables;
};

/* gen8_ppgtt_alloc(): page tables for [start, start + length), scratch-filled. */
int parity_gt_ppgtt_alloc_range(struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp,
	uint64_t start, uint64_t length);

/* gen8_ppgtt_foreach(): every PT (leaf table) of the range, ascending. */
typedef void (*parity_gt_ppgtt_pt_fn)(struct parity_gt_ppgtt *pp,
	struct parity_gt_object *pt, uint64_t pt_dma, void *data);
int parity_gt_ppgtt_foreach_pt(struct parity_gt_ppgtt *pp, uint64_t start,
	uint64_t length, parity_gt_ppgtt_pt_fn fn, void *data);

/* gen8_ppgtt_insert_entry(): one PTE at offset; the tables must exist. */
int parity_gt_ppgtt_insert_page(struct parity_gt_ppgtt *pp, uint64_t dma,
	uint64_t offset, unsigned pat_index);

/* --- encoders (exposed so the tests compare against the reference values) --- */
uint64_t parity_gen12_ppgtt_pte_encode(uint64_t dma, unsigned pat_index);
uint64_t parity_gen8_pde_encode(uint64_t dma);

/* --- pool / GGTT window --- */
int parity_gt_mem_init(struct parity_gt_mem *gm, struct drv_dma_device *dma,
	uint64_t dma_mask, void *table, unsigned entries, uint64_t scratch_pte,
	struct osdep_mmio *m);
void parity_gt_mem_fini(struct parity_gt_mem *gm);

/* --- objects --- */
struct parity_gt_object *parity_gt_object_create(struct parity_gt_mem *gm, uint32_t bytes);
void parity_gt_object_destroy(struct parity_gt_mem *gm, struct parity_gt_object *o);
/* DMA address of one 4 KiB page of the object; 0 on success, -errno otherwise. */
int parity_gt_object_page_dma(const struct parity_gt_object *o, unsigned page, uint64_t *dma_out);

/* --- GGTT binding --- */
int parity_gt_ggtt_bind(struct parity_gt_mem *gm, struct parity_gt_object *o);
void parity_gt_ggtt_unbind(struct parity_gt_mem *gm, struct parity_gt_object *o);
void parity_gt_ggtt_flush(struct parity_gt_mem *gm);

/* --- the two things intel_gt_init() asks for before the engines --- */
int parity_gt_init_scratch(struct parity_gt_mem *gm, struct parity_gt_object **out);
int parity_gt_ppgtt_create(struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp);
void parity_gt_ppgtt_destroy(struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp);

#endif /* PARITY_GT_MEM_H */
