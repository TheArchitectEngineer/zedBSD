/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Type-C PHYs of the display 12 and 13 code (xd-tc.c): the register
 * side of the Dekel PHY of each Type-C port.
 *
 * A Dekel PHY's registers are larger than the 4 KiB MMIO window each port
 * has: the upper bits of a PHY offset name a bank, which an index register
 * in front of the windows selects.  One index register serves four ports,
 * so the index write and the access through the window it selects are
 * made under one lock that every port shares, the innermost lock of the
 * xd display.  The display owns one struct i915_xd_dkl with that lock.
 *
 * Every PHY access of the xd code goes through the struct: the PHY
 * microcontroller's health after a Type-C AUX well came up, the lanes'
 * DisplayPort mode for the pin assignment, the transmit swing of the
 * lanes from the platform's Dekel table, the release of the lanes' PCS
 * soft reset, and the words of the port's PLL (xd-clock.c).  A caller
 * that has its own route to the PHY (a host test's model, or the older
 * modeset while it still records its writes) gives the struct hooks
 * instead, and the hooks' owner keeps the lock.
 *
 * A Type-C port is numbered here from 0 (TC1).  The connection state of
 * the ports (live state, ownership, TC cold) is not in this file.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_TC_H
#define DRIVERS_GPU_I915_DISPLAY_XD_TC_H

#include <stdint.h>

#include <kern/lock.h>

#include "xd-io.h"
#include "xd-platform.h"

/* The most Type-C ports a platform has. */
#define I915_XD_TC_PORTS		6U

/* The lanes of a Dekel PHY, each with two transmitters. */
#define I915_XD_DKL_LANES		2U

/* How long a Type-C AUX well's enable waits for the PHY microcontroller's health. */
#define I915_XD_DKL_HEALTH_US		1000U

/*
 * The pin assignments of a Type-C port as the FIA reports them: none (a
 * legacy port wired straight to its connector), then A to F.
 */
#define I915_XD_TC_PIN_LEGACY		0x0U
#define I915_XD_TC_PIN_A		0x1U
#define I915_XD_TC_PIN_B		0x2U
#define I915_XD_TC_PIN_C		0x3U
#define I915_XD_TC_PIN_D		0x4U
#define I915_XD_TC_PIN_E		0x5U
#define I915_XD_TC_PIN_F		0x6U

/*
 * Another route to the PHY registers: read, write and change bits of the
 * register at a PHY offset of a Type-C port, each one access whole, under
 * the owner's own lock.  NULL hooks leave the windows in place.
 */
struct i915_xd_dkl_hooks {
	uint32_t (*read)(void *context, unsigned tc, uint32_t phy_offset);
	void (*write)(void *context, unsigned tc, uint32_t phy_offset, uint32_t value);
	void (*rmw)(void *context, unsigned tc, uint32_t phy_offset, uint32_t clear, uint32_t set);
	void *context;
};

/*
 * The Dekel PHY windows of one display.
 *
 * The display owns it for the device's life.  The lock is taken around
 * each index write and the access it selects, by any thread, never from
 * an interrupt; nothing else is taken under it.  ready is zero until
 * drv_i915_xd_dkl_init() ran: a struct that is not ready reads all-ones
 * (a PHY that does not answer) and writes nothing.
 */
struct i915_xd_dkl {
	/* The register window of the display. */
	struct i915_xd_io *io;

	/* Serializes the bank index registers and the accesses through the windows. */
	struct spinlock lock;

	/* Nonzero once the windows may be used. */
	int ready;

	/* Another route to the PHY; all NULL on the device. */
	struct i915_xd_dkl_hooks hooks;
};

void drv_i915_xd_dkl_init(struct i915_xd_dkl *dkl, struct i915_xd_io *io);
void drv_i915_xd_dkl_set_hooks(struct i915_xd_dkl *dkl, const struct i915_xd_dkl_hooks *hooks);
uint32_t drv_i915_xd_dkl_window(unsigned tc, uint32_t phy_offset, uint32_t *index_reg, uint32_t *index_value);
uint32_t drv_i915_xd_dkl_read(struct i915_xd_dkl *dkl, unsigned tc, uint32_t phy_offset);
void drv_i915_xd_dkl_write(struct i915_xd_dkl *dkl, unsigned tc, uint32_t phy_offset, uint32_t value);
void drv_i915_xd_dkl_rmw(struct i915_xd_dkl *dkl, unsigned tc, uint32_t phy_offset, uint32_t clear, uint32_t set);
int drv_i915_xd_tc_wait_health(struct i915_xd_dkl *dkl, unsigned tc, unsigned timeout_us);
int drv_i915_xd_tc_dp_mode(struct i915_xd_dkl *dkl, unsigned tc, unsigned pin_assignment, unsigned lanes);
const struct i915_xd_dkl_swing_table *drv_i915_xd_tc_swing_table(const struct i915_xd_platform *platform, int hdmi, uint32_t rate_khz);
void drv_i915_xd_tc_signal_levels(struct i915_xd_dkl *dkl, const struct i915_xd_platform *platform, unsigned tc, const struct i915_xd_dkl_swing_table *table, const uint8_t levels[4], int hdmi);
void drv_i915_xd_tc_release_soft_reset(struct i915_xd_dkl *dkl, unsigned tc);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_TC_H */
