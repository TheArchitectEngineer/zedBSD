/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Raspberry Pi 4 PCIe: the BCM2711 root complex and what sits behind it.
 *
 * The firmware's device tree says where the controller is and whether it is
 * present; QEMU's raspi4b marks it disabled because it does not emulate it.
 * A missing or disabled controller, and a link that does not come up, leave
 * the machine without PCI devices but do not stop the boot.
 */

#include "drivers/platform/rpi4/rpi4-pcie.h"
#include <drivers/generic/fdt.h>
#include <drivers/pci/pci.h>
#include <drivers/pci/pci-brcmstb.h>
#include <kern/klog.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/*
 * How much of memory at the device tree's address may be read.
 *
 * The HAL checks the tree against the same bound before the kernel starts,
 * and the tree's header states its real size inside it.
 */
#define RPI4_FDT_READ_LIMIT	(2U * 1024U * 1024U)

/*
 * Starts the PCI core and the BCM2711 root complex.
 */
int
drv_rpi4_pcie_init(
	uint64_t fdt_phys)
{
	struct drv_pci_brcmstb_config config;
	struct drv_pci_brcmstb *host;
	struct drv_fdt fdt;
	const void *blob;
	uint32_t node;
	int error;

	/* Brings the PCI core into service, even when no controller follows. */
	error = drv_pci_init();
	if (error != 0 && error != EALREADY)
		return error;

	/* Finds the device tree in the direct map. */
	blob = kern_pmem_to_kernel((hal_physaddr_t)fdt_phys);
	if (blob == NULL)
		return ENODEV;

	/* Refuses a tree whose header does not check out. */
	error = drv_fdt_open(&fdt, blob, RPI4_FDT_READ_LIMIT);
	if (error != 0) {
		kern_logf("pcie: unreadable device tree (%d)\n", error);
		return error;
	}

	/* Finds the controller; a board without one has nothing to do. */
	error = drv_fdt_find_compatible(&fdt, "brcm,bcm2711-pcie", DRV_FDT_NO_NODE, &node);
	if (error != 0)
		return ENODEV;

	/* Reads the controller's windows and interrupts; a disabled one is skipped. */
	error = drv_pci_brcmstb_describe(&fdt, node, &config);
	if (error == ENODEV) {
		kern_logf("pcie: controller disabled in the device tree\n");
		return ENODEV;
	}

	/* Reports a node that does not describe a usable controller. */
	if (error != 0) {
		kern_logf("pcie: unusable device tree node (%d)\n", error);
		return error;
	}

	/* Resets the controller, trains the link and assigns the bus. */
	error = drv_pci_brcmstb_start(&config, &host);
	if (error != 0) {
		kern_logf("pcie: controller start failed (%d)\n", error);
		return error;
	}

	/* Enumerates the bus, which binds the registered drivers. */
	error = drv_pci_brcmstb_publish(host);
	if (error != 0) {
		kern_logf("pcie: enumeration failed (%d)\n", error);
		return error;
	}

	/* Succeeded: the PCI core owns the controller's tree. */
	return 0;
}
