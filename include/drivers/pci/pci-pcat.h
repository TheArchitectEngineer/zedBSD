/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * PC/AT PCI host bridge.
 */
#ifndef KERN_DRIVERS_PCI_PCAT_H
#define KERN_DRIVERS_PCI_PCAT_H

int
drv_pci_pcat_init(void);

/*
 * Assigns the memory BARs the firmware left unassigned, once the ACPI
 * namespace is loaded, and attaches the functions that waited for them
 * (pci-pcat-assign.c, BUG-210).
 */
void
drv_pci_pcat_assign_deferred(void);

#endif
