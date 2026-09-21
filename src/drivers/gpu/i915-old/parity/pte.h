/*
 * WS031 Linux-parity — GGTT / PPGTT page-table entry encoders.
 *
 * GGTT and PPGTT encode a DMA page address into a 64-bit PTE DIFFERENTLY, and the
 * two must not be shared (bit 1 means GEN12_GGTT_PTE_LM for GGTT but GEN8_PAGE_RW
 * for PPGTT).  For system memory (SMEM):
 *     GGTT  PTE = dma_addr | PRESENT            (LM bit clear)
 *     PPGTT leaf = dma_addr | PRESENT | RW
 * Every encode first range-checks the whole [addr, addr+len) against the DMA mask
 * with overflow-safe arithmetic and requires page alignment; it never ANDs high
 * bits off to force an address into range, and never truncates to 32 bits.
 */
#ifndef PARITY_PTE_H
#define PARITY_PTE_H

#include <stdint.h>
#include "osdep/dma.h"

#define PARITY_GEN8_PAGE_PRESENT   (((uint64_t)1) << 0)
#define PARITY_GEN8_PAGE_RW        (((uint64_t)1) << 1)  /* PPGTT read/write */
#define PARITY_GEN12_GGTT_PTE_LM   (((uint64_t)1) << 1)  /* GGTT local-memory (SMEM: clear) */
#define PARITY_PAGE_SIZE           ((uint64_t)0x1000)

/* Overflow-safe: the whole [addr, addr+length) lies within [0, mask]. */
int parity_dma_in_range(uint64_t dma_addr, uint64_t length, uint64_t mask);

/*
 * Encode a system-memory PTE.  Returns 1 and writes *pte_out on success; returns
 * 0 (and does NOT write) if the address is misaligned or out of range — the
 * caller must then not update the table.
 */
int parity_ggtt_pte_encode(osdep_dma_addr_t dma, uint64_t length, uint64_t mask, uint64_t *pte_out);
int parity_ppgtt_pte_encode(osdep_dma_addr_t dma, uint64_t length, uint64_t mask, uint64_t *pte_out);

#endif /* PARITY_PTE_H */
