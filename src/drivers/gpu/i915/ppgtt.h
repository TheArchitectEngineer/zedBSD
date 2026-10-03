/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Per-process GTTs: 48-bit, four levels of 512 entries.
 *
 * Two independent address-space implementations live here and are not
 * merged yet:
 *
 *   kernel PPGTT   (struct i915_gt_ppgtt, drv_i915_gt_ppgtt_*)
 *       The address space of the GT's kernel contexts.  On Alder Lake-P
 *       INTEL_PPGTT is FULL, so the kernel context gets its own PPGTT
 *       (i915_ppgtt_create) rather than a reference to the GGTT.  Its
 *       tables are GT objects; every entry write is clflushed, because the
 *       GPU's table walker does not snoop the CPU caches.  It stands for
 *       gen8_init_scratch(), gen8_alloc_top_pd(), gen8_ppgtt_alloc(),
 *       gen8_ppgtt_foreach(), gen8_ppgtt_insert_entry() and
 *       gen8_ppgtt_clear().
 *
 *   session PPGTT  (struct i915_ppgtt, drv_i915_ppgtt_*)
 *       The private address space of one GPU session.  Its tables are
 *       single pages from the page manager written through the direct map,
 *       and the LLC keeps the table walks coherent with those stores.
 *       Ranges come from a bump allocator and are never reused.
 *
 * Facts both are built on (Linux 6.8 gen8_ppgtt.c and intel_gtt.h):
 *   - four levels: ppgtt_size is 48, so vm->top is 3 and the top table has
 *     512 entries
 *   - vm->has_read_only is false on graphics versions 11 and 12, so the
 *     PTE_READ_ONLY branch of gen12_pte_encode never fires
 *   - I915_CACHE_NONE maps to PAT index 3, which is UC in the PAT this
 *     driver programs (WB, WC, WT, UC, WB, WB, WB, WB); index 3 sets PAT0
 *     (bit 3) and PAT1 (bit 4)
 *   - gen8_pde_encode(address, I915_CACHE_NONE) is address | PRESENT | RW |
 *     PPAT_UNCACHED, where PPAT_UNCACHED is _PAGE_PWT | _PAGE_PCD (bits 3
 *     and 4)
 */

#ifndef DRIVERS_GPU_I915_PPGTT_H
#define DRIVERS_GPU_I915_PPGTT_H

#include <kern/pmem.h>
#include <stdint.h>

struct i915_gt_mem;
struct i915_gt_object;

/* The Gen12 PPGTT entry bits of the kernel PPGTT (intel_gtt.h). */
#define I915_GEN8_PAGE_PRESENT_B	(((uint64_t)1) << 0)
#define I915_GEN8_PAGE_RW_B		(((uint64_t)1) << 1)
#define I915_GEN12_PTE_PAT0		(((uint64_t)1) << 3)
#define I915_GEN12_PTE_PAT1		(((uint64_t)1) << 4)
#define I915_GEN12_PTE_PAT2		(((uint64_t)1) << 7)

/* PPAT_UNCACHED: _PAGE_PWT | _PAGE_PCD. */
#define I915_PPAT_UNCACHED		((((uint64_t)1) << 3) | (((uint64_t)1) << 4))

/* The PAT index of I915_CACHE_NONE in the Tiger Lake cache-level table. */
#define I915_PAT_INDEX_CACHE_NONE	3U

/* The top level of the four-level tree and how many entries its table has. */
#define I915_PPGTT_TOP			3
#define I915_PPGTT_TOP_COUNT		512U

/* How many tables below the top the kernel PPGTT can allocate. */
#define I915_PPGTT_MAX_TABLES		16U

/* The session PPGTT: 48-bit canonical, four levels of 512 entries (I915_PDES). */
#define I915_PPGTT_LEVELS		4U
#define I915_PPGTT_ENTRIES		512U
#define I915_PPGTT_ADDRESS_BITS		48U

/* Session objects are placed above the first 4 GiB, each on a 2 MiB boundary. */
#define I915_PPGTT_VA_START		0x100000000ULL
#define I915_PPGTT_VA_ALIGN		0x200000ULL

/* The address bits of a session PPGTT entry. */
#define I915_PPGTT_ADDRESS_MASK		(((1ULL << I915_PPGTT_ADDRESS_BITS) - 1ULL) & ~((uint64_t)4096U - 1ULL))

/*
 * One table the kernel PPGTT allocated below its top directory.
 *
 * It is recorded in the address space's table list, which is how a walk
 * finds the child an entry names; the address space frees it at destroy.
 */
struct i915_gt_ppgtt_table {
	/* The table page. */
	struct i915_gt_object *obj;

	/* The directory whose entry names this table, and that entry's index. */
	struct i915_gt_object *parent;
	unsigned idx;

	/* The table's level: 2 is a PDP, 1 a PD, 0 a PT. */
	int lvl;

	/* The table page's DMA address. */
	uint64_t dma;
};

/*
 * The kernel PPGTT (i915_ppgtt): the scratch tower, the top directory and
 * every table allocated below it.
 *
 * Its tables are reached by the GPU through a context's PDP0 pair, by DMA
 * address; they are not bound into the GGTT.  The owner embeds it and
 * destroys it before the GT memory goes.
 */
struct i915_gt_ppgtt {
	/* scratch[0] is the data page; scratch[n] is a table filled with scratch[n - 1]'s encoding. */
	struct i915_gt_object *scratch[I915_PPGTT_TOP + 1];
	uint64_t scratch_encode[I915_PPGTT_TOP + 1];

	/* The top directory (PML4) and its DMA address, which PDP0 of a context holds. */
	struct i915_gt_object *top_pd;
	uint64_t top_pd_dma;

	/* How many entries the top directory has, and its level. */
	unsigned top_count;
	int top;

	/* Nonzero between a successful create and destroy. */
	int inited;

	/* The tables allocated below the top directory. */
	struct i915_gt_ppgtt_table tables[I915_PPGTT_MAX_TABLES];
	unsigned n_tables;
};

/* Receives one leaf table (PT) of a range, in ascending order. */
typedef void (*i915_gt_ppgtt_pt_fn)(struct i915_gt_ppgtt *pp, struct i915_gt_object *pt, uint64_t pt_dma, void *data);

/*
 * One page-table page of a session PPGTT, kept for release at destroy.
 *
 * The walk reads child addresses out of the entries, so this list only has
 * to remember which pages to return to the pool.
 */
struct i915_ppgtt_page {
	struct kern_pmem run;
	struct i915_ppgtt_page *next;
};

/*
 * One session's private 48-bit address space.
 *
 * The scratch chain (page, table, directory, directory pointer) backs every
 * range no object occupies.  Ranges are handed out by a bump allocator and
 * never reused within the lifetime of the space.
 */
struct i915_ppgtt {
	/* The top table and the scratch chain with each level's scratch entry. */
	struct kern_pmem pml4;
	struct kern_pmem scratch[I915_PPGTT_LEVELS];
	uint64_t scratch_entry[I915_PPGTT_LEVELS];

	/* The tables inserts allocated, and how many. */
	struct i915_ppgtt_page *pages;
	unsigned page_count;

	/* The next range the bump allocator hands out. */
	uint64_t next_va;

	/* Nonzero between a successful create and destroy. */
	unsigned created;

	/* A space whose session closed while quarantined waits on the device list for reset. */
	uint32_t owner;
	struct i915_ppgtt *next;
};

uint64_t drv_i915_gen12_ppgtt_pte_encode(uint64_t dma, unsigned pat_index);
uint64_t drv_i915_gen8_pde_encode(uint64_t dma);
uint64_t drv_i915_gen8_pde_encode_cached(uint64_t dma);

int drv_i915_gt_ppgtt_create(struct i915_gt_mem *gm, struct i915_gt_ppgtt *pp);
void drv_i915_gt_ppgtt_destroy(struct i915_gt_mem *gm, struct i915_gt_ppgtt *pp);
int drv_i915_gt_ppgtt_alloc_range(struct i915_gt_mem *gm, struct i915_gt_ppgtt *pp, uint64_t start, uint64_t length);
int drv_i915_gt_ppgtt_foreach_pt(struct i915_gt_ppgtt *pp, uint64_t start, uint64_t length, i915_gt_ppgtt_pt_fn fn, void *data);
int drv_i915_gt_ppgtt_insert_page(struct i915_gt_ppgtt *pp, uint64_t dma, uint64_t offset, unsigned pat_index);
int drv_i915_gt_ppgtt_insert_scratch(struct i915_gt_ppgtt *pp, uint64_t offset);

int drv_i915_ppgtt_create(struct i915_ppgtt *vm);
void drv_i915_ppgtt_destroy(struct i915_ppgtt *vm);
int drv_i915_ppgtt_va_alloc(struct i915_ppgtt *vm, uint64_t bytes, uint64_t *va);
int drv_i915_ppgtt_insert(struct i915_ppgtt *vm, uint64_t va, uint64_t physical, unsigned pages);
int drv_i915_ppgtt_insert_uncached(struct i915_ppgtt *vm, uint64_t va, uint64_t physical, unsigned pages);
void drv_i915_ppgtt_clear(struct i915_ppgtt *vm, uint64_t va, unsigned pages);

#endif
