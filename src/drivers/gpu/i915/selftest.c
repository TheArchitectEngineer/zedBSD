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

/* The render target the 3D pipeline draws into: one page of linear BGRA. */
#define I915_DRAW_WIDTH			32U
#define I915_DRAW_HEIGHT		32U
#define I915_DRAW_PITCH			(I915_DRAW_WIDTH * 4U)

/* Offsets within the surface state heap. */
#define I915_DRAW_BINDING_TABLE_OFFSET	0U
#define I915_DRAW_SURFACE_STATE_OFFSET	64U

/* Offsets within the dynamic state heap; each state is 64-byte aligned. */
#define I915_DRAW_COLOR_CALC_OFFSET	0U
#define I915_DRAW_BLEND_OFFSET		64U
#define I915_DRAW_CC_VIEWPORT_OFFSET	128U

/* Offsets within the vertex buffer page. */
#define I915_DRAW_POSITION_OFFSET	0U
#define I915_DRAW_VUE_HEADER_OFFSET	64U

/* The markers the batch stores so a hang can be placed within it. */
#define I915_DRAW_MARKER_BEFORE		0xa5a50001U
#define I915_DRAW_MARKER_AFTER		0xd7a3f00dU

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

	i915_draw_emit(batch, GFX_OP_PIPE_CONTROL(6));
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
	surface[1] = mocs << 24;
	surface[2] = (I915_DRAW_WIDTH - 1U) | ((I915_DRAW_HEIGHT - 1U) << 16);
	surface[3] = I915_DRAW_PITCH - 1U;

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

	memset(heap, 0, 256U);

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

	memset(page, 0, 128U);

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
	i915_draw_emit(batch, (0U << 26) | (mocs << 16) | (1U << 14) | 12U);
	i915_draw_emit(batch, (uint32_t)(vb_va + I915_DRAW_POSITION_OFFSET));
	i915_draw_emit(batch, (uint32_t)((vb_va + I915_DRAW_POSITION_OFFSET) >> 32));
	i915_draw_emit(batch, 36U);

	/* Buffer 1 holds the zeroed VUE header, read with a zero pitch by every vertex. */
	header_va = vb_va + I915_DRAW_VUE_HEADER_OFFSET;
	i915_draw_emit(batch, (1U << 26) | (mocs << 16) | (1U << 14) | 0U);
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
	     opcode <= GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_PS;
	     opcode++)
		i915_draw_emit_disabled(batch, opcode, GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS);

	/* The vertex stage owns chunk zero: 64 entries of one 64-byte slot. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_URB_ALLOC_VS,
		GEN12_3DSTATE_URB_ALLOC_DWORDS));
	i915_draw_emit(batch, (4U << 10) | (4U << 21));
	i915_draw_emit(batch, 64U | (64U << 16));

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
static void
i915_draw_emit_raster_state(
	struct i915_draw_batch *batch,
	unsigned pixel_shader_valid)
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
	i915_draw_emit(batch, GEN12_URB_DEREF_BLOCK_SIZE_PER_POLY << 29);
	i915_draw_emit(batch, 0U);

	/* Both faces of the rectangle are filled solid and neither is culled. */
	i915_draw_emit_disabled(batch, GEN12_CMD_3DSTATE_RASTER, GEN12_3DSTATE_RASTER_DWORDS);

	/* The setup stage forwards no attributes; only the position is read. */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SBE, GEN12_3DSTATE_SBE_DWORDS));
	i915_draw_emit(batch, (1U << 5) | (1U << 11) | (1U << 28) | (1U << 29));
	for (index = 2U; index < GEN12_3DSTATE_SBE_DWORDS; index++)
		i915_draw_emit(batch, 0U);

	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_WM, GEN12_3DSTATE_WM_DWORDS));
	i915_draw_emit(batch, 1U << 31);			/* statistics enable */

	/*
	 * 3DSTATE_PS: the thread count must be non-zero even with dispatch off,
	 * which the documentation notes is required to keep the GPU from hanging.
	 */
	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS, GEN12_3DSTATE_PS_DWORDS));
	for (index = 1U; index < GEN12_3DSTATE_PS_DWORDS; index++) {
		if (index == 6U)
			i915_draw_emit(batch, (GEN12_MAX_THREADS_PER_PSD - 1U) << 23);
		else
			i915_draw_emit(batch, 0U);
	}

	i915_draw_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS_EXTRA, GEN12_3DSTATE_PS_EXTRA_DWORDS));
	i915_draw_emit(batch, pixel_shader_valid != 0U ? (1U << 31) : 0U);

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
	i915_draw_emit(batch, GEN12_SURFTYPE_NULL << 29);
	for (index = 2U; index < GEN12_3DSTATE_DEPTH_BUFFER_DWORDS; index++)
		i915_draw_emit(batch, 0U);

	i915_draw_emit_disabled(batch, GEN12_CMD_3DSTATE_STENCIL_BUFFER,
		GEN12_3DSTATE_STENCIL_BUFFER_DWORDS);
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
	const struct i915_gem_object *rt,
	const struct i915_gem_object *surface,
	const struct i915_gem_object *dynamic,
	const struct i915_gem_object *instruction,
	const struct i915_gem_object *vb,
	uint32_t mocs,
	unsigned pixel_shader_valid)
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
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, mocs << 16);
	i915_draw_emit(&batch, 1U | (mocs << 4) | ((uint32_t)surface->va & 0xfffff000U));
	i915_draw_emit(&batch, (uint32_t)(surface->va >> 32));
	i915_draw_emit(&batch, 1U | (mocs << 4) | ((uint32_t)dynamic->va & 0xfffff000U));
	i915_draw_emit(&batch, (uint32_t)(dynamic->va >> 32));
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 1U | (mocs << 4) | ((uint32_t)instruction->va & 0xfffff000U));
	i915_draw_emit(&batch, (uint32_t)(instruction->va >> 32));
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 1U | (0xfffffU << 12));
	i915_draw_emit(&batch, 0U);
	i915_draw_emit(&batch, 1U | (0xfffffU << 12));
	for (index = 0U; index < 6U; index++)
		i915_draw_emit(&batch, 0U);

	/* The state caches must be invalidated before anything reads the new bases. */
	i915_draw_emit_pipe_control(&batch, PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_STATE_CACHE_INVALIDATE | PIPE_CONTROL_CONST_CACHE_INVALIDATE |
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE);
	i915_draw_emit_marker(&batch, rt->va + 4U, I915_DRAW_MARKER_BEFORE);

	i915_draw_emit_vertex_state(&batch, vb->va, mocs);
	i915_draw_emit_urb(&batch);

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

	i915_draw_emit_raster_state(&batch, pixel_shader_valid);
	i915_draw_emit_depth_state(&batch);

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
	i915_draw_emit_marker(&batch, rt->va, I915_DRAW_MARKER_AFTER);

	i915_draw_emit(&batch, MI_BATCH_BUFFER_END);
	i915_draw_emit(&batch, MI_NOOP);

	return batch.count;
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

	/* Render target, the three state heaps, the vertex data and the counters. */
	for (index = 0U; index < 6U; index++) {
		if (drv_i915_gem_create(device, 4096U, &objects[index]) != 0)
			return EIO;
		if (drv_i915_gem_bind_vm(&engine->kernel_vm, objects[index]) != 0)
			return EIO;
	}
	rt = objects[0];
	surface = objects[1];
	dynamic = objects[2];
	instruction = objects[3];
	vb = objects[4];
	stats = objects[5];

	rt_cpu = kern_pmem_to_kernel(rt->run.paddr);
	memset((void *)rt_cpu, 0, 4096U);
	i915_draw_write_surface_state(kern_pmem_to_kernel(surface->run.paddr), rt->va,
		I915_MOCS_UNCACHED_INDEX);
	i915_draw_write_dynamic_state(kern_pmem_to_kernel(dynamic->run.paddr));
	memset(kern_pmem_to_kernel(instruction->run.paddr), 0, 4096U);

	/* The rectangle covers the whole target in screen space. */
	i915_draw_write_vertices(kern_pmem_to_kernel(vb->run.paddr), 0x42000000U, 0x42000000U);

	/* The batch itself needs a page of its own in the same address space. */
	if (drv_i915_gem_create(device, 4096U, &batch) != 0)
		return EIO;
	if (drv_i915_gem_bind_vm(&engine->kernel_vm, batch) != 0)
		return EIO;
	cmds = kern_pmem_to_kernel(batch->run.paddr);
	dwords = i915_draw_build_batch(cmds, 1024U, rt, surface, dynamic, instruction, vb,
		I915_MOCS_UNCACHED_INDEX, 0U);

	/* Every heap and the batch are pushed out of the CPU caches before submission. */
	kern_io_write_barrier();
	for (line = 0U; line < dwords * 4U; line += 64U)
		i915_selftest_clflush((const uint8_t *)cmds + line);

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
	request->extra[0] = MI_LOAD_REGISTER_IMM(1);
	request->extra[1] = GEN12_REG_L3ALLOC;
	request->extra[2] = GEN12_L3ALLOC_VALUE;
	request->extra_count = 3U;
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
	i915_selftest_clflush(&rt_cpu[0]);
	kern_logf("i915: draw batch %u dwords markerA=0x%08x markerB=0x%08x completed=%u seqno=%u\n",
		dwords, rt_cpu[1], rt_cpu[0], engine->completed_seqno, request->seqno);
	error = (engine->completed_seqno == request->seqno && rt_cpu[0] == I915_DRAW_MARKER_AFTER) ?
		0 : EIO;
	if (error != 0) {
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
	kern_logf("i915: draw step2 %s\n",
		error == 0 ? "passed (3D pipeline state parses and assembles the rectangle)" : "FAILED");

	for (index = 0U; index < 6U; index++) {
		drv_i915_gem_unbind_vm(objects[index]);
		drv_i915_gem_destroy(device, objects[index]);
	}
	drv_i915_gem_unbind_vm(batch);
	drv_i915_gem_destroy(device, batch);
	return error;
}
