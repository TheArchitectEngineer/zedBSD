/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The JH7110's glue for the VisionFive 2's first Ethernet port.
 *
 * The glue finds the port's DesignWare controller in the device tree,
 * switches its pins, clocks and resets on, selects RGMII in the AON
 * system control block, reads the PHY's settings, and hands the
 * controller to the DesignWare driver.
 */

#ifndef KERN_DRIVERS_VF2_ETHERNET_H
#define KERN_DRIVERS_VF2_ETHERNET_H

#include <drivers/generic/fdt.h>

/*
 * Brings up the first Ethernet port (GMAC0) and publishes it as en0.
 *
 * The system control blocks, the clock and reset generators and the pin
 * controllers must be up.  dma_default_coherent is the board's answer when
 * the device tree does not say whether the controller's DMA is coherent.
 * Reports ENOENT for a tree without the port, or the errno of the step
 * that failed.
 */
int drv_vf2_ethernet_attach(const struct drv_fdt *fdt, int dma_default_coherent);

#endif
