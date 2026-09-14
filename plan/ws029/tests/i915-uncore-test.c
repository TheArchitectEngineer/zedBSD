/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises register access, forcewake reference counting and the reset handshake
 * against the synthetic register file.
 */

#include "i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"

static void test_forcewake(void);
static void test_forcewake_timeout(void);
static void test_reset(void);
static void test_bounds(void);

int
main(void)
{
	test_forcewake();
	test_forcewake_timeout();
	test_reset();
	test_bounds();
	printf("i915 uncore host test PASS\n");
	return 0;
}

/* Forcewake wakes on the first reference, counts, and releases on the last. */
static void
test_forcewake(void)
{
	struct i915_device device;
	int error;

	fixture_reset();
	fixture_device(&device);

	/* Initialization releases both domains and finds no reset pending. */
	fixture_store32(fixture_regs + FORCEWAKE_ACK_GT_GEN9, FORCEWAKE_KERNEL);
	error = drv_i915_uncore_init(&device);
	assert(error == 0);
	assert(fixture_load32(fixture_regs + FORCEWAKE_GT_GEN9) == 0xffff0000U);
	assert(fixture_load32(fixture_regs + FORCEWAKE_RENDER_GEN9) == 0xffff0000U);
	assert(fixture_load32(fixture_regs + FORCEWAKE_ACK_GT_GEN9) == 0U);

	/* The first GT reference writes the masked enable and sees the acknowledgment. */
	error = drv_i915_forcewake_get(&device, I915_FORCEWAKE_GT);
	assert(error == 0);
	assert(fixture_load32(fixture_regs + FORCEWAKE_GT_GEN9) == ((FORCEWAKE_KERNEL << 16) | FORCEWAKE_KERNEL));
	assert(fixture_load32(fixture_regs + FORCEWAKE_ACK_GT_GEN9) == FORCEWAKE_KERNEL);
	assert(device.forcewake_count[0] == 1U);
	assert(device.forcewake_count[1] == 0U);

	/* A second reference to both domains wakes render and counts GT again. */
	error = drv_i915_forcewake_get(&device, I915_FORCEWAKE_ALL);
	assert(error == 0);
	assert(device.forcewake_count[0] == 2U);
	assert(device.forcewake_count[1] == 1U);
	assert(fixture_load32(fixture_regs + FORCEWAKE_ACK_RENDER_GEN9) == FORCEWAKE_KERNEL);

	/* Releasing both keeps GT awake for the remaining holder. */
	error = drv_i915_forcewake_put(&device, I915_FORCEWAKE_ALL);
	assert(error == 0);
	assert(device.forcewake_count[0] == 1U);
	assert(device.forcewake_count[1] == 0U);
	assert(fixture_load32(fixture_regs + FORCEWAKE_ACK_RENDER_GEN9) == 0U);
	assert(fixture_load32(fixture_regs + FORCEWAKE_ACK_GT_GEN9) == FORCEWAKE_KERNEL);

	/* The last release clears the acknowledgment. */
	error = drv_i915_forcewake_put(&device, I915_FORCEWAKE_GT);
	assert(error == 0);
	assert(device.forcewake_count[0] == 0U);
	assert(fixture_load32(fixture_regs + FORCEWAKE_GT_GEN9) == (FORCEWAKE_KERNEL << 16));
	assert(fixture_load32(fixture_regs + FORCEWAKE_ACK_GT_GEN9) == 0U);

	/* An unbalanced release is refused and reported. */
	error = drv_i915_forcewake_put(&device, I915_FORCEWAKE_GT);
	assert(error == EINVAL);
	assert(fixture_log_lines == 1U);
}

/* A domain that never acknowledges reports a timeout and leaves the counts unchanged. */
static void
test_forcewake_timeout(void)
{
	struct i915_device device;
	int error;

	fixture_reset();
	fixture_device(&device);
	fixture_forcewake_stuck = 1U;

	/* The GT request times out with no reference recorded. */
	error = drv_i915_forcewake_get(&device, I915_FORCEWAKE_GT);
	assert(error == ETIMEDOUT);
	assert(device.forcewake_count[0] == 0U);

	/* A render failure after a successful GT wake releases the GT reference again. */
	fixture_forcewake_stuck = 0U;
	error = drv_i915_forcewake_get(&device, I915_FORCEWAKE_GT);
	assert(error == 0);
	fixture_forcewake_stuck = 1U;
	error = drv_i915_forcewake_get(&device, I915_FORCEWAKE_ALL);
	assert(error == ETIMEDOUT);
	assert(device.forcewake_count[0] == 1U);
	assert(device.forcewake_count[1] == 0U);
}

/* The graphics-domain reset writes the full bit and waits for it to clear. */
static void
test_reset(void)
{
	struct i915_device device;
	int error;

	fixture_reset();
	fixture_device(&device);

	/* A completing reset leaves the register clear. */
	error = drv_i915_gt_reset(&device);
	assert(error == 0);
	assert(fixture_load32(fixture_regs + GEN6_GDRST) == 0U);

	/* A stuck reset is reported as a timeout. */
	fixture_reset_stuck = 1U;
	error = drv_i915_gt_reset(&device);
	assert(error == ETIMEDOUT);
	assert(fixture_load32(fixture_regs + GEN6_GDRST) == GEN6_GRDOM_FULL);

	/* Initialization with a reset in progress waits and reports the timeout. */
	error = drv_i915_uncore_init(&device);
	assert(error == ETIMEDOUT);
}

/* Accesses outside the register window are refused without touching memory. */
static void
test_bounds(void)
{
	struct i915_device device;
	uint32_t value;
	int error;

	fixture_reset();
	fixture_device(&device);

	/* A read past the window yields all ones and a log line. */
	value = drv_i915_read32(&device, FIXTURE_REGS_BYTES);
	assert(value == 0xffffffffU);
	assert(fixture_log_lines == 1U);

	/* A write past the window is dropped. */
	drv_i915_write32(&device, FIXTURE_REGS_BYTES - 2U, 0x12345678U);
	assert(fixture_log_lines == 2U);

	/* A wait for a value that never appears ends with a timeout. */
	error = drv_i915_wait32(&device, GEN6_GDRST, 0xffU, 0x5aU, 1U);
	assert(error == ETIMEDOUT);
}
