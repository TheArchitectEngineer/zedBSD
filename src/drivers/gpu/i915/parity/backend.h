/*
 * WS031 Linux-parity — real (zedBSD) backend declarations shared by probe.c and
 * the backend_*.c files.  These bind the portable osdep contract vtables to the
 * kernel's low-level APIs.
 */
#ifndef PARITY_BACKEND_H
#define PARITY_BACKEND_H

#include "osdep/pci.h"
#include "osdep/mmio.h"
#include "osdep/runtime_pm.h"
#include "osdep/dma.h"

struct drv_pci_device;
struct drv_dma_device;

/* PCI backend: wraps drv_pci_device_config_* . */
struct parity_pci_priv {
	struct drv_pci_device *pci;
	int msi_irq;            /* logical IRQ from hal_irq_alloc_msi(); -1 when none */
	char msi_source[17];    /* canonical "PCI ssss:bb:dd.f" identity for the HAL */
};
const struct osdep_pci_backend *parity_pci_backend(void);

/* MMIO backend: wraps kern_mmio_* over a mapped BAR + the Gen9+ forcewake regs. */
struct parity_mmio_priv {
	volatile unsigned char *base;   /* mapped register BAR base */
	unsigned long size;             /* mapped window size */
};
const struct osdep_mmio_backend *parity_mmio_backend(void);
extern const struct osdep_mmio_range parity_mmio_ranges[];
extern const unsigned parity_mmio_range_count;

/* DMA backend: wraps drv_dma_* ; priv is the struct drv_dma_device *. */
const struct osdep_dma_backend *parity_dma_backend(void);

/* Device runtime-PM backend: a stub sufficient for init_early during P0..P2. */
const struct osdep_rpm_backend *parity_rpm_backend(void);
const struct osdep_rpm_backend *parity_pci_probe_pm_backend(void);

#endif /* PARITY_BACKEND_H */
