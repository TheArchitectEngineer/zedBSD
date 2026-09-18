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
#include "linux/i915-workarounds.inc"
#include "draw_fixture.h"

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
	request->extra[1] = ((I915_CLEAR_WIDTH * 4U) - 1U) |
		(GEN12_MOCS(I915_MOCS_UNCACHED_INDEX) << 21);
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

/* The render target the 3D pipeline draws into: one page of linear BGRA. */
#define I915_DRAW_WIDTH			32U
#define I915_DRAW_HEIGHT		32U
#define I915_DRAW_PITCH			(I915_DRAW_WIDTH * 4U)

/* Offsets within the surface state heap. */
#define I915_DRAW_BINDING_TABLE_OFFSET	0U
#define I915_DRAW_SURFACE_STATE_OFFSET	64U

/* Offsets within the dynamic state heap; each state is 64-byte aligned. */
#define I915_DRAW_COLOR_CALC_OFFSET	512U
#define I915_DRAW_BLEND_OFFSET		576U
#define I915_DRAW_CC_VIEWPORT_OFFSET	640U
#define I915_DRAW_CPS_STATE_OFFSET	768U

/* Offsets within the vertex buffer page. */
#define I915_DRAW_POSITION_OFFSET	2048U
#define I915_DRAW_VUE_HEADER_OFFSET	2112U

/* The markers the batch stores so a hang can be placed within it. */
#define I915_DRAW_MARKER_BEFORE		0xa5a50001U
#define I915_DRAW_MARKER_AFTER		0xd7a3f00dU
#define I915_DRAW_MARKER_MIDDRAW	0xc5c50003U

/*
 * A constant-colour SIMD8 fragment shader, assembled for Tiger Lake from
 * plan/ws031/shaders/const_color_ps.asm:
 *
 *   (W) mov (8|M0)  r112:f  0x3f800000:f        red
 *   (W) mov (8|M0)  r113:f  0x0:f               green
 *   (W) mov (8|M0)  r114:f  0x0:f               blue
 *   (W) mov (8|M0)  r115:f  0x3f800000:f        alpha
 *       send.render (8|M0) null r112 null 0x0 0x08031400 {EOT,@1}
 *
 * The descriptor is a SIMD8 single-source render target write to binding table
 * entry zero, marked as the last render target, with a four-register message
 * and no response.  A send that retires the thread must source its payload from
 * the high register file, which is why the colours are built in r112-r115.
 */
/*
 * Constant opaque-red SIMD8 fragment shader, produced by the real Intel
 * compiler (brw_compile_fs, ADL-P) and disassembled with gentool:
 *
 *   mov (8)  r127:d 0x3f800000   ; red
 *   mov (8)  r124:d 0            ; green
 *   mov (8)  r125:d 0            ; blue
 *   mov (8)  r126:d 0x3f800000   ; alpha
 *   sendc.render (8) null r127 r124 0xC0 0x02031400 {EOT,@1}
 *      ; SIMD8 single-source RT write, last RT, bti 0, SPLIT SEND wr:1+3
 *
 * The render target write is a split send (mlen=1 payload r127, ex_mlen=3
 * payload r124-r126, ex_desc carries ex_mlen), which is the form Gen12
 * requires; a single send with mlen=4 assembles but the hardware does not
 * process it.
 */
/*
 * Constant opaque-red fragment shader from the real Intel compiler (ADL-P):
 * SIMD8 variant at byte 0, SIMD16 variant at byte 128 (dispatch_grf_start=2).
 */
/* BISECT: reference SIMD8 PS with null_rt render target write (no memory write). */
/* Test C: reference const-colour PS with an A64 data-cache entry marker
 * (send.hdc1 a64_untyped_write of 0xc0ffee01 to 0x100400c10) emitted before
 * the RT write.  refps_marker / brw_compile_fs, SIMD8+SIMD16, grf_start=2. */
static const uint32_t i915_draw_const_color_ps[] = {
	0x80000061U, 0x31010110U, 0x000001e4U, 0x00000000U,
	0x00030061U, 0x06054220U, 0x00000000U, 0xc0ffee01U,
	0x00030061U, 0x7f054660U, 0x00000000U, 0x3f800000U,
	0x00030061U, 0x7c054660U, 0x00000000U, 0x00000000U,
	0x00030061U, 0x7d054660U, 0x00000000U, 0x00000000U,
	0x00030061U, 0x7e054660U, 0x00000000U, 0x3f800000U,
	0x80030061U, 0x02264aa0U, 0x00000000U, 0x00000001U,
	0x80030161U, 0x02064aa0U, 0x00000000U, 0x00400c10U,
	0x80000101U, 0x00000000U, 0x00000000U, 0x00000000U,
	0x00030061U, 0x04260660U, 0x00000224U, 0x00000000U,
	0x00030161U, 0x04060660U, 0x00000204U, 0x00000000U,
	0x01839031U, 0x00000000U, 0xcdfa0414U, 0x019a060cU,
	0x00030132U, 0x00000004U, 0x58007f0cU, 0x00c47c1cU,
	0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
	0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
	0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
	0x80000061U, 0x31010110U, 0x000001e4U, 0x00000000U,
	0x00040061U, 0x08054220U, 0x00000000U, 0xc0ffee01U,
	0x00040061U, 0x7e054660U, 0x00000000U, 0x3f800000U,
	0x00040061U, 0x78054660U, 0x00000000U, 0x00000000U,
	0x00040061U, 0x7a054660U, 0x00000000U, 0x00000000U,
	0x00040061U, 0x7c054660U, 0x00000000U, 0x3f800000U,
	0x80030061U, 0x02264aa0U, 0x00000000U, 0x00000001U,
	0x80030161U, 0x02064aa0U, 0x00000000U, 0x00400c10U,
	0x80000101U, 0x00000000U, 0x00000000U, 0x00000000U,
	0x00030061U, 0x04260660U, 0x00000224U, 0x00000000U,
	0x00130061U, 0x06260660U, 0x00000224U, 0x00000000U,
	0x00030261U, 0x04060660U, 0x00000204U, 0x00000000U,
	0x00130261U, 0x06060660U, 0x00000204U, 0x00000000U,
	0x01849031U, 0x00000000U, 0xcdfa0424U, 0x01960814U,
	0x00040132U, 0x00000004U, 0x50007e14U, 0x00c47834U,
};

/* The SIMD16 variant begins 128 bytes into the program. */
#define I915_DRAW_PS_SIMD16_OFFSET	128U

/* Kernels are named by a 64-byte aligned offset from the instruction base. */
#define I915_DRAW_PS_KERNEL_OFFSET	1024U

/* Spare room in the vertex page where the batch leaves its progress markers. */
#define I915_DRAW_MARKER_OFFSET		3072U

/* The render target holds this once the shader has run: opaque red as BGRA. */
#define I915_DRAW_EXPECTED_PIXEL	0xffff0000U

/*
 * Applies the Alder Lake-P render workarounds Linux programs through MMIO.
 *
 * These are engine and GT registers, not context state, so one write holds
 * until the next reset.  The multicast registers are steered to every slice
 * first, the way intel_gt_mcr_multicast_write does.
 */
static void
i915_draw_apply_engine_workarounds(
	struct i915_device *device,
	struct i915_engine *engine)
{
	uint32_t value;

	/* Wa_14015795083: render DOP clock gating off.  Firmware may lock this. */
	value = drv_i915_read32(device, GEN7_MISCCPCTL);
	drv_i915_write32(device, GEN7_MISCCPCTL, value & ~GEN12_DOP_CLOCK_GATE_RENDER_ENABLE);

	/* The row, sampler and cache registers are replicated per subslice. */
	drv_i915_write32(device, GEN8_MCR_SELECTOR, GEN11_MCR_MULTICAST);
	drv_i915_write32(device, GEN8_ROW_CHICKEN2,
		I915_WA_MASKED_ENABLE(GEN12_DISABLE_EARLY_READ | GEN12_PUSH_CONST_DEREF_HOLD_DIS));
	drv_i915_write32(device, GEN9_ROW_CHICKEN4, I915_WA_MASKED_ENABLE(GEN12_DISABLE_TDL_PUSH));
	drv_i915_write32(device, GEN10_SAMPLER_MODE,
		I915_WA_MASKED_ENABLE(ENABLE_SMALLPL | GEN11_INDIRECT_STATE_BASE_ADDR_OVERRIDE));

	/* Fixed-function clock gating and power-down are disabled on the render engine. */
	drv_i915_write32(device, GEN9_CS_DEBUG_MODE1, I915_WA_MASKED_ENABLE(FF_DOP_CLOCK_GATE_DISABLE));
	value = drv_i915_read32(device, GEN7_FF_THREAD_MODE);
	drv_i915_write32(device, GEN7_FF_THREAD_MODE, value | GEN12_FF_TESSELATION_DOP_GATE_DISABLE);
	drv_i915_write32(device, RING_PSMI_CTL(engine->base),
		I915_WA_MASKED_ENABLE(GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE | GEN8_RC_SEMA_IDLE_MSG_DISABLE));
	drv_i915_write32(device, GEN7_FF_SLICE_CS_CHICKEN1, I915_WA_MASKED_ENABLE(GEN9_FFSC_PERCTX_PREEMPT_CTRL));

	/* The command streamer's own reads and writes are uncached. */
	drv_i915_write32(device, RING_CMD_CCTL(engine->base),
		I915_WA_MASKED_FIELD(CMD_CCTL_WRITE_OVERRIDE_MASK | CMD_CCTL_READ_OVERRIDE_MASK,
			CMD_CCTL_MOCS_OVERRIDE(I915_MOCS_UNCACHED_INDEX, I915_MOCS_UNCACHED_INDEX)));
	kern_io_write_barrier();
	(void)drv_i915_read32(device, RING_CMD_CCTL(engine->base));
}

/*
 * A thread-ending instruction with no side effect: a render target write marked
 * null.  BISECT: pages the execution units might fetch from by mistake are
 * carpeted with it, so a thread that starts anywhere in them retires at once.
 */
static const uint32_t i915_draw_eot_only[4] = {
	0x00030032U, 0x00001004U, 0x58007024U, 0x00c40000U,
};

static void
i915_draw_fill_eot(
	void *page,
	unsigned offset,
	unsigned length)
{
	unsigned at;

	for (at = offset; at + sizeof(i915_draw_eot_only) <= offset + length; at += sizeof(i915_draw_eot_only))
		memcpy((uint8_t *)page + at, i915_draw_eot_only, sizeof(i915_draw_eot_only));
}

/* Emits one dword into a batch under construction. */
struct i915_draw_batch {
	uint32_t *cmds;
	unsigned count;
	unsigned capacity;
};

static void
i915_draw_emit(
	struct i915_draw_batch *batch,
	uint32_t dword)
{
	/* A full batch drops the write; the caller checks the count afterwards. */
	if (batch->count >= batch->capacity)
		return;

	batch->cmds[batch->count] = dword;
	batch->count++;
}

/* Emits a packet header followed by count-1 zero dwords. */
static void
i915_draw_emit_disabled(
	struct i915_draw_batch *batch,
	uint32_t opcode,
	uint32_t dwords)
{
	uint32_t index;

	i915_draw_emit(batch, GEN12_CMD_HEADER(opcode, dwords));
	for (index = 1U; index < dwords; index++)
		i915_draw_emit(batch, 0U);
}

/* Emits an MI_STORE_DWORD_IMM into the batch's own address space. */
static void
i915_draw_emit_marker(
	struct i915_draw_batch *batch,
	uint64_t address,
	uint32_t value)
{
	i915_draw_emit(batch, MI_STORE_DWORD_IMM_GEN4);
	i915_draw_emit(batch, (uint32_t)address);
	i915_draw_emit(batch, (uint32_t)(address >> 32));
	i915_draw_emit(batch, value);
}

/* Emits a stalling flush or a state-cache invalidate around a base change. */
static void
i915_draw_emit_pipe_control(
	struct i915_draw_batch *batch,
	uint32_t flags)
{
	unsigned index;

	/* The stalling render/depth flush that precedes SBA also needs the HDC pipeline flush. */
	i915_draw_emit(batch, GFX_OP_PIPE_CONTROL(6) |
		((flags & PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH) != 0U ? PIPE_CONTROL0_HDC_PIPELINE_FLUSH : 0U));
	i915_draw_emit(batch, flags);
	for (index = 0U; index < 4U; index++)
		i915_draw_emit(batch, 0U);
}

/*
 * Writes the RENDER_SURFACE_STATE and binding table that name the render target.
 *
 * The binding table sits at the start of the surface heap and its single entry
 * is the byte offset of the surface state from the surface state base.
 */
static void
i915_draw_write_surface_state(
	uint32_t *heap,
	uint64_t rt_va,
	uint32_t mocs)
{
	uint32_t *surface;

	heap[I915_DRAW_BINDING_TABLE_OFFSET / 4U] = I915_DRAW_SURFACE_STATE_OFFSET;

	surface = &heap[I915_DRAW_SURFACE_STATE_OFFSET / 4U];
	memset(surface, 0, GEN12_RENDER_SURFACE_STATE_DWORDS * 4U);

	/* A linear two-dimensional BGRA target; alignment fields are unused when linear. */
	surface[0] = (GEN12_SURFTYPE_2D << 29) |
		(GEN12_FORMAT_B8G8R8A8_UNORM << 18) |
		(GEN12_SURFACE_ALIGN_4 << 16) |
		(GEN12_SURFACE_ALIGN_4 << 14) |
		(GEN12_TILEMODE_LINEAR << 12);
	/*
	 * dword 1 carries MOCS (30:24), the QPitch, and bit 31 "Enable Unorm Path
	 * In Color Pipe" which the color pipe needs for a UNORM render target;
	 * isl sets it, and without it the render target write never completes.
	 */
	surface[1] = (1U << 31) | (mocs << 24) | 8U;
	surface[2] = (I915_DRAW_WIDTH - 1U) | ((I915_DRAW_HEIGHT - 1U) << 16);
	surface[3] = I915_DRAW_PITCH - 1U;
	/* dword 5: the mip-tail start LOD isl programs for a single-level surface. */
	surface[5] = 0x00000100U;

	/* Channels are taken straight from the target's own components. */
	surface[7] = (4U << 25) | (5U << 22) | (6U << 19) | (7U << 16);
	surface[8] = (uint32_t)rt_va;
	surface[9] = (uint32_t)(rt_va >> 32);
}

/* Writes the colour calculator, blend and viewport states the pipeline points at. */
static void
i915_draw_write_dynamic_state(
	uint32_t *heap)
{
	uint32_t *blend;
	uint32_t *viewport;

	memset(&heap[I915_DRAW_COLOR_CALC_OFFSET / 4U], 0, 256U);
	/* A zeroed CPS_STATE is the disabled coarse pixel shading state. */
	memset(&heap[I915_DRAW_CPS_STATE_OFFSET / 4U], 0, GEN12_CPS_STATE_DWORDS * 4U);

	/* BLEND_STATE is one dword of global controls followed by one entry per target. */
	blend = &heap[I915_DRAW_BLEND_OFFSET / 4U];
	blend[0] = 0U;
	blend[1] = 0U;
	blend[2] = 1U | (1U << 1) | (GEN12_COLORCLAMP_RTFORMAT << 2);

	/* The depth range is the full unit interval even though depth is unused. */
	viewport = &heap[I915_DRAW_CC_VIEWPORT_OFFSET / 4U];
	viewport[0] = GEN12_F32_0;
	viewport[1] = GEN12_F32_1;
}

/*
 * Writes the RECTLIST vertex data.
 *
 * A rectangle primitive is three screen-space vertices; the fourth corner is
 * implied.  With the vertex shader disabled the clipper reads each VUE straight
 * from the URB, so the vertex fetcher builds the VUE header from a separate
 * zero-filled buffer and the position from these coordinates.
 */
static void
i915_draw_write_vertices(
	uint32_t *page,
	uint32_t width_bits,
	uint32_t height_bits)
{
	uint32_t *position;

	memset(&page[I915_DRAW_POSITION_OFFSET / 4U], 0, 128U);

	position = &page[I915_DRAW_POSITION_OFFSET / 4U];
	position[0] = width_bits;	position[1] = height_bits;	position[2] = GEN12_F32_0;
	position[3] = GEN12_F32_0;	position[4] = height_bits;	position[5] = GEN12_F32_0;
	position[6] = GEN12_F32_0;	position[7] = GEN12_F32_0;	position[8] = GEN12_F32_0;
}

/* Emits the vertex fetch state: two buffers, two elements and the topology. */
static void
i915_draw_emit_vertex_state(
	struct i915_draw_batch *batch,
	uint64_t vb_va,
	uint32_t mocs)
{
	uint64_t header_va;

	/* Two VERTEX_BUFFER_STATE structures follow the header dword. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VERTEX_BUFFERS,
		1U + 2U * GEN12_VERTEX_BUFFER_STATE_DWORDS));

	/* Buffer 0 holds three positions of three floats each. */
	i915_draw_emit(batch, (0U << 26) | GEN12_VERTEX_BUFFER_L3_BYPASS_DISABLE | (mocs << 16) | (1U << 14) | 12U);
	i915_draw_emit(batch, (uint32_t)(vb_va + I915_DRAW_POSITION_OFFSET));
	i915_draw_emit(batch, (uint32_t)((vb_va + I915_DRAW_POSITION_OFFSET) >> 32));
	i915_draw_emit(batch, 36U);

	/* Buffer 1 holds the zeroed VUE header, read with a zero pitch by every vertex. */
	header_va = vb_va + I915_DRAW_VUE_HEADER_OFFSET;
	i915_draw_emit(batch, (1U << 26) | GEN12_VERTEX_BUFFER_L3_BYPASS_DISABLE | (mocs << 16) | (1U << 14) | 0U);
	i915_draw_emit(batch, (uint32_t)header_va);
	i915_draw_emit(batch, (uint32_t)(header_va >> 32));
	i915_draw_emit(batch, 16U);

	/* Element 0 is the VUE header; only its first dword comes from memory. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VERTEX_ELEMENTS,
		1U + 2U * GEN12_VERTEX_ELEMENT_STATE_DWORDS));
	i915_draw_emit(batch, (1U << 26) | (1U << 25) |
		(GEN12_FORMAT_R32G32B32A32_FLOAT << 16) | 0U);
	i915_draw_emit(batch, (GEN12_VFCOMP_STORE_SRC << 28) | (GEN12_VFCOMP_STORE_0 << 24) |
		(GEN12_VFCOMP_STORE_0 << 20) | (GEN12_VFCOMP_STORE_0 << 16));

	/* Element 1 is the position; W is supplied as one because the buffer holds XYZ. */
	i915_draw_emit(batch, (0U << 26) | (1U << 25) |
		(GEN12_FORMAT_R32G32B32_FLOAT << 16) | 0U);
	i915_draw_emit(batch, (GEN12_VFCOMP_STORE_SRC << 28) | (GEN12_VFCOMP_STORE_SRC << 24) |
		(GEN12_VFCOMP_STORE_SRC << 20) | (GEN12_VFCOMP_STORE_1_FP << 16));

	/* Statistics are enabled so the counters below report what the pipeline saw. */
	i915_draw_emit(batch, (GEN12_CMD_3DSTATE_VF_STATISTICS << 16) | 1U);
	i915_draw_emit_disabled(batch, GEN12_CMD_3DSTATE_VF, GEN12_3DSTATE_VF_DWORDS);
	i915_draw_emit_disabled(batch, GEN12_CMD_3DSTATE_VF_SGVS, GEN12_3DSTATE_VF_SGVS_DWORDS);
	i915_draw_emit_disabled(batch, GEN12_CMD_3DSTATE_VF_SGVS_2, GEN12_3DSTATE_VF_SGVS_2_DWORDS);

	/* Instancing is per element and survives in the context, so both are cleared. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_INSTANCING,
		GEN12_3DSTATE_VF_INSTANCING_DWORDS));
	i915_draw_emit(batch, 0U);
	i915_draw_emit(batch, 0U);
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_INSTANCING,
		GEN12_3DSTATE_VF_INSTANCING_DWORDS));
	i915_draw_emit(batch, 1U);
	i915_draw_emit(batch, 0U);

	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_TOPOLOGY,
		GEN12_3DSTATE_VF_TOPOLOGY_DWORDS));
	i915_draw_emit(batch, GEN12_3DPRIM_RECTLIST);
}

/*
 * Emits the URB and push-constant allocation.
 *
 * The URB is handed out in 8 KiB chunks in pipeline order.  No stage uses push
 * constants, so the whole URB starts at chunk zero and only the vertex stage
 * receives entries; its minimum of 64 entries of one 64-byte slot each fits in
 * a single chunk.
 */
static void
i915_draw_emit_urb(
	struct i915_draw_batch *batch)
{
	uint32_t opcode;

	for (opcode = GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_VS;
	     opcode < GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_PS;
	     opcode++)
		i915_draw_emit_disabled(batch, opcode, GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS);
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_PS,
		GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS));
	i915_draw_emit(batch, GEN12_PUSH_CONSTANT_KB);

	/* The vertex stage owns chunk zero: 64 entries of one 64-byte slot. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_URB_ALLOC_VS,
		GEN12_3DSTATE_URB_ALLOC_DWORDS));
	i915_draw_emit(batch, (4U << 10) | (4U << 21));		/* start chunk 4, size 1 */
	/* Mesa's intel_get_urb_config gives the VS the whole URB: 3576 entries. */
	i915_draw_emit(batch, 3576U | (3576U << 16));

	/* The other stages start past the vertex chunk and receive no entries. */
	for (opcode = GEN12_CMD_3DSTATE_URB_ALLOC_HS;
	     opcode <= GEN12_CMD_3DSTATE_URB_ALLOC_GS;
	     opcode++) {
		i915_draw_emit(batch, GEN12_CMD_HEADER(opcode, GEN12_3DSTATE_URB_ALLOC_DWORDS));
		i915_draw_emit(batch, (5U << 10) | (5U << 21));
		i915_draw_emit(batch, 0U);
	}
}

/*
 * Emits the rasterizer and pixel state.
 *
 * A screen-space rectangle needs no viewport transform and no perspective
 * divide.  The pixel shader is left invalid in this step: the statistics
 * counters still report whether the rectangle reached the pixel dispatcher.
 */
/* What the textured draw changes in the command list (NULL = the single-colour draw, byte for byte). */
struct i915_draw_tex_opts {
	uint32_t ps_dw3;		/* sampler count / binding table entry count */
	uint32_t ps_dw7;		/* dispatch GRF start (from the compiler's prog_data) */
	uint32_t ps_extra_dw1;		/* valid, UAV, source depth / W as the compiler requires */
	uint32_t ssp_dw0, ssp_dw1;	/* 3DSTATE_SAMPLER_STATE_POINTERS_PS */
};

static void
i915_draw_emit_raster_state(
	struct i915_draw_batch *batch,
	unsigned pixel_shader_valid,
	const struct i915_draw_tex_opts *tex)
{
	uint32_t index;

	/* The clipper consumes the URB entries directly, without a perspective divide. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_CLIP, GEN12_3DSTATE_CLIP_DWORDS));
	i915_draw_emit(batch, 1U << 10);			/* statistics enable */
	i915_draw_emit(batch, 1U << 9);
	i915_draw_emit(batch, 0U);

	/* Viewport mapping is disabled, as a rectangle list requires. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SF, GEN12_3DSTATE_SF_DWORDS));
	i915_draw_emit(batch, 1U << 10);			/* statistics enable */
	i915_draw_emit(batch, GEN12_URB_DEREF_BLOCK_SIZE_32 << 29);	/* deref matches 3576-entry URB */
	i915_draw_emit(batch, 0U);

	/*
	 * Both faces are filled solid and neither is culled.  The cull mode has to
	 * be named: its zero value is CULLMODE_BOTH, which discards everything.
	 */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_RASTER, GEN12_3DSTATE_RASTER_DWORDS));
	i915_draw_emit(batch, GEN12_CULLMODE_NONE << 16);
	for (index = 2U; index < GEN12_3DSTATE_RASTER_DWORDS; index++)
		i915_draw_emit(batch, 0U);

	/* The setup stage forwards no attributes; only the position is read. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SBE, GEN12_3DSTATE_SBE_DWORDS));
	i915_draw_emit(batch, (1U << 5) | (1U << 11) | (1U << 28) | (1U << 29));
	i915_draw_emit(batch, 0U);			/* point sprite texture coordinates */
	i915_draw_emit(batch, 0U);			/* constant interpolation */
	/* Sixteen two-bit slots per dword, every one of them all four components. */
	for (index = 4U; index < GEN12_3DSTATE_SBE_DWORDS; index++)
		i915_draw_emit(batch, 0xffffffffU);

	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_WM, GEN12_3DSTATE_WM_DWORDS));
	i915_draw_emit(batch, (1U << 31) | (2U << 19) | (1U << 21));	/* WM DW1: StatEnable|ForceThreadDispatch=ForceON|EDSC_PSEXEC (Test B) */

	/*
	 * 3DSTATE_PS: the thread count must be non-zero even with dispatch off,
	 * which the documentation notes is required to keep the GPU from hanging.
	 */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS, GEN12_3DSTATE_PS_DWORDS));
	for (index = 1U; index < GEN12_3DSTATE_PS_DWORDS; index++) {
		switch (index) {
		case 1U:
			/* The kernel is named by its offset from the instruction base. */
			i915_draw_emit(batch, pixel_shader_valid != 0U ? I915_DRAW_PS_KERNEL_OFFSET : 0U);
			break;
		case 3U:
			/* One binding table entry for the render target (BLORP sets this). */
			i915_draw_emit(batch, tex != NULL ? tex->ps_dw3 : (1U << 18));
			break;
		case 6U:
			i915_draw_emit(batch, ((GEN12_MAX_THREADS_PER_PSD - 1U) << 23) |
				(pixel_shader_valid != 0U ? 1U : 0U));
			break;
		case 7U:
			i915_draw_emit(batch, tex != NULL ? tex->ps_dw7 :
				(pixel_shader_valid != 0U ? (2U << 16) : 0U));
			break;
		default:
			i915_draw_emit(batch, 0U);
			break;
		}
	}

	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS_EXTRA, GEN12_3DSTATE_PS_EXTRA_DWORDS));
	i915_draw_emit(batch, tex != NULL ? tex->ps_extra_dw1 :
		(pixel_shader_valid != 0U ? ((1U << 31) | (1U << 2)) : 0U));	/* PixelShaderValid | HasUAV (marker store) */

	/* The target is writeable so the colour pipe is not short-circuited. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS_BLEND, GEN12_3DSTATE_PS_BLEND_DWORDS));
	i915_draw_emit(batch, 1U << 30);

	i915_draw_emit_disabled(batch, GEN12_CMD_3DSTATE_WM_DEPTH_STENCIL,
		GEN12_3DSTATE_WM_DEPTH_STENCIL_DWORDS);
}

/* Emits the null depth, stencil and hierarchical depth buffers. */
static void
i915_draw_emit_depth_state(
	struct i915_draw_batch *batch)
{
	uint32_t index;

	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DEPTH_BUFFER,
		GEN12_3DSTATE_DEPTH_BUFFER_DWORDS));
	i915_draw_emit(batch, (GEN12_SURFTYPE_NULL << 29) |
		(GEN12_DEPTH_FORMAT_D32_FLOAT << 24));
	for (index = 2U; index < GEN12_3DSTATE_DEPTH_BUFFER_DWORDS; index++)
		i915_draw_emit(batch, 0U);

	/* The stencil buffer carries its own surface type from this generation on. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_STENCIL_BUFFER,
		GEN12_3DSTATE_STENCIL_BUFFER_DWORDS));
	i915_draw_emit(batch, GEN12_SURFTYPE_NULL << 29);
	for (index = 2U; index < GEN12_3DSTATE_STENCIL_BUFFER_DWORDS; index++)
		i915_draw_emit(batch, 0U);
	i915_draw_emit_disabled(batch, GEN12_CMD_3DSTATE_HIER_DEPTH_BUFFER,
		GEN12_3DSTATE_HIER_DEPTH_BUFFER_DWORDS);
	i915_draw_emit_disabled(batch, GEN12_CMD_3DSTATE_CLEAR_PARAMS,
		GEN12_3DSTATE_CLEAR_PARAMS_DWORDS);
}

/*
 * Builds the whole 3D pipeline batch and returns its dword count.
 *
 * The order follows Mesa's BLORP, which is the smallest sequence known to draw
 * on this hardware: vertex state, then the pipeline, then the primitive.
 */
static unsigned
i915_draw_build_batch(
	uint32_t *cmds,
	unsigned capacity,
	const struct i915_gem_object *surface,
	const struct i915_gem_object *dynamic,
	const struct i915_gem_object *instruction,
	const struct i915_gem_object *vb,
	uint32_t mocs,
	unsigned pixel_shader_valid,
	const struct i915_draw_tex_opts *tex)
{
	struct i915_draw_batch batch;
	uint32_t index;

	batch.cmds = cmds;
	batch.count = 0U;
	batch.capacity = capacity;

	/* A stalling flush precedes the pipeline select and the base addresses. */
	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE);
	i915_draw_emit(&batch, GEN12_PIPELINE_SELECT_DWORD(GEN12_PIPELINE_SELECT_3D));

	/* The three heaps this draw reads from are anchored here. */
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_STATE_BASE_ADDRESS,
		GEN12_STATE_BASE_ADDRESS_DWORDS));
	i915_draw_emit(&batch, 1U | (mocs << 4));		/* general: base 0, modify */
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, mocs << 16);
	i915_draw_emit(&batch, 1U | (mocs << 4) | ((uint32_t)surface->va & 0xfffff000U));
	i915_draw_emit(&batch, (uint32_t)(surface->va >> 32));
	i915_draw_emit(&batch, 1U | (mocs << 4) | ((uint32_t)dynamic->va & 0xfffff000U));
	i915_draw_emit(&batch, (uint32_t)(dynamic->va >> 32));
	i915_draw_emit(&batch, 1U | (mocs << 4));		/* indirect: base 0, modify */
	i915_draw_emit(&batch, 0U);
	/* Instructions are fetched through the L3 into the instruction cache: write-back. */
	i915_draw_emit(&batch, 1U | (GEN12_MOCS(I915_MOCS_WRITEBACK_INDEX) << 4) |
		((uint32_t)instruction->va & 0xfffff000U));
	i915_draw_emit(&batch, (uint32_t)(instruction->va >> 32));
	i915_draw_emit(&batch, 1U | (0xfffffU << 12));	/* general size */
	i915_draw_emit(&batch, 1U | (0xfffffU << 12));	/* dynamic size */
	i915_draw_emit(&batch, 1U | (0xfffffU << 12));	/* indirect size */
	i915_draw_emit(&batch, 1U | (0xfffffU << 12));	/* instruction size */
	/* Bindless surface state shares the surface heap; bindless samplers are null. */
	i915_draw_emit(&batch, 1U | (mocs << 4) | ((uint32_t)surface->va & 0xfffff000U));
	i915_draw_emit(&batch, (uint32_t)(surface->va >> 32));
	i915_draw_emit(&batch, (4096U / 64U - 1U) << 12);
	i915_draw_emit(&batch, 1U | (mocs << 4));
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 0U);

	/* The state caches must be invalidated before anything reads the new bases. */
	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_STATE_CACHE_INVALIDATE | PIPE_CONTROL_CONST_CACHE_INVALIDATE |
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE);
	i915_draw_emit_marker(&batch, vb->va + I915_DRAW_MARKER_OFFSET, I915_DRAW_MARKER_BEFORE);

	/*
	 * The once-per-context state anv programs before its first draw: the
	 * depth-clear override cleared (it is not guaranteed zero in a fresh
	 * context and is a known source of hangs), the sample at the pixel centre,
	 * depth bounds off and the other stages' binding tables cleared.
	 */
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_WM_HZ_OP, GEN12_3DSTATE_WM_HZ_OP_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_AA_LINE_PARAMETERS,
		GEN12_3DSTATE_AA_LINE_PARAMETERS_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_WM_CHROMAKEY, GEN12_3DSTATE_WM_CHROMAKEY_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_POLY_STIPPLE_OFFSET,
		GEN12_3DSTATE_POLY_STIPPLE_OFFSET_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_LINE_STIPPLE, GEN12_3DSTATE_LINE_STIPPLE_DWORDS);
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SAMPLE_PATTERN,
		GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS));
	for (index = 1U; index < GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS; index++)
		i915_draw_emit(&batch, index == 8U ? GEN12_SAMPLE_PATTERN_1X_CENTRE : 0U);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_DEPTH_BOUNDS, GEN12_3DSTATE_DEPTH_BOUNDS_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_VS,
		GEN12_3DSTATE_POINTERS_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_HS,
		GEN12_3DSTATE_POINTERS_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_DS,
		GEN12_3DSTATE_POINTERS_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_GS,
		GEN12_3DSTATE_POINTERS_DWORDS);

	i915_draw_emit_vertex_state(&batch, vb->va, mocs);
	i915_draw_emit_urb(&batch);

	/* Each stage's push constants are declared empty rather than left undefined. */
	{
		static const uint32_t constant_opcodes[] = {
			GEN12_CMD_3DSTATE_CONSTANT_VS, GEN12_CMD_3DSTATE_CONSTANT_HS,
			GEN12_CMD_3DSTATE_CONSTANT_DS, GEN12_CMD_3DSTATE_CONSTANT_GS,
			GEN12_CMD_3DSTATE_CONSTANT_PS,
		};
		unsigned stage;

		for (stage = 0U; stage < 5U; stage++) {
			i915_draw_emit(&batch, GEN12_CMD_HEADER(constant_opcodes[stage],
				GEN12_3DSTATE_CONSTANT_DWORDS) | (mocs << 8));
			for (index = 1U; index < GEN12_3DSTATE_CONSTANT_DWORDS; index++)
				i915_draw_emit(&batch, 0U);
		}
	}

	/* The dynamic heap holds the colour calculator, blend state and viewport. */
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_CC_STATE_POINTERS,
		GEN12_3DSTATE_POINTERS_DWORDS));
	i915_draw_emit(&batch, I915_DRAW_COLOR_CALC_OFFSET | 1U);
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_BLEND_STATE_POINTERS,
		GEN12_3DSTATE_POINTERS_DWORDS));
	i915_draw_emit(&batch, I915_DRAW_BLEND_OFFSET | 1U);
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_CC,
		GEN12_3DSTATE_POINTERS_DWORDS));
	i915_draw_emit(&batch, I915_DRAW_CC_VIEWPORT_OFFSET);
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_CPS_POINTERS,
		GEN12_3DSTATE_POINTERS_DWORDS));
	i915_draw_emit(&batch, I915_DRAW_CPS_STATE_OFFSET);
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_BINDING_TABLE_POOL_ALLOC, 4U));
	i915_draw_emit(&batch, mocs);			/* pool disabled, MOCS still valid */
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 0U);

	/* One sample per pixel and that sample enabled. */
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_MULTISAMPLE,
		GEN12_3DSTATE_MULTISAMPLE_DWORDS);
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SAMPLE_MASK,
		GEN12_3DSTATE_SAMPLE_MASK_DWORDS));
	i915_draw_emit(&batch, 1U);

	/* Every geometry stage is disabled; the vertex fetcher feeds the clipper. */
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VS, GEN12_3DSTATE_VS_DWORDS));
	for (index = 1U; index < GEN12_3DSTATE_VS_DWORDS; index++)
		i915_draw_emit(&batch, index == 7U ? (1U << 10) : 0U);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_HS, GEN12_3DSTATE_HS_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_TE, GEN12_3DSTATE_TE_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_DS, GEN12_3DSTATE_DS_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_STREAMOUT, GEN12_3DSTATE_STREAMOUT_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_GS, GEN12_3DSTATE_GS_DWORDS);
	i915_draw_emit_disabled(&batch, GEN12_CMD_3DSTATE_PRIMITIVE_REPLICATION,
		GEN12_3DSTATE_PRIMITIVE_REPLICATION_DWORDS);

	i915_draw_emit_raster_state(&batch, pixel_shader_valid, tex);
	i915_draw_emit_depth_state(&batch);

	/* The textured draw names its sampler state (dynamic heap) before the binding table. */
	if (tex != NULL) {
		i915_draw_emit(&batch, tex->ssp_dw0);
		i915_draw_emit(&batch, tex->ssp_dw1);
	}

	/* The pixel shader's one binding table entry names the render target. */
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_PS,
		GEN12_3DSTATE_POINTERS_DWORDS));
	i915_draw_emit(&batch, I915_DRAW_BINDING_TABLE_OFFSET);

	/* Rasterization is clipped to the target's own extent. */
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DRAWING_RECTANGLE,
		GEN12_3DSTATE_DRAWING_RECTANGLE_DWORDS));
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, (I915_DRAW_WIDTH - 1U) | ((I915_DRAW_HEIGHT - 1U) << 16));
	i915_draw_emit(&batch, 0U);

	/*
	 * The pixel pipeline is synced before the primitive: a command-streamer
	 * stall that also waits on the pixel scoreboard settles the windower and
	 * pixel-shader-setup state a fresh context leaves undefined.
	 */
	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_STALL_AT_SCOREBOARD | PIPE_CONTROL_DEPTH_STALL_ENABLE);

	/* CS marker: proves the command streamer cleared the pre-draw stall and issued the primitive. */
	i915_draw_emit_marker(&batch, vb->va + I915_DRAW_MARKER_OFFSET + 8U, I915_DRAW_MARKER_MIDDRAW);

	/* Three sequential vertices of one instance form the rectangle. */
	i915_draw_emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DPRIMITIVE, GEN12_3DPRIMITIVE_DWORDS));
	i915_draw_emit(&batch, GEN12_3DPRIM_RECTLIST);
	i915_draw_emit(&batch, 3U);
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 1U);
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 0U);

	/* The draw's writes are flushed before the marker reports completion. */
	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DC_FLUSH_ENABLE |
		PIPE_CONTROL_FLUSH_ENABLE);
	i915_draw_emit_marker(&batch, vb->va + I915_DRAW_MARKER_OFFSET + 4U, I915_DRAW_MARKER_AFTER);

	i915_draw_emit(&batch, MI_BATCH_BUFFER_END);
	i915_draw_emit(&batch, MI_NOOP);

	return batch.count;
}

/* ---- WS031 parity draw harness: the same fixture, addressed by VA only ---- */

_Static_assert(I915_DRAW_FIXTURE_WIDTH == I915_DRAW_WIDTH &&
	I915_DRAW_FIXTURE_HEIGHT == I915_DRAW_HEIGHT &&
	I915_DRAW_FIXTURE_MARKER_OFFSET == I915_DRAW_MARKER_OFFSET &&
	I915_DRAW_FIXTURE_MARKER_BEFORE == I915_DRAW_MARKER_BEFORE &&
	I915_DRAW_FIXTURE_MARKER_AFTER == I915_DRAW_MARKER_AFTER &&
	I915_DRAW_FIXTURE_MARKER_MIDDRAW == I915_DRAW_MARKER_MIDDRAW &&
	I915_DRAW_FIXTURE_EXPECTED_PIXEL == I915_DRAW_EXPECTED_PIXEL &&
	I915_DRAW_FIXTURE_PS_OFFSET == I915_DRAW_PS_KERNEL_OFFSET &&
	I915_DRAW_FIXTURE_PS_BYTES == sizeof(i915_draw_const_color_ps),
	"draw_fixture.h must describe this fixture");

void
drv_i915_draw_fixture_write_state(void *state_page, uint64_t rt_va, uint32_t mocs)
{
	memset(state_page, 0, 4096U);
	i915_draw_write_surface_state(state_page, rt_va, mocs);
	i915_draw_write_dynamic_state(state_page);
	memcpy((uint8_t *)state_page + I915_DRAW_PS_KERNEL_OFFSET, i915_draw_const_color_ps,
		sizeof(i915_draw_const_color_ps));
	i915_draw_fill_eot(state_page,
		I915_DRAW_PS_KERNEL_OFFSET + sizeof(i915_draw_const_color_ps),
		I915_DRAW_POSITION_OFFSET - I915_DRAW_PS_KERNEL_OFFSET - sizeof(i915_draw_const_color_ps));
	i915_draw_write_vertices(state_page, 0x42000000U, 0x42000000U);
}

unsigned
drv_i915_draw_fixture_build_batch(uint32_t *cmds, unsigned capacity, uint64_t state_va, uint32_t mocs)
{
	static struct i915_gem_object state;   /* only ->va is read by the builder */

	memset(&state, 0, sizeof(state));
	state.va = state_va;
	return i915_draw_build_batch(cmds, capacity, &state, &state, &state, &state, mocs, 1U, NULL);
}

/* ---- T1: the textured draw (generated shader / surface / sampler words) ---- */

#include "tex_fixture_gen.inc"

_Static_assert(TEXFIX_PS_BYTES <= I915_DRAW_POSITION_OFFSET - I915_DRAW_PS_KERNEL_OFFSET,
	"the sampling PS must fit between the kernel offset and the vertex data");
/*
 * What the rest of the batch assumes about the kernel.  The payload layout (GRF
 * start, source depth / W) is NOT assumed: it is taken from the generated
 * 3DSTATE_PS DW7 and 3DSTATE_PS_EXTRA words.
 */
_Static_assert(TEXFIX_PS_DISPATCH_8 == 1U && TEXFIX_PS_NUM_VARYING == 0U &&
	TEXFIX_PS_USES_POS_OFFSET == 0U && TEXFIX_PS_TOTAL_SCRATCH == 0U && TEXFIX_PS_PUSH_SIZE0 == 0U,
	"this batch dispatches the SIMD8 kernel at offset 0 and provides no varyings, position offsets, scratch or push constants");
_Static_assert(TEXFIX_3DSTATE_PS_EXTRA_DW0 == 0x784f0000U &&
	(TEXFIX_3DSTATE_PS_DW7 >> 16) == TEXFIX_PS_GRF_START_8,
	"generated packet words must be the 3DSTATE_PS_EXTRA this batch emits and carry the SIMD8 GRF start");
_Static_assert(TEXFIX_TEX_VA_PLACEHOLDER == I915_TEX_FIXTURE_TEX_VA &&
	TEXFIX_TEX_WIDTH == I915_TEX_FIXTURE_TEX_W && TEXFIX_TEX_HEIGHT == I915_TEX_FIXTURE_TEX_H &&
	TEXFIX_TEX_SIZE == I915_TEX_FIXTURE_TEX_BYTES && TEXFIX_TEX_ROW_PITCH == 4U * I915_TEX_FIXTURE_TEX_W &&
	TEXFIX_SAMPLER_OFFSET == I915_TEX_FIXTURE_SAMPLER_OFFSET,
	"draw_fixture.h must describe the generated texture fixture");
_Static_assert(I915_TEX_FIXTURE_TEX_RSS_OFFSET >= I915_DRAW_SURFACE_STATE_OFFSET + 64U &&
	I915_TEX_FIXTURE_TEX_RSS_OFFSET + 64U <= I915_DRAW_COLOR_CALC_OFFSET &&
	I915_TEX_FIXTURE_SAMPLER_OFFSET >= I915_DRAW_CPS_STATE_OFFSET + GEN12_CPS_STATE_DWORDS * 4U &&
	I915_TEX_FIXTURE_SAMPLER_OFFSET + sizeof(texfix_sampler) <= I915_DRAW_PS_KERNEL_OFFSET &&
	(I915_TEX_FIXTURE_SAMPLER_OFFSET & 31U) == 0U && (I915_TEX_FIXTURE_TEX_RSS_OFFSET & 63U) == 0U,
	"the added states must not overlap the existing ones and must keep their alignment");

void
drv_i915_tex_fixture_write_state(void *state_page, uint64_t rt_va, uint64_t tex_va, uint32_t mocs)
{
	uint32_t *heap = state_page;
	uint32_t *rss = &heap[I915_TEX_FIXTURE_TEX_RSS_OFFSET / 4U];

	drv_i915_draw_fixture_write_state(state_page, rt_va, mocs);

	/* The sampling PS replaces the single-colour one; the EOT carpet follows it. */
	memset((uint8_t *)state_page + I915_DRAW_PS_KERNEL_OFFSET, 0,
		I915_DRAW_POSITION_OFFSET - I915_DRAW_PS_KERNEL_OFFSET);
	memcpy((uint8_t *)state_page + I915_DRAW_PS_KERNEL_OFFSET, texfix_ps, TEXFIX_PS_BYTES);
	i915_draw_fill_eot(state_page, I915_DRAW_PS_KERNEL_OFFSET + TEXFIX_PS_BYTES,
		I915_DRAW_POSITION_OFFSET - I915_DRAW_PS_KERNEL_OFFSET - TEXFIX_PS_BYTES);

	/* Binding table entry 1 -> the texture's surface state (isl), its address patched in. */
	heap[I915_DRAW_BINDING_TABLE_OFFSET / 4U + 1U] = I915_TEX_FIXTURE_TEX_RSS_OFFSET;
	memcpy(rss, texfix_tex_rss, sizeof(texfix_tex_rss));
	rss[8] = (uint32_t)tex_va;
	rss[9] = (uint32_t)(tex_va >> 32);
	(void)mocs;	/* the generated surface state already carries MOCS 6 (checked by the caller's test) */

	memcpy(&heap[I915_TEX_FIXTURE_SAMPLER_OFFSET / 4U], texfix_sampler, sizeof(texfix_sampler));
}

_Static_assert(I915_TEX_FIXTURE_TEX_B_RSS_OFFSET >= I915_TEX_FIXTURE_TEX_RSS_OFFSET + 64U &&
	I915_TEX_FIXTURE_TEX_B_RSS_OFFSET + 64U <= I915_DRAW_COLOR_CALC_OFFSET &&
	(I915_TEX_FIXTURE_TEX_B_RSS_OFFSET & 63U) == 0U,
	"texture B's surface state must sit in the surface heap, 64-byte aligned, after texture A's");

void
drv_i915_tex_fixture_write_state_ab_filter(void *state_page, uint64_t rt_va, uint64_t tex_a_va,
	uint64_t tex_b_va, unsigned bind_b, unsigned linear, uint32_t mocs)
{
	uint32_t *heap = state_page;

	drv_i915_tex_fixture_write_state_ab(state_page, rt_va, tex_a_va, tex_b_va, bind_b, mocs);
	if (linear != 0U)
		memcpy(&heap[I915_TEX_FIXTURE_SAMPLER_OFFSET / 4U], texfix_sampler_linear,
			sizeof(texfix_sampler_linear));
}

uint32_t
drv_i915_tex_fixture_expected_pixel_linear(const uint8_t *rgba, unsigned x, unsigned y, int *inexact)
{
	/* texel-space coordinate in eighths: (2*pixel + 1)/8 - 1/2 = (2*pixel - 3)/8 */
	int cx = 2 * (int)x - 3, cy = 2 * (int)y - 3;
	int x0 = cx >= 0 ? cx / 8 : -1, y0 = cy >= 0 ? cy / 8 : -1;
	unsigned fx = (unsigned)(cx - 8 * x0), fy = (unsigned)(cy - 8 * y0);
	int xs[2], ys[2];
	unsigned w[2][2], ch, i, j;
	uint32_t out[4];

	xs[0] = x0; xs[1] = x0 + 1; ys[0] = y0; ys[1] = y0 + 1;
	for (i = 0U; i < 2U; i++) {
		if (xs[i] < 0) xs[i] = 0;
		if (xs[i] > (int)I915_TEX_FIXTURE_TEX_W - 1) xs[i] = (int)I915_TEX_FIXTURE_TEX_W - 1;
		if (ys[i] < 0) ys[i] = 0;
		if (ys[i] > (int)I915_TEX_FIXTURE_TEX_H - 1) ys[i] = (int)I915_TEX_FIXTURE_TEX_H - 1;
	}
	w[0][0] = (8U - fx) * (8U - fy); w[0][1] = fx * (8U - fy);
	w[1][0] = (8U - fx) * fy;        w[1][1] = fx * fy;
	for (ch = 0U; ch < 4U; ch++) {
		unsigned sum = 0U;

		for (j = 0U; j < 2U; j++)
			for (i = 0U; i < 2U; i++)
				sum += w[j][i] * rgba[((unsigned)ys[j] * I915_TEX_FIXTURE_TEX_W + (unsigned)xs[i]) * 4U + ch];
		if (inexact != NULL && (sum & 63U) != 0U)
			*inexact = 1;
		out[ch] = (sum + 32U) / 64U;
	}
	return out[2] | (out[1] << 8) | (out[0] << 16) | (out[3] << 24);
}

void
drv_i915_tex_fixture_write_state_ab(void *state_page, uint64_t rt_va, uint64_t tex_a_va,
	uint64_t tex_b_va, unsigned bind_b, uint32_t mocs)
{
	uint32_t *heap = state_page;
	uint32_t *rss_b = &heap[I915_TEX_FIXTURE_TEX_B_RSS_OFFSET / 4U];

	drv_i915_tex_fixture_write_state(state_page, rt_va, tex_a_va, mocs);
	memcpy(rss_b, texfix_tex_rss, sizeof(texfix_tex_rss));
	rss_b[8] = (uint32_t)tex_b_va;
	rss_b[9] = (uint32_t)(tex_b_va >> 32);
	heap[I915_DRAW_BINDING_TABLE_OFFSET / 4U + 1U] = bind_b != 0U ?
		I915_TEX_FIXTURE_TEX_B_RSS_OFFSET : I915_TEX_FIXTURE_TEX_RSS_OFFSET;
}

unsigned
drv_i915_tex_fixture_build_batch(uint32_t *cmds, unsigned capacity, uint64_t state_va, uint32_t mocs)
{
	static const struct i915_draw_tex_opts tex = {
		TEXFIX_3DSTATE_PS_DW3, TEXFIX_3DSTATE_PS_DW7, TEXFIX_3DSTATE_PS_EXTRA_DW1,
		TEXFIX_SAMPLER_POINTERS_PS_DW0, TEXFIX_SAMPLER_POINTERS_PS_DW1,
	};
	static struct i915_gem_object state;

	memset(&state, 0, sizeof(state));
	state.va = state_va;
	return i915_draw_build_batch(cmds, capacity, &state, &state, &state, &state, mocs, 1U, &tex);
}

void
drv_i915_tex_fixture_pattern(uint8_t *rgba, unsigned variant)
{
	unsigned u, v;

	for (v = 0U; v < I915_TEX_FIXTURE_TEX_H; v++) {
		for (u = 0U; u < I915_TEX_FIXTURE_TEX_W; u++) {
			uint8_t *t = &rgba[(v * I915_TEX_FIXTURE_TEX_W + u) * 4U];

			if (variant == 0U) {
				t[0] = (uint8_t)(16U + 32U * u);
				t[1] = (uint8_t)(16U + 32U * v);
				t[2] = (uint8_t)(16U + 32U * ((u + 3U * v) & 7U));
			} else if (variant == 1U) {
				t[0] = (uint8_t)(239U - 32U * v);
				t[1] = (uint8_t)(16U + 32U * u);
				t[2] = (uint8_t)(16U + 32U * ((3U * u + v) & 7U));
			} else if (variant == 2U) {
				t[0] = (uint8_t)(240U - 32U * u);
				t[1] = (uint8_t)(240U - 32U * v);
				t[2] = (uint8_t)(16U + 32U * ((u ^ v) & 7U));
			} else {
				t[0] = (uint8_t)(64U * (u & 3U));
				t[1] = (uint8_t)(64U * (v & 3U));
				t[2] = (uint8_t)(64U * ((u >> 2) + 2U * (v >> 2)));
			}
			t[3] = 255U;
		}
	}
}

uint32_t
drv_i915_tex_fixture_expected_pixel(const uint8_t *rgba, unsigned x, unsigned y)
{
	/* nearest, uv = (pixel + 0.5) / 32 over an 8x8 texture: texel = pixel / 4. */
	const uint8_t *t = &rgba[((y / 4U) * I915_TEX_FIXTURE_TEX_W + (x / 4U)) * 4U];

	/* B8G8R8A8_UNORM in memory is B,G,R,A; read as a little-endian dword. */
	return (uint32_t)t[2] | ((uint32_t)t[1] << 8) | ((uint32_t)t[0] << 16) | ((uint32_t)t[3] << 24);
}

uint32_t
drv_i915_draw_fixture_mocs(void)
{
	return GEN12_MOCS(I915_MOCS_UNCACHED_INDEX);
}

/*
 * Runs one request that only copies the pipeline statistics counters to memory.
 *
 * The counters are read from the ring rather than from the batch: a register
 * store is a privileged command and the ring is the privileged stream.  The
 * engine runs requests in order, so these values describe the draw before it.
 */
static int
i915_draw_read_statistics(
	struct i915_device *device,
	struct i915_gem_object *stats,
	uint32_t *values,
	unsigned count)
{
	static const uint32_t registers[] = {
		GEN12_REG_IA_VERTICES_COUNT,
		GEN12_REG_IA_PRIMITIVES_COUNT,
		GEN12_REG_VS_INVOCATION_COUNT,
		GEN12_REG_CL_INVOCATION_COUNT,
		GEN12_REG_CL_PRIMITIVES_COUNT,
		GEN12_REG_PS_INVOCATION_COUNT,
	};
	struct i915_engine *engine;
	struct i915_request *request;
	volatile uint32_t *page;
	uint64_t iteration;
	unsigned long irq;
	bool prior_enabled;
	unsigned index;
	unsigned n;
	int error;

	engine = &device->engines[I915_ENGINE_RCS0];
	page = kern_pmem_to_kernel(stats->run.paddr);
	for (index = 0U; index < count; index++)
		page[index * 2U] = 0xffffffffU;
	kern_io_write_barrier();

	irq = spin_lock_irqsave(&device->irq_lock);
	error = drv_i915_request_alloc(engine, NULL, NULL, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return error;
	}
	request->context = &engine->kernel_context;

	n = 0U;
	for (index = 0U; index < count && index < sizeof(registers) / sizeof(registers[0]); index++) {
		request->extra[n++] = MI_STORE_REGISTER_MEM_GEN8;
		request->extra[n++] = registers[index];
		request->extra[n++] = (uint32_t)(stats->va + index * 8U);
		request->extra[n++] = (uint32_t)((stats->va + index * 8U) >> 32);
	}
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
	for (index = 0U; index < count; index++) {
		i915_selftest_clflush(&page[index * 2U]);
		values[index] = page[index * 2U];
	}

	/* A request that never retired leaves the counters unread. */
	return engine->completed_seqno == request->seqno ? 0 : EIO;
}

/*
 * Compute positive control (expert-directed).  A minimal 1x1x1 SIMD8 compute
 * shader does one unconditional A64 store (C1) or nothing but a proper EOT (C2),
 * run on a clean engine.  C0 omits the walker and copies the IDD and the kernel
 * back through the GPU's own PPGTT so their contents can be verified from the
 * device, not just the CPU.  A landed store, or a completing store-less thread,
 * separates "the EU cannot run any thread" from "only the A64 store path stalls".
 */
#define I915_COMPUTE_IDD_OFFSET		896U	/* dynamic-heap byte offset, 64B aligned */
#define I915_COMPUTE_KSP_OFFSET		I915_DRAW_PS_KERNEL_OFFSET	/* 1024 */
#define I915_COMPUTE_READY_OFF		(I915_DRAW_MARKER_OFFSET)	/* 0xc00 */
#define I915_COMPUTE_EU_OFF		0xc20U		/* 0x..400c20, hard-coded in the store shader */
#define I915_COMPUTE_DONE_OFF		0xc28U		/* 8-byte post-sync slot */
#define I915_COMPUTE_CS_OFF		0xc30U
#define I915_COMPUTE_IDD_RB_OFF		0xd00U		/* GPU read-back of the IDD (8 dw) */
#define I915_COMPUTE_KERNEL_RB_OFF	0xe00U		/* GPU read-back of the kernel (36 dw) */
#define I915_COMPUTE_READY_TAG		0xc0ffee10U
#define I915_COMPUTE_DONE_TAG		0xc0ffee20U
#define I915_COMPUTE_CS_TAG		0xc0ffee30U
#define I915_COMPUTE_KERNEL_RB_DWORDS	36U

/* C1: SIMD8, unconditional send.hdc1 a64_untyped_write of 0xc0ffee02, then send.ts EOT. */
static const uint32_t i915_compute_marker_cs[] = {
	0x00030061U, 0x05054220U, 0x00000000U, 0xc0ffee02U,
	0x80030061U, 0x7f050220U, 0x00460005U, 0x00000000U,
	0x80030061U, 0x01264aa0U, 0x00000000U, 0x00000001U,
	0x80030161U, 0x01064aa0U, 0x00000000U, 0x00400c20U,
	0x80000101U, 0x00000000U, 0x00000000U, 0x00000000U,
	0x00030061U, 0x03260660U, 0x00000124U, 0x00000000U,
	0x00030161U, 0x03060660U, 0x00000104U, 0x00000000U,
	0x00039031U, 0x00000000U, 0xcdfa0314U, 0x019a050cU,
	0x80030131U, 0x00000004U, 0x70007f0cU, 0x00000000U,
};

/* C2: SIMD8, store-less, mov r127 r0 then send.ts EOT (proper compute termination). */
static const uint32_t i915_compute_empty_cs[] = {
	0x80030061U, 0x7f050220U, 0x00460005U, 0x00000000U,
	0x80030131U, 0x00000004U, 0x70007f0cU, 0x00000000U,
};

static void
i915_compute_emit_sba(struct i915_draw_batch *batch,
	const struct i915_gem_object *shared, uint64_t inst_base, uint32_t mocs)
{
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_STATE_BASE_ADDRESS,
		GEN12_STATE_BASE_ADDRESS_DWORDS));
	i915_draw_emit(batch, 1U | (mocs << 4));		/* general: base 0, modify */
	i915_draw_emit(batch, 0U);
	i915_draw_emit(batch, mocs << 16);			/* stateless data-port MOCS */
	i915_draw_emit(batch, 1U | (mocs << 4) | ((uint32_t)shared->va & 0xfffff000U));
	i915_draw_emit(batch, (uint32_t)(shared->va >> 32));
	i915_draw_emit(batch, 1U | (mocs << 4) | ((uint32_t)shared->va & 0xfffff000U));
	i915_draw_emit(batch, (uint32_t)(shared->va >> 32));
	i915_draw_emit(batch, 1U | (mocs << 4));		/* indirect: base 0, modify */
	i915_draw_emit(batch, 0U);
	i915_draw_emit(batch, 1U | (GEN12_MOCS(I915_MOCS_WRITEBACK_INDEX) << 4) |
		((uint32_t)inst_base & 0xfffff000U));
	i915_draw_emit(batch, (uint32_t)(inst_base >> 32));
	i915_draw_emit(batch, 1U | (0xfffffU << 12));		/* general size */
	i915_draw_emit(batch, 1U | (0xfffffU << 12));		/* dynamic size */
	i915_draw_emit(batch, 1U | (0xfffffU << 12));		/* indirect size */
	i915_draw_emit(batch, 1U | (0xfffffU << 12));		/* instruction size */
	i915_draw_emit(batch, 1U | (mocs << 4) | ((uint32_t)shared->va & 0xfffff000U));
	i915_draw_emit(batch, (uint32_t)(shared->va >> 32));
	i915_draw_emit(batch, (4096U / 64U - 1U) << 12);
	i915_draw_emit(batch, 1U | (mocs << 4));
	i915_draw_emit(batch, 0U);
	i915_draw_emit(batch, 0U);
}

/* One dword copy, both operands in the PPGTT (no Global GTT select bits). */
static void
i915_compute_emit_copy(struct i915_draw_batch *batch, uint64_t dst, uint64_t src)
{
	i915_draw_emit(batch, 0x17000003U);		/* MI_COPY_MEM_MEM, PPGTT->PPGTT */
	i915_draw_emit(batch, (uint32_t)dst);
	i915_draw_emit(batch, (uint32_t)(dst >> 32));
	i915_draw_emit(batch, (uint32_t)src);
	i915_draw_emit(batch, (uint32_t)(src >> 32));
}

static unsigned
i915_compute_build_batch(uint32_t *cmds, unsigned capacity,
	const struct i915_gem_object *shared, uint64_t inst_base, uint32_t mocs, uint32_t max_threads,
	unsigned emit_walker, unsigned emit_readback)
{
	struct i915_draw_batch batch;
	uint64_t done_va = shared->va + I915_COMPUTE_DONE_OFF;
	unsigned index;

	batch.cmds = cmds;
	batch.count = 0U;
	batch.capacity = capacity;

	/* Establish 3D mode (SBA is applied in 3D: Wa_1607854226 covers ADL). */
	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE);
	i915_draw_emit(&batch, GEN12_PIPELINE_SELECT_DWORD(GEN12_PIPELINE_SELECT_3D));

	i915_compute_emit_sba(&batch, shared, inst_base, mocs);

	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_STATE_CACHE_INVALIDATE | PIPE_CONTROL_CONST_CACHE_INVALIDATE |
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE);

	/* The GPU reads the IDD and the kernel back through this context's PPGTT. */
	if (emit_readback != 0U) {
		for (index = 0U; index < 8U; index++)
			i915_compute_emit_copy(&batch,
				shared->va + I915_COMPUTE_IDD_RB_OFF + index * 4U,
				shared->va + I915_COMPUTE_IDD_OFFSET + index * 4U);
		for (index = 0U; index < I915_COMPUTE_KERNEL_RB_DWORDS; index++)
			i915_compute_emit_copy(&batch,
				shared->va + I915_COMPUTE_KERNEL_RB_OFF + index * 4U,
				inst_base + I915_COMPUTE_KSP_OFFSET + index * 4U);
	}

	/* 3D -> GPGPU: stalling flush (RT flush pulls in the HDC pipeline flush). */
	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH);
	i915_draw_emit(&batch, GEN12_PIPELINE_SELECT_DWORD(2U));	/* GPGPU */

	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STALL_AT_SCOREBOARD);

	/* MEDIA_VFE_STATE (9 dwords). */
	i915_draw_emit(&batch, 0x70000007U);
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, (max_threads << 16) | (2U << 8));
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, (2U << 16));
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 0U);

	/* MEDIA_STATE_FLUSH before MIDL clears temporary interface-descriptor storage. */
	i915_draw_emit(&batch, 0x70040000U);
	i915_draw_emit(&batch, 0U);

	/* MEDIA_INTERFACE_DESCRIPTOR_LOAD (4 dwords): 32-byte IDD at the dynamic offset. */
	i915_draw_emit(&batch, 0x70020002U);
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 32U);
	i915_draw_emit(&batch, I915_COMPUTE_IDD_OFFSET);

	i915_draw_emit_marker(&batch, shared->va + I915_COMPUTE_READY_OFF, I915_COMPUTE_READY_TAG);

	if (emit_walker != 0U) {
		i915_draw_emit(&batch, 0x7105000DU);	/* GPGPU_WALKER */
		i915_draw_emit(&batch, 0U);		/* interface descriptor offset */
		i915_draw_emit(&batch, 0U);		/* indirect data length */
		i915_draw_emit(&batch, 0U);		/* indirect data start */
		i915_draw_emit(&batch, 0U);		/* thread counters + SIMD8 */
		i915_draw_emit(&batch, 0U);		/* thread group id starting X */
		i915_draw_emit(&batch, 0U);
		i915_draw_emit(&batch, 1U);		/* X dimension */
		i915_draw_emit(&batch, 0U);		/* starting Y */
		i915_draw_emit(&batch, 0U);
		i915_draw_emit(&batch, 1U);		/* Y dimension */
		i915_draw_emit(&batch, 0U);		/* starting Z */
		i915_draw_emit(&batch, 1U);		/* Z dimension */
		i915_draw_emit(&batch, 0x1U);		/* right execution mask */
		i915_draw_emit(&batch, 0xffffffffU);	/* bottom execution mask */
	} else {
		for (index = 0U; index < 15U; index++)
			i915_draw_emit(&batch, MI_NOOP);
	}

	i915_draw_emit(&batch, 0x70040000U);		/* MEDIA_STATE_FLUSH after the walker */
	i915_draw_emit(&batch, 0U);

	/* Wa_1607156449: a stalling flush without post-sync precedes the post-sync PC. */
	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE);

	/* Post-sync write of the done tag; PPGTT, not GGTT (a non-secure batch's
	 * GGTT/StoreDataIndex post-sync is a NOOP), so the address is the raw VA. */
	i915_draw_emit(&batch, GFX_OP_PIPE_CONTROL(6));
	i915_draw_emit(&batch, PIPE_CONTROL_CS_STALL | (1U << 14));	/* PostSync = WriteImmediate */
	i915_draw_emit(&batch, (uint32_t)done_va);
	i915_draw_emit(&batch, (uint32_t)(done_va >> 32));
	i915_draw_emit(&batch, I915_COMPUTE_DONE_TAG);
	i915_draw_emit(&batch, 0U);

	i915_draw_emit_marker(&batch, shared->va + I915_COMPUTE_CS_OFF, I915_COMPUTE_CS_TAG);

	i915_draw_emit(&batch, MI_BATCH_BUFFER_END);
	i915_draw_emit(&batch, MI_NOOP);
	return batch.count;
}

/* Records the EU power-gating ACKs and the MCR selector (raw; not decoded). */
static void
i915_compute_power_probe(struct i915_device *device, const char *when)
{
	kern_logf("i915: compute power[%s] eu_dis(0x9134)=0x%08x slice_ack(0x804c)=0x%08x "
		"ss01_eu_ack(0x805c)=0x%08x ss23_eu_ack(0x8060)=0x%08x mcr(0xfdc)=0x%08x\n",
		when,
		drv_i915_read32(device, 0x9134U),
		drv_i915_read32(device, 0x804cU),
		drv_i915_read32(device, 0x805cU),
		drv_i915_read32(device, 0x8060U),
		drv_i915_read32(device, GEN8_MCR_SELECTOR));
}

/*
 * Submits one request to a chosen context and spins until its breadcrumb lands.
 *
 * Returns 0 when the request completed, or a negative value when the poll bound
 * elapsed with the engine still busy (a hang).  Used by the golden-context
 * bootstrap, which drives several distinct contexts in sequence.
 */
static int
i915_golden_run(
	struct i915_device *device,
	struct i915_engine *engine,
	struct i915_context *ctx,
	const uint32_t *extra,
	unsigned extra_count,
	struct i915_gem_object *batch,
	uint64_t batch_va)
{
	struct i915_request *request;
	unsigned long irq;
	uint64_t iteration;
	bool prior_enabled;
	unsigned i;
	int error;

	irq = spin_lock_irqsave(&device->irq_lock);
	error = drv_i915_request_alloc(engine, NULL, NULL, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return error;
	}
	request->context = ctx;
	for (i = 0U; i < extra_count; i++)
		request->extra[i] = extra[i];
	request->extra_count = extra_count;
	request->batch = batch;
	request->batch_va = batch_va;
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
	if (engine->completed_seqno != request->seqno)
		return -1;
	return 0;
}

int
drv_i915_compute_selftest(
	struct i915_device *device)
{
	static const uint32_t context_registers[][2] = {
		{ GEN12_REG_L3ALLOC, GEN12_L3ALLOC_VALUE },
		{ GEN12_FF_MODE2, GEN12_FF_MODE2_MESA_VALUE },
	};
	const unsigned count = sizeof(context_registers) / sizeof(context_registers[0]);
	struct i915_engine *engine;
	struct i915_request *request;
	struct i915_gem_object *shared;
	struct i915_gem_object *batch;
	volatile uint32_t *page;
	uint64_t iteration;
	unsigned long irq;
	bool prior_enabled;
	uint32_t max_threads;
	uint32_t dss;
	unsigned dwords, line, variant, bit, dss_count;
	uint32_t *cmds;
	int error = 0;

	engine = &device->engines[I915_ENGINE_RCS0];

	dss = drv_i915_read32(device, 0x913cU);
	dss_count = 0U;
	for (bit = 0U; bit < 32U; bit++)
		if (((dss >> bit) & 1U) != 0U)
			dss_count++;
	max_threads = 112U * dss_count - 1U;

	if (drv_i915_gem_create(device, 4096U, &shared) != 0)
		return 0;
	if (drv_i915_gem_bind_vm(&engine->kernel_vm, shared) != 0)
		return 0;
	if (drv_i915_gem_create(device, 4096U, &batch) != 0)
		return 0;
	if (drv_i915_gem_bind_vm(&engine->kernel_vm, batch) != 0)
		return 0;

	page = kern_pmem_to_kernel(shared->run.paddr);
	kern_logf("i915: compute dss=0x%08x dss_count=%u max_threads=%u shared=0x%llx batch=0x%llx done_va=0x%llx\n",
		dss, dss_count, max_threads, (unsigned long long)shared->va,
		(unsigned long long)batch->va, (unsigned long long)(shared->va + I915_COMPUTE_DONE_OFF));
	i915_draw_apply_engine_workarounds(device, engine);
	{
		static const uint32_t regdump[] = {
			0x2084U, 0x20c4U, 0x4008U, 0x400cU, 0xb024U, 0x209cU, 0x2244U, 0x2580U,
			0x229cU, 0x9424U, 0x9550U, 0xb134U, 0xb100U, 0xb118U, 0xe4f4U, 0xe48cU,
			0xe18cU, 0x7008U, 0x14800U,
		};
		unsigned ri;
		drv_i915_write32(device, GEN8_MCR_SELECTOR, GEN11_MCR_MULTICAST);
		kern_io_write_barrier();
		for (ri = 0U; ri < sizeof(regdump) / sizeof(regdump[0]); ri++)
			kern_logf("i915: regdump 0x%05x = 0x%08x\n", regdump[ri],
				drv_i915_read32(device, regdump[ri]));
	}
	i915_compute_power_probe(device, "baseline");

	/*
	 * Golden-context lifecycle (Linux __engines_record_defaults equivalent).
	 *
	 * The compute walker hangs on a fresh context whose image was memset to zero
	 * and then partially overwritten by the offset-table LRI subset: the per-thread
	 * dispatch state the EU reads was never produced by a real hardware context
	 * save.  Here we build one: bootstrap context A (restore-inhibit) applies the
	 * context workarounds, context B forces A to switch out so the hardware SAVES
	 * A's full image, and test context C INHERITS that saved image (copying the
	 * engine-state pages, clearing its own ppHWSP, and pointing the ring/PDP at
	 * itself with restore-inhibit cleared).  If the walker then runs on C, the
	 * missing piece was the golden save/inherit, not any single register.
	 */
	{
		static struct i915_context ctxA;
		static struct i915_context ctxB;
		static struct i915_context ctxC;
		struct i915_gem_object *glow;
		volatile uint32_t *glow_page;
		const uint64_t glow_va = 0x00810000ULL;
		uint32_t wa_extra[16];
		unsigned wa_n;
		unsigned entry;
		int rc;

		if (drv_i915_lrc_create(device, engine, &engine->kernel_vm, 0x210U, &ctxA) != 0)
			goto golden_done;
		if (drv_i915_lrc_create(device, engine, &engine->kernel_vm, 0x211U, &ctxB) != 0) {
			drv_i915_lrc_destroy(device, &ctxA);
			goto golden_done;
		}
		if (drv_i915_lrc_create(device, engine, &engine->kernel_vm, 0x212U, &ctxC) != 0) {
			drv_i915_lrc_destroy(device, &ctxB);
			drv_i915_lrc_destroy(device, &ctxA);
			goto golden_done;
		}
		if (drv_i915_gem_create(device, 4096U, &glow) != 0)
			goto golden_teardown;
		if (drv_i915_ppgtt_insert(&engine->kernel_vm, glow_va, glow->run.paddr, 1U) != 0) {
			drv_i915_gem_destroy(device, glow);
			goto golden_teardown;
		}
		glow_page = kern_pmem_to_kernel(glow->run.paddr);
		memset((void *)glow_page, 0, 4096U);
		memcpy((uint8_t *)glow_page + I915_COMPUTE_KSP_OFFSET, i915_compute_empty_cs,
			sizeof(i915_compute_empty_cs));
		kern_io_write_barrier();
		__asm__ volatile("wbinvd" : : : "memory");

		kern_logf("i915: golden A.ggtt=0x%08x B.ggtt=0x%08x C.ggtt=0x%08x A.ctrl=0x%08x A.rpcs=0x%08x\n",
			ctxA.image->ggtt_offset, ctxB.image->ggtt_offset, ctxC.image->ggtt_offset,
			ctxA.state[CTX_CONTEXT_CONTROL], ctxA.state[CTX_R_PWR_CLK_STATE]);

		/* Step 1: bootstrap A applies the context workarounds (walkerless). */
		wa_n = 0U;
		wa_extra[wa_n++] = MI_LOAD_REGISTER_IMM(count);
		for (entry = 0U; entry < count; entry++) {
			wa_extra[wa_n++] = context_registers[entry][0];
			wa_extra[wa_n++] = context_registers[entry][1];
		}
		wa_extra[wa_n++] = GFX_OP_PIPE_CONTROL(6);
		wa_extra[wa_n++] = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
			PIPE_CONTROL_DEPTH_CACHE_FLUSH | PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE;
		wa_extra[wa_n++] = 0U; wa_extra[wa_n++] = 0U;
		wa_extra[wa_n++] = 0U; wa_extra[wa_n++] = 0U;
		rc = i915_golden_run(device, engine, &ctxA, wa_extra, wa_n, NULL, 0U);
		kern_logf("i915: golden step1 A(ctxWA walkerless) rc=%d completed=%u\n", rc, engine->completed_seqno);

		/* Step 2: B runs so the engine switches away from A and SAVES A's image. */
		wa_n = 0U;
		wa_extra[wa_n++] = MI_NOOP;
		wa_extra[wa_n++] = MI_NOOP;
		rc = i915_golden_run(device, engine, &ctxB, wa_extra, wa_n, NULL, 0U);
		kern_logf("i915: golden step2 B(switch/save) rc=%d completed=%u\n", rc, engine->completed_seqno);

		/* A's saved image is now golden.  Read it back coherently. */
		for (line = 0U; line < ctxA.image->bytes; line += 64U)
			i915_selftest_clflush((const uint8_t *)ctxA.image->address + line);
		kern_io_read_barrier();
		kern_logf("i915: golden saved A ctrl=0x%08x rpcs=0x%08x ringctl=0x%08x head=0x%08x tail=0x%08x s0=0x%08x s1=0x%08x s2=0x%08x s3=0x%08x\n",
			ctxA.state[CTX_CONTEXT_CONTROL], ctxA.state[CTX_R_PWR_CLK_STATE],
			ctxA.state[CTX_RING_CTL], ctxA.state[CTX_RING_HEAD], ctxA.state[CTX_RING_TAIL],
			ctxA.state[0], ctxA.state[1], ctxA.state[2], ctxA.state[3]);

		/* Step 3: C inherits A's saved engine state, then re-owns its identity. */
		memcpy((uint8_t *)ctxC.image->address + LRC_STATE_OFFSET,
			(const uint8_t *)ctxA.image->address + LRC_STATE_OFFSET,
			(size_t)ctxA.image->bytes - LRC_STATE_OFFSET);
		memset((void *)ctxC.image->address, 0, LRC_STATE_OFFSET);	/* fresh ppHWSP */
		ctxC.state[CTX_CONTEXT_CONTROL] = 0x00090008U;			/* restore, not inhibit */
		ctxC.state[CTX_RING_START] = ctxC.ring->ggtt_offset;
		ctxC.state[CTX_RING_HEAD] = 0U;
		ctxC.state[CTX_RING_TAIL] = 0U;
		ctxC.state[CTX_RING_CTL] = RING_CTL_SIZE(I915_RING_BYTES) | RING_VALID;
		ctxC.state[CTX_PDP0_UDW] = (uint32_t)((uint64_t)ctxC.vm->pml4.paddr >> 32);
		ctxC.state[CTX_PDP0_LDW] = (uint32_t)ctxC.vm->pml4.paddr;
		ctxC.ring_tail = 0U;
		kern_io_write_barrier();
		for (line = 0U; line < ctxC.image->bytes; line += 64U)
			i915_selftest_clflush((const uint8_t *)ctxC.image->address + line);
		__asm__ volatile("wbinvd" : : : "memory");
		kern_logf("i915: golden C inherited ctrl=0x%08x rpcs=0x%08x ringstart=0x%08x\n",
			ctxC.state[CTX_CONTEXT_CONTROL], ctxC.state[CTX_R_PWR_CLK_STATE], ctxC.state[CTX_RING_START]);

		/* Step 4 on golden C: gC0 (marker, no walker), gC2 (marker + walker,
		 * the exact L-C1 case that completed on Linux), gC3-low (empty + walker). */
		for (variant = 0U; variant <= 2U; variant++) {
			const char *name = variant == 0U ? "gC0" : (variant == 1U ? "gC2" : "gC3-low");
			uint64_t inst_base = variant == 2U ? glow_va : shared->va;
			unsigned do_walker = variant == 0U ? 0U : 1U;

			memset((void *)page, 0, 4096U);
			if (variant != 2U)
				memcpy((uint8_t *)page + I915_COMPUTE_KSP_OFFSET,
					i915_compute_marker_cs, sizeof(i915_compute_marker_cs));
			{
				volatile uint32_t *idd = page + I915_COMPUTE_IDD_OFFSET / 4U;
				idd[0] = I915_COMPUTE_KSP_OFFSET;
				idd[1] = 0U; idd[2] = 1U << 20; idd[3] = 0U;
				idd[4] = 0U; idd[5] = 0U; idd[6] = 1U; idd[7] = 0U;
			}
			page[I915_COMPUTE_READY_OFF / 4U] = 0xdead0000U;
			page[I915_COMPUTE_EU_OFF / 4U] = 0xdead0000U;
			page[I915_COMPUTE_DONE_OFF / 4U] = 0xdead0000U;
			page[I915_COMPUTE_CS_OFF / 4U] = 0xdead0000U;
			kern_io_write_barrier();

			cmds = kern_pmem_to_kernel(batch->run.paddr);
			dwords = i915_compute_build_batch(cmds, 1024U, shared, inst_base,
				GEN12_MOCS(I915_MOCS_UNCACHED_INDEX), max_threads, do_walker, 0U);
			kern_io_write_barrier();
			for (line = 0U; line < dwords * 4U; line += 64U)
				i915_selftest_clflush((const uint8_t *)cmds + line);
			for (line = 0U; line < 4096U; line += 64U)
				i915_selftest_clflush((const uint8_t *)page + line);
			for (line = 0U; line < 4096U; line += 64U)
				i915_selftest_clflush((const uint8_t *)glow_page + line);
			__asm__ volatile("wbinvd" : : : "memory");

			wa_n = 0U;
			wa_extra[wa_n++] = MI_LOAD_REGISTER_IMM(count);
			for (entry = 0U; entry < count; entry++) {
				wa_extra[wa_n++] = context_registers[entry][0];
				wa_extra[wa_n++] = context_registers[entry][1];
			}
			wa_extra[wa_n++] = GFX_OP_PIPE_CONTROL(6);
			wa_extra[wa_n++] = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
				PIPE_CONTROL_DEPTH_CACHE_FLUSH | PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE;
			wa_extra[wa_n++] = 0U; wa_extra[wa_n++] = 0U;
			wa_extra[wa_n++] = 0U; wa_extra[wa_n++] = 0U;

			rc = i915_golden_run(device, engine, &ctxC, wa_extra, wa_n, batch, batch->va);

			kern_io_read_barrier();
			for (line = 0U; line < 4096U; line += 64U)
				i915_selftest_clflush((const uint8_t *)page + line);
			kern_io_read_barrier();
			kern_logf("i915: golden %s rc=%d inst_base=0x%llx ready=0x%08x eu=0x%08x done=0x%08x completed=%u\n",
				name, rc, (unsigned long long)inst_base,
				page[I915_COMPUTE_READY_OFF / 4U], page[I915_COMPUTE_EU_OFF / 4U],
				page[I915_COMPUTE_DONE_OFF / 4U], engine->completed_seqno);
			if (rc != 0) {
				kern_logf("i915: golden %s HANG ipehr=0x%08x acthd=0x%08x%08x instdone=0x%08x fault=0x%08x row=0x%08x\n",
					name,
					drv_i915_read32(device, RING_IPEHR(engine->base)),
					drv_i915_read32(device, RING_ACTHD_UDW(engine->base)),
					drv_i915_read32(device, RING_ACTHD(engine->base)),
					drv_i915_read32(device, RING_INSTDONE(engine->base)),
					drv_i915_read32(device, 0xcec4U),
					drv_i915_read32(device, 0xe164U));
				break;
			}
		}

		drv_i915_ppgtt_clear(&engine->kernel_vm, glow_va, 1U);
		drv_i915_gem_destroy(device, glow);
golden_teardown:
		drv_i915_lrc_destroy(device, &ctxC);
		drv_i915_lrc_destroy(device, &ctxB);
		drv_i915_lrc_destroy(device, &ctxA);
	}
golden_done:

	/*
	 * Indirect-context restore path + Wa_18022495364 (GEN12_CS_DEBUG_MODE2 |=
	 * INSTRUCTION_STATE_CACHE_INVALIDATE), which the LRC did not implement.  A
	 * GGTT batch is wired into the kernel context's restore (RING_INDIRECT_CTX)
	 * and runs before every restored context's ring work.  It writes an enter and
	 * a leave marker so we can prove the restore path actually executed, and (pass
	 * B) applies the WA.  Pass B (with WA) runs first: if it unblocks the compute
	 * thread the engine stays alive for the pass-A (no-WA) baseline.
	 */
	{
		struct i915_gem_object *wa;
		struct i915_gem_object *low_inst;
		struct i915_context *ctx = &engine->kernel_context;
		volatile uint32_t *wa_page;
		volatile uint32_t *low_page;
		const uint64_t low_va = 0x00800000ULL;
		uint32_t wa_ggtt;
		unsigned pass;
		unsigned broke = 0U;

		if (drv_i915_gem_create(device, 4096U, &wa) != 0)
			goto compute_done;
		if (drv_i915_gem_bind_ggtt(device, wa) != 0) {
			drv_i915_gem_destroy(device, wa);
			goto compute_done;
		}
		wa_ggtt = wa->ggtt_offset;
		wa_page = kern_pmem_to_kernel(wa->run.paddr);

		if (drv_i915_gem_create(device, 4096U, &low_inst) != 0) {
			drv_i915_gem_unbind_ggtt(device, wa);
			drv_i915_gem_destroy(device, wa);
			goto compute_done;
		}
		if (drv_i915_ppgtt_insert(&engine->kernel_vm, low_va, low_inst->run.paddr, 1U) != 0) {
			drv_i915_gem_destroy(device, low_inst);
			drv_i915_gem_unbind_ggtt(device, wa);
			drv_i915_gem_destroy(device, wa);
			goto compute_done;
		}
		low_page = kern_pmem_to_kernel(low_inst->run.paddr);
		memset((void *)low_page, 0, 4096U);
		memcpy((uint8_t *)low_page + I915_COMPUTE_KSP_OFFSET, i915_compute_empty_cs,
			sizeof(i915_compute_empty_cs));
		kern_io_write_barrier();

		kern_logf("i915: compute wa_ggtt=0x%08x lrc regs[0x12]=0x%08x [0x14]=0x%08x [0x16]=0x%08x ctx_ctrl=0x%08x desc_low=0x%08x\n",
			wa_ggtt, ctx->state[0x12U], ctx->state[0x14U], ctx->state[0x16U],
			ctx->state[CTX_CONTEXT_CONTROL], ctx->descriptor_low);

		for (pass = 0U; pass <= 1U && broke == 0U; pass++) {
			unsigned with_wa = pass == 0U ? 1U : 0U;
			const char *pname = with_wa ? "B(WA)" : "A(noWA)";

			/* Build the 16-DWORD indirect-context batch (64 bytes = 1 cacheline). */
			memset((void *)wa_page, 0, 4096U);
			wa_page[0] = 0x10400002U;		/* MI_STORE_DWORD_IMM, GGTT: enter marker */
			wa_page[1] = wa_ggtt + 0x80U;
			wa_page[2] = 0U;
			wa_page[3] = 0xE07E0000U | pass;
			wa_page[4] = 0U;			/* NOOP before the LRI */
			wa_page[5] = with_wa ? 0x11000001U : 0U;	/* MI_LOAD_REGISTER_IMM(1) or NOOP */
			wa_page[6] = with_wa ? 0x000020d8U : 0U;	/* GEN12_CS_DEBUG_MODE2 */
			wa_page[7] = with_wa ? 0x00400040U : 0U;	/* masked INSTRUCTION_STATE_CACHE_INVALIDATE */
			wa_page[8] = 0x10400002U;		/* MI_STORE_DWORD_IMM, GGTT: leave marker */
			wa_page[9] = wa_ggtt + 0x84U;
			wa_page[10] = 0U;
			wa_page[11] = 0x1EA7E000U | pass;
			/* 12..15 stay zero (padding, not MI_BATCH_BUFFER_END). */
			kern_io_write_barrier();

			/* Point the kernel context's restore at the WA buffer. */
			ctx->state[0x13U] = 0U;			/* RING_BB_PER_CTX_PTR value */
			ctx->state[0x15U] = wa_ggtt | 1U;	/* RING_INDIRECT_CTX = addr | 1 cacheline */
			ctx->state[0x17U] = 0x00000340U;	/* RING_INDIRECT_CTX_OFFSET = 0xD << 6 */
			kern_io_write_barrier();
			__asm__ volatile("wbinvd" : : : "memory");

			for (variant = 0U; variant <= 1U; variant++) {
				const char *name = variant == 0U ? "C0" : "C3-low";
				uint64_t inst_base = variant == 0U ? shared->va : low_va;
				unsigned do_walker = variant == 0U ? 0U : 1U;

				memset((void *)page, 0, 4096U);
				if (variant == 0U)
					memcpy((uint8_t *)page + I915_COMPUTE_KSP_OFFSET,
						i915_compute_marker_cs, sizeof(i915_compute_marker_cs));
				{
					volatile uint32_t *idd = page + I915_COMPUTE_IDD_OFFSET / 4U;
					idd[0] = I915_COMPUTE_KSP_OFFSET;
					idd[1] = 0U; idd[2] = 1U << 20; idd[3] = 0U;
					idd[4] = 0U; idd[5] = 0U; idd[6] = 1U; idd[7] = 0U;
				}
				page[I915_COMPUTE_READY_OFF / 4U] = 0xdead0000U;
				page[I915_COMPUTE_EU_OFF / 4U] = 0xdead0000U;
				page[I915_COMPUTE_DONE_OFF / 4U] = 0xdead0000U;
				page[I915_COMPUTE_CS_OFF / 4U] = 0xdead0000U;
				wa_page[0x80U / 4U] = 0xdead0000U;	/* enter marker slot */
				wa_page[0x84U / 4U] = 0xdead0000U;	/* leave marker slot */
				kern_io_write_barrier();

				cmds = kern_pmem_to_kernel(batch->run.paddr);
				dwords = i915_compute_build_batch(cmds, 1024U, shared, inst_base,
					GEN12_MOCS(I915_MOCS_UNCACHED_INDEX), max_threads, do_walker, 0U);

				kern_io_write_barrier();
				for (line = 0U; line < dwords * 4U; line += 64U)
					i915_selftest_clflush((const uint8_t *)cmds + line);
				for (line = 0U; line < 4096U; line += 64U)
					i915_selftest_clflush((const uint8_t *)page + line);
				for (line = 0U; line < 4096U; line += 64U)
					i915_selftest_clflush((const uint8_t *)low_page + line);
				__asm__ volatile("wbinvd" : : : "memory");

				irq = spin_lock_irqsave(&device->irq_lock);
				error = drv_i915_request_alloc(engine, NULL, NULL, &request);
				if (error != 0) {
					spin_unlock_irqrestore(&device->irq_lock, irq);
					broke = 1U;
					break;
				}
				request->context = ctx;
				{
					unsigned n = 0U;
					unsigned entry;

					request->extra[n++] = MI_LOAD_REGISTER_IMM(count);
					for (entry = 0U; entry < count; entry++) {
						request->extra[n++] = context_registers[entry][0];
						request->extra[n++] = context_registers[entry][1];
					}
					request->extra[n++] = GFX_OP_PIPE_CONTROL(6);
					request->extra[n++] = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
						PIPE_CONTROL_DEPTH_CACHE_FLUSH | PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE;
					request->extra[n++] = 0U; request->extra[n++] = 0U;
					request->extra[n++] = 0U; request->extra[n++] = 0U;
					request->extra_count = n;
				}
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
				for (line = 0U; line < 4096U; line += 64U)
					i915_selftest_clflush((const uint8_t *)page + line);
				i915_selftest_clflush((const uint8_t *)wa_page + 0x80U);
				kern_io_read_barrier();
				kern_logf("i915: compute %s %s inst_base=0x%llx enter=0x%08x leave=0x%08x ready=0x%08x eu=0x%08x done=0x%08x completed=%u seqno=%u\n",
					pname, name, (unsigned long long)inst_base,
					wa_page[0x80U / 4U], wa_page[0x84U / 4U],
					page[I915_COMPUTE_READY_OFF / 4U], page[I915_COMPUTE_EU_OFF / 4U],
					page[I915_COMPUTE_DONE_OFF / 4U], engine->completed_seqno, request->seqno);

				if (engine->completed_seqno != request->seqno) {
					kern_logf("i915: compute %s %s HANG ipehr=0x%08x acthd=0x%08x%08x instdone=0x%08x fault=0x%08x row=0x%08x\n",
						pname, name,
						drv_i915_read32(device, RING_IPEHR(engine->base)),
						drv_i915_read32(device, RING_ACTHD_UDW(engine->base)),
						drv_i915_read32(device, RING_ACTHD(engine->base)),
						drv_i915_read32(device, RING_INSTDONE(engine->base)),
						drv_i915_read32(device, 0xcec4U),
						drv_i915_read32(device, 0xe164U));
					broke = 1U;
					break;
				}
			}
		}

		ctx->state[0x13U] = 0U;
		ctx->state[0x15U] = 0U;
		ctx->state[0x17U] = 0U;
		kern_io_write_barrier();
		__asm__ volatile("wbinvd" : : : "memory");
		drv_i915_ppgtt_clear(&engine->kernel_vm, low_va, 1U);
		drv_i915_gem_destroy(device, low_inst);
		drv_i915_gem_unbind_ggtt(device, wa);
		drv_i915_gem_destroy(device, wa);
	}
compute_done:

	drv_i915_gem_unbind_vm(shared);
	drv_i915_gem_destroy(device, shared);
	drv_i915_gem_unbind_vm(batch);
	drv_i915_gem_destroy(device, batch);
	return 0;	/* observation-only: never abort the selftest chain */
}

/*
 * Draws a screen-space rectangle over the render target through the 3D pipeline.
 *
 * This step validates every packet of the pipeline and the geometry path: the
 * statistics counters report how far the rectangle travelled.  The pixel shader
 * is added in the next step, so the target itself stays unwritten for now.
 */
int
drv_i915_draw_selftest(
	struct i915_device *device)
{
	static const char *const names[] = {
		"ia_vertices", "ia_primitives", "vs_invocations",
		"cl_invocations", "cl_primitives", "ps_invocations",
	};
	struct i915_engine *engine;
	struct i915_request *request;
	struct i915_gem_object *objects[6];
	struct i915_gem_object *rt;
	struct i915_gem_object *surface;
	struct i915_gem_object *dynamic;
	struct i915_gem_object *instruction;
	struct i915_gem_object *vb;
	struct i915_gem_object *stats;
	struct i915_gem_object *batch;
	volatile uint32_t *rt_cpu;
	volatile uint32_t *marker_cpu;
	uint32_t counters[6];
	uint64_t iteration;
	unsigned long irq;
	bool prior_enabled;
	unsigned dwords;
	unsigned index;
	unsigned line;
	uint32_t *cmds;
	int error;

	engine = &device->engines[I915_ENGINE_RCS0];

	/*
	 * One page carries the surface heap, the dynamic heap, the instruction
	 * heap, the vertex data and the markers at disjoint offsets, so the three
	 * state bases are one and the same address.  It is allocated first so it
	 * lands at the lowest free address; the render target and the counters
	 * follow.
	 */
	for (index = 0U; index < 3U; index++) {
		if (drv_i915_gem_create(device, 4096U, &objects[index]) != 0)
			return EIO;
		if (drv_i915_gem_bind_vm(&engine->kernel_vm, objects[index]) != 0)
			return EIO;
	}
	surface = objects[0];
	dynamic = objects[0];
	instruction = objects[0];
	vb = objects[0];
	rt = objects[1];
	stats = objects[2];

	rt_cpu = kern_pmem_to_kernel(rt->run.paddr);
	memset((void *)rt_cpu, 0, 4096U);
	memset(kern_pmem_to_kernel(surface->run.paddr), 0, 4096U);
	i915_draw_write_surface_state(kern_pmem_to_kernel(surface->run.paddr), rt->va,
		GEN12_MOCS(I915_MOCS_UNCACHED_INDEX));
	i915_draw_write_dynamic_state(kern_pmem_to_kernel(dynamic->run.paddr));
	/* The pixel shader sits at its aligned offset in the same page. */
	memcpy((uint8_t *)kern_pmem_to_kernel(instruction->run.paddr) +
		I915_DRAW_PS_KERNEL_OFFSET, i915_draw_const_color_ps,
		sizeof(i915_draw_const_color_ps));

	/*
	 * BISECT: the rest of the instruction region and the address space's
	 * scratch page are carpeted with thread-ending instructions.  A thread
	 * fetching from the wrong place then retires instead of running forever;
	 * the target stays black either way, so completion alone is the signal.
	 */
	i915_draw_fill_eot(kern_pmem_to_kernel(instruction->run.paddr),
		I915_DRAW_PS_KERNEL_OFFSET + sizeof(i915_draw_const_color_ps),
		I915_DRAW_POSITION_OFFSET - I915_DRAW_PS_KERNEL_OFFSET - sizeof(i915_draw_const_color_ps));
	{
		void *scratch = kern_pmem_to_kernel(engine->kernel_vm.scratch[0].paddr);
		void *ggtt_scratch = kern_pmem_to_kernel(device->ggtt.scratch.paddr);

		i915_draw_fill_eot(ggtt_scratch, 0U, 4096U);
		kern_io_write_barrier();
		for (line = 0U; line < 4096U; line += 64U)
			i915_selftest_clflush((const uint8_t *)ggtt_scratch + line);
		i915_draw_fill_eot(kern_pmem_to_kernel(stats->run.paddr), 64U, 4096U - 64U);
		i915_draw_fill_eot(scratch, 0U, 4096U);
		kern_io_write_barrier();
		for (line = 0U; line < 4096U; line += 64U)
			i915_selftest_clflush((const uint8_t *)scratch + line);
	}

	/* The rectangle covers the whole target in screen space. */
	i915_draw_write_vertices(kern_pmem_to_kernel(vb->run.paddr), 0x42000000U, 0x42000000U);

	/* The batch itself needs a page of its own in the same address space. */
	if (drv_i915_gem_create(device, 4096U, &batch) != 0)
		return EIO;
	if (drv_i915_gem_bind_vm(&engine->kernel_vm, batch) != 0)
		return EIO;
	cmds = kern_pmem_to_kernel(batch->run.paddr);
	dwords = i915_draw_build_batch(cmds, 1024U, surface, dynamic, instruction, vb,
		GEN12_MOCS(I915_MOCS_UNCACHED_INDEX), 1U, NULL);

	/* Every heap and the batch are pushed out of the CPU caches before submission. */
	{
		unsigned di;
		unsigned pc = 0U, sba = 0U, ps = 0U;

		/* Locate the pre-SBA PIPE_CONTROL, STATE_BASE_ADDRESS and 3DSTATE_PS in the built batch. */
		for (di = 0U; di < dwords; di++) {
			uint32_t op = cmds[di] >> 16;
			if (pc == 0U && (op & 0xff00U) == 0x7a00U) pc = di;
			if (sba == 0U && op == 0x6101U) sba = di;
			if (ps == 0U && op == 0x7820U) ps = di;
		}
		kern_logf("i915: raw preSBA_PC@%u: %08x %08x %08x %08x %08x %08x\n", pc,
			cmds[pc], cmds[pc+1U], cmds[pc+2U], cmds[pc+3U], cmds[pc+4U], cmds[pc+5U]);
		for (di = 0U; di < 22U; di++)
			kern_logf("i915: raw SBA[%02u]=0x%08x\n", di, cmds[sba+di]);
		for (di = 0U; di < 12U; di++)
			kern_logf("i915: raw PS[%02u]=0x%08x\n", di, cmds[ps+di]);
		kern_logf("i915: raw CTX_CONTEXT_CONTROL=0x%08x\n",
			engine->kernel_context.state[CTX_CONTEXT_CONTROL]);
	}
	kern_io_write_barrier();
	for (line = 0U; line < dwords * 4U; line += 64U)
		i915_selftest_clflush((const uint8_t *)cmds + line);
	for (index = 0U; index < 2U; index++) {
		for (line = 0U; line < 4096U; line += 64U)
			i915_selftest_clflush((const uint8_t *)
				kern_pmem_to_kernel(objects[index]->run.paddr) + line);
	}

	{
		volatile uint32_t *insn = kern_pmem_to_kernel(instruction->run.paddr);
		unsigned kw;

		i915_selftest_clflush((const uint8_t *)insn + I915_DRAW_PS_KERNEL_OFFSET);
		kern_io_read_barrier();
		kw = I915_DRAW_PS_KERNEL_OFFSET / 4U;
		kern_logf("i915: kernel@0x%llx [0]=0x%08x [1]=0x%08x [16]=0x%08x [17]=0x%08x\\n",
			(unsigned long long)(instruction->va + I915_DRAW_PS_KERNEL_OFFSET),
			insn[kw], insn[kw + 1U], insn[kw + 16U], insn[kw + 17U]);
		__asm__ volatile("wbinvd" : : : "memory");
	}
	i915_draw_apply_engine_workarounds(device, engine);
	kern_logf("i915: idle instdone ring=0x%08x sc=0x%08x row=0x%08x\n",
		drv_i915_read32(device, RING_INSTDONE(engine->base)),
		drv_i915_read32(device, 0x7100U), drv_i915_read32(device, 0xe164U));
	kern_logf("i915: draw heaps state=0x%llx rt=0x%llx stats=0x%llx batch=0x%llx\n",
		(unsigned long long)surface->va, (unsigned long long)rt->va,
		(unsigned long long)stats->va, (unsigned long long)batch->va);
	kern_logf("i915: draw fuses slice_en=0x%08x dss_en=0x%08x eu_dis=0x%08x rpcs(ctx)=0x%08x rpcs(reg)=0x%08x\n",
		drv_i915_read32(device, GEN11_GT_SLICE_ENABLE),
		drv_i915_read32(device, 0x913cU),
		drv_i915_read32(device, 0x9134U),
		engine->kernel_context.state[CTX_R_PWR_CLK_STATE],
		drv_i915_read32(device, engine->base + 0xc8U));
	{
		uint32_t ss;
		for (ss = 0U; ss < 6U; ss++) {
			uint32_t row;
			drv_i915_write32(device, GEN8_MCR_SELECTOR, ss << 24);
			kern_io_write_barrier();
			row = drv_i915_read32(device, 0xe164U);
			drv_i915_write32(device, GEN8_MCR_SELECTOR, GEN11_MCR_MULTICAST);
			kern_io_write_barrier();
			kern_logf("i915: idle ss%u row_instdone=0x%08x\\n", ss, row);
		}
	}

	irq = spin_lock_irqsave(&device->irq_lock);
	error = drv_i915_request_alloc(engine, NULL, NULL, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return error;
	}
	request->context = &engine->kernel_context;

	/*
	 * The URB lives in the L3, which a fresh context leaves unpartitioned; with
	 * no URB ways the vertex fetcher has nowhere to write its vertex entries.
	 * A register load is privileged, so it is issued from the ring rather than
	 * from the batch below.
	 */
	{
		static const uint32_t context_registers[][2] = {
			{ GEN12_REG_L3ALLOC, GEN12_L3ALLOC_VALUE },
			{ GEN11_COMMON_SLICE_CHICKEN3,
			  I915_WA_MASKED_ENABLE(GEN12_DISABLE_CPS_AWARE_COLOR_PIPE) },
			{ GEN8_CS_CHICKEN1,
			  I915_WA_MASKED_FIELD(GEN9_PREEMPT_GPGPU_LEVEL_MASK | CS_CHICKEN1_REPLAY_MODE_MASK |
				CS_CHICKEN1_NO_3DPRIM_PAUSE,
				GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL | CS_CHICKEN1_REPLAY_MIDCMDBUFFER |
				CS_CHICKEN1_NO_3DPRIM_PAUSE) },
			{ GEN12_FF_MODE2, GEN12_FF_MODE2_MESA_VALUE },
			{ HIZ_CHICKEN, I915_WA_MASKED_ENABLE(HZ_DEPTH_TEST_LE_GE_OPT_DISABLE) },
			{ COMMON_SLICE_CHICKEN4, I915_WA_MASKED_ENABLE(DISABLE_TDC_LOAD_BALANCING_CALC) },
			{ COMMON_SLICE_CHICKEN1, I915_WA_MASKED_ENABLE(RCC_RHWO_OPTIMIZATION_DISABLE) },
		};
		const unsigned count = sizeof(context_registers) / sizeof(context_registers[0]);
		unsigned n;
		unsigned entry;

		/*
		 * The L3 partition and the context workarounds are loaded from the ring:
		 * a register load is privileged, and these registers are saved with the
		 * context.  Linux brackets the loads with a full flush; the batch's own
		 * first PIPE_CONTROL follows, so one stalling flush here suffices.
		 */
		n = 0U;
		request->extra[n++] = MI_LOAD_REGISTER_IMM(count);
		for (entry = 0U; entry < count; entry++) {
			request->extra[n++] = context_registers[entry][0];
			request->extra[n++] = context_registers[entry][1];
		}
		request->extra[n++] = GFX_OP_PIPE_CONTROL(6);
		request->extra[n++] = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
			PIPE_CONTROL_DEPTH_CACHE_FLUSH | PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE;
		request->extra[n++] = 0U;
		request->extra[n++] = 0U;
		request->extra[n++] = 0U;
		request->extra[n++] = 0U;
		request->extra_count = n;
	}
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
	marker_cpu = kern_pmem_to_kernel(vb->run.paddr);
	i915_selftest_clflush(&marker_cpu[I915_DRAW_MARKER_OFFSET / 4U]);
	i915_selftest_clflush(&marker_cpu[I915_DRAW_MARKER_OFFSET / 4U + 2U]);
	i915_selftest_clflush(&marker_cpu[I915_DRAW_MARKER_OFFSET / 4U + 4U]);
	kern_logf("i915: draw batch %u dwords markerA=0x%08x markerMid=0x%08x markerPS=0x%08x markerB=0x%08x completed=%u seqno=%u\n",
		dwords, marker_cpu[I915_DRAW_MARKER_OFFSET / 4U],
		marker_cpu[I915_DRAW_MARKER_OFFSET / 4U + 2U],
		marker_cpu[I915_DRAW_MARKER_OFFSET / 4U + 4U],
		marker_cpu[I915_DRAW_MARKER_OFFSET / 4U + 1U],
		engine->completed_seqno, request->seqno);
	error = (engine->completed_seqno == request->seqno &&
		marker_cpu[I915_DRAW_MARKER_OFFSET / 4U + 1U] == I915_DRAW_MARKER_AFTER) ?
		0 : EIO;
	if (error != 0) {
		/*
		 * The statistics registers are readable over MMIO even while the engine
		 * is stuck, which the request-based readback below cannot be.  They say
		 * how far the rectangle travelled before the pipeline stopped draining.
		 */
		kern_logf("i915: hang stats ia_vert=%u ia_prim=%u vs=%u cl_inv=%u cl_prim=%u ps=%u\n",
			drv_i915_read32(device, GEN12_REG_IA_VERTICES_COUNT),
			drv_i915_read32(device, GEN12_REG_IA_PRIMITIVES_COUNT),
			drv_i915_read32(device, GEN12_REG_VS_INVOCATION_COUNT),
			drv_i915_read32(device, GEN12_REG_CL_INVOCATION_COUNT),
			drv_i915_read32(device, GEN12_REG_CL_PRIMITIVES_COUNT),
			drv_i915_read32(device, GEN12_REG_PS_INVOCATION_COUNT));
		kern_logf("i915: hang instdone sc=0x%08x sc_extra=0x%08x sc_extra2=0x%08x sampler=0x%08x row=0x%08x\n",
			drv_i915_read32(device, 0x7100U), drv_i915_read32(device, 0x7104U),
			drv_i915_read32(device, 0x7108U), drv_i915_read32(device, 0xe160U),
			drv_i915_read32(device, 0xe164U));
		{
			uint32_t ss;
			uint32_t row;

			/* ROW_INSTDONE is per-subslice (MCR): steer to each subslice, read, restore multicast. */
			for (ss = 0U; ss < 6U; ss++) {
				drv_i915_write32(device, GEN8_MCR_SELECTOR, ss << 24);
				kern_io_write_barrier();
				row = drv_i915_read32(device, 0xe164U);
				drv_i915_write32(device, GEN8_MCR_SELECTOR, GEN11_MCR_MULTICAST);
				kern_io_write_barrier();
				kern_logf("i915: hang ss%u row_instdone=0x%08x\n", ss, row);
			}
		}
		kern_logf("i915: hang ps_depth=%u\n",
			drv_i915_read32(device, GEN12_REG_PS_DEPTH_COUNT));
		kern_logf("i915: hang fault=0x%08x eir=0x%08x emr=0x%08x ipeir=0x%08x\n",
			drv_i915_read32(device, 0xcec4U),
			drv_i915_read32(device, RING_EIR(engine->base)),
			drv_i915_read32(device, engine->base + 0xb4U),
			drv_i915_read32(device, engine->base + 0x64U));
		kern_logf("i915: rcs0 head=0x%08x tail=0x%08x acthd=0x%08x%08x ipehr=0x%08x instdone=0x%08x esr=0x%08x\n",
			drv_i915_read32(device, RING_HEAD(engine->base)),
			drv_i915_read32(device, RING_TAIL(engine->base)),
			drv_i915_read32(device, RING_ACTHD_UDW(engine->base)),
			drv_i915_read32(device, RING_ACTHD(engine->base)),
			drv_i915_read32(device, RING_IPEHR(engine->base)),
			drv_i915_read32(device, RING_INSTDONE(engine->base)),
			drv_i915_read32(device, RING_ESR(engine->base)));
	}

	/* The counters say how far the rectangle travelled even when nothing was drawn. */
	memset(counters, 0, sizeof(counters));
	if (i915_draw_read_statistics(device, stats, counters, 6U) == 0) {
		for (index = 0U; index < 6U; index++)
			kern_logf("i915: draw stat %s = %u\n", names[index], counters[index]);
	} else {
		kern_logf("i915: draw statistics readback failed (raw 0x%08x)\n", counters[0]);
	}

	/*
	 * The vertex fetcher assembling one rectangle from three vertices is what
	 * this step proves.  The clipper is bypassed, as a screen-space rectangle
	 * requires, and no pixel shader is valid yet, so both of those counters
	 * are zero by definition until the shader arrives in the next step.
	 */
	if (error == 0 && (counters[1] != 1U || counters[2] != 3U)) {
		kern_logf("i915: draw step2 geometry did not reach the vertex stage\n");
		error = EIO;
	}

	/* The shaded rectangle covers the target, so every pixel carries its colour. */
	for (line = 0U; line < I915_DRAW_HEIGHT * I915_DRAW_PITCH; line += 64U)
		i915_selftest_clflush((const uint8_t *)rt_cpu + line);
	kern_io_read_barrier();
	kern_logf("i915: draw pixels [0]=0x%08x [mid]=0x%08x [last]=0x%08x expected=0x%08x\n",
		rt_cpu[0], rt_cpu[I915_DRAW_WIDTH * I915_DRAW_HEIGHT / 2U],
		rt_cpu[I915_DRAW_WIDTH * I915_DRAW_HEIGHT - 1U], I915_DRAW_EXPECTED_PIXEL);
	if (error == 0 &&
	    (rt_cpu[0] != I915_DRAW_EXPECTED_PIXEL ||
	     rt_cpu[I915_DRAW_WIDTH * I915_DRAW_HEIGHT / 2U] != I915_DRAW_EXPECTED_PIXEL ||
	     rt_cpu[I915_DRAW_WIDTH * I915_DRAW_HEIGHT - 1U] != I915_DRAW_EXPECTED_PIXEL))
		error = EIO;
	kern_logf("i915: draw step2 %s\n",
		error == 0 ? "passed (solid colour drawn through the 3D pipeline)" : "FAILED");

	for (index = 0U; index < 3U; index++) {
		drv_i915_gem_unbind_vm(objects[index]);
		drv_i915_gem_destroy(device, objects[index]);
	}
	drv_i915_gem_unbind_vm(batch);
	drv_i915_gem_destroy(device, batch);
	return error;
}
