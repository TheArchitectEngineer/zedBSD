/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register access of the Type-C Dekel (DKL) PHYs (see dkl-phy.h).
 *
 * The register layout is the hardware's, as Linux v6.8.12 describes it
 * (intel_dkl_phy_regs.h, MIT): port n's window starts at 0x168000 +
 * 0x1000 * n and shows the low 12 bits of the PHY's internal address; bits
 * 12 to 15 of that address are the bank, written to the byte of the port
 * (n % 4) in the index register 0x1010a0 (ports 1 to 4) or 0x1010a4 (ports
 * 5 and 6).  The index register is written whole, as Linux writes it.
 */

#include "dkl-phy.h"

#include "../mmio.h"
#include "../sync.h"

#include <kern/kcrt.h>

#include <uapi/errno.h>

/* The MMIO window of the first Type-C port's PHY, and the distance to the next port's. */
#define I915_DKL_WINDOW_BASE		0x168000u
#define I915_DKL_WINDOW_STRIDE		0x1000u

/* The bits of the internal address a window shows, and where the bank starts. */
#define I915_DKL_WINDOW_MASK		0x0fffu
#define I915_DKL_BANK_SHIFT		12u
#define I915_DKL_BANK_MASK		0xfu

/* The bank index registers of ports 1 to 4 and of ports 5 and 6. */
#define I915_DKL_INDEX_REG_LOW		0x1010a0u
#define I915_DKL_INDEX_REG_HIGH		0x1010a4u

/* How many ports one index register serves, and the bits of each port's field. */
#define I915_DKL_PORTS_PER_INDEX	4u
#define I915_DKL_INDEX_FIELD_BITS	8u

/* How long one poll of a waited-for PHY bit sleeps. */
#define I915_DKL_POLL_US		10u

/*
 * Prepares the DKL PHY access of a display.
 */
void
drv_i915_dkl_phy_init(
	struct i915_dkl_phy *dkl,
	struct i915_mmio *mmio)
{
	/* Binds the registers and prepares the lock the ports share. */
	dkl->mmio = mmio;
	spin_init(&dkl->lock, LOCK_RANK_DEVICE, "i915 dkl phy");

	/* The access may be used from here on. */
	dkl->live = 1;
}

/*
 * Computes where a PHY register is reached.
 *
 * tc_port counts from 0 for the first Type-C port.  Returns the MMIO
 * register of the window, and stores the index register and the whole value
 * to write to it first.
 */
uint32_t
drv_i915_dkl_phy_window(
	unsigned tc_port,
	uint32_t phy_address,
	uint32_t *index_reg,
	uint32_t *index_value)
{
	uint32_t bank;
	uint32_t window;
	unsigned field_shift;

	/* The bank is the part of the internal address above the window. */
	bank = (phy_address >> I915_DKL_BANK_SHIFT) & I915_DKL_BANK_MASK;

	/* Ports 1 to 4 share the low index register, ports 5 and 6 the high one. */
	if (tc_port < I915_DKL_PORTS_PER_INDEX) {
		*index_reg = I915_DKL_INDEX_REG_LOW;
	} else {
		*index_reg = I915_DKL_INDEX_REG_HIGH;
	}

	/* The port's byte of the index register holds the bank. */
	field_shift = (tc_port % I915_DKL_PORTS_PER_INDEX) * I915_DKL_INDEX_FIELD_BITS;
	*index_value = bank << field_shift;

	/* The window shows the low part of the internal address. */
	window = I915_DKL_WINDOW_BASE + I915_DKL_WINDOW_STRIDE * tc_port;
	window += phy_address & I915_DKL_WINDOW_MASK;

	/* Succeeded: the register of the window. */
	return window;
}

/*
 * Reads a register of a Type-C port's DKL PHY.
 *
 * A display whose access is not prepared reads all-ones, the value of a
 * PHY that does not answer.
 */
uint32_t
drv_i915_dkl_phy_read(
	struct i915_dkl_phy *dkl,
	unsigned tc_port,
	uint32_t phy_address)
{
	uint32_t index_reg;
	uint32_t index_value;
	uint32_t window;
	uint32_t value;

	/* An access that is not prepared has no registers. */
	if (!dkl->live)
		return 0xffffffffu;

	/* Finds the window and the bank. */
	window = drv_i915_dkl_phy_window(tc_port, phy_address, &index_reg, &index_value);

	/* Selects the bank and reads through the window. */
	spin_lock(&dkl->lock);

	drv_i915_raw_write32(dkl->mmio, index_reg, index_value);
	value = drv_i915_raw_read32(dkl->mmio, window);

	spin_unlock(&dkl->lock);

	/* Succeeded: the register's value. */
	return value;
}

/*
 * Writes a register of a Type-C port's DKL PHY.
 */
void
drv_i915_dkl_phy_write(
	struct i915_dkl_phy *dkl,
	unsigned tc_port,
	uint32_t phy_address,
	uint32_t value)
{
	uint32_t index_reg;
	uint32_t index_value;
	uint32_t window;

	/* An access that is not prepared has no registers. */
	if (!dkl->live)
		return;

	/* Finds the window and the bank. */
	window = drv_i915_dkl_phy_window(tc_port, phy_address, &index_reg, &index_value);

	/* Selects the bank and writes through the window. */
	spin_lock(&dkl->lock);

	drv_i915_raw_write32(dkl->mmio, index_reg, index_value);
	drv_i915_raw_write32(dkl->mmio, window, value);

	spin_unlock(&dkl->lock);
}

/*
 * Waits until every bit of a mask is set in a register of a Type-C port's
 * DKL PHY, polling without sleeping.
 *
 * Returns 0, ETIMEDOUT when the bits did not come within timeout_us, or EIO
 * when the time base failed.
 */
int
drv_i915_dkl_phy_wait_set(
	struct i915_dkl_phy *dkl,
	unsigned tc_port,
	uint32_t phy_address,
	uint32_t mask,
	unsigned timeout_us)
{
	uint32_t value;
	unsigned waited_us;
	int delay_result;

	/* Polls until the bits are set or the time is up; the last read decides. */
	waited_us = 0u;
	for (;;) {
		/* Reads the register; the wait is over once every bit is set. */
		value = drv_i915_dkl_phy_read(dkl, tc_port, phy_address);
		if ((value & mask) == mask)
			break;

		/* The time is up: the bits did not come. */
		if (waited_us >= timeout_us)
			return ETIMEDOUT;

		/* Waits a little before the next read. */
		delay_result = drv_i915_udelay(I915_DKL_POLL_US);
		if (delay_result != 0)
			return EIO;

		/* The time waited so far. */
		waited_us += I915_DKL_POLL_US;
	}

	/* Succeeded: every bit of the mask is set. */
	return 0;
}
