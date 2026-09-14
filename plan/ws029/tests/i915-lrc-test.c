/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises context image construction, descriptors, submit-queue writes,
 * ring emission and status-buffer decoding against the synthetic device.
 */

#include "i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"
#include "../../../src/drivers/gpu/i915/ggtt.c"
#include "../../../src/drivers/gpu/i915/ppgtt.c"
#include "../../../src/drivers/gpu/i915/gem.c"
#include "../../../src/drivers/gpu/i915/lrc.c"

static void prepare(struct i915_device *device, struct i915_engine *engine, unsigned class, uint32_t base);
static void finish(struct i915_device *device, struct i915_engine *engine);
static void test_image(void);
static void test_render_image(void);
static void test_submit_and_csb(void);
static void test_ring(void);

int
main(void)
{
	test_image();
	test_render_image();
	test_submit_and_csb();
	test_ring();
	printf("i915 lrc host test PASS\n");
	return 0;
}

/* Builds a device with a GGTT and one engine description without touching the interrupt path. */
static void
prepare(
	struct i915_device *device,
	struct i915_engine *engine,
	unsigned class,
	uint32_t base)
{
	int error;

	fixture_reset();
	fixture_device(device);
	error = drv_i915_ggtt_start(device);
	assert(error == 0);
	memset(engine, 0, sizeof(*engine));
	engine->device = device;
	engine->class = class;
	engine->base = base;
	engine->ccid = (0U << (GEN11_ENGINE_INSTANCE_SHIFT - 32U)) | (class << (GEN11_ENGINE_CLASS_SHIFT - 32U));
	error = drv_i915_gem_create(device, I915_HWSP_BYTES, &engine->hwsp);
	assert(error == 0);
	error = drv_i915_gem_bind_ggtt(device, engine->hwsp);
	assert(error == 0);
	engine->status = engine->hwsp->address;
}

/* Releases the status page and the GGTT so every scenario ends without a leak. */
static void
finish(
	struct i915_device *device,
	struct i915_engine *engine)
{
	drv_i915_gem_unbind_ggtt(device, engine->hwsp);
	drv_i915_gem_destroy(device, engine->hwsp);
	engine->hwsp = NULL;
	assert(device->object_count == 0U);
	drv_i915_ggtt_stop(device);
	assert(fixture_pool_live == 0U);
}

/* The copy-engine image follows the gen12 offset table and names its ring and space. */
static void
test_image(void)
{
	struct i915_device device;
	struct i915_engine engine;
	struct i915_ppgtt vm;
	struct i915_context context;
	uint32_t *state;
	int error;

	prepare(&device, &engine, I915_CLASS_COPY, I915_BCS0_BASE);
	memset(&vm, 0, sizeof(vm));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	memset(&context, 0, sizeof(context));
	error = drv_i915_lrc_create(&device, &engine, &vm, 5U, &context);
	assert(error == 0);

	/* A copy context is two pages: status page then state page. */
	assert(context.image->bytes == 2U * I915_PAGE_BYTES);
	assert(context.image->ggtt_pages == 2U);
	assert(context.ring->bytes == I915_RING_BYTES);
	assert(context.state == (uint32_t *)((uint8_t *)context.image->address + I915_PAGE_BYTES));

	/* The first load names 13 posted registers starting with context control. */
	state = context.state;
	assert(state[0] == 0U);
	assert(state[1] == (MI_LOAD_REGISTER_IMM(13) | MI_LRI_LRM_CS_MMIO | MI_LRI_FORCE_POSTED));
	assert(state[2] == I915_BCS0_BASE + 0x244U);
	assert(state[CTX_RING_HEAD - 1U] == I915_BCS0_BASE + 0x34U);
	assert(state[CTX_RING_TAIL - 1U] == I915_BCS0_BASE + 0x30U);
	assert(state[CTX_RING_START - 1U] == I915_BCS0_BASE + 0x38U);
	assert(state[CTX_RING_CTL - 1U] == I915_BCS0_BASE + 0x3cU);
	assert(state[CTX_BB_STATE - 1U] == I915_BCS0_BASE + 0x110U);
	assert(state[CTX_TIMESTAMP - 1U] == I915_BCS0_BASE + 0x3a8U);
	assert(state[CTX_PDP0_UDW - 1U] == I915_BCS0_BASE + 0x274U);
	assert(state[CTX_PDP0_LDW - 1U] == I915_BCS0_BASE + 0x270U);

	/* The values name this context's ring, address space and first-run inhibit. */
	assert(state[CTX_RING_START] == context.ring->ggtt_offset);
	assert(state[CTX_RING_HEAD] == 0U);
	assert(state[CTX_RING_TAIL] == 0U);
	assert(state[CTX_RING_CTL] == (RING_CTL_SIZE(I915_RING_BYTES) | RING_VALID));
	assert(state[CTX_CONTEXT_CONTROL] == ((CTX_CTRL_INHIBIT_SYN_CTX_SWITCH << 16) | CTX_CTRL_INHIBIT_SYN_CTX_SWITCH |
	    (CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT << 16) | CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT));
	assert(state[CTX_PDP0_LDW] == (uint32_t)vm.pml4.paddr);
	assert(state[CTX_PDP0_UDW] == (uint32_t)((uint64_t)vm.pml4.paddr >> 32));
	assert(state[0x61] == (STOP_RING << 16));
	assert(state[52] == (MI_BATCH_BUFFER_END | 1U));

	/* The descriptor: 64-bit addressing, valid, privileged, image offset; id and engine above. */
	assert(context.descriptor_low == ((I915_LEGACY_64B_CONTEXT << GEN8_CTX_ADDRESSING_MODE_SHIFT) | GEN8_CTX_VALID | GEN8_CTX_PRIVILEGE | context.image->ggtt_offset));
	assert(context.descriptor_high == ((5U << 5) | (COPY_ENGINE_CLASS << 29)));

	/* Destroy returns both objects. */
	drv_i915_lrc_destroy(&device, &context);
	assert(context.created == 0U);
	assert(device.object_count == 1U);
	drv_i915_ppgtt_destroy(&vm);
	finish(&device, &engine);
}

/* The render image is larger and keeps the power state register at its known slot. */
static void
test_render_image(void)
{
	struct i915_device device;
	struct i915_engine engine;
	struct i915_ppgtt vm;
	struct i915_context context;
	int error;

	prepare(&device, &engine, I915_CLASS_RENDER, I915_RCS0_BASE);
	memset(&vm, 0, sizeof(vm));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	memset(&context, 0, sizeof(context));
	error = drv_i915_lrc_create(&device, &engine, &vm, 7U, &context);
	assert(error == 0);
	assert(context.image->bytes == GEN11_LR_CONTEXT_RENDER_SIZE);
	assert(context.state[CTX_R_PWR_CLK_STATE - 1U] == I915_RCS0_BASE + 0x0c8U);
	assert(context.state[CTX_R_PWR_CLK_STATE] == 0U);
	assert(context.state[CTX_RING_TAIL - 1U] == I915_RCS0_BASE + 0x30U);
	assert(context.descriptor_high == ((7U << 5) | (RENDER_CLASS << 29)));
	drv_i915_lrc_destroy(&device, &context);
	drv_i915_ppgtt_destroy(&vm);
	finish(&device, &engine);
}

/* Submission writes both ports and the load bit; status-buffer events are classified. */
static void
test_submit_and_csb(void)
{
	struct i915_device device;
	struct i915_engine engine;
	struct i915_ppgtt vm;
	struct i915_context context;
	unsigned promotions;
	unsigned completions;
	unsigned index;
	int error;

	prepare(&device, &engine, I915_CLASS_COPY, I915_BCS0_BASE);
	memset(&vm, 0, sizeof(vm));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	memset(&context, 0, sizeof(context));
	error = drv_i915_lrc_create(&device, &engine, &vm, 9U, &context);
	assert(error == 0);

	/* The pointer reset primes the status page and register. */
	drv_i915_lrc_reset_csb(&engine);
	assert(engine.csb_head == I915_CSB_ENTRIES - 1U);
	assert(engine.status[I915_CSB_WRITE_INDEX] == I915_CSB_ENTRIES - 1U);
	assert(fixture_load32(fixture_regs + RING_CONTEXT_STATUS_PTR(I915_BCS0_BASE)) == ((0xffffU << 16) | (11U << 8) | 11U));
	for (index = 0U; index < I915_CSB_ENTRIES; index++)
		assert(engine.status[I915_HWS_CSB_BUF0_INDEX + 2U * index] == 0xffffffffU);

	/* Submit records the tail and writes port one cleared, port zero with force restore. */
	context.ring_tail = 64U;
	drv_i915_lrc_submit(&context);
	assert(context.state[CTX_RING_TAIL] == 64U);
	assert(fixture_load32(fixture_regs + RING_EXECLIST_SQ_CONTENTS(I915_BCS0_BASE) + 8U) == 0U);
	assert(fixture_load32(fixture_regs + RING_EXECLIST_SQ_CONTENTS(I915_BCS0_BASE) + 12U) == 0U);
	assert(fixture_load32(fixture_regs + RING_EXECLIST_SQ_CONTENTS(I915_BCS0_BASE)) == (context.descriptor_low | (uint32_t)CTX_DESC_FORCE_RESTORE));
	assert(fixture_load32(fixture_regs + RING_EXECLIST_SQ_CONTENTS(I915_BCS0_BASE) + 4U) == context.descriptor_high);
	assert(fixture_load_pending[I915_ENGINE_BCS0] == 1U);

	/* No new entry: nothing is consumed. */
	completions = drv_i915_lrc_csb_consume(&engine, &promotions);
	assert(completions == 0U && promotions == 0U);

	/* A promotion entry starts the context. */
	engine.status[I915_HWS_CSB_BUF0_INDEX + 0U] = (9U << 15) | GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE;
	engine.status[I915_HWS_CSB_BUF0_INDEX + 1U] = GEN12_IDLE_CTX_ID << 15;
	engine.status[I915_CSB_WRITE_INDEX] = 0U;
	completions = drv_i915_lrc_csb_consume(&engine, &promotions);
	assert(completions == 0U && promotions == 1U);
	assert(engine.csb_head == 0U);
	assert(engine.status[I915_HWS_CSB_BUF0_INDEX + 0U] == 0xffffffffU);

	/* A completion entry: idle switched to, the context switched away. */
	engine.status[I915_HWS_CSB_BUF0_INDEX + 2U] = GEN12_IDLE_CTX_ID << 15;
	engine.status[I915_HWS_CSB_BUF0_INDEX + 3U] = 9U << 15;
	engine.status[I915_CSB_WRITE_INDEX] = 1U;
	completions = drv_i915_lrc_csb_consume(&engine, &promotions);
	assert(completions == 1U && promotions == 0U);
	assert(engine.csb_completions == 1U && engine.csb_promotions == 1U);

	/* An entry the engine never wrote, in the status page or the register mirror, is skipped. */
	fixture_store32(fixture_regs + I915_BCS0_BASE + GEN8_EXECLISTS_STATUS_BUF + 16U, 0xffffffffU);
	fixture_store32(fixture_regs + I915_BCS0_BASE + GEN8_EXECLISTS_STATUS_BUF + 20U, 0xffffffffU);
	engine.status[I915_CSB_WRITE_INDEX] = 2U;
	completions = drv_i915_lrc_csb_consume(&engine, &promotions);
	assert(completions == 0U && promotions == 0U);
	assert(engine.csb_errors == 1U);
	assert(engine.csb_head == 2U);

	/* An entry late in the status page but present in the mirror is taken from the mirror. */
	fixture_store32(fixture_regs + I915_BCS0_BASE + GEN8_EXECLISTS_STATUS_BUF + 24U, (9U << 15) | GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE);
	fixture_store32(fixture_regs + I915_BCS0_BASE + GEN8_EXECLISTS_STATUS_BUF + 28U, GEN12_IDLE_CTX_ID << 15);
	engine.status[I915_CSB_WRITE_INDEX] = 3U;
	completions = drv_i915_lrc_csb_consume(&engine, &promotions);
	assert(completions == 0U && promotions == 1U);
	assert(engine.csb_head == 3U);

	drv_i915_lrc_destroy(&device, &context);
	drv_i915_ppgtt_destroy(&vm);
	finish(&device, &engine);
}

/* Ring emission pads to the end and wraps; space accounting respects the saved head. */
static void
test_ring(void)
{
	struct i915_device device;
	struct i915_engine engine;
	struct i915_ppgtt vm;
	struct i915_context context;
	uint32_t dwords[4];
	unsigned index;
	int error;

	prepare(&device, &engine, I915_CLASS_COPY, I915_BCS0_BASE);
	memset(&vm, 0, sizeof(vm));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	memset(&context, 0, sizeof(context));
	error = drv_i915_lrc_create(&device, &engine, &vm, 3U, &context);
	assert(error == 0);

	/* An empty ring has room for almost everything. */
	error = drv_i915_lrc_ring_space(&context, I915_RING_DWORDS - 4U);
	assert(error == 0);
	error = drv_i915_lrc_ring_space(&context, I915_RING_DWORDS);
	assert(error == ENOSPC);

	/* Four dwords near the end pad the remainder with no-ops and land at offset zero. */
	dwords[0] = 0x11111111U;
	dwords[1] = 0x22222222U;
	dwords[2] = 0x33333333U;
	dwords[3] = 0x44444444U;
	context.ring_tail = I915_RING_BYTES - 8U;
	context.state[CTX_RING_HEAD] = I915_RING_BYTES - 8U;
	drv_i915_lrc_ring_emit(&context, dwords, 4U);
	assert(context.ring_dwords[I915_RING_DWORDS - 2U] == MI_NOOP);
	assert(context.ring_dwords[I915_RING_DWORDS - 1U] == MI_NOOP);
	for (index = 0U; index < 4U; index++)
		assert(context.ring_dwords[index] == dwords[index]);
	assert(context.ring_tail == 16U);

	/* With the head just ahead of the tail the ring is full. */
	context.state[CTX_RING_HEAD] = 24U;
	error = drv_i915_lrc_ring_space(&context, 1U);
	assert(error == ENOSPC);
	context.state[CTX_RING_HEAD] = 32U;
	error = drv_i915_lrc_ring_space(&context, 2U);
	assert(error == 0);

	drv_i915_lrc_destroy(&device, &context);
	drv_i915_ppgtt_destroy(&vm);
	assert(device.object_count == 1U);
	finish(&device, &engine);
}
