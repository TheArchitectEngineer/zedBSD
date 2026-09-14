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

#include <string.h>

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

/* The clear check fills a small buffer with one color and reads it back. */
#define I915_CLEAR_WIDTH	32U
#define I915_CLEAR_HEIGHT	32U
#define I915_CLEAR_COLOR	0xffff0000U	/* opaque red, ARGB8888 */

/*
 * Foundation clear: the copy engine fills a GGTT-mapped color buffer with one
 * solid color via XY_COLOR_BLT.  The CPU reads the pages back to prove the GPU
 * produced the color end to end.  This is the smoke test the render path builds on.
 */
int
drv_i915_clear_selftest(
	struct i915_device *device)
{
	struct i915_engine *engine;
	struct i915_request *request;
	struct i915_gem_object *object;
	volatile uint32_t *pixels;
	uint64_t iteration;
	uint32_t pixel_count;
	uint32_t bytes;
	unsigned pages;
	unsigned long irq;
	bool prior_enabled;
	int error;

	engine = &device->engines[I915_ENGINE_BCS0];
	pixel_count = I915_CLEAR_WIDTH * I915_CLEAR_HEIGHT;
	bytes = pixel_count * 4U;
	pages = (bytes + 4095U) / 4096U;

	error = drv_i915_gem_create(device, (uint64_t)pages * 4096U, &object);
	if (error != 0)
		return error;

	/* The blit runs in the copy engine's kernel context, so bind into its PPGTT. */
	error = drv_i915_gem_bind_vm(&engine->kernel_vm, object);
	if (error != 0) {
		drv_i915_gem_destroy(device, object);
		return error;
	}

	/* Start from zero so an incomplete fill cannot pass the check. */
	pixels = kern_pmem_to_kernel(object->run.paddr);
	memset((void *)pixels, 0, bytes);
	kern_io_write_barrier();

	irq = spin_lock_irqsave(&device->irq_lock);
	error = drv_i915_request_alloc(engine, NULL, NULL, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		drv_i915_gem_unbind_vm(object);
		drv_i915_gem_destroy(device, object);
		return error;
	}
	request->context = &engine->kernel_context;
	/*
	 * Gen12 XY_FAST_COLOR_BLT (opcode 0x44), 11 dwords: destination pitch is
	 * zero-based (bytes - 1), MOCS uncached, and the fill color spans 4 dwords.
	 */
	request->extra[0] = (2U << 29) | (0x44U << 22) | (2U << 19) | (11U - 2U);
	request->extra[1] = ((I915_CLEAR_WIDTH * 4U) - 1U) | (I915_MOCS_UNCACHED_INDEX << 21);
	request->extra[2] = 0U;				/* X1,Y1 */
	request->extra[3] = (I915_CLEAR_HEIGHT << 16) | I915_CLEAR_WIDTH;	/* X2,Y2 */
	request->extra[4] = (uint32_t)object->va;	/* dest address low (PPGTT) */
	request->extra[5] = (uint32_t)(object->va >> 32);	/* dest address high */
	request->extra[6] = 0U;				/* dest X/Y offset */
	request->extra[7] = I915_CLEAR_COLOR;
	request->extra[8] = I915_CLEAR_COLOR;
	request->extra[9] = I915_CLEAR_COLOR;
	request->extra[10] = I915_CLEAR_COLOR;
	request->extra_count = 11U;
	drv_i915_request_queue(engine, request);
	drv_i915_request_kick(engine);
	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Bring-up runs with delivery off; enable it for the completion wait. */
	prior_enabled = kern_irq_disable();
	kern_irq_enable();
	for (iteration = 0U; iteration < I915_SELFTEST_POLL_BOUND; iteration++) {
		kern_io_read_barrier();
		if (engine->completed_seqno == request->seqno)
			break;
	}
	if (!prior_enabled)
		(void)kern_irq_disable();

	/* The corners and centre must all carry the fill color. */
	kern_io_read_barrier();
	kern_logf("i915: clear selftest color=0x%08x px[0]=0x%08x px[mid]=0x%08x px[last]=0x%08x seqno=%u/%u\n",
		I915_CLEAR_COLOR, pixels[0], pixels[pixel_count / 2U], pixels[pixel_count - 1U],
		engine->completed_seqno, request->seqno);

	error = EIO;
	if (engine->completed_seqno == request->seqno &&
	    pixels[0] == I915_CLEAR_COLOR &&
	    pixels[pixel_count / 2U] == I915_CLEAR_COLOR &&
	    pixels[pixel_count - 1U] == I915_CLEAR_COLOR)
		error = 0;

	if (error == 0)
		kern_logf("i915: clear selftest passed (solid color fill on bcs0)\n");
	else
		kern_logf("i915: clear selftest FAILED\n");

	drv_i915_gem_unbind_vm(object);
	drv_i915_gem_destroy(device, object);
	return error;
}

/* The RCS0 marker proves the render engine runs a request in its PPGTT context. */
#define I915_RCS_MARKER		0xcafef00dU

/*
 * RCS0 execution check: the render engine stores a marker through its kernel
 * context's PPGTT and raises the completion interrupt.  This is the foundation
 * the render-target clear and the draw path build on.
 */
int
drv_i915_rcs_selftest(
	struct i915_device *device)
{
	struct i915_engine *engine;
	struct i915_request *request;
	struct i915_gem_object *object;
	volatile uint32_t *marker;
	uint64_t iteration;
	unsigned long irq;
	bool prior_enabled;
	int error;

	engine = &device->engines[I915_ENGINE_RCS0];

	error = drv_i915_gem_create(device, 4096U, &object);
	if (error != 0)
		return error;
	error = drv_i915_gem_bind_vm(&engine->kernel_vm, object);
	if (error != 0) {
		drv_i915_gem_destroy(device, object);
		return error;
	}

	marker = kern_pmem_to_kernel(object->run.paddr);
	marker[0] = 0U;
	kern_io_write_barrier();

	irq = spin_lock_irqsave(&device->irq_lock);
	error = drv_i915_request_alloc(engine, NULL, NULL, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		drv_i915_gem_unbind_vm(object);
		drv_i915_gem_destroy(device, object);
		return error;
	}
	request->context = &engine->kernel_context;
	/* One store through the render engine's PPGTT (no GGTT flag). */
	request->extra[0] = MI_STORE_DWORD_IMM_GEN4;
	request->extra[1] = (uint32_t)object->va;
	request->extra[2] = (uint32_t)(object->va >> 32);
	request->extra[3] = I915_RCS_MARKER;
	request->extra_count = 4U;
	drv_i915_request_queue(engine, request);
	drv_i915_request_kick(engine);
	spin_unlock_irqrestore(&device->irq_lock, irq);

	prior_enabled = kern_irq_disable();
	kern_irq_enable();
	for (iteration = 0U; iteration < I915_SELFTEST_POLL_BOUND; iteration++) {
		kern_io_read_barrier();
		if (engine->completed_seqno == request->seqno)
			break;
	}
	if (!prior_enabled)
		(void)kern_irq_disable();

	kern_io_read_barrier();
	kern_logf("i915: rcs selftest marker=0x%08x seqno=%u/%u\n",
		marker[0], engine->completed_seqno, request->seqno);

	error = (engine->completed_seqno == request->seqno && marker[0] == I915_RCS_MARKER) ? 0 : EIO;
	kern_logf("i915: rcs selftest %s\n", error == 0 ? "passed (render engine executes)" : "FAILED");

	drv_i915_gem_unbind_vm(object);
	drv_i915_gem_destroy(device, object);
	return error;
}
