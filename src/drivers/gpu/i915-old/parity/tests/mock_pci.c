/* Mock PCI backend (test only) — see mock_pci.h. */
#include "mock_pci.h"

static uint8_t
m_read8(void *priv, unsigned off)
{
	struct mock_pci *m = (struct mock_pci *)priv;
	return (off < 256u) ? m->cfg[off] : 0xffu;
}

static uint16_t
m_read16(void *priv, unsigned off)
{
	struct mock_pci *m = (struct mock_pci *)priv;
	if (off + 1u >= 256u)
		return 0xffffu;
	return (uint16_t)(m->cfg[off] | ((uint16_t)m->cfg[off + 1u] << 8));
}

static uint32_t
m_read32(void *priv, unsigned off)
{
	struct mock_pci *m = (struct mock_pci *)priv;
	if (off + 3u >= 256u)
		return 0xffffffffu;
	return (uint32_t)m->cfg[off] | ((uint32_t)m->cfg[off + 1u] << 8) |
	       ((uint32_t)m->cfg[off + 2u] << 16) | ((uint32_t)m->cfg[off + 3u] << 24);
}

static void
m_write8(void *priv, unsigned off, uint8_t v)
{
	struct mock_pci *m = (struct mock_pci *)priv;
	m->write8_calls++;
	if (off < 256u)
		m->cfg[off] = v;
}

static void
m_write16(void *priv, unsigned off, uint16_t v)
{
	struct mock_pci *m = (struct mock_pci *)priv;
	m->write16_calls++;
	if (off + 1u < 256u) {
		m->cfg[off] = (uint8_t)(v & 0xffu);
		m->cfg[off + 1u] = (uint8_t)((v >> 8) & 0xffu);
	}
}

static void
m_write32(void *priv, unsigned off, uint32_t v)
{
	struct mock_pci *m = (struct mock_pci *)priv;
	m->write32_calls++;
	if (off + 3u < 256u) {
		m->cfg[off] = (uint8_t)(v & 0xffu);
		m->cfg[off + 1u] = (uint8_t)((v >> 8) & 0xffu);
		m->cfg[off + 2u] = (uint8_t)((v >> 16) & 0xffu);
		m->cfg[off + 3u] = (uint8_t)((v >> 24) & 0xffu);
	}
}

static int
m_alloc_msi_vector(void *priv)
{
	struct mock_pci *m = (struct mock_pci *)priv;

	m->alloc_calls++;
	if (m->msi_alloc_fail)
		return -5;               /* -EIO: resource allocation failed */
	if (m->msi_alloc_bad) {
		m->last_alloc_vector = 0x100;  /* data byte 0 -> message config will fail */
		return 0x100;
	}
	m->last_alloc_vector = m->msi_vector_next;
	return m->msi_vector_next++;
}

static void
m_free_msi_vector(void *priv, int vector)
{
	struct mock_pci *m = (struct mock_pci *)priv;

	m->free_calls++;
	m->last_free_vector = vector;
}

const struct osdep_pci_backend *
mock_pci_backend(void)
{
	static const struct osdep_pci_backend backend = {
		"mock-pci",
		m_read8, m_read16, m_read32,
		m_write8, m_write16, m_write32,
		m_alloc_msi_vector, m_free_msi_vector,
	};
	return &backend;
}

static void
base(struct mock_pci *m)
{
	unsigned i;

	for (i = 0u; i < 256u; i++)
		m->cfg[i] = 0u;
	m->write8_calls = m->write16_calls = m->write32_calls = 0;
	m->msi_alloc_fail = 0;
	m->msi_alloc_bad = 0;
	m->msi_vector_next = 0x21;
	m->alloc_calls = m->free_calls = 0;
	m->last_alloc_vector = -1;
	m->last_free_vector = -1;
	m->cfg[0x00] = 0x86; m->cfg[0x01] = 0x80;
	m->cfg[0x02] = 0xa8; m->cfg[0x03] = 0x46;
	m->cfg[0x04] = 0x00; m->cfg[0x05] = 0x00;      /* command = 0 */
	m->cfg[0x06] = 0x90; m->cfg[0x07] = 0x02;      /* status: cap list + RO bits */
}

static void
mem_bar(struct mock_pci *m, unsigned i, uint32_t addr)
{
	unsigned off = 0x10u + i * 4u;
	uint32_t v = addr & ~0xFu;                     /* bit0=0 -> memory space */
	m->cfg[off + 0u] = (uint8_t)(v & 0xffu);
	m->cfg[off + 1u] = (uint8_t)((v >> 8) & 0xffu);
	m->cfg[off + 2u] = (uint8_t)((v >> 16) & 0xffu);
	m->cfg[off + 3u] = (uint8_t)((v >> 24) & 0xffu);
}

static void
io_bar(struct mock_pci *m, unsigned i, uint32_t addr)
{
	unsigned off = 0x10u + i * 4u;
	uint32_t v = (addr & ~0x3u) | 0x1u;            /* bit0=1 -> IO space */
	m->cfg[off + 0u] = (uint8_t)(v & 0xffu);
	m->cfg[off + 1u] = (uint8_t)((v >> 8) & 0xffu);
	m->cfg[off + 2u] = (uint8_t)((v >> 16) & 0xffu);
	m->cfg[off + 3u] = (uint8_t)((v >> 24) & 0xffu);
}

static void
pm_cap(struct mock_pci *m, unsigned at, uint8_t next)
{
	m->cfg[at + 0u] = OSDEP_PCI_CAP_ID_PM;
	m->cfg[at + 1u] = next;
	m->cfg[at + 4u] = 0x03; m->cfg[at + 5u] = 0x00;   /* PMCSR: D3hot */
}

static void
msi_cap(struct mock_pci *m, unsigned at, uint8_t next)
{
	m->cfg[at + 0u] = OSDEP_PCI_CAP_ID_MSI;
	m->cfg[at + 1u] = next;
	/* message control: 64-bit capable, MSI disabled */
	m->cfg[at + 2u] = 0x80; m->cfg[at + 3u] = 0x00;
}

static void
pcie_cap(struct mock_pci *m, unsigned at)
{
	m->cfg[at + 0u] = OSDEP_PCI_CAP_ID_PCIE;
	m->cfg[at + 1u] = 0x00;
}

void
mock_pci_setup_full(struct mock_pci *m)
{
	base(m);
	m->cfg[0x34] = 0x40;
	pm_cap(m, 0x40u, 0x50u);
	msi_cap(m, 0x50u, 0x60u);
	pcie_cap(m, 0x60u);
	mem_bar(m, 0u, 0xdf000000u);   /* MEM-only device */
}

void
mock_pci_setup_no_msi(struct mock_pci *m)
{
	base(m);
	m->cfg[0x34] = 0x40;
	pm_cap(m, 0x40u, 0x60u);
	pcie_cap(m, 0x60u);
	mem_bar(m, 0u, 0xdf000000u);
}

void
mock_pci_setup_no_pm(struct mock_pci *m)
{
	base(m);
	m->cfg[0x34] = 0x50;
	msi_cap(m, 0x50u, 0x60u);
	pcie_cap(m, 0x60u);
	mem_bar(m, 0u, 0xdf000000u);
}

void
mock_pci_setup_io_and_mem(struct mock_pci *m)
{
	base(m);
	m->cfg[0x34] = 0x40;
	pm_cap(m, 0x40u, 0x50u);
	msi_cap(m, 0x50u, 0x60u);
	pcie_cap(m, 0x60u);
	mem_bar(m, 0u, 0xdf000000u);
	io_bar(m, 2u, 0x0000e000u);
}
