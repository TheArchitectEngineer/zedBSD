/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PCIe binding of GHCI: a display-class function whose BAR0 holds
 * the register window (identified by GHCI_MAGIC at offset 0, not by its
 * IDs), whose optional BAR2 is the window onto the GPU's local memory,
 * and whose one interrupt is a message (MSI-X or MSI) or the INTx pin.
 * The GPU makes no memory access until bus mastering is on and the
 * aperture is set.
 *
 * A host that offers none of the three (the VisionFive 2's PLDA root
 * complexes take no message, and a function may have no INTx pin), or a
 * kernel built with CONFIG_GHCI_POLL, leaves the device polled.  A
 * window BAR the host did not place, or could not map, leaves the device
 * without a local window, which the driver does not use anyway.  A bus
 * whose DMA does not snoop the processor's caches, or a kernel built with
 * CONFIG_GHCI_HOST_UNCACHED, has the processor reach host memory through
 * uncached views.
 *
 * Values referenced: GHCI 1.0 (draft) section 15.2 (the PCIe binding:
 * the class codes, BAR0, BAR2 and the interrupt).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/generic/dma.h>
#include <drivers/pci/pci.h>
#include <drivers/pci/pci-ghci.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "ghci.h"
#include "hcr.h"

/*
 * The functions offered to the driver: base class 03h with subclass 02h
 * (3D controller) or 80h (another display controller), any vendor.
 */
#define CLASS_3D			0x030200U
#define CLASS_DISPLAY_OTHER		0x038000U
#define CLASS_MASK			0xffff00U

/* The register window's BAR and the local window's BAR. */
#define BAR_REGISTERS			0U
#define BAR_WINDOW			2U

/* The register window: 128 KiB, of which a function may implement only the first 64 KiB. */
#define REGISTER_BYTES_MAX		0x20000U
#define REGISTER_BYTES_MIN		0x10000U

/* The largest local window the driver maps. */
#define WINDOW_BYTES_MAX		(1ULL << 30)

static int ghci_pci_match(struct drv_pci_device *pci, const struct drv_pci_id *id);
static int ghci_pci_attach(struct drv_pci_device *pci, const struct drv_pci_id *id);
static int ghci_pci_detach(struct drv_pci_device *pci, unsigned flags);
static void ghci_pci_shutdown(struct drv_pci_device *pci);
static int map_registers(struct ghci_device *device);
static void map_window(struct ghci_device *device);
static int take_interrupt(struct ghci_device *device);
static void choose_host_view(struct ghci_device *device);
static void release_bus(struct ghci_device *device);

/*
 * Registers the GHCI PCI driver for 3D and other display controllers.
 *
 * The driver matches by class only, so a vendor's own driver of the same
 * function wins the match; the attach then keeps only a function whose
 * BAR0 says GHCI.
 */
int
drv_pci_ghci_driver_register(void)
{
	static const struct drv_pci_id identifiers[] = {
		{ DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, CLASS_3D, CLASS_MASK, 0U },
		{ DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, CLASS_DISPLAY_OTHER, CLASS_MASK, 0U }
	};
	static struct drv_pci_driver driver = {
		"ghci",
		identifiers,
		sizeof(identifiers) / sizeof(identifiers[0]),
		ghci_pci_match,
		ghci_pci_attach,
		ghci_pci_detach,
		ghci_pci_shutdown,
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

/* Matches by class, below any driver that knows the function's vendor. */
static int
ghci_pci_match(
	struct drv_pci_device *pci,
	const struct drv_pci_id *id)
{
	(void)pci;
	(void)id;

	/* Reports the weakest match that still binds. */
	return DRV_PCI_MATCH_CLASS;
}

/* Maps the function, checks that it is GHCI, and starts the device. */
static int
ghci_pci_attach(
	struct drv_pci_device *pci,
	const struct drv_pci_id *id)
{
	struct ghci_device *device;
	uint32_t magic;
	int error;

	(void)id;

	/* The device's record, which lives until the detach. */
	device = kern_calloc(1U, sizeof(*device));
	if (device == NULL)
		return ENOMEM;

	/* The record belongs to the function. */
	device->pci = pci;

	/* The register window, which must say GHCI before anything else is touched. */
	error = map_registers(device);
	if (error != 0) {
		release_bus(device);
		kern_free(device);
		return error;
	}

	/* A function that is not GHCI is left as it was. */
	magic = ghci_read(device, GHCI_MAGIC);
	if (magic != GHCI_MAGIC_VALUE) {
		release_bus(device);
		kern_free(device);
		return ENODEV;
	}

	/* The local window, when the function has one the host placed. */
	map_window(device);

	/* How the processor reaches the host memory the GPU uses. */
	choose_host_view(device);

	/* The interrupt, before the GPU can raise one, or the poll. */
	error = take_interrupt(device);
	if (error != 0) {
		release_bus(device);
		kern_free(device);
		return error;
	}

	/* The GPU may now reach memory; the aperture still keeps it off until the start sets it. */
	error = drv_pci_device_set_bus_master(pci, true);
	if (error != 0) {
		release_bus(device);
		kern_free(device);
		return error;
	}

	/* The record is the function's from here. */
	error = drv_pci_device_set_driver_data(pci, device);
	if (error != 0) {
		release_bus(device);
		kern_free(device);
		return error;
	}

	/* Starts the device: identify, reset, memory, ring, display, registration. */
	error = ghci_device_start(device);
	if (error != 0) {
		kern_logf("ghci: function not started (%d)\n", error);
		(void)drv_pci_device_set_driver_data(pci, NULL);
		release_bus(device);
		kern_free(device);
		return error;
	}

	/* Succeeded: the function is a GHCI device. */
	return 0;
}

/* Stops the device and gives the function back; EBUSY while a session is open. */
static int
ghci_pci_detach(
	struct drv_pci_device *pci,
	unsigned flags)
{
	struct ghci_device *device;
	int error;

	(void)flags;

	/* A function without a started device has nothing to stop. */
	device = drv_pci_device_driver_data(pci);
	if (device == NULL)
		return 0;

	/* The device stops only when no session is open, whatever the flags say. */
	error = ghci_device_stop(device);
	if (error != 0)
		return error;

	/* The bus resources and the record go. */
	(void)drv_pci_device_set_driver_data(pci, NULL);
	release_bus(device);
	kern_free(device);

	/* Succeeded: the function is free. */
	return 0;
}

/* Stops the GPU's memory accesses before the machine restarts. */
static void
ghci_pci_shutdown(
	struct drv_pci_device *pci)
{
	struct ghci_device *device;

	/* A function without a started device does nothing. */
	device = drv_pci_device_driver_data(pci);
	if (device == NULL)
		return;

	/* No interrupt, the processor halted, the display off, no bus mastering. */
	ghci_write(device, GHCI_IRQ_ENABLE, 0U);
	ghci_write(device, GHCI_CTRL, GHCI_CTRL_HALT);
	ghci_display_detach(device);
	(void)drv_pci_device_set_bus_master(pci, false);
}

/* Claims and maps BAR0, the register window. */
static int
map_registers(
	struct ghci_device *device)
{
	struct drv_pci_bar bar;
	int error;

	/* BAR0 must be memory of at least 64 KiB. */
	error = drv_pci_device_bar(device->pci, BAR_REGISTERS, &bar);
	if (error != 0)
		return ENODEV;
	if (bar.type != DRV_PCI_BAR_MEMORY32 && bar.type != DRV_PCI_BAR_MEMORY64)
		return ENODEV;
	if (bar.size < REGISTER_BYTES_MIN)
		return ENODEV;

	/* The registers decode from here. */
	error = drv_pci_device_enable_memory(device->pci);
	if (error != 0)
		return error;

	/* No other driver maps the window. */
	error = drv_pci_device_claim_bar(device->pci, BAR_REGISTERS);
	if (error != 0)
		return error;

	/* Maps up to the 128 KiB GHCI decodes, uncached. */
	device->register_bytes = REGISTER_BYTES_MAX;
	if (bar.size < REGISTER_BYTES_MAX)
		device->register_bytes = (uint32_t)bar.size;
	error = drv_pci_device_map_bar_region(device->pci,
	    BAR_REGISTERS,
	    0U,
	    device->register_bytes,
	    DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
	    &device->register_map);
	if (error != 0) {
		drv_pci_device_release_bar(device->pci, BAR_REGISTERS);
		return error;
	}

	/* The registers are reached through the mapping from here. */
	device->registers = device->register_map.address;

	/* Succeeded: the registers are mapped. */
	return 0;
}

/*
 * Claims and maps BAR2, the local window, when there is one.
 *
 * The window is optional to the driver, which does not use it: a BAR2
 * the host left unplaced (it did not fit the host's window) or could
 * not map leaves the device without one, and its local memory is the
 * GPU's alone.
 */
static void
map_window(
	struct ghci_device *device)
{
	struct drv_pci_bar bar;
	uint64_t bytes;
	int error;

	/* A function without a memory BAR2 has no window. */
	device->window = NULL;
	device->window_bytes = 0U;
	error = drv_pci_device_bar(device->pci, BAR_WINDOW, &bar);
	if (error != 0)
		return;
	if (bar.type != DRV_PCI_BAR_MEMORY32 && bar.type != DRV_PCI_BAR_MEMORY64)
		return;
	if (bar.size == 0U)
		return;

	/* A BAR the host did not place decodes nothing. */
	if (bar.bus_address == 0U) {
		kern_logf("ghci: BAR2 (%llu MiB) not placed; no local window\n", (unsigned long long)(bar.size >> 20));
		return;
	}

	/* No other driver maps the window. */
	error = drv_pci_device_claim_bar(device->pci, BAR_WINDOW);
	if (error != 0) {
		kern_logf("ghci: BAR2 not claimed (%d); no local window\n", error);
		return;
	}

	/*
	 * Maps it uncached, so a write reaches the GPU's memory in program
	 * order and before the doorbell that follows it.
	 */
	bytes = bar.size;
	if (bytes > WINDOW_BYTES_MAX)
		bytes = WINDOW_BYTES_MAX;
	error = drv_pci_device_map_bar_region(device->pci,
	    BAR_WINDOW,
	    0U,
	    (size_t)bytes,
	    DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
	    &device->window_map);
	if (error != 0) {
		kern_logf("ghci: BAR2 not mapped (%d); no local window\n", error);
		drv_pci_device_release_bar(device->pci, BAR_WINDOW);
		return;
	}

	/* The processor's view of the window and its physical place, for user mappings. */
	device->window = device->window_map.address;
	device->window_physical = device->window_map.physical_address;
	device->window_bytes = bytes;
}

/*
 * Decides whether the processor reaches host memory through uncached
 * views: when the bus's DMA does not snoop the caches, or when the kernel
 * is built to.
 */
static void
choose_host_view(
	struct ghci_device *device)
{
	struct drv_dma_device *dma;
	int coherent;

	/* The bus's DMA: a function without one is taken as coherent, as on the PC. */
	coherent = 1;
	dma = drv_pci_device_dma(device->pci);
	if (dma != NULL)
		coherent = drv_dma_device_is_coherent(dma);

	/* A coherent bus shares the direct map, unless the kernel says otherwise. */
	device->host_uncached = 0;
	if (!coherent) {
		device->host_uncached = 1;
	} else if (CONFIG_GHCI_HOST_UNCACHED) {
		device->host_uncached = 1;
	}

	/* Says which view the processor uses. */
	if (device->host_uncached)
		kern_logf("ghci: host memory reached uncached (bus DMA %s)\n", coherent ? "coherent" : "not coherent");
}

/*
 * Takes one interrupt, a message where the function offers one, and puts
 * the handler on it; a host that offers none, or a kernel built to poll,
 * leaves the device polled.
 */
static int
take_interrupt(
	struct ghci_device *device)
{
	int error;

	/* A kernel built to poll takes no interrupt. */
	device->polled = 0;
	if (CONFIG_GHCI_POLL) {
		kern_logf("ghci: polled every %u ms (CONFIG_GHCI_POLL)\n", (unsigned)GHCI_POLL_MS);
		device->polled = 1;
		return 0;
	}

	/* One vector: MSI-X, MSI, or the INTx pin. */
	error = drv_pci_device_allocate_irqs(device->pci,
	    DRV_PCI_IRQ_ALLOW_MSIX | DRV_PCI_IRQ_ALLOW_MSI | DRV_PCI_IRQ_ALLOW_INTX,
	    1U,
	    1U,
	    &device->irq,
	    &device->irq_count);
	if (error == ENOTSUP || error == ENODEV) {
		/* No message and no pin: the poll stands in for the interrupt. */
		kern_logf("ghci: no interrupt offered (%d); polled every %u ms\n", error, (unsigned)GHCI_POLL_MS);
		device->irq_count = 0U;
		device->polled = 1;
		return 0;
	}
	if (error != 0)
		return error;
	if (device->irq_count != 1U)
		return ENODEV;

	/* The handler. */
	error = drv_pci_device_establish_irq(device->pci, &device->irq, ghci_cp_interrupt, device, "ghci", &device->irq_cookie);
	if (error != 0)
		return error;

	/* Says when the GPU raises the INTx pin rather than a message. */
	if (device->irq.type == DRV_PCI_IRQ_INTX)
		kern_logf("ghci: interrupt on the INTx pin\n");

	/* Succeeded: the handler serves the GPU's interrupt. */
	return 0;
}

/* Gives back whatever of the bus the attach took, in the reverse order. */
static void
release_bus(
	struct ghci_device *device)
{
	/* No bus mastering. */
	(void)drv_pci_device_set_bus_master(device->pci, false);

	/* The handler and the interrupt. */
	if (device->irq_cookie != NULL) {
		drv_pci_device_disestablish_irq(device->pci, device->irq_cookie);
		device->irq_cookie = NULL;
	}
	if (device->irq_count != 0U) {
		drv_pci_device_free_irqs(device->pci, &device->irq, device->irq_count);
		device->irq_count = 0U;
	}

	/* The local window. */
	if (device->window != NULL) {
		drv_pci_device_unmap_bar(device->pci, &device->window_map);
		drv_pci_device_release_bar(device->pci, BAR_WINDOW);
		device->window = NULL;
	}

	/* The register window. */
	if (device->registers != NULL) {
		drv_pci_device_unmap_bar(device->pci, &device->register_map);
		drv_pci_device_release_bar(device->pci, BAR_REGISTERS);
		device->registers = NULL;
	}
}
