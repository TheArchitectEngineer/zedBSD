/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * PCI discovery entry for the Intel i915 native GPU backend.
 */

#ifndef DRIVERS_I915_H
#define DRIVERS_I915_H

int
drv_i915_pci_driver_register(void);

#endif
