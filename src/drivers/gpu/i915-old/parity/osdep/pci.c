/* WS031 Linux-parity OS adaptation layer — PCI / PCIe (see pci.h). */
#include "pci.h"

#ifndef OSDEP_EINVAL
#define OSDEP_EINVAL 22
#define OSDEP_ENODEV 19
#define OSDEP_EIO     5
#endif

#define OSDEP_PCI_CAP_WALK_MAX 48u
#define OSDEP_MSI_ARCH_ADDR 0xFEE00000u   /* x86 arch MSI address base (destination-fixed) */

static void
tr(struct osdep_pci *p, uint16_t op, const char *what, uint64_t a0, uint64_t a1)
{
	if (p->trace != 0)
		osdep_trace_emit(p->trace, 0u, op, what, a0, a1);
}

void
osdep_pci_init(struct osdep_pci *p, const struct osdep_pci_backend *backend,
	       void *priv, struct osdep_trace *trace)
{
	p->backend = backend;
	p->priv = priv;
	p->trace = trace;
	p->enable_cnt = 0;
	p->saved_command = 0u;
	p->saved_valid = 0;
	p->bus_master = 0;
	p->msi_enabled = 0;
	p->msi_vector = -1;
}

uint8_t  osdep_pci_read8(struct osdep_pci *p, unsigned off)  { return p->backend->read8(p->priv, off); }
uint16_t osdep_pci_read16(struct osdep_pci *p, unsigned off) { return p->backend->read16(p->priv, off); }
uint32_t osdep_pci_read32(struct osdep_pci *p, unsigned off) { return p->backend->read32(p->priv, off); }
void osdep_pci_write8(struct osdep_pci *p, unsigned off, uint8_t v)  { p->backend->write8(p->priv, off, v); }
void osdep_pci_write16(struct osdep_pci *p, unsigned off, uint16_t v){ p->backend->write16(p->priv, off, v); }
void osdep_pci_write32(struct osdep_pci *p, unsigned off, uint32_t v){ p->backend->write32(p->priv, off, v); }

unsigned
osdep_pci_find_capability(struct osdep_pci *p, uint8_t cap_id)
{
	uint16_t status;
	unsigned off;
	unsigned guard;

	status = p->backend->read16(p->priv, OSDEP_PCI_STATUS);
	if ((status & OSDEP_PCI_STATUS_CAP_LIST) == 0u)
		return 0u;
	off = p->backend->read8(p->priv, OSDEP_PCI_CAP_PTR) & 0xFCu;
	for (guard = 0u; guard < OSDEP_PCI_CAP_WALK_MAX && off >= 0x40u; guard++) {
		uint8_t id = p->backend->read8(p->priv, off + 0u);
		uint8_t next = p->backend->read8(p->priv, off + 1u);
		if (id == cap_id)
			return off;
		off = next & 0xFCu;
	}
	return 0u;
}

int
osdep_pci_bar_kind(struct osdep_pci *p, unsigned i, uint64_t *addr)
{
	uint32_t bar;

	if (addr != 0)
		*addr = 0u;
	if (i >= OSDEP_PCI_BAR_COUNT)
		return OSDEP_PCI_RES_NONE;
	bar = p->backend->read32(p->priv, OSDEP_PCI_BAR0 + i * 4u);
	if (bar == 0u)
		return OSDEP_PCI_RES_NONE;
	if (bar & 0x1u) {
		if (addr != 0)
			*addr = bar & ~0x3u;
		return OSDEP_PCI_RES_IO;
	}
	if (addr != 0)
		*addr = bar & ~0xFu;
	return OSDEP_PCI_RES_MEM;
}

int
osdep_pci_set_power_state(struct osdep_pci *p, enum osdep_pci_power state)
{
	unsigned pm = osdep_pci_find_capability(p, OSDEP_PCI_CAP_ID_PM);
	uint16_t pmcsr;

	if (pm == 0u) {
		tr(p, OSDEP_TR_NOTE, "pci_set_power_no_pm_cap", (uint64_t)state, 0u);
		return 0;
	}
	pmcsr = p->backend->read16(p->priv, pm + OSDEP_PCI_PM_CTRL);
	pmcsr = (uint16_t)((pmcsr & ~OSDEP_PCI_PM_STATE_MASK) | ((uint16_t)state & OSDEP_PCI_PM_STATE_MASK));
	p->backend->write16(p->priv, pm + OSDEP_PCI_PM_CTRL, pmcsr);
	tr(p, OSDEP_TR_NOTE, "pci_set_power_state", (uint64_t)state, pm);
	return 0;
}

/* Enable decode only for the resource kinds the device actually has. */
static uint16_t
wanted_decode(struct osdep_pci *p)
{
	uint16_t want = 0u;
	unsigned i;

	for (i = 0u; i < OSDEP_PCI_BAR_COUNT; i++) {
		uint64_t addr;
		int kind = osdep_pci_bar_kind(p, i, &addr);
		if (kind == OSDEP_PCI_RES_IO && addr != 0u)
			want |= OSDEP_PCI_CMD_IO;
		else if (kind == OSDEP_PCI_RES_MEM && addr != 0u)
			want |= OSDEP_PCI_CMD_MEMORY;
	}
	return want;
}

int
osdep_pci_enable_device(struct osdep_pci *p)
{
	uint16_t cmd;
	uint16_t want;

	tr(p, OSDEP_TR_ENTRY, "pci_enable_device", (uint64_t)p->enable_cnt, 0u);
	if (p->enable_cnt++ > 0) {
		/* Reference held already: do not re-touch hardware, just count. */
		tr(p, OSDEP_TR_NOTE, "pci_enable_refcount", (uint64_t)p->enable_cnt, 0u);
		return 0;
	}

	(void)osdep_pci_set_power_state(p, OSDEP_PCI_D0);

	cmd = p->backend->read16(p->priv, OSDEP_PCI_COMMAND);
	if (!p->saved_valid) {
		p->saved_command = cmd;
		p->saved_valid = 1;
	}
	want = wanted_decode(p);
	cmd = (uint16_t)(cmd | want);   /* only the resources present, never a blanket IO|MEM */
	p->backend->write16(p->priv, OSDEP_PCI_COMMAND, cmd);

	tr(p, OSDEP_TR_ACQUIRE, "pci_device", want, cmd);
	tr(p, OSDEP_TR_EXIT, "pci_enable_device", 0u, 0u);
	return 0;
}

void
osdep_pci_disable_device(struct osdep_pci *p)
{
	uint16_t cmd;

	if (p->enable_cnt == 0)
		return;
	if (--p->enable_cnt > 0) {
		/* Other users remain: must NOT disable decode. */
		tr(p, OSDEP_TR_NOTE, "pci_disable_refcount", (uint64_t)p->enable_cnt, 0u);
		return;
	}
	cmd = p->backend->read16(p->priv, OSDEP_PCI_COMMAND);
	cmd = (uint16_t)(cmd & ~(OSDEP_PCI_CMD_IO | OSDEP_PCI_CMD_MEMORY));
	p->backend->write16(p->priv, OSDEP_PCI_COMMAND, cmd);
	tr(p, OSDEP_TR_RELEASE, "pci_device", 0u, cmd);
}

int
osdep_pci_is_enabled(const struct osdep_pci *p)
{
	return p->enable_cnt > 0;
}

void
osdep_pci_restore(struct osdep_pci *p)
{
	if (!p->saved_valid)
		return;
	p->backend->write16(p->priv, OSDEP_PCI_COMMAND, p->saved_command);
	tr(p, OSDEP_TR_RELEASE, "pci_device_restore", 0u, p->saved_command);
	p->enable_cnt = 0;
	p->bus_master = (p->saved_command & OSDEP_PCI_CMD_MASTER) ? 1 : 0;
}

void
osdep_pci_set_bus_master(struct osdep_pci *p, int on)
{
	uint16_t cmd = p->backend->read16(p->priv, OSDEP_PCI_COMMAND);

	if (on)
		cmd = (uint16_t)(cmd | OSDEP_PCI_CMD_MASTER);
	else
		cmd = (uint16_t)(cmd & ~OSDEP_PCI_CMD_MASTER);
	p->backend->write16(p->priv, OSDEP_PCI_COMMAND, cmd);
	p->bus_master = on ? 1 : 0;
	tr(p, on ? OSDEP_TR_ACQUIRE : OSDEP_TR_RELEASE, "pci_bus_master", (uint64_t)on, cmd);
}

/* --- MSI: low-level helpers underneath the parent setup --- */

static void
msi_write_message(struct osdep_pci *p, unsigned msi, uint16_t flags, uint32_t addr, uint16_t data)
{
	unsigned data_off = (flags & OSDEP_PCI_MSI_64BIT) ? OSDEP_PCI_MSI_DATA_64 : OSDEP_PCI_MSI_DATA_32;

	p->backend->write32(p->priv, msi + OSDEP_PCI_MSI_ADDR_LO, addr);
	if (flags & OSDEP_PCI_MSI_64BIT)
		p->backend->write32(p->priv, msi + OSDEP_PCI_MSI_ADDR_LO + 4u, 0u);
	p->backend->write16(p->priv, msi + data_off, data);
}

static void
msi_set_enable(struct osdep_pci *p, unsigned msi, int on)
{
	uint16_t flags = p->backend->read16(p->priv, msi + OSDEP_PCI_MSI_FLAGS);

	if (on)
		flags = (uint16_t)(flags | OSDEP_PCI_MSI_ENABLE);
	else
		flags = (uint16_t)(flags & ~OSDEP_PCI_MSI_ENABLE);
	p->backend->write16(p->priv, msi + OSDEP_PCI_MSI_FLAGS, flags);
}

int
osdep_pci_setup_msi(struct osdep_pci *p)
{
	unsigned msi = osdep_pci_find_capability(p, OSDEP_PCI_CAP_ID_MSI);
	uint16_t flags;
	int vector;
	uint16_t data;

	if (msi == 0u) {
		tr(p, OSDEP_TR_NOTE, "pci_msi_absent", 0u, 0u);
		return -OSDEP_ENODEV;   /* no MSI: caller uses INTx, not fatal */
	}
	if (p->backend->alloc_msi_vector == 0)
		return -OSDEP_ENODEV;

	/* 1) acquire the interrupt vector. */
	vector = p->backend->alloc_msi_vector(p->priv);
	if (vector < 0) {
		tr(p, OSDEP_TR_FAIL, "pci_msi_alloc", (uint64_t)vector, 0u);
		return vector;   /* nothing enabled, nothing to release */
	}

	/* 2) build the message FROM the allocated vector (never a fixed copy). */
	flags = p->backend->read16(p->priv, msi + OSDEP_PCI_MSI_FLAGS);
	data = (uint16_t)(vector & 0xFFu);
	if (data == 0u) {
		/* An invalid (zero) vector cannot form a message: release and fail. */
		if (p->backend->free_msi_vector != 0)
			p->backend->free_msi_vector(p->priv, vector);
		tr(p, OSDEP_TR_FAIL, "pci_msi_message", 0u, 0u);
		return -OSDEP_EIO;
	}
	msi_write_message(p, msi, flags, OSDEP_MSI_ARCH_ADDR, data);

	/* 3) enable only after the resource and message are in place. */
	msi_set_enable(p, msi, 1);
	p->msi_enabled = 1;
	p->msi_vector = vector;
	tr(p, OSDEP_TR_ACQUIRE, "pci_msi", (uint64_t)vector, msi);
	return 0;
}

void
osdep_pci_teardown_msi(struct osdep_pci *p)
{
	unsigned msi;

	if (!p->msi_enabled)
		return;
	msi = osdep_pci_find_capability(p, OSDEP_PCI_CAP_ID_MSI);
	if (msi != 0u)
		msi_set_enable(p, msi, 0);
	if (p->backend->free_msi_vector != 0 && p->msi_vector >= 0)
		p->backend->free_msi_vector(p->priv, p->msi_vector);
	tr(p, OSDEP_TR_RELEASE, "pci_msi", (uint64_t)p->msi_vector, 0u);
	p->msi_enabled = 0;
	p->msi_vector = -1;
}

int
osdep_pci_msi_enabled(const struct osdep_pci *p)
{
	return p->msi_enabled;
}
