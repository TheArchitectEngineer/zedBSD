/* Mock DMA backend (test only) — see mock_dma.c. */
#ifndef PARITY_TESTS_MOCK_DMA_H
#define PARITY_TESTS_MOCK_DMA_H

#include "../osdep/dma.h"

struct mock_dma {
	int coalesce;                    /* coalesce physically contiguous entries */
	int fail;                        /* one-shot: force next map to fail */
	int set_info_calls;
	unsigned last_mask_bits;
	uint64_t last_max_seg;
	int sync_dev_calls;
	int sync_cpu_calls;
	int unmap_sg_calls;
	int unmap_page_calls;
	unsigned last_unmap_orig_nents;
};

const struct osdep_dma_backend *mock_dma_backend(void);
void mock_dma_reset(struct mock_dma *m);
uint64_t mock_dma_translate(uint64_t phys);
uint64_t mock_dma_untranslate(uint64_t dma);

#endif
