/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the Gen11 interrupt enable sequence, bank scanning, identity decode
 * and the counters the handler publishes.
 */

#include "i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"
#include "../../../src/drivers/gpu/i915/irq.c"

static uint32_t identity(unsigned class, unsigned instance, uint16_t sources);
static void test_enable(void);
static void test_dispatch(void);
static void test_identity_timeout(void);

/* Records what the handler forwards to the engine code, which this test leaves out. */
static unsigned forwarded_calls;
static unsigned forwarded_engine;
static uint16_t forwarded_sources;

void
drv_i915_engine_interrupt(
	struct i915_device *device,
	unsigned index,
	uint16_t sources)
{
	(void)device;
	forwarded_calls++;
	forwarded_engine = index;
	forwarded_sources = sources;
}

int
main(void)
{
	test_enable();
	test_dispatch();
	test_identity_timeout();
	printf("i915 irq host test PASS\n");
	return 0;
}

/* Packs class, instance and sources the way the identity register presents them. */
static uint32_t
identity(
	unsigned class,
	unsigned instance,
	uint16_t sources)
{
	return (class << 16) | (instance << 20) | sources;
}

/* Start programs the enables and masks and arms the master bit; stop reverses it. */
static void
test_enable(void)
{
	struct i915_device device;
	uint32_t sources;
	int error;

	fixture_reset();
	fixture_device(&device);

	/* The vector is allocated, the handler installed and the sources enabled. */
	error = drv_i915_irq_start(&device);
	assert(error == 0);
	assert(fixture_irq_allocations == 1U);
	assert(fixture_irq_established == 1U);
	assert(device.interrupt_count == 1U);
	assert(device.interrupt_cookie == &device);
	sources = GT_RENDER_USER_INTERRUPT | GT_CS_MASTER_ERROR_INTERRUPT | GT_CONTEXT_SWITCH_INTERRUPT | GT_WAIT_SEMAPHORE_INTERRUPT;
	assert(fixture_load32(fixture_regs + GEN11_RENDER_COPY_INTR_ENABLE) == ((sources << 16) | sources));
	assert(fixture_load32(fixture_regs + GEN11_VCS_VECS_INTR_ENABLE) == 0U);
	assert(fixture_load32(fixture_regs + GEN11_RCS0_RSVD_INTR_MASK) == ~(sources << 16));
	assert(fixture_load32(fixture_regs + GEN11_BCS_RSVD_INTR_MASK) == ~(sources << 16));
	assert(fixture_load32(fixture_regs + GEN11_VCS0_VCS1_INTR_MASK) == 0xffffffffU);
	assert(fixture_load32(fixture_regs + GEN11_GUC_SG_INTR_MASK) == 0xffffffffU);
	assert(fixture_load32(fixture_regs + GEN11_GFX_MSTR_IRQ) == GEN11_MASTER_IRQ);
	assert(device.irq_enabled == 1U);

	/* Stop masks everything again and returns the vector. */
	error = drv_i915_irq_stop(&device);
	assert(error == 0);
	assert(fixture_irq_allocations == 0U);
	assert(fixture_irq_established == 0U);
	assert(device.interrupt_cookie == NULL);
	assert(fixture_load32(fixture_regs + GEN11_GFX_MSTR_IRQ) == 0U);
	assert(fixture_load32(fixture_regs + GEN11_RENDER_COPY_INTR_ENABLE) == 0U);
	assert(fixture_load32(fixture_regs + GEN11_RCS0_RSVD_INTR_MASK) == 0xffffffffU);
}

/* The handler decodes each pending source and counts it against its engine. */
static void
test_dispatch(void)
{
	struct i915_device device;
	int handled;
	int error;

	fixture_reset();
	fixture_device(&device);
	error = drv_i915_irq_start(&device);
	assert(error == 0);

	/* Nothing pending: the vector is not ours and the master bit is restored. */
	handled = drv_i915_irq_handler(&device);
	assert(handled == 0);
	assert(fixture_load32(fixture_regs + GEN11_GFX_MSTR_IRQ) == GEN11_MASTER_IRQ);
	assert(device.irq_total == 0U);

	/* Three bank-0 sources: RCS0 user+switch, BCS0 user, and an unused video engine. */
	fixture_pending[0] = (1U << 0) | (1U << 5) | (1U << 9);
	fixture_identity[0][0] = identity(RENDER_CLASS, 0U, GT_RENDER_USER_INTERRUPT | GT_CONTEXT_SWITCH_INTERRUPT);
	fixture_identity[0][5] = identity(COPY_ENGINE_CLASS, 0U, GT_RENDER_USER_INTERRUPT);
	fixture_identity[0][9] = identity(VIDEO_DECODE_CLASS, 0U, GT_RENDER_USER_INTERRUPT);
	handled = drv_i915_irq_handler(&device);
	assert(handled == 1);
	assert(device.irq_total == 1U);
	assert(device.user_interrupts[I915_ENGINE_RCS0] == 1U);
	assert(device.context_switches[I915_ENGINE_RCS0] == 1U);
	assert(device.user_interrupts[I915_ENGINE_BCS0] == 1U);
	assert(device.context_switches[I915_ENGINE_BCS0] == 0U);
	assert(device.irq_unknown == 1U);
	assert(fixture_pending[0] == 0U);
	assert(fixture_load32(fixture_regs + GEN11_GFX_MSTR_IRQ) == GEN11_MASTER_IRQ);
	assert(fixture_load32(fixture_regs + GEN11_INTR_IDENTITY_REG(0)) == 0U);

	/* A command streamer error on BCS0 records the error register. */
	fixture_store32(fixture_regs + RING_EIR(I915_BCS0_BASE), I915_ERROR_INSTRUCTION);
	fixture_pending[1] = 1U << 3;
	fixture_identity[1][3] = identity(COPY_ENGINE_CLASS, 0U, GT_CS_MASTER_ERROR_INTERRUPT);
	handled = drv_i915_irq_handler(&device);
	assert(handled == 1);
	assert(device.error_interrupts[I915_ENGINE_BCS0] == 1U);
	assert(device.last_error[I915_ENGINE_BCS0] == I915_ERROR_INSTRUCTION);
	assert(device.irq_total == 2U);
	assert(fixture_pending[1] == 0U);

	/* Every engine identity was forwarded; the last one carried the error source. */
	assert(forwarded_calls == 3U);
	assert(forwarded_engine == I915_ENGINE_BCS0);
	assert(forwarded_sources == GT_CS_MASTER_ERROR_INTERRUPT);

	error = drv_i915_irq_stop(&device);
	assert(error == 0);
}

/* An identity that never becomes valid is counted and the source left pending. */
static void
test_identity_timeout(void)
{
	struct i915_device device;
	int handled;
	int error;

	fixture_reset();
	fixture_device(&device);
	error = drv_i915_irq_start(&device);
	assert(error == 0);
	fixture_identity_invalid = 1U;
	fixture_pending[0] = 1U << 0;
	fixture_identity[0][0] = identity(RENDER_CLASS, 0U, GT_RENDER_USER_INTERRUPT);

	/* The bank word is still acknowledged so the device does not wedge. */
	handled = drv_i915_irq_handler(&device);
	assert(handled == 1);
	assert(device.irq_identity_timeouts == 1U);
	assert(device.user_interrupts[I915_ENGINE_RCS0] == 0U);
	assert(fixture_pending[0] == 0U);
	assert(fixture_load32(fixture_regs + GEN11_GFX_MSTR_IRQ) == GEN11_MASTER_IRQ);

	error = drv_i915_irq_stop(&device);
	assert(error == 0);
}
