/*
 * WS031 Linux-parity OS adaptation layer — PCI / PCIe.
 *
 * rev2 (M2 §1.1): the parent-API behaviour is connected, not just the bit ops.
 *
 *   - enable_device enables decode PER RESOURCE (IO bit only for an IO BAR, MEM
 *     bit only for a MEM BAR), mirroring pci_enable_resources(); it does not blindly
 *     OR both.  It also carries a reference count: N enables need N disables, and
 *     the last disable is the one that clears decode.
 *   - MSI is a setup, not a bit: allocate an interrupt vector, build the message
 *     from THAT vector (never a fixed copy), then enable; a mid-way failure frees
 *     what was taken and does not report the MSI as enabled.  The raw enable-bit
 *     write remains a low-level helper underneath.
 *   - a capability is operated only when the walk found it; an absent PM/MSI cap
 *     is a no-op / -ENODEV, never a fabricated write.
 */
#ifndef PARITY_OSDEP_PCI_H
#define PARITY_OSDEP_PCI_H

#include <stdint.h>
#include "trace.h"

#define OSDEP_PCI_COMMAND        0x04u
#define OSDEP_PCI_STATUS         0x06u
#define OSDEP_PCI_CMD_IO         0x0001u
#define OSDEP_PCI_CMD_MEMORY     0x0002u
#define OSDEP_PCI_CMD_MASTER     0x0004u
#define OSDEP_PCI_STATUS_CAP_LIST 0x0010u
#define OSDEP_PCI_CAP_PTR        0x34u
#define OSDEP_PCI_BAR0           0x10u   /* BARs 0..5 at 0x10,0x14,...,0x24 */
#define OSDEP_PCI_BAR_COUNT      6u

#define OSDEP_PCI_CAP_ID_PM      0x01u
#define OSDEP_PCI_CAP_ID_MSI     0x05u
#define OSDEP_PCI_CAP_ID_PCIE    0x10u
#define OSDEP_PCI_CAP_ID_MSIX    0x11u

#define OSDEP_PCI_PM_CTRL        0x04u
#define OSDEP_PCI_PM_STATE_MASK  0x0003u
#define OSDEP_PCI_MSI_FLAGS      0x02u   /* message control */
#define OSDEP_PCI_MSI_ENABLE     0x0001u
#define OSDEP_PCI_MSI_64BIT      0x0080u /* message control: 64-bit address capable */
#define OSDEP_PCI_MSI_ADDR_LO    0x04u
#define OSDEP_PCI_MSI_DATA_32    0x08u   /* data offset when 32-bit */
#define OSDEP_PCI_MSI_DATA_64    0x0Cu   /* data offset when 64-bit */

enum osdep_pci_power { OSDEP_PCI_D0 = 0, OSDEP_PCI_D3HOT = 3 };
enum osdep_pci_res { OSDEP_PCI_RES_NONE = 0, OSDEP_PCI_RES_IO, OSDEP_PCI_RES_MEM };

struct osdep_pci_backend {
	const char *name;
	uint8_t  (*read8)(void *priv, unsigned off);
	uint16_t (*read16)(void *priv, unsigned off);
	uint32_t (*read32)(void *priv, unsigned off);
	void (*write8)(void *priv, unsigned off, uint8_t v);
	void (*write16)(void *priv, unsigned off, uint16_t v);
	void (*write32)(void *priv, unsigned off, uint32_t v);
	/* Interrupt vector allocation for MSI.  Returns a vector >=0, or -errno. */
	int  (*alloc_msi_vector)(void *priv);
	void (*free_msi_vector)(void *priv, int vector);
};

struct osdep_pci {
	const struct osdep_pci_backend *backend;
	void *priv;
	struct osdep_trace *trace;

	int enable_cnt;          /* pci_enable_device reference count */
	uint16_t saved_command;  /* PCI_COMMAND before first enable */
	int saved_valid;
	int bus_master;

	int msi_enabled;
	int msi_vector;
};

void osdep_pci_init(struct osdep_pci *p, const struct osdep_pci_backend *backend,
		    void *priv, struct osdep_trace *trace);

uint8_t  osdep_pci_read8(struct osdep_pci *p, unsigned off);
uint16_t osdep_pci_read16(struct osdep_pci *p, unsigned off);
uint32_t osdep_pci_read32(struct osdep_pci *p, unsigned off);
void osdep_pci_write8(struct osdep_pci *p, unsigned off, uint8_t v);
void osdep_pci_write16(struct osdep_pci *p, unsigned off, uint16_t v);
void osdep_pci_write32(struct osdep_pci *p, unsigned off, uint32_t v);

unsigned osdep_pci_find_capability(struct osdep_pci *p, uint8_t cap_id);

/* Classify BAR i; returns enum osdep_pci_res, and *addr (0 if unassigned). */
int osdep_pci_bar_kind(struct osdep_pci *p, unsigned i, uint64_t *addr);

int osdep_pci_set_power_state(struct osdep_pci *p, enum osdep_pci_power state);

/*
 * enable_device: on the 0->1 reference transition, drive D0 and enable decode
 * for the resources the device actually has (IO/MEM per BAR).  Later enables
 * only bump the count.  Returns 0 / -errno.
 */
int osdep_pci_enable_device(struct osdep_pci *p);
/* Drop one reference; the last one clears decode. */
void osdep_pci_disable_device(struct osdep_pci *p);
int osdep_pci_is_enabled(const struct osdep_pci *p);

void osdep_pci_restore(struct osdep_pci *p);
void osdep_pci_set_bus_master(struct osdep_pci *p, int on);

/*
 * MSI setup: allocate a vector, build the message from it, enable.  Returns 0 on
 * success, -ENODEV if there is no MSI capability (caller uses INTx; not fatal),
 * or another -errno if allocation/config failed (nothing left enabled).  This is
 * the P2 MSI resource setup; installing the IRQ handler is a separate P4 step.
 */
int osdep_pci_setup_msi(struct osdep_pci *p);
void osdep_pci_teardown_msi(struct osdep_pci *p);
int osdep_pci_msi_enabled(const struct osdep_pci *p);

#endif /* PARITY_OSDEP_PCI_H */
