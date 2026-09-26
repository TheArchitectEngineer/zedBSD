/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Broadcom STB PCIe root complex (BCM2711).
 *
 * The Raspberry Pi 4 reaches its USB controller through this root complex.
 * The firmware leaves it in no particular state, so the driver resets it,
 * trains the link, opens the memory windows, numbers the bus below the root
 * port and places the endpoints' BARs before the PCI core enumerates it.
 */

#ifndef KERN_DRIVERS_PCI_BRCMSTB_H
#define KERN_DRIVERS_PCI_BRCMSTB_H

#include <stdint.h>

#include <drivers/generic/fdt.h>
#include <drivers/pci/pci.h>

/* The legacy interrupt pins INTA to INTD. */
#define DRV_PCI_BRCMSTB_INTX_COUNT	4U

struct drv_pci_brcmstb;

/*
 * Where one root complex sits and what it decodes.
 *
 * The platform fills it from its device tree.  Addresses named cpu are CPU
 * physical addresses; those named pci are addresses on the PCI bus.  The
 * outbound window carries the CPU's accesses to device BARs; the inbound
 * window carries device DMA to system memory.  The interrupt numbers are the
 * kernel's numbers for the lines INTA to INTD, zero where a pin is absent.
 */
struct drv_pci_brcmstb_config {
	uint64_t register_base;
	uint64_t register_size;
	uint64_t outbound_cpu_base;
	uint64_t outbound_pci_base;
	uint64_t outbound_size;
	uint64_t inbound_cpu_base;
	uint64_t inbound_pci_base;
	uint64_t inbound_size;
	unsigned intx_irq[DRV_PCI_BRCMSTB_INTX_COUNT];
};

int
drv_pci_brcmstb_describe(
	const struct drv_fdt *fdt,
	uint32_t node,
	struct drv_pci_brcmstb_config *config);

int
drv_pci_brcmstb_start(
	const struct drv_pci_brcmstb_config *config,
	struct drv_pci_brcmstb **result);

int
drv_pci_brcmstb_config_read(
	struct drv_pci_brcmstb *host,
	const struct drv_pci_address *address,
	unsigned offset,
	unsigned width,
	uint32_t *value);

int
drv_pci_brcmstb_reassign(
	struct drv_pci_brcmstb *host);

int
drv_pci_brcmstb_publish(
	struct drv_pci_brcmstb *host);

#endif
