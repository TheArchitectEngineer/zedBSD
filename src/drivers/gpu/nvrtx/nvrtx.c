/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The nvrtx PCI driver: registration and attach.
 *
 * nvrtx drives NVIDIA GPUs from Turing on through the GSP firmware
 * (plan/ws037/nvrtx-design.md).  This revision has only the stages that read:
 * the attach finds the GPU, reads its chip identity (N0) and records where the
 * firmware's picture lies (N1), then gives the function back to the firmware
 * by refusing the bind.  The firmware's console keeps working, the PCI core
 * never suspends the function, and a machine without an NVIDIA GPU is not
 * touched at all.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/pci/pci-nvrtx.h>
#include <drivers/pci/pci.h>
#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-private.h"

static int nvrtx_attach(struct drv_pci_device *pci, const struct drv_pci_id *id);
static int nvrtx_detach(struct drv_pci_device *pci, unsigned flags);

/*
 * Registers the nvrtx PCI driver for NVIDIA display-class functions.
 *
 * Every NVIDIA function of the display class is offered to the attach, which
 * names the chip from its identity register; the device ID only names the
 * product.  The audio, USB and USB-C functions of the same card are of other
 * classes and are not matched.
 */
int
drv_pci_nvrtx_driver_register(void)
{
	static const struct drv_pci_id identifiers[] = {
		{ NVRTX_PCI_VENDOR, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID,
		  NVRTX_PCI_CLASS_DISPLAY, NVRTX_PCI_CLASS_DISPLAY_MASK, 0U }
	};
	static struct drv_pci_driver driver = {
		"nvrtx",
		identifiers,
		sizeof(identifiers) / sizeof(identifiers[0]),
		NULL,
		nvrtx_attach,
		nvrtx_detach,
		NULL,
		NULL,
		NULL,
		{ 0U, 0U, 0U, 0U }
	};
	int error;

	/* Hands probing and binding to PCI. */
	error = drv_pci_driver_register(&driver);
	if (error != 0)
		return error;

	/* Succeeded: matching functions are offered to the attach during PCI scanning. */
	return 0;
}

/*
 * Runs the read-only stages on one function and gives it back.
 *
 * The attach always refuses the bind in this revision: the stages that would
 * keep the function (the GSP boot, nvrtx.gsp=1) are not built yet, so the
 * function stays with the firmware whatever N0 found.
 */
static int
nvrtx_attach(
	struct drv_pci_device *pci,
	const struct drv_pci_id *id)
{
	struct nvrtx_device device;
	bool off;
	bool allowed;
	bool gsp_wanted;
	int error;

	UNUSED_PARAMETER(id);

	/* Stays away from the GPU when the boot says so. */
	off = nvrtx_stage_driver_off();
	if (off)
		return ENODEV;

	/* Starts with nothing taken. */
	kern_memset(&device, 0, sizeof(device));
	device.pci = pci;
	drv_pci_device_address(pci, &device.address);
	device.product = drv_pci_device_product(pci);

	/* Stops before N0 when the boot asks. */
	allowed = nvrtx_stage_allowed("N0");
	if (!allowed) {
		nvrtx_stage_mark("N0 skipped (nvrtx.stop=N0)");
		return ENODEV;
	}

	/* Finds the GPU and reads its identity. */
	error = nvrtx_n0_discover(&device);
	if (error != 0) {
		nvrtx_n0_release(&device);
		return ENODEV;
	}

	/* Records the firmware's picture unless the boot stops before N1. */
	allowed = nvrtx_stage_allowed("N1");
	if (allowed) {
		nvrtx_n1_readout(&device);
	} else {
		nvrtx_stage_mark("N1 skipped (nvrtx.stop=N1)");
	}

	/* Gives the registers and the command bits back before saying what happens next. */
	nvrtx_n0_release(&device);

	/* Says why the function stays with the firmware. */
	gsp_wanted = nvrtx_stage_gsp_wanted();
	if (!gsp_wanted) {
		nvrtx_stage_mark("N1 done; %s left to the firmware (no nvrtx.gsp=1)",
		    device.chip.name);
	} else if (device.chip.gsp_tu102 == 0) {
		nvrtx_stage_mark("N1 done; no GSP path for %s yet", device.chip.name);
	} else {
		nvrtx_stage_mark("N1 done; the GSP boot (P0) is not in this kernel");
	}

	/* Refuses the bind: the function keeps its firmware state and is never suspended by PCI. */
	return ENODEV;
}

/* Detaches nothing: the attach never binds in this revision. */
static int
nvrtx_detach(
	struct drv_pci_device *pci,
	unsigned flags)
{
	UNUSED_PARAMETER(pci);
	UNUSED_PARAMETER(flags);

	/* Succeeded: no state was kept. */
	return 0;
}
