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
 *
 * Device bring-up runs with processor interrupts disabled, so the
 * completion interrupt this request raises could not be taken while a
 * plain wait spun. The test therefore enables interrupt delivery for
 * the duration of the wait and restores the caller's state afterwards.
 */

#include "internal.h"

#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/klog.h>

#include <errno.h>

#include "linux/i915-commands.inc"

/* The marker the copy engine must write. */
#define I915_SELFTEST_MARKER		0xdeadbeefU

/* The completion interrupt fires within microseconds; this only guards a hang. */
#define I915_SELFTEST_POLL_BOUND	200000000ULL

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
	uint64_t iteration;
	uint32_t address;
	uint32_t marker;
	unsigned long irq;
	bool prior_enabled;
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

	/*
	 * Bring-up runs with delivery disabled, so enable it for the wait and
	 * restore the caller's state after; without this the breadcrumb
	 * interrupt would stay pending and the request would never retire.
	 */
	prior_enabled = kern_irq_disable();
	kern_irq_enable();

	/* The interrupt handler retires the request; this loop watches for it. */
	for (iteration = 0U; iteration < I915_SELFTEST_POLL_BOUND; iteration++) {
		kern_io_read_barrier();
		if (engine->completed_seqno == request->seqno)
			break;
	}

	/* Restore the interrupt-disabled state bring-up expects. */
	if (!prior_enabled)
		(void)kern_irq_disable();

	marker = engine->status[I915_GEM_HWS_SCRATCH];

	/* A store without an interrupt-driven retirement, or no store, fails. */
	if (marker != I915_SELFTEST_MARKER ||
	    device->user_interrupts[I915_ENGINE_BCS0] == interrupts_before ||
	    engine->completed_seqno != request->seqno) {
		device->selftest_passed = 0U;
		kern_logf("i915: selftest failed store=%s user_irq=%llu seqno=%u/%u\n",
			marker == I915_SELFTEST_MARKER ? "ok" : "missing",
			(unsigned long long)(device->user_interrupts[I915_ENGINE_BCS0] -
					     interrupts_before),
			engine->completed_seqno, request->seqno);
		return EIO;
	}

	/* Succeeded: the copy engine executed a request end to end. */
	device->selftest_passed = 1U;
	kern_logf("i915: selftest passed (bcs0 store and user interrupt)\n");
	return 0;
}
