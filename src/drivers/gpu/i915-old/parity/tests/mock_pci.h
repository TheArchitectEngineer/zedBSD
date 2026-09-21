/* Mock PCI backend (test only) — 256-byte config space + MSI vector model. */
#ifndef PARITY_TESTS_MOCK_PCI_H
#define PARITY_TESTS_MOCK_PCI_H

#include "../osdep/pci.h"

struct mock_pci {
	uint8_t cfg[256];
	int write8_calls, write16_calls, write32_calls;
	/* MSI vector allocation model */
	int msi_alloc_fail;    /* alloc returns -EIO */
	int msi_alloc_bad;     /* alloc returns a vector whose data byte is 0 (config fails) */
	int msi_vector_next;   /* next vector to hand out */
	int alloc_calls;
	int free_calls;
	int last_alloc_vector;
	int last_free_vector;
};

const struct osdep_pci_backend *mock_pci_backend(void);

/* Config presets (all include a MEM BAR0 unless noted). */
void mock_pci_setup_full(struct mock_pci *m);      /* PM@0x40, MSI@0x50, PCIe@0x60, MEM BAR0 */
void mock_pci_setup_no_msi(struct mock_pci *m);
void mock_pci_setup_no_pm(struct mock_pci *m);
void mock_pci_setup_io_and_mem(struct mock_pci *m);/* MEM BAR0 + IO BAR2 */

#endif
