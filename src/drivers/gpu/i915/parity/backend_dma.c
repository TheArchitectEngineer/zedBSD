/*
 * WS031 Linux-parity — real DMA backend.
 *
 * Binds the osdep DMA contract (osdep/dma.h) to zedBSD's drv_dma_* device.
 * set_info validates the requested mask against the DEVICE's actual DMA address
 * width (drv_dma_device_address_bits) — never a hardcoded host phys-bits value.
 * Mapping ops are not wired yet (GGTT/PPGTT is a later P2 step); left NULL so an
 * attempt records UNIMPL rather than fabricating a DMA address.
 */
#include "../internal.h"
#include <drivers/dma.h>
#include "osdep/dma.h"
#include "backend.h"

static int
b_set_info(void *priv, unsigned mask_bits, uint64_t max_segment)
{
	struct drv_dma_device *dma = priv;
	unsigned bits = drv_dma_device_address_bits(dma);

	(void)max_segment;
	/* The device must be able to address the requested width. */
	if (mask_bits == 0u || mask_bits > bits)
		return -22;   /* -EINVAL */
	return 0;
}

static const struct osdep_dma_backend parity_dma_backend_def = {
	"zedbsd-dma",
	0u,          /* address_bits: authoritative value comes from the device at runtime */
	0u,          /* max_segment: idem */
	0,           /* coherent: idem */
	b_set_info,
	0,           /* map_sg  (not wired yet) */
	0,           /* unmap_sg */
	0,           /* map_page */
	0,           /* unmap_page */
	0,           /* sync_for_device */
	0,           /* sync_for_cpu */
};

const struct osdep_dma_backend *
parity_dma_backend(void)
{
	return &parity_dma_backend_def;
}
