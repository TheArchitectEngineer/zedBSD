/*
 * Mock DMA backend for GPU-free contract tests.  It is a TEST backend only: it
 * simulates address translation so the contract layer can be exercised without
 * hardware.  The production driver never fabricates DMA addresses this way.
 *
 *  - Non-identity, non-linear translation: each physical page maps to a DMA page
 *    via a 24-bit reversal of the page number, so a device address never equals
 *    the CPU physical, and physically adjacent pages are NOT DMA-adjacent (a
 *    "base + i*PAGE" consumer is caught).
 *  - Optional coalescing of physically contiguous input entries into one segment
 *    (mapped segment count < input entry count), to exercise the sg count split.
 *  - Optional forced failure, to exercise the failure/cleanup path.
 */
#include "mock_dma.h"

#define MOCK_IOVA_BASE 0x0000004000000000ULL   /* != any test phys; makes dma != phys */

static uint64_t
reverse24(uint64_t x)
{
	uint64_t r = 0u;
	int i;

	for (i = 0; i < 24; i++) {
		r = (r << 1) | (x & 1u);
		x >>= 1;
	}
	return r;
}

uint64_t
mock_dma_translate(uint64_t phys)
{
	uint64_t page = (phys >> 12) & 0xFFFFFFu;
	uint64_t off = phys & 0xFFFu;
	return MOCK_IOVA_BASE + (reverse24(page) << 12) + off;
}

uint64_t
mock_dma_untranslate(uint64_t dma)
{
	uint64_t rel = dma - MOCK_IOVA_BASE;
	uint64_t page = reverse24((rel >> 12) & 0xFFFFFFu);
	uint64_t off = rel & 0xFFFu;
	return (page << 12) + off;
}

static int
mock_set_info(void *priv, unsigned mask_bits, uint64_t max_segment)
{
	struct mock_dma *m = (struct mock_dma *)priv;

	m->set_info_calls++;
	m->last_mask_bits = mask_bits;
	m->last_max_seg = max_segment;
	return 0;
}

static int
mock_map_sg(void *priv, const struct osdep_sg_entry *in, unsigned orig_nents,
	    struct osdep_dma_segment *out, unsigned out_cap, enum osdep_dma_dir dir)
{
	struct mock_dma *m = (struct mock_dma *)priv;
	unsigned i;
	unsigned produced = 0u;

	(void)dir;
	if (m->fail) {
		m->fail = 0;   /* one-shot */
		return 0;      /* dma_map_sg failure convention */
	}

	i = 0u;
	while (i < orig_nents) {
		uint64_t base_phys = in[i].phys.value;
		uint32_t length = in[i].length;

		/* Coalesce physically contiguous following entries into one segment. */
		if (m->coalesce) {
			while (i + 1u < orig_nents &&
			       in[i].phys.value + in[i].length == in[i + 1u].phys.value) {
				length += in[i + 1u].length;
				i++;
			}
		}
		if (produced >= out_cap)
			return 0;
		out[produced].addr = osdep_dma_addr(mock_dma_translate(base_phys));
		out[produced].length = length;
		produced++;
		i++;
	}
	return (int)produced;
}

static void
mock_unmap_sg(void *priv, const struct osdep_dma_segment *segs, unsigned nents,
	      unsigned orig_nents, enum osdep_dma_dir dir)
{
	struct mock_dma *m = (struct mock_dma *)priv;

	(void)segs;
	(void)nents;
	(void)dir;
	m->unmap_sg_calls++;
	m->last_unmap_orig_nents = orig_nents;
}

static uint64_t
mock_map_page(void *priv, uint64_t phys, uint32_t size, enum osdep_dma_dir dir)
{
	struct mock_dma *m = (struct mock_dma *)priv;

	(void)size;
	(void)dir;
	if (m->fail) {
		m->fail = 0;
		return OSDEP_DMA_MAPPING_ERROR;
	}
	return mock_dma_translate(phys);
}

static void
mock_unmap_page(void *priv, uint64_t dma, uint32_t size, enum osdep_dma_dir dir)
{
	struct mock_dma *m = (struct mock_dma *)priv;

	(void)dma;
	(void)size;
	(void)dir;
	m->unmap_page_calls++;
}

static void
mock_sync_for_device(void *priv, uint64_t dma, uint32_t size, enum osdep_dma_dir dir)
{
	struct mock_dma *m = (struct mock_dma *)priv;

	(void)dma;
	(void)size;
	(void)dir;
	m->sync_dev_calls++;
}

static void
mock_sync_for_cpu(void *priv, uint64_t dma, uint32_t size, enum osdep_dma_dir dir)
{
	struct mock_dma *m = (struct mock_dma *)priv;

	(void)dma;
	(void)size;
	(void)dir;
	m->sync_cpu_calls++;
}

const struct osdep_dma_backend *
mock_dma_backend(void)
{
	static const struct osdep_dma_backend backend = {
		"mock",
		39u,                 /* address_bits */
		0x100000u,           /* max_segment */
		1,                   /* coherent */
		mock_set_info,
		mock_map_sg,
		mock_unmap_sg,
		mock_map_page,
		mock_unmap_page,
		mock_sync_for_device,
		mock_sync_for_cpu,
	};
	return &backend;
}

void
mock_dma_reset(struct mock_dma *m)
{
	m->coalesce = 0;
	m->fail = 0;
	m->set_info_calls = 0;
	m->last_mask_bits = 0u;
	m->last_max_seg = 0u;
	m->sync_dev_calls = 0;
	m->sync_cpu_calls = 0;
	m->unmap_sg_calls = 0;
	m->unmap_page_calls = 0;
	m->last_unmap_orig_nents = 0u;
}
