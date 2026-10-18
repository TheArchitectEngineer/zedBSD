/* Host stand-in for the PPGTT equivalence test. */
#ifndef PPGTT_EQUIV_MEMORY_H
#define PPGTT_EQUIV_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#define I915_GT_PAGE_BYTES 4096U
#define I915_GT_PTES_PER_PAGE (I915_GT_PAGE_BYTES / 8U)
#define I915_PAGE_BYTES 4096U
#define I915_DMA_MAX_ADDRESS ((1ULL << 39) - 1ULL)
struct i915_gt_object { void *cpu; unsigned id; };
struct i915_gt_mem { uint64_t dma_mask; };
struct i915_gt_object *drv_i915_gt_object_create(struct i915_gt_mem *gm, uint32_t bytes);
void drv_i915_gt_object_destroy(struct i915_gt_mem *gm, struct i915_gt_object *o);
int drv_i915_gt_object_page_dma(const struct i915_gt_object *o, unsigned page, uint64_t *dma_out);
void drv_i915_gt_clflush(const volatile void *address, size_t bytes);
#endif
