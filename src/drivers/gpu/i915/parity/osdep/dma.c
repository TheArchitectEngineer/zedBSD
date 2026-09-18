/* WS031 Linux-parity OS adaptation layer — DMA contract (see dma.h). */
#include "dma.h"

/* Linux-valued errnos, used at this Linux-facing boundary (negative on return). */
#ifndef OSDEP_EPERM
#define OSDEP_EINVAL     22
#define OSDEP_ENOMEM     12
#define OSDEP_EBUSY      16
#define OSDEP_ENOSPC     28
#define OSDEP_EOPNOTSUPP 95
#endif

static void
tr(struct osdep_dma_device *dev, uint16_t op, const char *what, uint64_t a0, uint64_t a1)
{
	if (dev->trace != 0)
		osdep_trace_emit(dev->trace, 0u, op, what, a0, a1);
}

void
osdep_dma_device_init(struct osdep_dma_device *dev,
		      const struct osdep_dma_backend *backend, void *priv,
		      struct osdep_trace *trace)
{
	unsigned i;

	dev->backend = backend;
	dev->priv = priv;
	dev->trace = trace;
	dev->mask_bits = 0u;
	dev->max_segment = 0u;
	dev->info_set = 0;
	dev->next_resource_id = 1u;
	for (i = 0u; i < OSDEP_DMA_MAX_MAPPINGS; i++) {
		dev->mappings[i].in_use = 0;
		dev->mappings[i].pinned = 0;
		dev->mappings[i].resource_id = 0u;
		dev->mappings[i].orig_nents = 0u;
		dev->mappings[i].nents = 0u;
	}
}

int
osdep_dma_set_info(struct osdep_dma_device *dev, unsigned mask_bits, uint64_t max_segment)
{
	int rc;

	tr(dev, OSDEP_TR_ENTRY, "dma_set_info", mask_bits, max_segment);

	/* A DMA mask is a device capability, not the guest phys-bits limit. Reject nonsense. */
	if (mask_bits < 32u || mask_bits > 64u || max_segment == 0u) {
		tr(dev, OSDEP_TR_FAIL, "dma_set_info", (uint64_t)(-OSDEP_EINVAL), 0u);
		return -OSDEP_EINVAL;
	}
	if (dev->backend->set_info == 0) {
		/* Unimplemented backend op: recorded as such, not a hardware refusal. */
		tr(dev, OSDEP_TR_UNIMPL, "dma_set_info", 0u, 0u);
		return -OSDEP_EOPNOTSUPP;
	}

	rc = dev->backend->set_info(dev->priv, mask_bits, max_segment);
	if (rc != 0) {
		tr(dev, OSDEP_TR_FAIL, "dma_set_info", (uint64_t)rc, 0u);
		return rc;
	}
	dev->mask_bits = mask_bits;
	dev->max_segment = max_segment;
	dev->info_set = 1;
	tr(dev, OSDEP_TR_EXIT, "dma_set_info", 0u, 0u);
	return 0;
}

unsigned
osdep_dma_address_bits(const struct osdep_dma_device *dev)
{
	return dev->backend->address_bits;
}

uint64_t
osdep_dma_max_segment(const struct osdep_dma_device *dev)
{
	return dev->backend->max_segment;
}

int
osdep_dma_is_coherent(const struct osdep_dma_device *dev)
{
	return dev->backend->coherent;
}

static struct osdep_dma_mapping *
alloc_mapping(struct osdep_dma_device *dev)
{
	unsigned i;

	for (i = 0u; i < OSDEP_DMA_MAX_MAPPINGS; i++) {
		if (dev->mappings[i].in_use == 0) {
			struct osdep_dma_mapping *m = &dev->mappings[i];
			m->in_use = 1;
			m->pinned = 0;
			m->resource_id = dev->next_resource_id++;
			m->orig_nents = 0u;
			m->nents = 0u;
			return m;
		}
	}
	return 0;
}

/* Core scatter map shared by the two return-contract wrappers below. */
static int
map_sg_core(struct osdep_dma_device *dev, const struct osdep_sg_entry *in,
	    unsigned orig_nents, enum osdep_dma_dir dir, struct osdep_dma_mapping **out_m)
{
	struct osdep_dma_mapping *m;
	int produced;
	unsigned i;

	*out_m = 0;
	tr(dev, OSDEP_TR_ENTRY, "dma_map_sg", orig_nents, (uint64_t)dir);

	if (in == 0 || orig_nents == 0u || orig_nents > OSDEP_DMA_MAX_SEGMENTS)
		return -OSDEP_EINVAL;
	if (dev->backend->map_sg == 0) {
		tr(dev, OSDEP_TR_UNIMPL, "dma_map_sg", 0u, 0u);
		return -OSDEP_EOPNOTSUPP;
	}

	m = alloc_mapping(dev);
	if (m == 0)
		return -OSDEP_ENOMEM;

	produced = dev->backend->map_sg(dev->priv, in, orig_nents,
					m->segs, OSDEP_DMA_MAX_SEGMENTS, dir);
	if (produced <= 0) {
		/* Failure: release the slot; the caller must not proceed to bind/submit. */
		m->in_use = 0;
		tr(dev, OSDEP_TR_FAIL, "dma_map_sg", 0u, 0u);
		return -OSDEP_ENOMEM;
	}

	m->dir = dir;
	m->orig_nents = orig_nents;      /* remembered for unmap */
	m->nents = (unsigned)produced;   /* coalesced count for the hardware walk */
	for (i = 0u; i < m->nents; i++)
		tr(dev, OSDEP_TR_MAP, "dma_map_sg", in[i < orig_nents ? i : 0u].phys.value,
		   m->segs[i].addr.value);

	*out_m = m;
	tr(dev, OSDEP_TR_EXIT, "dma_map_sg", (uint64_t)m->nents, m->resource_id);
	return (int)m->nents;
}

int
osdep_dma_map_sg(struct osdep_dma_device *dev, const struct osdep_sg_entry *in,
		 unsigned orig_nents, enum osdep_dma_dir dir, struct osdep_dma_mapping **out_m)
{
	/* dma_map_sg contract: mapped count (>=1) on success, 0 on failure. */
	int rc = map_sg_core(dev, in, orig_nents, dir, out_m);
	return rc > 0 ? rc : 0;
}

int
osdep_dma_map_sgtable(struct osdep_dma_device *dev, const struct osdep_sg_entry *in,
		      unsigned orig_nents, enum osdep_dma_dir dir, struct osdep_dma_mapping **out_m)
{
	/* dma_map_sgtable contract: 0 on success, -errno on failure. */
	int rc = map_sg_core(dev, in, orig_nents, dir, out_m);
	if (rc > 0)
		return 0;
	return rc < 0 ? rc : -OSDEP_ENOMEM;
}

int
osdep_dma_unmap_sg(struct osdep_dma_device *dev, struct osdep_dma_mapping *m)
{
	if (m == 0 || m->in_use == 0)
		return -OSDEP_EINVAL;

	/* Lifetime safety: a mapping still pinned into a GPU page table is never freed. */
	if (m->pinned != 0) {
		tr(dev, OSDEP_TR_FAIL, "dma_unmap_sg", (uint64_t)(-OSDEP_EBUSY), m->resource_id);
		return -OSDEP_EBUSY;
	}

	if (dev->backend->unmap_sg != 0)
		dev->backend->unmap_sg(dev->priv, m->segs, m->nents, m->orig_nents, m->dir);
	tr(dev, OSDEP_TR_UNMAP, "dma_unmap_sg", m->orig_nents, m->resource_id);
	tr(dev, OSDEP_TR_RELEASE, "dma_mapping", m->resource_id, 0u);
	m->in_use = 0;
	return 0;
}

osdep_dma_addr_t
osdep_dma_map_page(struct osdep_dma_device *dev, osdep_cpu_phys_t phys,
		   uint32_t size, enum osdep_dma_dir dir)
{
	uint64_t d;

	tr(dev, OSDEP_TR_ENTRY, "dma_map_page", phys.value, size);
	if (dev->backend->map_page == 0) {
		tr(dev, OSDEP_TR_UNIMPL, "dma_map_page", 0u, 0u);
		return osdep_dma_addr(OSDEP_DMA_MAPPING_ERROR);
	}
	d = dev->backend->map_page(dev->priv, phys.value, size, dir);
	if (d == OSDEP_DMA_MAPPING_ERROR) {
		tr(dev, OSDEP_TR_FAIL, "dma_map_page", 0u, 0u);
		return osdep_dma_addr(OSDEP_DMA_MAPPING_ERROR);
	}
	tr(dev, OSDEP_TR_MAP, "dma_map_page", phys.value, d);
	return osdep_dma_addr(d);
}

void
osdep_dma_unmap_page(struct osdep_dma_device *dev, osdep_dma_addr_t addr,
		     uint32_t size, enum osdep_dma_dir dir)
{
	if (dev->backend->unmap_page != 0)
		dev->backend->unmap_page(dev->priv, addr.value, size, dir);
	tr(dev, OSDEP_TR_UNMAP, "dma_unmap_page", addr.value, 0u);
}

osdep_dma_addr_t
osdep_dma_pin(struct osdep_dma_device *dev, struct osdep_dma_mapping *m)
{
	if (m == 0 || m->in_use == 0)
		return osdep_dma_addr(OSDEP_DMA_MAPPING_ERROR);
	m->pinned++;
	tr(dev, OSDEP_TR_ACQUIRE, "dma_pin", m->resource_id, m->segs[0].addr.value);
	/* Only a mapped device address ever leaves here — never a CPU physical. */
	return m->segs[0].addr;
}

void
osdep_dma_unpin(struct osdep_dma_device *dev, struct osdep_dma_mapping *m)
{
	if (m == 0 || m->in_use == 0 || m->pinned == 0)
		return;
	m->pinned--;
	tr(dev, OSDEP_TR_RELEASE, "dma_pin", m->resource_id, 0u);
}

void
osdep_dma_sync_for_device(struct osdep_dma_device *dev, osdep_dma_addr_t addr,
			  uint32_t size, enum osdep_dma_dir dir)
{
	if (dev->backend->sync_for_device != 0)
		dev->backend->sync_for_device(dev->priv, addr.value, size, dir);
	tr(dev, OSDEP_TR_SYNC_DEV, "dma_sync_for_device", addr.value, size);
}

void
osdep_dma_sync_for_cpu(struct osdep_dma_device *dev, osdep_dma_addr_t addr,
		       uint32_t size, enum osdep_dma_dir dir)
{
	if (dev->backend->sync_for_cpu != 0)
		dev->backend->sync_for_cpu(dev->priv, addr.value, size, dir);
	tr(dev, OSDEP_TR_SYNC_CPU, "dma_sync_for_cpu", addr.value, size);
}

unsigned
osdep_dma_live_mappings(const struct osdep_dma_device *dev)
{
	unsigned i, n = 0u;

	for (i = 0u; i < OSDEP_DMA_MAX_MAPPINGS; i++)
		if (dev->mappings[i].in_use != 0)
			n++;
	return n;
}
