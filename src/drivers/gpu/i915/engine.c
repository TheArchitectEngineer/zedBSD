/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Command streamers RCS0 and BCS0: bring-up, MOCS programming,
 * per-engine reset and the interrupt-driven retirement of requests.
 *
 * References: Linux intel_engine_cs.c intel_engine_setup,
 * init_status_page, intel_engine_stop_cs;
 * intel_execlists_submission.c enable_execlists and
 * enable_error_interrupt; intel_reset.c gen8_engine_reset_prepare,
 * gen6_hw_domain_reset and gen8_engine_reset_cancel; intel_mocs.c
 * __init_mocs_table and init_l3cc_table; intel_workarounds.c
 * BLIT_CCTL programming, re-expressed for this driver.
 */

#include "internal.h"

#include <kern/device-io.h>
#include <kern/klog.h>

#include <errno.h>
#include <string.h>

/* The MOCS table rows the transcribed Linux table fills. */
struct i915_mocs_entry {
	uint32_t control_value;
	uint16_t l3cc_value;
	uint16_t used;
};

#include "linux/i915-mocs.inc"

/* A masked register value carries the changed bits in its upper half. */
#define I915_ENGINE_MASKED_ENABLE(bits)		(((bits) << 16) | (bits))
#define I915_ENGINE_MASKED_DISABLE(bits)	((bits) << 16)

/* Copy engines address memory through the MOCS index this register names. */
#define I915_BLIT_CCTL_VALUE(index)	((((index) << 1) << 8) | ((index) << 1))

static int i915_engine_init(struct i915_device *device, struct i915_engine *engine);
static void i915_engine_fini(struct i915_device *device, struct i915_engine *engine);
static void i915_engine_program(struct i915_engine *engine);
static int i915_engine_stop_cs(struct i915_engine *engine);
static int i915_engine_reset_prepare(struct i915_engine *engine);
static void i915_engine_reset_cancel(struct i915_engine *engine);
static void i915_mocs_init(struct i915_device *device);
static uint32_t i915_mocs_control(unsigned index);
static uint16_t i915_mocs_l3cc(unsigned index);

/*
 * Brings up both engines, their status pages, MOCS tables and kernel contexts.
 *
 * Forcewake is taken for the life of the device: submissions happen from
 * interrupt context where the wake handshake could not be waited for.
 */
int
drv_i915_engines_start(
	struct i915_device *device)
{
	struct i915_engine *engine;
	unsigned index;
	int error;

	/* Every engine register below lives in the GT or render domain. */
	error = drv_i915_forcewake_get(device, I915_FORCEWAKE_ALL);
	if (error != 0)
		return error;

	device->forcewake_held = 1U;

	/*
	 * A device handed over by VFIO may still be mid-operation from the host
	 * driver. A full graphics reset with forcewake held brings every engine
	 * to a known idle state before the first register is programmed, which
	 * the reset issued before forcewake during attach cannot guarantee.
	 */
	error = drv_i915_gt_reset(device);
	if (error != 0) {
		(void)drv_i915_forcewake_put(device, I915_FORCEWAKE_ALL);
		device->forcewake_held = 0U;
		return error;
	}

	/* Cache policy tables are global and are programmed once. */
	i915_mocs_init(device);

	/* Engine slots are fixed: RCS0 first, BCS0 second. */
	for (index = 0U; index < I915_ENGINE_COUNT; index++) {
		engine = &device->engines[index];
		engine->device = device;
		engine->index = index;
		engine->instance = 0U;
		if (index == I915_ENGINE_RCS0) {
			engine->class = I915_CLASS_RENDER;
			engine->base = I915_RCS0_BASE;
			engine->reset_domain = GEN11_GRDOM_RENDER;
		} else {
			engine->class = I915_CLASS_COPY;
			engine->base = I915_BCS0_BASE;
			engine->reset_domain = GEN11_GRDOM_BLT;
		}

		/* The identity part of every context descriptor names class and instance. */
		engine->ccid = (engine->instance << (GEN11_ENGINE_INSTANCE_SHIFT - 32U)) |
		    (engine->class << (GEN11_ENGINE_CLASS_SHIFT - 32U));

		error = i915_engine_init(device, engine);
		if (error != 0)
			return error;

	}

	/* Succeeded: both engines accept submissions. */
	return 0;
}

/*
 * Tears down both engines after the GPU was reset and no request is live.
 */
void
drv_i915_engines_stop(
	struct i915_device *device)
{
	unsigned index;

	/* Engines are released in reverse so BCS0 goes before the render engine. */
	for (index = I915_ENGINE_COUNT; index > 0U; index--)
		i915_engine_fini(device, &device->engines[index - 1U]);

	/* The permanent forcewake references are dropped last. */
	if (device->forcewake_held != 0U) {
		(void)drv_i915_forcewake_put(device, I915_FORCEWAKE_ALL);
		device->forcewake_held = 0U;
	}
}

/*
 * Resets one engine and reprograms it; the caller fails the affected requests.
 */
int
drv_i915_engine_reset(
	struct i915_engine *engine)
{
	struct i915_device *device;
	int error;
	int stop_error;

	device = engine->device;

	/* A stopped command streamer makes the reset request reliable. */
	stop_error = i915_engine_stop_cs(engine);
	if (stop_error != 0)
		kern_logf("i915: engine %u did not stop before reset: %d\n", engine->index, stop_error);

	/* The engine must acknowledge that it is ready before the domain is reset. */
	error = i915_engine_reset_prepare(engine);
	if (error != 0) {
		i915_engine_reset_cancel(engine);
		return error;
	}

	/* The domain reset drops the engine's state; the request is withdrawn afterwards. */
	error = drv_i915_domain_reset(device, engine->reset_domain);
	i915_engine_reset_cancel(engine);
	if (error != 0)
		return error;

	/* The engine forgets its status page and queue; both are reprogrammed. */
	engine->reset_count++;
	engine->hw_active = 0U;
	i915_engine_program(engine);

	/* Succeeded: the engine is idle and ready for a fresh submission. */
	return 0;
}

/*
 * Fails every request of one session (or all when NULL) and resets the engine
 * if that session's request was running; callbacks are delivered afterwards.
 *
 * The reset runs without the IRQ lock so the interrupt handler can still
 * acknowledge sources; the resetting flag keeps it from touching the queue.
 */
int
drv_i915_engine_recover(
	struct i915_engine *engine,
	struct i915_session *session,
	int error)
{
	struct i915_device *device;
	struct i915_request *retired;
	unsigned long irq;
	unsigned reset_needed;
	int reset_error;

	device = engine->device;
	retired = NULL;

	/* A running request of the session means the hardware may be stuck on it. */
	irq = spin_lock_irqsave(&device->irq_lock);

	reset_needed = 0U;
	if (engine->active != NULL && (session == NULL || engine->active->session == session))
		reset_needed = 1U;
	drv_i915_request_fail(engine, session, error, &retired);
	if (reset_needed != 0U)
		engine->resetting = 1U;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* The reset handshake spins on registers and must not hold the IRQ lock. */
	reset_error = 0;
	if (reset_needed != 0U)
		reset_error = drv_i915_engine_reset(engine);

	/* Other sessions' queued requests resume once the engine is back. */
	irq = spin_lock_irqsave(&device->irq_lock);

	engine->resetting = 0U;
	if (reset_error == 0)
		drv_i915_request_kick(engine);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Callbacks run last, outside every driver lock. */
	drv_i915_request_complete_list(engine, retired);

	/* A failed reset leaves the engine unusable; the caller escalates. */
	if (reset_error != 0)
		return reset_error;

	/* Succeeded: the session's work is ended and the engine serves its peers. */
	return 0;
}

/*
 * Serves the interrupt sources one engine raised.
 *
 * Retirement runs under the IRQ lock; completion callbacks run after it, and
 * the next queued request is loaded once the engine reported idle.
 */
void
drv_i915_engine_interrupt(
	struct i915_device *device,
	unsigned index,
	uint16_t sources)
{
	struct i915_engine *engine;
	struct i915_request *retired;
	unsigned promotions;
	unsigned completions;

	engine = &device->engines[index];
	retired = NULL;

	/* Queue and completion state change only under the IRQ lock. */
	spin_lock(&device->irq_lock);

	/* An engine being reset publishes nothing until the reset owner kicks it again. */
	if (engine->resetting != 0U) {
		spin_unlock(&device->irq_lock);
		return;
	}

	/* A context switch event tells whether the engine went idle. */
	if ((sources & GT_CONTEXT_SWITCH_INTERRUPT) != 0U && engine->initialized != 0U) {
		completions = drv_i915_lrc_csb_consume(engine, &promotions);
		if (completions != 0U)
			engine->hw_active = 0U;
	}

	/* A user interrupt follows a breadcrumb; the seqno decides what retired. */
	if ((sources & GT_RENDER_USER_INTERRUPT) != 0U && engine->initialized != 0U)
		drv_i915_request_retire(engine, &retired);

	/* An idle engine takes the next queued request. */
	if (engine->initialized != 0U)
		drv_i915_request_kick(engine);

	spin_unlock(&device->irq_lock);

	/* Callbacks run without any driver lock held. */
	drv_i915_request_complete_list(engine, retired);
}

/*
 * Reports whether the engine has no active or queued request.
 */
unsigned
drv_i915_engine_idle(
	struct i915_engine *engine)
{
	/* Both the hardware and the driver queue must be empty. */
	if (engine->hw_active != 0U)
		return 0U;
	if (engine->active != NULL || engine->queue_head != NULL)
		return 0U;

	return 1U;
}

/* Allocates the status page and the kernel context, then programs the engine. */
static int
i915_engine_init(
	struct i915_device *device,
	struct i915_engine *engine)
{
	int error;

	/* The status page holds breadcrumbs and the context status buffer. */
	error = drv_i915_gem_create(device, I915_HWSP_BYTES, &engine->hwsp);
	if (error != 0)
		return error;
	error = drv_i915_gem_bind_ggtt(device, engine->hwsp);
	if (error != 0)
		return error;

	engine->status = engine->hwsp->address;
	engine->next_seqno = 1U;
	engine->completed_seqno = 0U;
	engine->queue_head = NULL;
	engine->queue_tail = NULL;
	engine->active = NULL;
	memset(engine->slots, 0, sizeof(engine->slots));

	/* The kernel context owns an empty address space for driver-internal requests. */
	error = drv_i915_ppgtt_create(&engine->kernel_vm);
	if (error != 0)
		return error;
	error = drv_i915_lrc_create(device, engine, &engine->kernel_vm, I915_CONTEXT_ID_MODULUS, &engine->kernel_context);
	if (error != 0)
		return error;

	i915_engine_program(engine);
	engine->initialized = 1U;

	/* Succeeded: the engine is idle with an empty queue. */
	return 0;
}

/* Releases the kernel context, its space and the status page. */
static void
i915_engine_fini(
	struct i915_device *device,
	struct i915_engine *engine)
{
	/* Nothing may retire after the status page disappears. */
	engine->initialized = 0U;

	if (engine->kernel_context.created != 0U)
		drv_i915_lrc_destroy(device, &engine->kernel_context);

	if (engine->kernel_vm.created != 0U)
		drv_i915_ppgtt_destroy(&engine->kernel_vm);

	if (engine->hwsp != NULL) {
		drv_i915_gem_unbind_ggtt(device, engine->hwsp);
		drv_i915_gem_destroy(device, engine->hwsp);
		engine->hwsp = NULL;
		engine->status = NULL;
	}
}

/* Programs execlists mode, the status page, error masks and the copy cache policy. */
static void
i915_engine_program(
	struct i915_engine *engine)
{
	struct i915_device *device;
	uint32_t status;
	uint32_t cctl;

	device = engine->device;

	/* The status page receives no hardware status writes except what the driver requests. */
	drv_i915_write32(device, RING_HWSTAM(engine->base), 0xffffffffU);

	/* Legacy ring mode is disabled so the engine takes contexts from the submit queue. */
	drv_i915_write32(device, RING_MODE_GEN7(engine->base), I915_ENGINE_MASKED_ENABLE(GEN11_GFX_DISABLE_LEGACY_MODE));
	drv_i915_write32(device, RING_MI_MODE(engine->base), I915_ENGINE_MASKED_DISABLE(STOP_RING));

	/* The engine writes breadcrumbs and status entries into this GGTT page. */
	drv_i915_write32(device, RING_HWS_PGA(engine->base), engine->hwsp->ggtt_offset);
	(void)drv_i915_read32(device, RING_HWS_PGA(engine->base));

	/* Existing errors are cleared and only invalid instructions raise an interrupt. */
	drv_i915_write32(device, RING_EMR(engine->base), 0xffffffffU);
	drv_i915_write32(device, RING_EIR(engine->base), 0xffffffffU);
	status = drv_i915_read32(device, RING_ESR(engine->base));
	if (status != 0U)
		kern_logf("i915: engine %u error status at bring-up: 0x%x\n", engine->index, status);
	drv_i915_write32(device, RING_EMR(engine->base), ~(uint32_t)I915_ERROR_INSTRUCTION);

	/* Copy engines address memory through the uncached MOCS index. */
	if (engine->class == I915_CLASS_COPY) {
		cctl = drv_i915_read32(device, BLIT_CCTL(engine->base));
		cctl &= ~(BLIT_CCTL_DST_MOCS_MASK | BLIT_CCTL_SRC_MOCS_MASK);
		cctl |= I915_BLIT_CCTL_VALUE(I915_MOCS_UNCACHED_INDEX);
		drv_i915_write32(device, BLIT_CCTL(engine->base), cctl);
	}

	/* The status buffer starts empty with both pointers at the last entry. */
	drv_i915_lrc_reset_csb(engine);
	kern_io_write_barrier();
}

/* Stops the command streamer and waits for it to report idle. */
static int
i915_engine_stop_cs(
	struct i915_engine *engine)
{
	struct i915_device *device;
	int error;

	device = engine->device;

	/* The stop bit halts parsing; prefetch is disabled so no further fetch is queued. */
	drv_i915_write32(device, RING_MI_MODE(engine->base), I915_ENGINE_MASKED_ENABLE(STOP_RING));
	drv_i915_write32(device, RING_MODE_GEN7(engine->base), I915_ENGINE_MASKED_ENABLE(GEN12_GFX_PREFETCH_DISABLE));

	/* The idle bit rises once the streamer finished its current instruction. */
	error = drv_i915_wait32(device, RING_MI_MODE(engine->base), MODE_IDLE, MODE_IDLE, I915_STOP_TIMEOUT_MS);
	(void)drv_i915_read32(device, RING_MI_MODE(engine->base));
	if (error != 0)
		return error;

	/* Succeeded: the streamer is stopped. */
	return 0;
}

/* Asks the engine to become ready for a domain reset. */
static int
i915_engine_reset_prepare(
	struct i915_engine *engine)
{
	struct i915_device *device;
	uint32_t ack;
	uint32_t request;
	uint32_t mask;
	uint32_t expected;
	int error;

	device = engine->device;

	/* A catastrophic error bypasses the ready handshake; hardware clears the bit itself. */
	ack = drv_i915_read32(device, RING_RESET_CTL(engine->base));
	if ((ack & RESET_CTL_CAT_ERROR) != 0U) {
		request = RESET_CTL_CAT_ERROR;
		mask = RESET_CTL_CAT_ERROR;
		expected = 0U;
	} else if ((ack & RESET_CTL_READY_TO_RESET) == 0U) {
		request = RESET_CTL_REQUEST_RESET;
		mask = RESET_CTL_READY_TO_RESET;
		expected = RESET_CTL_READY_TO_RESET;
	} else {
		return 0;
	}

	/* The engine answers the masked request by raising ready or clearing the error. */
	drv_i915_write32(device, RING_RESET_CTL(engine->base), I915_ENGINE_MASKED_ENABLE(request));
	error = drv_i915_wait32(device, RING_RESET_CTL(engine->base), mask, expected, I915_RESET_READY_TIMEOUT_MS);
	if (error != 0)
		return error;

	/* Succeeded: the domain may be reset. */
	return 0;
}

/* Withdraws the reset request so the engine can run again. */
static void
i915_engine_reset_cancel(
	struct i915_engine *engine)
{
	drv_i915_write32(engine->device, RING_RESET_CTL(engine->base), I915_ENGINE_MASKED_DISABLE(RESET_CTL_REQUEST_RESET));
}

/* Programs the global MOCS control table and the L3 cache control pairs. */
static void
i915_mocs_init(
	struct i915_device *device)
{
	unsigned index;
	uint32_t pair;

	/* Every one of the 64 global entries gets a value; unused rows take the unused index. */
	for (index = 0U; index < I915_MOCS_ENTRIES; index++)
		drv_i915_write32(device, GEN12_GLOBAL_MOCS(index), i915_mocs_control(index));

	/* L3 control packs two entries per register. */
	for (index = 0U; index < I915_MOCS_ENTRIES / 2U; index++) {
		pair = (uint32_t)i915_mocs_l3cc(2U * index) | ((uint32_t)i915_mocs_l3cc(2U * index + 1U) << 16);
		drv_i915_write32(device, GEN9_LNCFCMOCS(index), pair);
	}

	device->mocs_initialized = 1U;
	kern_io_write_barrier();
}

/* Reports the control value for a MOCS index, substituting the unused entry. */
static uint32_t
i915_mocs_control(
	unsigned index)
{
	unsigned count;

	/* Rows the table leaves out use the designated unused entry. */
	count = sizeof(gen12_mocs_table) / sizeof(gen12_mocs_table[0]);
	if (index < count && gen12_mocs_table[index].used != 0U)
		return gen12_mocs_table[index].control_value;

	return gen12_mocs_table[I915_MOCS_UNUSED_INDEX].control_value;
}

/* Reports the L3 control value for a MOCS index, substituting the unused entry. */
static uint16_t
i915_mocs_l3cc(
	unsigned index)
{
	unsigned count;

	count = sizeof(gen12_mocs_table) / sizeof(gen12_mocs_table[0]);
	if (index < count && gen12_mocs_table[index].used != 0U)
		return gen12_mocs_table[index].l3cc_value;

	return gen12_mocs_table[I915_MOCS_UNUSED_INDEX].l3cc_value;
}
