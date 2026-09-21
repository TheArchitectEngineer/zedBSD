/*
 * WS031 Linux-parity — real PCI backend.
 *
 * Connects the osdep PCI contract (osdep/pci.h) to zedBSD's low-level config
 * accessors.  It is only the OS boundary: the enable/decode/cap-walk/MSI LOGIC
 * lives in the portable layer.  It does NOT call the legacy i915 attach.
 */
#include "../internal.h"
#include "osdep/pci.h"
#include "backend.h"
#include <hal/hal.h>

static uint8_t
b_read8(void *priv, unsigned off)
{
	struct parity_pci_priv *p = priv;
	uint8_t v = 0xffu;
	(void)drv_pci_device_config_read8(p->pci, off, &v);
	return v;
}

static uint16_t
b_read16(void *priv, unsigned off)
{
	struct parity_pci_priv *p = priv;
	uint16_t v = 0xffffu;
	(void)drv_pci_device_config_read16(p->pci, off, &v);
	return v;
}

static uint32_t
b_read32(void *priv, unsigned off)
{
	struct parity_pci_priv *p = priv;
	uint32_t v = 0xffffffffu;
	(void)drv_pci_device_config_read32(p->pci, off, &v);
	return v;
}

static void
b_write8(void *priv, unsigned off, uint8_t v)
{
	struct parity_pci_priv *p = priv;
	(void)drv_pci_device_config_write8(p->pci, off, v);
}

static void
b_write16(void *priv, unsigned off, uint16_t v)
{
	struct parity_pci_priv *p = priv;
	(void)drv_pci_device_config_write16(p->pci, off, v);
}

static void
b_write32(void *priv, unsigned off, uint32_t v)
{
	struct parity_pci_priv *p = priv;
	(void)drv_pci_device_config_write32(p->pci, off, v);
}

/*
 * MSI resource split (C3): allocate the vector WITHOUT a handler; the handler is
 * attached later (P4).  The message address/data come from the HAL allocation and
 * the portable osdep layer programs them into the PCI capability.
 */
static void
format_msi_source(char *out, const struct drv_pci_address *a)
{
	static const char hex[] = "0123456789abcdef";

	out[0] = 'P'; out[1] = 'C'; out[2] = 'I'; out[3] = ' ';
	out[4] = hex[(a->segment >> 12) & 0xfu];
	out[5] = hex[(a->segment >> 8) & 0xfu];
	out[6] = hex[(a->segment >> 4) & 0xfu];
	out[7] = hex[a->segment & 0xfu];
	out[8] = ':';
	out[9] = hex[(a->bus >> 4) & 0xfu];
	out[10] = hex[a->bus & 0xfu];
	out[11] = ':';
	out[12] = hex[(a->device >> 4) & 0xfu];
	out[13] = hex[a->device & 0xfu];
	out[14] = '.';
	out[15] = hex[a->function & 0xfu];
	out[16] = '\0';
}

static int
b_alloc_msi_vector(void *priv)
{
	struct parity_pci_priv *p = priv;
	struct drv_pci_address addr;
	int irq = -1;
	hal_physaddr_t msg_addr = 0;
	uint32_t msg_event = 0;

	drv_pci_device_address(p->pci, &addr);
	format_msi_source(p->msi_source, &addr);

	/* Reserve the vector/routing/message; do NOT attach a handler here. */
	if (hal_irq_alloc_msi(p->msi_source, &irq, &msg_addr, &msg_event) != HAL_OK)
		return -1;
	p->msi_irq = irq;

	/* The osdep layer builds the PCI message DATA from this returned vector. */
	return (int)(msg_event & 0xffu);
}

static void
b_free_msi_vector(void *priv, int vector)
{
	struct parity_pci_priv *p = priv;

	(void)vector;
	/* The caller (osdep_pci_teardown_msi) has already disabled PCI MSI = source stopped. */
	if (p->msi_irq >= 0) {
		(void)hal_irq_free_msi(p->msi_irq);
		p->msi_irq = -1;
	}
}

/*
 * MSI vector allocation is the resource-only half of the C3 split:
 * b_alloc_msi_vector reserves a HAL vector with NO handler (attached at P4),
 * and b_free_msi_vector releases it after the source has been stopped.
 */
static const struct osdep_pci_backend parity_pci_backend_def = {
	"zedbsd-pci",
	b_read8, b_read16, b_read32,
	b_write8, b_write16, b_write32,
	b_alloc_msi_vector,
	b_free_msi_vector,
};

const struct osdep_pci_backend *
parity_pci_backend(void)
{
	return &parity_pci_backend_def;
}
