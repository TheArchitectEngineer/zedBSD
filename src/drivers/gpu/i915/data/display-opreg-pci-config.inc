/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/intel_pci_config.h
 * (sha256 4306da3efbe38e8b959bd148992b7e575f5ac014bd40678a3c7d378e395ea0b7) by tools/port_lcd_calc.py:
 * ASLE, ASLS, SWSCI, SWSCI_SCISEL, SWSCI_GSSCIE.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/opreg_pci_config.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_OPREG_PCI_CONFIG_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_OPREG_PCI_CONFIG_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_OPREG_PCI_CONFIG_INC

#define ASLE					0xe4
#define ASLS					0xfc

#define SWSCI					0xe8
#define   SWSCI_SCISEL				(1 << 15)
#define   SWSCI_GSSCIE				(1 << 0)

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_OPREG_PCI_CONFIG_INC */
