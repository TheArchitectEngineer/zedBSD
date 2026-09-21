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
#ifndef PARITY_OPREG_PCI_CONFIG_H
#define PARITY_OPREG_PCI_CONFIG_H

#define ASLE					0xe4
#define ASLS					0xfc

#define SWSCI					0xe8
#define   SWSCI_SCISEL				(1 << 15)
#define   SWSCI_GSSCIE				(1 << 0)

#endif /* PARITY_OPREG_PCI_CONFIG_H */
