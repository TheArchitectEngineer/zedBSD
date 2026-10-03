/* WS031 Linux-parity — GGTT / PPGTT PTE encoders (see pte.h). */
#include "pte.h"

int
parity_dma_in_range(uint64_t dma_addr, uint64_t length, uint64_t mask)
{
	if (length == 0u)
		return 0;
	if (dma_addr > mask)
		return 0;
	/* length - 1 <= mask - dma_addr, computed to avoid addition overflow. */
	if (length - 1u > mask - dma_addr)
		return 0;
	return 1;
}

static int
encode_common(osdep_dma_addr_t dma, uint64_t length, uint64_t mask, uint64_t flags, uint64_t *pte_out)
{
	uint64_t a = osdep_dma_addr_raw(dma);

	if ((a & (PARITY_PAGE_SIZE - 1u)) != 0u)   /* must be page-aligned */
		return 0;
	if (!parity_dma_in_range(a, length, mask))
		return 0;
	/* 64-bit PTE; the full DMA address is preserved (never truncated to 32 bits). */
	*pte_out = a | flags;
	return 1;
}

int
parity_ggtt_pte_encode(osdep_dma_addr_t dma, uint64_t length, uint64_t mask, uint64_t *pte_out)
{
	/* SMEM GGTT PTE: PRESENT only.  The GEN12_GGTT_PTE_LM bit (bit 1) stays clear. */
	return encode_common(dma, length, mask, PARITY_GEN8_PAGE_PRESENT, pte_out);
}

int
parity_ppgtt_pte_encode(osdep_dma_addr_t dma, uint64_t length, uint64_t mask, uint64_t *pte_out)
{
	/* PPGTT leaf PTE: PRESENT | RW.  Bit 1 here is RW, NOT the GGTT LM bit. */
	return encode_common(dma, length, mask,
		PARITY_GEN8_PAGE_PRESENT | PARITY_GEN8_PAGE_RW, pte_out);
}
