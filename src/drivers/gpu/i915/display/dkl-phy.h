/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register access of the Type-C Dekel (DKL) PHYs (dkl-phy.c).
 *
 * Each Type-C port's PHY is reached through a 4 KB MMIO window, and its
 * register space is larger than that: a bank index register in front of the
 * windows says which 4 KB bank of the PHY a window shows.  One index
 * register serves four ports, so every access writes the index and then
 * uses the window under one lock shared by all ports.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DKL_PHY_H
#define DRIVERS_GPU_I915_DISPLAY_DKL_PHY_H

#include <kern/lock.h>
#include <stdint.h>

struct i915_mmio;

/* The internal PHY address of the common microcontroller's status word 27. */
#define I915_DKL_CMN_UC_DW27		0x236cu

/* The PHY microcontroller reports itself healthy (DKL_CMN_UC_DW27). */
#define I915_DKL_CMN_UC_DW27_UC_HEALTH	(1u << 15)

/*
 * The DKL PHY access of one display.
 *
 * It lives in the display for the device's lifetime.  The lock orders the
 * bank index write and the window access of every port, since the ports
 * share the index registers; it is the innermost display lock (taken after
 * the power domains' mutex).  live is zero until the access is prepared,
 * and a display whose access is not prepared reads nothing.
 */
struct i915_dkl_phy {
	/* The display's registers. */
	struct i915_mmio *mmio;

	/* Orders each bank index write with the window access that follows it. */
	struct spinlock lock;

	/* Nonzero once drv_i915_dkl_phy_init() has run. */
	int live;
};

void drv_i915_dkl_phy_init(struct i915_dkl_phy *dkl, struct i915_mmio *mmio);
uint32_t drv_i915_dkl_phy_window(unsigned tc_port, uint32_t phy_address, uint32_t *index_reg, uint32_t *index_value);
uint32_t drv_i915_dkl_phy_read(struct i915_dkl_phy *dkl, unsigned tc_port, uint32_t phy_address);
void drv_i915_dkl_phy_write(struct i915_dkl_phy *dkl, unsigned tc_port, uint32_t phy_address, uint32_t value);
void drv_i915_dkl_phy_rmw(struct i915_dkl_phy *dkl, unsigned tc_port, uint32_t phy_address, uint32_t clear, uint32_t set);
int drv_i915_dkl_phy_wait_set(struct i915_dkl_phy *dkl, unsigned tc_port, uint32_t phy_address, uint32_t mask, unsigned timeout_us);

#endif
