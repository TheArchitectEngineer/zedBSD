/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Register access, forcewake domains and the graphics-domain reset
 * handshake.
 *
 * The register BAR is mapped uncached, so every access reaches the
 * device in order, explicit barriers separate a write from the
 * readback that confirms it.
 *
 * References: Linux intel_uncore.c
 * fw_domain_get/fw_domain_wait_ack_set and intel_reset.c
 * gen6_hw_domain_reset, re-expressed for this driver.
 */

#include "internal.h"

#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/sched.h>

#include <errno.h>

/* A masked register write carries the changed bits in the upper half. */
#define I915_MASKED_ENABLE(bits)	(((bits) << 16) | (bits))
#define I915_MASKED_DISABLE(bits)	((bits) << 16)

static int i915_forcewake_domain_get(struct i915_device *device, unsigned domain);
static int i915_forcewake_domain_put(struct i915_device *device, unsigned domain);
static uint32_t i915_forcewake_request_register(unsigned domain);
static uint32_t i915_forcewake_ack_register(unsigned domain);
static uint64_t i915_timeout_ticks(unsigned timeout_ms);

/*
 * Reads one 32-bit register from the mapped register BAR.
 */
uint32_t
drv_i915_read32(
	struct i915_device *device,
	uint32_t offset)
{
	const volatile uint8_t *base;
	uint32_t value;

	/* An offset outside the mapped register half of BAR0 would read unrelated memory. */
	if (offset > device->regs.size - 4U) {
		kern_logf("i915: register read outside BAR0 window: 0x%x\n", offset);
		return 0xffffffffU;
	}

	/* Uncached device access observes the register at the time of the read. */
	base = device->regs.address;
	value = kern_mmio_read32(base + offset);

	/* Succeeded: the caller receives the current register value. */
	return value;
}

/*
 * Writes one 32-bit register in the mapped register BAR.
 */
void
drv_i915_write32(
	struct i915_device *device,
	uint32_t offset,
	uint32_t value)
{
	volatile uint8_t *base;

	/* A write outside the register window is dropped and reported rather than trusted. */
	if (offset > device->regs.size - 4U) {
		kern_logf("i915: register write outside BAR0 window: 0x%x\n", offset);
		return;
	}

	/* The write is posted; callers that need completion read the register back. */
	base = device->regs.address;
	kern_mmio_write32(base + offset, value);
}

/*
 * Polls a register until the masked value appears or the bounded time passes.
 *
 * Attach-time handshakes are short, so this spins instead of sleeping; the
 * caller chooses a timeout no longer than the hardware promises.
 */
int
drv_i915_wait32(
	struct i915_device *device,
	uint32_t offset,
	uint32_t mask,
	uint32_t value,
	unsigned timeout_ms)
{
	uint64_t deadline;
	uint64_t now;
	uint32_t observed;

	/* Samples the register at least once even with a zero timeout. */
	deadline = sched_ticks() + i915_timeout_ticks(timeout_ms);
	for (;;) {
		/* A match ends the wait regardless of the elapsed time. */
		observed = drv_i915_read32(device, offset);
		if ((observed & mask) == value)
			return 0;

		/* The deadline is checked after the sample so a slow tick cannot skip one. */
		now = sched_ticks();
		if (now > deadline)
			break;

		/* Keeps the compiler from hoisting the sample out of the loop. */
		kern_compiler_barrier();
	}

	/* The register never reached the expected value within the bound. */
	kern_logf("i915: register 0x%x stayed 0x%x (wanted 0x%x under 0x%x) for %u ms\n",
		offset, observed, value, mask, timeout_ms);
	return ETIMEDOUT;
}

/*
 * Takes forcewake references so the selected domains stay powered for MMIO.
 *
 * The first reference wakes the domain and waits for its acknowledgment.
 * The caller holds the device mutex; interrupt handlers never need forcewake
 * because the Gen11 interrupt registers live outside these domains.
 */
int
drv_i915_forcewake_get(
	struct i915_device *device,
	unsigned domains)
{
	int error;

	/* The GT domain covers the common register blocks every engine uses. */
	if ((domains & I915_FORCEWAKE_GT) != 0U) {
		error = i915_forcewake_domain_get(device, 0U);
		if (error != 0)
			return error;
	}

	/* The render domain covers RCS0 and its sampler and cache registers. */
	if ((domains & I915_FORCEWAKE_RENDER) != 0U) {
		error = i915_forcewake_domain_get(device, 1U);
		if (error != 0) {
			/* Releases the GT reference this call took so counts stay balanced. */
			if ((domains & I915_FORCEWAKE_GT) != 0U)
				(void)i915_forcewake_domain_put(device, 0U);

			return error;
		}
	}

	/* Succeeded: every requested domain is awake and counted. */
	return 0;
}

/*
 * Drops forcewake references; the last reference lets the domain sleep again.
 */
int
drv_i915_forcewake_put(
	struct i915_device *device,
	unsigned domains)
{
	int error;
	int first_error;

	/* Both domains are released even when one release times out. */
	first_error = 0;
	if ((domains & I915_FORCEWAKE_RENDER) != 0U) {
		error = i915_forcewake_domain_put(device, 1U);
		if (error != 0)
			first_error = error;
	}

	/* The GT domain is released last because render registers depend on it. */
	if ((domains & I915_FORCEWAKE_GT) != 0U) {
		error = i915_forcewake_domain_put(device, 0U);
		if (error != 0 && first_error == 0)
			first_error = error;
	}

	/* Reports the first failed release after attempting every requested one. */
	if (first_error != 0)
		return first_error;

	/* Succeeded: the requested references are dropped. */
	return 0;
}

/*
 * Puts every forcewake domain into the released state and checks for a stuck reset.
 *
 * Firmware or a previous driver may have left wake requests pending; clearing
 * them makes later reference counting match the hardware state.
 */
int
drv_i915_uncore_init(
	struct i915_device *device)
{
	uint32_t reset;
	int error;

	/* A masked all-bits-clear write releases whatever the previous owner requested. */
	drv_i915_write32(device, FORCEWAKE_GT_GEN9, 0xffff0000U);
	drv_i915_write32(device, FORCEWAKE_RENDER_GEN9, 0xffff0000U);
	device->forcewake_count[0] = 0U;
	device->forcewake_count[1] = 0U;

	/* Posts the releases before any later register access relies on them. */
	kern_io_write_barrier();
	(void)drv_i915_read32(device, FORCEWAKE_ACK_GT_GEN9);

	/* A reset still in progress must finish before the driver programs anything. */
	reset = drv_i915_read32(device, GEN6_GDRST);
	if (reset != 0U) {
		kern_logf("i915: reset in progress at attach: GDRST=0x%x\n", reset);
		error = drv_i915_wait32(device, GEN6_GDRST, 0xffffffffU, 0U, I915_RESET_TIMEOUT_MS);
		if (error != 0)
			return error;
	}

	/* Succeeded: forcewake accounting starts from a fully released device. */
	return 0;
}

/*
 * Resets the whole graphics domain and waits for the hardware to finish.
 *
 * Every engine loses its state, so callers reinitialize engines afterwards.
 */
int
drv_i915_gt_reset(
	struct i915_device *device)
{
	int error;

	/* The full domain covers every engine and the shared GT blocks. */
	error = drv_i915_domain_reset(device, GEN6_GRDOM_FULL);
	if (error != 0)
		return error;

	/* Succeeded: the graphics domain is idle and its state is undefined until reprogrammed. */
	return 0;
}

/*
 * Resets the given reset domains and waits for the hardware to acknowledge.
 *
 * The request is issued twice because some parts report completion before
 * their register state settled; the second request serializes behind the
 * first and is a no-op once the state is clean.
 */
int
drv_i915_domain_reset(
	struct i915_device *device,
	uint32_t domains)
{
	unsigned pass;
	unsigned settle;
	int error;

	/* Each pass writes the request bits and waits for the hardware to clear them. */
	for (pass = 0U; pass < 2U; pass++) {
		drv_i915_write32(device, GEN6_GDRST, domains);
		kern_io_write_barrier();
		error = drv_i915_wait32(device, GEN6_GDRST, domains, 0U, I915_RESET_TIMEOUT_MS);
		if (error != 0)
			return error;
	}

	/* A handful of uncached reads gives the engine state time to settle. */
	for (settle = 0U; settle < 64U; settle++)
		(void)drv_i915_read32(device, GEN6_GDRST);

	/* Succeeded: the domains acknowledged their reset. */
	return 0;
}

/* Wakes one forcewake domain on its first reference. */
static int
i915_forcewake_domain_get(
	struct i915_device *device,
	unsigned domain)
{
	uint32_t request;
	uint32_t ack;
	int error;

	/* A domain that is already awake only gains a reference. */
	if (device->forcewake_count[domain] != 0U) {
		device->forcewake_count[domain]++;
		return 0;
	}

	/* The kernel bit asks the power controller to keep the domain on. */
	request = i915_forcewake_request_register(domain);
	ack = i915_forcewake_ack_register(domain);
	drv_i915_write32(device, request, I915_MASKED_ENABLE(FORCEWAKE_KERNEL));
	kern_io_write_barrier();

	/* The acknowledgment bit rises once the domain is powered and clocked. */
	error = drv_i915_wait32(device, ack, FORCEWAKE_KERNEL, FORCEWAKE_KERNEL, I915_FORCEWAKE_TIMEOUT_MS);
	if (error != 0)
		return error;

	/* The count keeps the domain awake until the matching put. */
	device->forcewake_count[domain] = 1U;

	/* Succeeded: registers in this domain can be accessed. */
	return 0;
}

/* Releases one forcewake domain when its last reference is dropped. */
static int
i915_forcewake_domain_put(
	struct i915_device *device,
	unsigned domain)
{
	uint32_t request;
	uint32_t ack;
	int error;

	/* Dropping a reference that was never taken is a driver bug worth logging. */
	if (device->forcewake_count[domain] == 0U) {
		kern_logf("i915: forcewake domain %u released without a reference\n", domain);
		return EINVAL;
	}

	/* Other holders keep the domain awake. */
	device->forcewake_count[domain]--;
	if (device->forcewake_count[domain] != 0U)
		return 0;

	/* A masked clear of the kernel bit lets the power controller gate the domain. */
	request = i915_forcewake_request_register(domain);
	ack = i915_forcewake_ack_register(domain);
	drv_i915_write32(device, request, I915_MASKED_DISABLE(FORCEWAKE_KERNEL));
	kern_io_write_barrier();

	/* The acknowledgment bit falls once the release is accepted. */
	error = drv_i915_wait32(device, ack, FORCEWAKE_KERNEL, 0U, I915_FORCEWAKE_TIMEOUT_MS);
	if (error != 0)
		return error;

	/* Succeeded: the domain may power down until the next get. */
	return 0;
}

/* Maps a domain index to its wake request register. */
static uint32_t
i915_forcewake_request_register(
	unsigned domain)
{
	/* Domain 0 is GT; anything else is the render domain. */
	if (domain == 0U)
		return FORCEWAKE_GT_GEN9;

	return FORCEWAKE_RENDER_GEN9;
}

/* Maps a domain index to its acknowledgment register. */
static uint32_t
i915_forcewake_ack_register(
	unsigned domain)
{
	/* Domain 0 is GT; anything else is the render domain. */
	if (domain == 0U)
		return FORCEWAKE_ACK_GT_GEN9;

	return FORCEWAKE_ACK_RENDER_GEN9;
}

/* Converts a millisecond bound into scheduler ticks, rounding up and adding one boundary. */
static uint64_t
i915_timeout_ticks(
	unsigned timeout_ms)
{
	uint64_t ticks;

	/* The extra tick guarantees the bound is never shorter than requested. */
	ticks = ((uint64_t)timeout_ms * KERN_CLOCK_HZ + 999U) / 1000U + 1U;

	return ticks;
}
