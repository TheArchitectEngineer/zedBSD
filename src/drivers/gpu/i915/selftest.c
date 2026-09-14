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
#include "vk/linux/3dstate-gen12.inc"

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

/* STATE_BASE_ADDRESS parse check: a marker after it proves the 22-dword command
 * is well formed (the parser reaches the store) before real heaps are attached. */
int
drv_i915_rt_selftest(
	struct i915_device *device)
{
	struct i915_engine *engine;
	struct i915_request *request;
	struct i915_gem_object *object;
	volatile uint32_t *marker;
	uint64_t iteration;
	unsigned long irq;
	bool prior_enabled;
	uint32_t mocs;
	unsigned n;
	int error;

	engine = &device->engines[I915_ENGINE_RCS0];
	mocs = I915_MOCS_UNCACHED_INDEX;
	(void)mocs;

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

	n = 0U;
	/* PIPELINE_SELECT: 3D pipeline. */
	request->extra[n++] = GEN12_PIPELINE_SELECT_DWORD(GEN12_PIPELINE_SELECT_3D);
	/* STATE_BASE_ADDRESS, 22 dwords; bases null with modify+MOCS, sizes maxed. */
	request->extra[n++] = (0x6101U << 16) | (22U - 2U);	/* DW0 header */
	request->extra[n++] = 0U;			/* DW1 general base lo */
	request->extra[n++] = 0U;				/* DW2 general base hi */
	request->extra[n++] = 0U;			/* DW3 stateless data port MOCS */
	request->extra[n++] = 0U;			/* DW4 surface base lo */
	request->extra[n++] = 0U;				/* DW5 surface base hi */
	request->extra[n++] = 0U;			/* DW6 dynamic base lo */
	request->extra[n++] = 0U;				/* DW7 dynamic base hi */
	request->extra[n++] = 0U;			/* DW8 indirect base lo */
	request->extra[n++] = 0U;				/* DW9 indirect base hi */
	request->extra[n++] = 0U;			/* DW10 instruction base lo */
	request->extra[n++] = 0U;				/* DW11 instruction base hi */
	request->extra[n++] = 0U;		/* DW12 general size */
	request->extra[n++] = 0U;		/* DW13 dynamic size */
	request->extra[n++] = 0U;		/* DW14 indirect size */
	request->extra[n++] = 0U;			/* DW15 instruction size */
	request->extra[n++] = 0U;			/* DW16 bindless surface lo */
	request->extra[n++] = 0U;				/* DW17 bindless surface hi */
	request->extra[n++] = 0U;			/* DW18 bindless surface size */
	request->extra[n++] = 0U;			/* DW19 bindless sampler lo */
	request->extra[n++] = 0U;				/* DW20 bindless sampler hi */
	request->extra[n++] = 0U;			/* DW21 bindless sampler size */
	/* MI_STORE marker proves the parser reached here. */
	request->extra[n++] = MI_STORE_DWORD_IMM_GEN4;
	request->extra[n++] = (uint32_t)object->va;
	request->extra[n++] = (uint32_t)(object->va >> 32);
	request->extra[n++] = 0x5ba5eba5U;
	request->extra_count = n;

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
	kern_logf("i915: rt sba selftest marker=0x%08x seqno=%u/%u\n",
		marker[0], engine->completed_seqno, request->seqno);
	error = (engine->completed_seqno == request->seqno && marker[0] == 0x5ba5eba5U) ? 0 : EIO;
	kern_logf("i915: rt sba selftest %s\n", error == 0 ? "passed (STATE_BASE_ADDRESS parses)" : "FAILED");

	drv_i915_gem_unbind_vm(object);
	drv_i915_gem_destroy(device, object);
	return error;
}

/*
 * Render-target draw (built incrementally on hardware).  Step 1 validates that
 * STATE_BASE_ADDRESS with real heap addresses does not fault: a marker after it
 * must land.  Later steps add the surface state, pipeline state and primitive.
 */
/* Evicts one cache line so a later read observes memory, not a stale CPU line. */
static void
i915_selftest_clflush(volatile const void *address)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("mfence; clflush (%0); mfence" : : "r"(address) : "memory");
#else
	(void)address;
#endif
}

/* Writes back and invalidates every CPU cache line; page tables then live in memory. */
static void
i915_selftest_wbinvd(void)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("wbinvd" : : : "memory");
#endif
}

static uint32_t
i915_sba_lo(uint64_t base, uint32_t mocs)
{
	if (base == 0U)
		return 0U;
	return 1U | (mocs << 4) | ((uint32_t)base & 0xfffff000U);
}

static uint32_t
i915_sba_hi(uint64_t base)
{
	if (base == 0U)
		return 0U;
	return (uint32_t)(base >> 32);
}

/*
 * One run: the 3D commands execute from a batch buffer in the kernel PPGTT.
 *
 * Commands placed directly in the ring run in the global GTT, so any
 * STATE_BASE_ADDRESS there names GGTT offsets (that is how Linux's golden
 * render state batch works).  The executor's batches live in the PPGTT, and
 * so does this one, so the heap bases below are PPGTT addresses.
 * Markers land before and after STATE_BASE_ADDRESS; the command streamer
 * state is dumped on failure.
 */
static int
i915_draw_sba_run(
	struct i915_device *device,
	const char *name,
	struct i915_gem_object *rt,
	struct i915_gem_object *probe,
	uint64_t surface,
	uint64_t dynamic,
	uint64_t instruction,
	unsigned flush)
{
	struct i915_engine *engine;
	struct i915_request *request;
	struct i915_gem_object *batch;
	volatile uint32_t *rt_cpu;
	volatile uint32_t *probe_cpu;
	uint32_t *cmds;
	uint64_t iteration;
	unsigned long irq;
	bool prior_enabled;
	uint32_t mocs;
	uint32_t hwsp;
	unsigned n;
	unsigned line;
	int error;

	engine = &device->engines[I915_ENGINE_RCS0];
	mocs = I915_MOCS_UNCACHED_INDEX;
	rt_cpu = kern_pmem_to_kernel(rt->run.paddr);
	if (probe == NULL)
		probe = rt;
	probe_cpu = kern_pmem_to_kernel(probe->run.paddr);
	rt_cpu[0] = 0U;
	probe_cpu[1] = 0U;
	engine->status[I915_GEM_HWS_SCRATCH] = 0U;
	kern_io_write_barrier();
	i915_selftest_clflush(&rt_cpu[0]);

	/* The batch is one page in the kernel PPGTT, like the executor's command buffers. */
	if (drv_i915_gem_create(device, 4096U, &batch) != 0)
		return ENOMEM;
	if (drv_i915_gem_bind_vm(&engine->kernel_vm, batch) != 0) {
		drv_i915_gem_destroy(device, batch);
		return ENOMEM;
	}
	cmds = kern_pmem_to_kernel(batch->run.paddr);

	n = 0U;
	/* Marker A lands before any 3D state changes, in the probed page. */
	cmds[n++] = MI_STORE_DWORD_IMM_GEN4;
	cmds[n++] = (uint32_t)(probe->va + 4U);
	cmds[n++] = (uint32_t)((probe->va + 4U) >> 32);
	cmds[n++] = 0xa5a50001U;
	/* Gen12: a stalling flush precedes PIPELINE_SELECT and STATE_BASE_ADDRESS. */
	cmds[n++] = GFX_OP_PIPE_CONTROL(6);
	cmds[n++] = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH | PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE;
	cmds[n++] = 0U;
	cmds[n++] = 0U;
	cmds[n++] = 0U;
	cmds[n++] = 0U;
	/* PIPELINE_SELECT 3D: Gen12 needs the mask bits (0x13) and the media sampler DOP gate. */
	cmds[n++] = GEN12_PIPELINE_SELECT_DWORD(GEN12_PIPELINE_SELECT_3D);
	/* STATE_BASE_ADDRESS with the selected bases real; the rest untouched. */
	cmds[n++] = (0x6101U << 16) | (22U - 2U);
	cmds[n++] = 0U;					/* general lo (no modify) */
	cmds[n++] = 0U;					/* general hi */
	cmds[n++] = (mocs << 16);			/* stateless MOCS */
	cmds[n++] = i915_sba_lo(surface, mocs);
	cmds[n++] = i915_sba_hi(surface);
	cmds[n++] = i915_sba_lo(dynamic, mocs);
	cmds[n++] = i915_sba_hi(dynamic);
	cmds[n++] = 0U;					/* indirect lo (no modify) */
	cmds[n++] = 0U;					/* indirect hi */
	cmds[n++] = i915_sba_lo(instruction, mocs);
	cmds[n++] = i915_sba_hi(instruction);
	cmds[n++] = 0U;					/* general size (no modify) */
	cmds[n++] = dynamic != 0U ? (1U | (0xfffffU << 12)) : 0U;	/* dynamic size */
	cmds[n++] = 0U;					/* indirect size (no modify) */
	cmds[n++] = instruction != 0U ? (1U | (0xfffffU << 12)) : 0U;	/* instruction size */
	cmds[n++] = 0U;					/* bindless surface lo (no modify) */
	cmds[n++] = 0U;					/* bindless surface hi */
	cmds[n++] = 0U;					/* bindless surface size */
	cmds[n++] = 0U;					/* bindless sampler lo (no modify) */
	cmds[n++] = 0U;					/* bindless sampler hi */
	cmds[n++] = 0U;					/* bindless sampler size */
	/* Invalidate state caches so the new bases take effect. */
	cmds[n++] = GFX_OP_PIPE_CONTROL(6);
	cmds[n++] = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STATE_CACHE_INVALIDATE | PIPE_CONTROL_CONST_CACHE_INVALIDATE | PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE;
	cmds[n++] = 0U;
	cmds[n++] = 0U;
	cmds[n++] = 0U;
	cmds[n++] = 0U;
	/* Marker B proves the pipeline passed STATE_BASE_ADDRESS and writes still land. */
	cmds[n++] = MI_STORE_DWORD_IMM_GEN4;
	cmds[n++] = (uint32_t)rt->va;
	cmds[n++] = (uint32_t)(rt->va >> 32);
	cmds[n++] = 0xd7a3f00dU;
	/* Marker C takes the GGTT path so the two address spaces can be told apart. */
	cmds[n++] = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	cmds[n++] = engine->hwsp->ggtt_offset + I915_GEM_HWS_SCRATCH * 4U;
	cmds[n++] = 0U;
	cmds[n++] = 0xc0ffee03U;
	cmds[n++] = MI_BATCH_BUFFER_END;
	cmds[n++] = MI_NOOP;

	/* The batch page is pushed out of the CPU caches before the engine fetches it. */
	kern_io_write_barrier();
	for (line = 0U; line < n * 4U; line += 64U)
		i915_selftest_clflush((const uint8_t *)cmds + line);
	if (flush != 0U)
		i915_selftest_wbinvd();

	irq = spin_lock_irqsave(&device->irq_lock);
	error = drv_i915_request_alloc(engine, NULL, NULL, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		drv_i915_gem_unbind_vm(batch);
		drv_i915_gem_destroy(device, batch);
		return error;
	}
	request->context = &engine->kernel_context;
	request->extra_count = 0U;
	request->batch = batch;
	request->batch_va = batch->va;

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
	hwsp = engine->status[I915_GEM_HWS_SEQNO];
	kern_logf("i915: sba %s batch=0x%llx probe=0x%llx surf=0x%llx dyn=0x%llx insn=0x%llx\n", name,
		(unsigned long long)batch->va, (unsigned long long)probe->va,
		(unsigned long long)surface, (unsigned long long)dynamic, (unsigned long long)instruction);
	kern_logf("i915: sba %s markerA=0x%08x markerB=0x%08x markerC=0x%08x hwsp=%u completed=%u seqno=%u\n",
		name, probe_cpu[1], rt_cpu[0], engine->status[I915_GEM_HWS_SCRATCH], hwsp, engine->completed_seqno, request->seqno);
	error = (engine->completed_seqno == request->seqno && rt_cpu[0] == 0xd7a3f00dU) ? 0 : EIO;
	if (error != 0) {
		kern_logf("i915: rcs0 head=0x%08x tail=0x%08x start=0x%08x ctl=0x%08x mi_mode=0x%08x acthd=0x%08x%08x\n",
			drv_i915_read32(device, RING_HEAD(engine->base)),
			drv_i915_read32(device, RING_TAIL(engine->base)),
			drv_i915_read32(device, RING_START(engine->base)),
			drv_i915_read32(device, RING_CTL(engine->base)),
			drv_i915_read32(device, RING_MI_MODE(engine->base)),
			drv_i915_read32(device, RING_ACTHD_UDW(engine->base)),
			drv_i915_read32(device, RING_ACTHD(engine->base)));
		kern_logf("i915: rcs0 ipeir=0x%08x ipehr=0x%08x instdone=0x%08x esr=0x%08x eir=0x%08x fault=0x%08x elsp=0x%08x%08x\n",
			drv_i915_read32(device, engine->base + 0x64U),
			drv_i915_read32(device, RING_IPEHR(engine->base)),
			drv_i915_read32(device, RING_INSTDONE(engine->base)),
			drv_i915_read32(device, RING_ESR(engine->base)),
			drv_i915_read32(device, RING_EIR(engine->base)),
			drv_i915_read32(device, 0xcec4U),
			drv_i915_read32(device, RING_EXECLIST_STATUS_HI(engine->base)),
			drv_i915_read32(device, RING_EXECLIST_STATUS_LO(engine->base)));
	}

	/* A hung batch is still released: the space is never reused within the boot. */
	drv_i915_gem_unbind_vm(batch);
	drv_i915_gem_destroy(device, batch);
	return error;
}

int
drv_i915_draw_selftest(
	struct i915_device *device)
{
	struct i915_engine *engine;
	struct i915_gem_object *rt;
	struct i915_gem_object *surface;
	struct i915_gem_object *dynamic;
	struct i915_gem_object *instruction;
	int error;

	engine = &device->engines[I915_ENGINE_RCS0];

	/* One page each: render target and the three state heaps. */
	if (drv_i915_gem_create(device, 4096U, &rt) != 0)
		return EIO;
	if (drv_i915_gem_create(device, 4096U, &surface) != 0)
		return EIO;
	if (drv_i915_gem_create(device, 4096U, &dynamic) != 0)
		return EIO;
	if (drv_i915_gem_create(device, 4096U, &instruction) != 0)
		return EIO;
	/* Bound in reverse creation order: the VA order then opposes the physical order. */
	if (drv_i915_gem_bind_vm(&engine->kernel_vm, instruction) != 0 ||
	    drv_i915_gem_bind_vm(&engine->kernel_vm, dynamic) != 0 ||
	    drv_i915_gem_bind_vm(&engine->kernel_vm, surface) != 0 ||
	    drv_i915_gem_bind_vm(&engine->kernel_vm, rt) != 0)
		return EIO;
	kern_logf("i915: draw step1 heaps surf=0x%llx dyn=0x%llx insn=0x%llx rt=0x%llx\n",
		(unsigned long long)surface->va, (unsigned long long)dynamic->va,
		(unsigned long long)instruction->va, (unsigned long long)rt->va);
	kern_logf("i915: draw step1 ptes rt=0x%llx surf=0x%llx dyn=0x%llx insn=0x%llx (paddr rt=0x%llx dyn=0x%llx)\n",
		(unsigned long long)drv_i915_ppgtt_lookup(&engine->kernel_vm, rt->va),
		(unsigned long long)drv_i915_ppgtt_lookup(&engine->kernel_vm, surface->va),
		(unsigned long long)drv_i915_ppgtt_lookup(&engine->kernel_vm, dynamic->va),
		(unsigned long long)drv_i915_ppgtt_lookup(&engine->kernel_vm, instruction->va),
		(unsigned long long)rt->run.paddr, (unsigned long long)dynamic->run.paddr);

	/* Every variant runs even after a failure: the trailing no-op shows whether the engine survived. */
	error = 0;
	if (i915_draw_sba_run(device, "none", rt, NULL, 0ULL, 0ULL, 0ULL, 0U) != 0)
		error = EIO;
	if (i915_draw_sba_run(device, "surf<-surf", rt, NULL, surface->va, 0ULL, 0ULL, 0U) != 0)
		error = EIO;
	if (i915_draw_sba_run(device, "dyn<-dyn", rt, NULL, 0ULL, dynamic->va, 0ULL, 0U) != 0)
		error = EIO;
	if (i915_draw_sba_run(device, "all", rt, NULL, surface->va, dynamic->va, instruction->va, 0U) != 0)
		error = EIO;
	if (i915_draw_sba_run(device, "none", rt, NULL, 0ULL, 0ULL, 0ULL, 0U) != 0)
		error = EIO;
	kern_logf("i915: draw step1 %s\n", error == 0 ? "passed (real-heap SBA parses)" : "FAILED");

	drv_i915_gem_unbind_vm(rt);
	drv_i915_gem_unbind_vm(surface);
	drv_i915_gem_unbind_vm(dynamic);
	drv_i915_gem_unbind_vm(instruction);
	drv_i915_gem_destroy(device, rt);
	drv_i915_gem_destroy(device, surface);
	drv_i915_gem_destroy(device, dynamic);
	drv_i915_gem_destroy(device, instruction);
	return error;
}
