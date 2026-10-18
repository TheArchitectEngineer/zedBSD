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
#include "xd-io.h"
#include <kern/kcrt.h>

#include <uapi/errno.h>

/* How often a wait polls, in microseconds. */
#define I915_SKL_WAIT_POLL_US	10U

/* The waits of the xd window from which a whole number of milliseconds is slept rather than spun. */
#define I915_SKL_XD_SLEEP_US	1000U

/*
 * The xd register window the Gen9 mode set reaches the xd code through,
 * and the table it is pointed at.
 *
 * drv_i915_skl_xd_io() points both at the caller's table; the node's
 * mutex serializes the callers, and the host tests run on one thread.
 */
static struct i915_xd_io i915_skl_xd_window;
static const struct i915_skl_io *i915_skl_xd_table;

static uint32_t i915_skl_xd_read(void *context, uint32_t offset);
static void i915_skl_xd_write(void *context, uint32_t offset, uint32_t value);
static void i915_skl_xd_wait(void *context, unsigned microseconds);

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

/*
 * Points the xd register window at a table: its reads and writes are the
 * table's, its waits the table's sleep for whole milliseconds and its spin
 * for the rest.
 */
struct i915_xd_io *
drv_i915_skl_xd_io(
	const struct i915_skl_io *io)
{
	struct i915_xd_io_hooks hooks;

	/* The window, without hardware of its own. */
	drv_i915_xd_io_init(&i915_skl_xd_window, NULL, NULL);
	i915_skl_xd_table = io;

	/* Every access through the table. */
	hooks.read = i915_skl_xd_read;
	hooks.write = i915_skl_xd_write;
	hooks.wait = i915_skl_xd_wait;
	hooks.lock = NULL;
	hooks.context = &i915_skl_xd_table;
	drv_i915_xd_io_set_hooks(&i915_skl_xd_window, &hooks);

	/* Succeeded: the window. */
	return &i915_skl_xd_window;
}

/* Reads a register through the table the window is pointed at. */
static uint32_t
i915_skl_xd_read(
	void *context,
	uint32_t offset)
{
	const struct i915_skl_io *const *table;
	uint32_t value;

	/* The table. */
	table = context;

	/* Succeeded: its read. */
	value = drv_i915_skl_read(*table, offset);
	return value;
}

/* Writes a register through the table the window is pointed at. */
static void
i915_skl_xd_write(
	void *context,
	uint32_t offset,
	uint32_t value)
{
	const struct i915_skl_io *const *table;

	/* The table. */
	table = context;

	/* Succeeded: its write. */
	drv_i915_skl_write(*table, offset, value);
}

/* Waits through the table the window is pointed at: whole milliseconds asleep, the rest spun. */
static void
i915_skl_xd_wait(
	void *context,
	unsigned microseconds)
{
	const struct i915_skl_io *const *table;

	/* The table. */
	table = context;

	/* A wait of whole milliseconds sleeps. */
	if (microseconds >= I915_SKL_XD_SLEEP_US && (microseconds % I915_SKL_XD_SLEEP_US) == 0U) {
		(*table)->msleep((*table)->context, microseconds / I915_SKL_XD_SLEEP_US);
		return;
	}

	/* Succeeded: a shorter or odd wait spins. */
	(*table)->udelay((*table)->context, microseconds);
}
