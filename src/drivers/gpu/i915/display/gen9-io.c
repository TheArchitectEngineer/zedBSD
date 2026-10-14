/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register access helpers of the Gen9 mode set (see gen9-io.h).
 */

#include "gen9-io.h"
#include <kern/kcrt.h>

#include <uapi/errno.h>

/* How often a wait polls, in microseconds. */
#define I915_SKL_WAIT_POLL_US	10U

/* Reads a register. */
uint32_t
drv_i915_skl_read(
	const struct i915_skl_io *io,
	uint32_t offset)
{
	uint32_t value;

	/* The table's read. */
	value = io->read(io->context, offset);

	/* Succeeded: the register's value. */
	return value;
}

/* Writes a register. */
void
drv_i915_skl_write(
	const struct i915_skl_io *io,
	uint32_t offset,
	uint32_t value)
{
	/* The table's write. */
	io->write(io->context, offset, value);
}

/* Clears and sets bits of a register; reports the value written. */
uint32_t
drv_i915_skl_rmw(
	const struct i915_skl_io *io,
	uint32_t offset,
	uint32_t clear,
	uint32_t set)
{
	uint32_t value;

	/* The register with the bits changed. */
	value = io->read(io->context, offset);
	value = (value & ~clear) | set;
	io->write(io->context, offset, value);

	/* Succeeded: the value written. */
	return value;
}

/*
 * Waits until (register & mask) == value, polling every 10 us up to
 * timeout_us.  Returns 0, or ETIMEDOUT; the last value read goes to *last
 * when last is not NULL.
 */
int
drv_i915_skl_wait(
	const struct i915_skl_io *io,
	uint32_t offset,
	uint32_t mask,
	uint32_t value,
	unsigned timeout_us,
	uint32_t *last)
{
	uint32_t read;
	unsigned waited;

	/* Reads until the bits match or the time is spent. */
	waited = 0U;
	for (;;) {
		read = io->read(io->context, offset);
		if ((read & mask) == value)
			break;

		/* The time is up. */
		if (waited >= timeout_us) {
			if (last != NULL)
				*last = read;
			return ETIMEDOUT;
		}

		/* Waits a polling interval. */
		io->udelay(io->context, I915_SKL_WAIT_POLL_US);
		waited += I915_SKL_WAIT_POLL_US;
	}

	/* Succeeded: the register reached the value. */
	if (last != NULL)
		*last = read;
	return 0;
}
