/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Attach-time smoke test: one BCS0 request stores a marker into the
 * status page and must raise the user interrupt within the bound.
 *
 * The test proves the whole path (context image, submit queue, ring
 * parsing, GGTT write, breadcrumb, MSI) before the GPU node is
 * published. It is linked only when CONFIG_DRIVER_PCI_I915_SELFTEST
 * selects it.
 */

#include "internal.h"

#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/sched.h>

#include <errno.h>

#include "linux/i915-commands.inc"

/* The marker the copy engine must write and the time it gets to do so. */
#define I915_SELFTEST_MARKER		0xdeadbeefU
#define I915_SELFTEST_TIMEOUT_MS	100U

/*
 * Runs the BCS0 store-and-interrupt check; a failure keeps the device unpublished.
 */
int
drv_i915_selftest(
	struct i915_device *device)
{
	struct i915_engine *engine;
	struct i915_request *request;
	uint64_t interrupts_before;
	uint64_t interrupts_after;
	uint64_t deadline;
	uint64_t now;
	uint32_t address;
	uint32_t marker;
	unsigned long irq;
	int error;

	/* The copy engine runs the kernel context with no session involved. */
	engine = &device->engines[I915_ENGINE_BCS0];
	engine->status[I915_GEM_HWS_SCRATCH] = 0U;
	kern_io_write_barrier();

	/* The request is built and loaded under the IRQ lock like any submission. */
	irq = spin_lock_irqsave(&device->irq_lock);

	interrupts_before = device->user_interrupts[I915_ENGINE_BCS0];
	error = drv_i915_request_alloc(engine, NULL, NULL, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return error;
	}

	/* One store through the GGTT into the scratch dword of the status page. */
	address = engine->hwsp->ggtt_offset + I915_GEM_HWS_SCRATCH * 4U;
	request->context = &engine->kernel_context;
	request->extra[0] = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	request->extra[1] = address;
	request->extra[2] = 0U;
	request->extra[3] = I915_SELFTEST_MARKER;
	request->extra_count = 4U;
	drv_i915_request_queue(engine, request);
	drv_i915_request_kick(engine);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* The interrupt handler retires the request; this loop only watches the outcome. */
	deadline = sched_ticks() + (I915_SELFTEST_TIMEOUT_MS * KERN_CLOCK_HZ + 999U) / 1000U + 1U;
	for (;;) {
		/* The marker, the seqno and the interrupt count must all agree. */
		kern_io_read_barrier();
		marker = engine->status[I915_GEM_HWS_SCRATCH];
		interrupts_after = device->user_interrupts[I915_ENGINE_BCS0];
		if (marker == I915_SELFTEST_MARKER && interrupts_after > interrupts_before && engine->active == NULL)
			break;

		/* The bound ends the wait whatever the engine did. */
		now = sched_ticks();
		if (now > deadline)
			break;

		sched_sleep(now + 1U);
	}

	/* The log line is the evidence the remote harness looks for. */
	kern_logf("i915: selftest bcs0 store=%s irq=%u seqno=%u/%u\n",
		marker == I915_SELFTEST_MARKER ? "ok" : "missing",
		(unsigned)(interrupts_after - interrupts_before),
		engine->completed_seqno,
		request->seqno);

	/* A store without an interrupt, or no store at all, fails the attach. */
	if (marker != I915_SELFTEST_MARKER || interrupts_after == interrupts_before || engine->active != NULL) {
		device->selftest_passed = 0U;
		return EIO;
	}

	/* Succeeded: the copy engine executed a request end to end. */
	device->selftest_passed = 1U;
	return 0;
}
