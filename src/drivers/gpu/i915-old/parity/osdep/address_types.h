/*
 * WS031 Linux-parity OS adaptation layer — typed addresses.
 *
 * The working driver failed in part because one kind of address (the page
 * manager's physical / guest-physical address) was fed straight into the GPU
 * page tables, where a DMA-mapped device address belongs.  In the current
 * QEMU/VFIO setup those two happen to be numerically equal, so the bug is
 * invisible; the adaptation layer refuses to rely on that coincidence.
 *
 * Each address kind is a one-field struct so the compiler rejects a value of
 * one kind used where another is expected.  A raw integer never silently
 * becomes a DMA address, and the GGTT/PPGTT insert path can accept only an
 * osdep_dma_addr_t produced by the DMA layer.
 *
 * C90-safe: constructors are inline functions, not compound literals.
 */
#ifndef PARITY_OSDEP_ADDRESS_TYPES_H
#define PARITY_OSDEP_ADDRESS_TYPES_H

#include <stdint.h>

/* Page-manager physical address (guest-physical inside a VM). DMA-layer internal only. */
typedef struct { uint64_t value; } osdep_cpu_phys_t;

/* Device-visible DMA address: the output of a DMA mapping. This is what goes into GPU PTEs. */
typedef struct { uint64_t value; } osdep_dma_addr_t;

/* GPU virtual address from the GGTT/PPGTT allocator (SBA, KSP base, batch, surface). */
typedef struct { uint64_t value; } osdep_gpu_vaddr_t;

/* A CPU virtual address stays a normal pointer (used directly by the CPU); no wrapper needed. */

static inline osdep_cpu_phys_t osdep_cpu_phys(uint64_t v)   { osdep_cpu_phys_t a; a.value = v; return a; }
static inline osdep_dma_addr_t osdep_dma_addr(uint64_t v)   { osdep_dma_addr_t a; a.value = v; return a; }
static inline osdep_gpu_vaddr_t osdep_gpu_vaddr(uint64_t v) { osdep_gpu_vaddr_t a; a.value = v; return a; }

static inline uint64_t osdep_cpu_phys_raw(osdep_cpu_phys_t a)   { return a.value; }
static inline uint64_t osdep_dma_addr_raw(osdep_dma_addr_t a)   { return a.value; }
static inline uint64_t osdep_gpu_vaddr_raw(osdep_gpu_vaddr_t a) { return a.value; }

/*
 * Sentinel for a failed DMA mapping, mirroring the dedicated mapping-error check
 * the DMA API requires (never test a mapping result against 0 or against the CPU
 * physical value).
 */
#define OSDEP_DMA_MAPPING_ERROR (~(uint64_t)0)
static inline int osdep_dma_mapping_failed(osdep_dma_addr_t a) { return a.value == OSDEP_DMA_MAPPING_ERROR; }

#endif /* PARITY_OSDEP_ADDRESS_TYPES_H */
