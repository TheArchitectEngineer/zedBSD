/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the complete backend against the synthetic device: PCI attach and
 * publication, sessions with private address spaces, storage objects with CPU
 * copies, the resource limits, and the reverse teardown.
 */

#include "i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"
#include "../../../src/drivers/gpu/i915/ggtt.c"
#include "../../../src/drivers/gpu/i915/ppgtt.c"
#include "../../../src/drivers/gpu/i915/gem.c"
#include "../../../src/drivers/gpu/i915/irq.c"
#include "../../../src/drivers/gpu/i915/engine.c"
#include "../../../src/drivers/gpu/i915/lrc.c"
#include "../../../src/drivers/gpu/i915/request.c"
#include "../../../src/drivers/gpu/i915/selftest.c"
#include "../../../src/drivers/gpu/i915/i915.c"

/* One fake PCI device identity; the stubs never dereference it. */
static int fixture_pci_token;
#define fixture_pci_device	((struct drv_pci_device *)&fixture_pci_token)

/* A completion token; the fixture records deliveries without dereferencing it. */
static int fixture_completion_token;
#define fixture_completion	((struct drv_gpu_completion *)&fixture_completion_token)

static struct i915_device *attach(void);
static void detach(void);
static void test_lifecycle(void);
static void test_resources(void);
static void test_failures(void);
static void test_execution(void);
static void test_hang_and_reset(void);
static void test_streams_and_jobs(void);
static void test_isolate(void);
static void test_fault_and_reset(void);
static struct i915_request *submit_batch(struct i915_device *device, struct i915_session *session, struct i915_gem_object *batch);
static uint32_t stream_build(uint8_t *stream, const uint32_t *batch, uint32_t dwords, uint32_t first_offset, uint64_t first, uint32_t second_offset, uint64_t second);
static struct i915_gem_object *create_resource(struct i915_session *session, uint64_t handle, uint64_t bytes);

int
main(void)
{
	test_lifecycle();
	test_resources();
	test_failures();
	test_execution();
	test_hang_and_reset();
	test_streams_and_jobs();
	test_isolate();
	test_fault_and_reset();
	printf("i915 backend host test PASS\n");
	return 0;
}

/* Builds a native stream for BCS0 with up to two relocations (a zero handle means none). */
static uint32_t
stream_build(
	uint8_t *stream,
	const uint32_t *batch,
	uint32_t dwords,
	uint32_t first_offset,
	uint64_t first,
	uint32_t second_offset,
	uint64_t second)
{
	uint32_t relocations;
	uint32_t word;
	uint8_t *at;
	uint32_t index;

	relocations = 0U;
	if (first != 0U)
		relocations++;
	if (second != 0U)
		relocations++;
	memset(stream, 0, 32U + 2U * 16U + dwords * 4U);
	word = I915_STREAM_MAGIC;
	memcpy(stream, &word, 4U);
	word = I915_STREAM_VERSION;
	memcpy(stream + 4U, &word, 4U);
	word = I915_STREAM_ENGINE_BCS0;
	memcpy(stream + 8U, &word, 4U);
	memcpy(stream + 12U, &relocations, 4U);
	memcpy(stream + 16U, &dwords, 4U);
	at = stream + 32U;
	if (first != 0U) {
		memcpy(at, &first_offset, 4U);
		memcpy(at + 8U, &first, 8U);
		at += 16U;
	}
	if (second != 0U) {
		memcpy(at, &second_offset, 4U);
		memcpy(at + 8U, &second, 8U);
		at += 16U;
	}
	for (index = 0U; index < dwords; index++)
		memcpy(at + index * 4U, &batch[index], 4U);
	return 32U + relocations * 16U + dwords * 4U;
}

/* Creates one storage resource through the published operations with a given public handle. */
static struct i915_gem_object *
create_resource(
	struct i915_session *session,
	uint64_t handle,
	uint64_t bytes)
{
	struct gpu_resource_create request;
	void *opaque;
	int error;

	memset(&request, 0, sizeof(request));
	request.usage = GPU_RESOURCE_USAGE_STORAGE;
	request.bytes = bytes;
	request.handle = handle;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == 0);
	assert(((struct i915_gem_object *)opaque)->handle == handle);
	return opaque;
}

/* Streams with relocations run copy and fill blits; markers, drain and jobs use the same path. */
static void
test_streams_and_jobs(void)
{
	struct i915_device *device;
	struct i915_engine *engine;
	struct i915_session *session;
	struct i915_gem_object *src;
	struct i915_gem_object *dst;
	struct i915_request *slot;
	uint32_t batch[12];
	uint32_t words[1024];
	uint32_t readback[1024];
	uint8_t stream[32U + 2U * 16U + 12U * 4U];
	uint32_t bytes;
	unsigned available;
	unsigned index;
	void *opaque;
	void *reservation;
	int error;

	fixture_reset();
	device = attach();
	engine = &device->engines[I915_ENGINE_BCS0];
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	session = opaque;
	src = create_resource(session, 11U, 4096U);
	dst = create_resource(session, 12U, 4096U);
	assert(session->objects == dst && dst->session_next == src);

	/* A copy blit moves one row of 1024 pixels from src to dst. */
	for (index = 0U; index < 1024U; index++)
		words[index] = 0x5a000000U + index;
	error = fixture_gpu_ops->resource_write(fixture_gpu_private, session, src, 0U, words, 4096U);
	assert(error == 0);
	batch[0] = XY_SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | 8U;
	batch[1] = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | 4096U;
	batch[2] = 0U;
	batch[3] = (1U << 16) | 1024U;
	batch[4] = 0U;
	batch[5] = 0U;
	batch[6] = 0U;
	batch[7] = 4096U;
	batch[8] = 0U;
	batch[9] = 0U;
	batch[10] = MI_BATCH_BUFFER_END;
	bytes = stream_build(stream, batch, 11U, 4U, 12U, 8U, 11U);
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, session, stream, bytes, 0U, 0U, fixture_completion);
	assert(error == 0);
	assert(session->batch_count == 1U && session->batches->busy == 1U);
	assert(((uint32_t *)session->batches->address)[4] == (uint32_t)dst->va);
	assert(((uint32_t *)session->batches->address)[9] == (uint32_t)(src->va >> 32));
	fixture_run_engines();
	assert(fixture_completions == 1U && fixture_completion_error == 0);
	assert(session->batches->busy == 0U);
	error = fixture_gpu_ops->resource_read(fixture_gpu_private, session, dst, 0U, readback, 4096U);
	assert(error == 0);
	assert(memcmp(words, readback, 4096U) == 0);

	/* A fill blit reuses the pooled batch object. */
	batch[0] = XY_COLOR_BLT_CMD | BLT_WRITE_RGBA | 5U;
	batch[1] = BLT_DEPTH_32 | BLT_ROP_COLOR_COPY | 4096U;
	batch[2] = 0U;
	batch[3] = (1U << 16) | 1024U;
	batch[4] = 0U;
	batch[5] = 0U;
	batch[6] = 0x3197a5e2U;
	batch[7] = MI_NOOP;
	batch[8] = MI_BATCH_BUFFER_END;
	bytes = stream_build(stream, batch, 9U, 4U, 12U, 0U, 0U);
	error = fixture_gpu_ops->command(fixture_gpu_private, session, stream, bytes);
	assert(error == 0);
	assert(session->batch_count == 1U);
	fixture_run_engines();
	error = fixture_gpu_ops->resource_read(fixture_gpu_private, session, dst, 0U, readback, 4096U);
	assert(error == 0);
	for (index = 0U; index < 1024U; index++)
		assert(readback[index] == 0x3197a5e2U);
	assert(fixture_completions == 1U);

	/* A stream naming a foreign handle or with a bad shape is refused without a slot. */
	bytes = stream_build(stream, batch, 9U, 4U, 99U, 0U, 0U);
	error = fixture_gpu_ops->command(fixture_gpu_private, session, stream, bytes);
	assert(error == EINVAL);
	error = fixture_gpu_ops->command(fixture_gpu_private, session, stream, bytes - 4U);
	assert(error == EINVAL);
	assert(session->batches->busy == 0U);
	assert(session->pending_requests == 0U);

	/* An empty submission is a marker on the copy engine's timeline. */
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, session, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 1U, fixture_completion);
	assert(error == 0);
	fixture_run_engines();
	assert(fixture_completions == 2U);
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, session, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 7U, fixture_completion);
	assert(error == EINVAL);

	/* Drain lets the modeled engine run while waiting for the last request. */
	fixture_run_in_sleep = 1U;
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, session, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 1U, fixture_completion);
	assert(error == 0);
	assert(session->pending_requests == 1U);
	fixture_gpu_ops->commands->drain(fixture_gpu_private, session);
	assert(session->pending_requests == 0U);
	assert(fixture_completions == 3U);
	fixture_run_in_sleep = 0U;

	/* Jobs: capacity, reserve, rollback, commit and fault cancel. */
	error = fixture_gpu_ops->jobs->capacity(fixture_gpu_private, session, 1U, &available);
	assert(error == 0 && available == I915_REQUEST_SLOTS);
	error = fixture_gpu_ops->jobs->capacity(fixture_gpu_private, session, 0U, &available);
	assert(error == EINVAL);
	error = fixture_gpu_ops->jobs->reserve(fixture_gpu_private, session, 1U, fixture_completion, &reservation);
	assert(error == 0);
	slot = reservation;
	assert(slot->state == I915_REQUEST_RESERVED && session->pending_requests == 1U);
	error = fixture_gpu_ops->jobs->capacity(fixture_gpu_private, session, 1U, &available);
	assert(error == 0 && available == I915_REQUEST_SLOTS - 1U);
	error = fixture_gpu_ops->jobs->cancel(fixture_gpu_private, session, reservation, fixture_completion, 0U);
	assert(error == 0);
	assert(slot->state == I915_REQUEST_FREE && session->pending_requests == 0U);
	error = fixture_gpu_ops->jobs->cancel(fixture_gpu_private, session, reservation, fixture_completion, 0U);
	assert(error == ESTALE);
	error = fixture_gpu_ops->jobs->reserve(fixture_gpu_private, session, 1U, fixture_completion, &reservation);
	assert(error == 0);
	error = fixture_gpu_ops->jobs->commit(fixture_gpu_private, session, reservation, fixture_completion);
	assert(error == 0);
	assert(engine->active == reservation);
	fixture_run_engines();
	assert(fixture_completions == 4U && fixture_completion_error == 0);
	assert(session->pending_requests == 0U);
	error = fixture_gpu_ops->jobs->commit(fixture_gpu_private, session, reservation, fixture_completion);
	assert(error == ESTALE);

	/* A fault cancel keeps the slot; stop begin and poll then see it until isolation ends it. */
	error = fixture_gpu_ops->jobs->reserve(fixture_gpu_private, session, 1U, fixture_completion, &reservation);
	assert(error == 0);
	slot = reservation;
	error = fixture_gpu_ops->jobs->cancel(fixture_gpu_private, session, reservation, fixture_completion, 1U);
	assert(error == 0);
	assert(slot->state == I915_REQUEST_RETAINED && session->pending_requests == 1U);
	error = fixture_gpu_ops->recovery->stop_poll(fixture_gpu_private, session);
	assert(error == EINVAL);
	error = fixture_gpu_ops->recovery->stop_begin(fixture_gpu_private, session, ETIMEDOUT);
	assert(error == 0);
	error = fixture_gpu_ops->recovery->stop_poll(fixture_gpu_private, session);
	assert(error == EAGAIN);
	error = fixture_gpu_ops->recovery->isolate(fixture_gpu_private, session);
	assert(error == 0);
	assert(fixture_completions == 5U && fixture_completion_error == EIO);
	assert(slot->state == I915_REQUEST_FREE);
	assert(session->quarantined == 1U);
	assert(engine->reset_count == 0U);
	error = fixture_gpu_ops->recovery->stop_poll(fixture_gpu_private, session);
	assert(error == 0);

	/* The quarantined session keeps its objects until detach resets the device. */
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, src);
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, dst);
	fixture_gpu_ops->close(fixture_gpu_private, session);
	assert(device->quarantined_objects == 7U);
	assert(device->quarantined_vms != NULL);
	detach();
}

/* A hung session is isolated with an engine reset while a peer keeps running. */
static void
test_isolate(void)
{
	struct i915_device *device;
	struct i915_engine *engine;
	struct i915_session *session;
	struct i915_session *peer;
	struct i915_gem_object *target;
	uint32_t batch[6];
	uint8_t stream[32U + 2U * 16U + 12U * 4U];
	uint32_t bytes;
	uint32_t value;
	void *opaque;
	int error;

	fixture_reset();
	device = attach();
	engine = &device->engines[I915_ENGINE_BCS0];
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	session = opaque;
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	peer = opaque;
	target = create_resource(peer, 21U, 4096U);

	/* The first session hangs the copy engine; the peer's store waits behind it. */
	batch[0] = MI_SEMAPHORE_WAIT | MI_SEMAPHORE_POLL | MI_SEMAPHORE_SAD_GTE_SDD;
	batch[1] = 1U;
	batch[2] = 0U;
	batch[3] = 0U;
	batch[4] = MI_BATCH_BUFFER_END;
	bytes = stream_build(stream, batch, 5U, 2U, 21U, 0U, 0U);
	target = create_resource(session, 21U, 4096U);
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, session, stream, bytes, 0U, 0U, fixture_completion);
	assert(error == 0);
	batch[0] = MI_STORE_DWORD_IMM_GEN4;
	batch[1] = 0U;
	batch[2] = 0U;
	batch[3] = 0x0badf00dU;
	batch[4] = MI_BATCH_BUFFER_END;
	bytes = stream_build(stream, batch, 5U, 1U, 21U, 0U, 0U);
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, peer, stream, bytes, 0U, 0U, fixture_completion);
	assert(error == 0);
	fixture_run_engines();
	assert(fixture_engine_hung[I915_ENGINE_BCS0] == 1U);
	assert(fixture_completions == 0U);
	assert(engine->queue_head != NULL);

	/* Stop cannot be confirmed, so the framework isolates the hung session. */
	error = fixture_gpu_ops->recovery->stop_begin(fixture_gpu_private, session, ETIMEDOUT);
	assert(error == 0);
	error = fixture_gpu_ops->recovery->stop_poll(fixture_gpu_private, session);
	assert(error == EAGAIN);
	error = fixture_gpu_ops->recovery->isolate(fixture_gpu_private, session);
	assert(error == 0);
	assert(fixture_completions == 1U && fixture_completion_error == EIO);
	assert(engine->reset_count == 1U);
	assert(session->quarantined == 1U);
	assert(fixture_engine_hung[I915_ENGINE_BCS0] == 0U);

	/* The peer's queued request was kicked after the reset and completes. */
	assert(engine->active != NULL && engine->active->session == peer);
	fixture_run_engines();
	assert(fixture_completions == 2U && fixture_completion_error == 0);
	target = peer->objects;
	error = fixture_gpu_ops->resource_read(fixture_gpu_private, peer, target, 0U, &value, 4U);
	assert(error == 0);
	assert(value == 0x0badf00dU);

	fixture_gpu_ops->resource_destroy(fixture_gpu_private, peer, target);
	fixture_gpu_ops->close(fixture_gpu_private, peer);
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, session->objects);
	fixture_gpu_ops->close(fixture_gpu_private, session);
	detach();
}

/* A device fault ends everything; the checked reset frees quarantine and reopens the device. */
static void
test_fault_and_reset(void)
{
	struct i915_device *device;
	struct i915_session *session;
	struct i915_gem_object *target;
	uint32_t batch[6];
	uint8_t stream[32U + 2U * 16U + 12U * 4U];
	uint32_t bytes;
	void *opaque;
	int error;

	fixture_reset();
	device = attach();
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	session = opaque;
	target = create_resource(session, 31U, 4096U);
	batch[0] = MI_SEMAPHORE_WAIT | MI_SEMAPHORE_POLL | MI_SEMAPHORE_SAD_GTE_SDD;
	batch[1] = 1U;
	batch[2] = 0U;
	batch[3] = 0U;
	batch[4] = MI_BATCH_BUFFER_END;
	bytes = stream_build(stream, batch, 5U, 2U, 31U, 0U, 0U);
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, session, stream, bytes, 0U, 0U, fixture_completion);
	assert(error == 0);
	fixture_run_engines();

	/* The fault ends the request and closes the device to new opens. */
	fixture_gpu_ops->recovery->fault(fixture_gpu_private, EIO);
	assert(fixture_completions == 1U && fixture_completion_error == EIO);
	assert(device->failed == 1U);
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == ENODEV);
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, session, stream, bytes, 0U, 0U, fixture_completion);
	assert(error == ENODEV);

	/* The session is quarantined by the framework before it closes. */
	error = fixture_gpu_ops->recovery->isolate(fixture_gpu_private, session);
	assert(error == 0);
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, target);
	fixture_gpu_ops->close(fixture_gpu_private, session);
	assert(device->quarantined_objects == 6U);

	/* The checked reset reprograms both engines and frees what quarantine retained. */
	error = fixture_gpu_ops->recovery->reset(fixture_gpu_private);
	assert(error == 0);
	assert(device->failed == 0U);
	assert(device->quarantined_objects == 0U);
	assert(device->quarantined_vms == NULL);
	assert(device->object_count == 6U);
	assert(device->engines[I915_ENGINE_BCS0].reset_count == 1U);
	assert(fixture_engine_hung[I915_ENGINE_BCS0] == 0U);

	/* A fresh session runs a request on the recovered engine. */
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	session = opaque;
	error = fixture_gpu_ops->commands->submit(fixture_gpu_private, session, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 1U, fixture_completion);
	assert(error == 0);
	fixture_run_engines();
	assert(fixture_completions == 2U && fixture_completion_error == 0);
	fixture_gpu_ops->close(fixture_gpu_private, session);
	detach();
}

/* Queues and kicks one BCS0 request that runs the given batch object. */
static struct i915_request *
submit_batch(
	struct i915_device *device,
	struct i915_session *session,
	struct i915_gem_object *batch)
{
	struct i915_engine *engine;
	struct i915_request *request;
	int error;

	engine = &device->engines[I915_ENGINE_BCS0];
	spin_lock(&device->irq_lock);
	error = drv_i915_request_alloc(engine, session, fixture_completion, &request);
	assert(error == 0);
	request->context = &session->contexts[I915_ENGINE_BCS0];
	request->batch = batch;
	request->batch_va = batch->va;
	drv_i915_request_queue(engine, request);
	drv_i915_request_kick(engine);
	spin_unlock(&device->irq_lock);
	return request;
}

/* Attach brings up both engines; the selftest and a batch run through the modeled streamer. */
static void
test_execution(void)
{
	struct i915_device *device;
	struct i915_engine *engine;
	struct gpu_resource_create request;
	struct i915_session *session;
	struct i915_gem_object *target;
	struct i915_gem_object *batch;
	struct i915_request *submitted;
	uint32_t program[8];
	uint32_t value;
	void *opaque;
	int error;

	fixture_reset();
	device = attach();
	engine = &device->engines[I915_ENGINE_BCS0];

	/* Both engines came up with status pages, kernel contexts and primed status buffers. */
	assert(device->engines[I915_ENGINE_RCS0].initialized == 1U);
	assert(engine->initialized == 1U);
	assert(device->mocs_initialized == 1U);
	assert(device->forcewake_held == 1U);
	assert(fixture_load32(fixture_regs + GEN12_GLOBAL_MOCS(2)) == gen12_mocs_table[2].control_value);
	assert(fixture_load32(fixture_regs + GEN12_GLOBAL_MOCS(30)) == gen12_mocs_table[2].control_value);
	assert(fixture_load32(fixture_regs + RING_HWS_PGA(I915_BCS0_BASE)) == engine->hwsp->ggtt_offset);
	assert(fixture_load32(fixture_regs + RING_MODE_GEN7(I915_BCS0_BASE)) == ((GEN11_GFX_DISABLE_LEGACY_MODE << 16) | GEN11_GFX_DISABLE_LEGACY_MODE));
	assert(fixture_load32(fixture_regs + RING_EMR(I915_BCS0_BASE)) == ~(uint32_t)I915_ERROR_INSTRUCTION);
	assert((fixture_load32(fixture_regs + BLIT_CCTL(I915_BCS0_BASE)) & 0x7f7fU) == 0x0606U);
	assert(engine->status[I915_CSB_WRITE_INDEX] == I915_CSB_ENTRIES - 1U);
	assert(device->object_count == 6U);

	/* The selftest runs the kernel context through the modeled engine and sees the interrupt. */
	fixture_run_in_sleep = 1U;
	error = drv_i915_selftest(device);
	assert(error == 0);
	assert(device->selftest_passed == 1U);
	assert(fixture_engine_runs[I915_ENGINE_BCS0] == 1U);
	assert(engine->status[I915_GEM_HWS_SCRATCH] == 0xdeadbeefU);
	assert(engine->status[I915_GEM_HWS_SEQNO] == 1U);
	assert(device->user_interrupts[I915_ENGINE_BCS0] == 1U);
	assert(device->context_switches[I915_ENGINE_BCS0] == 1U);
	assert(engine->csb_promotions == 1U && engine->csb_completions == 1U);
	assert(engine->active == NULL && engine->hw_active == 0U);
	assert(engine->slots[0].state == I915_REQUEST_FREE);

	/* A session brings one context per engine. */
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	session = opaque;
	assert(session->contexts[I915_ENGINE_RCS0].created == 1U);
	assert(session->contexts[I915_ENGINE_BCS0].created == 1U);
	assert(device->object_count == 10U);

	/* A batch stores a value into a resource through the session's address space. */
	memset(&request, 0, sizeof(request));
	request.usage = GPU_RESOURCE_USAGE_STORAGE;
	request.bytes = 4096U;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == 0);
	target = opaque;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == 0);
	batch = opaque;
	program[0] = MI_STORE_DWORD_IMM_GEN4;
	program[1] = (uint32_t)(target->va + 256U);
	program[2] = (uint32_t)((target->va + 256U) >> 32);
	program[3] = 0x12345678U;
	program[4] = MI_BATCH_BUFFER_END;
	error = drv_i915_gem_write(batch, 0U, program, 5U * 4U);
	assert(error == 0);
	submitted = submit_batch(device, session, batch);
	assert(submitted->state == I915_REQUEST_ACTIVE);
	assert(engine->hw_active == 1U);
	assert(session->pending_requests == 1U);

	/* The modeled streamer runs the ring, follows the batch and raises both interrupts. */
	fixture_run_engines();
	assert(fixture_batches == 1U);
	error = drv_i915_gem_read(target, 256U, &value, 4U);
	assert(error == 0);
	assert(value == 0x12345678U);
	assert(fixture_completions == 1U);
	assert(fixture_completion_error == 0);
	assert(submitted->state == I915_REQUEST_FREE);
	assert(engine->active == NULL && engine->hw_active == 0U);
	assert(engine->completed_seqno == 2U);
	assert(session->pending_requests == 0U);
	assert(drv_i915_engine_idle(engine) == 1U);

	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, batch);
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, target);
	fixture_gpu_ops->close(fixture_gpu_private, session);
	assert(device->object_count == 6U);
	detach();
}

/* A hung batch never retires; failing the request and resetting the engine recovers it. */
static void
test_hang_and_reset(void)
{
	struct i915_device *device;
	struct i915_engine *engine;
	struct gpu_resource_create request;
	struct i915_session *session;
	struct i915_gem_object *batch;
	struct i915_request *submitted;
	struct i915_request *retired;
	uint32_t program[8];
	void *opaque;
	int error;

	fixture_reset();
	device = attach();
	engine = &device->engines[I915_ENGINE_BCS0];
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	session = opaque;
	memset(&request, 0, sizeof(request));
	request.usage = GPU_RESOURCE_USAGE_STORAGE;
	request.bytes = 4096U;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == 0);
	batch = opaque;

	/* A semaphore wait on a value that never arrives models a hang. */
	program[0] = MI_SEMAPHORE_WAIT | MI_SEMAPHORE_POLL | MI_SEMAPHORE_SAD_GTE_SDD;
	program[1] = 1U;
	program[2] = (uint32_t)batch->va;
	program[3] = (uint32_t)(batch->va >> 32);
	program[4] = MI_BATCH_BUFFER_END;
	error = drv_i915_gem_write(batch, 0U, program, 5U * 4U);
	assert(error == 0);
	submitted = submit_batch(device, session, batch);
	fixture_run_engines();
	assert(fixture_engine_hung[I915_ENGINE_BCS0] == 1U);
	assert(fixture_completions == 0U);
	assert(submitted->state == I915_REQUEST_ACTIVE);
	assert(engine->active == submitted);

	/* The driver withdraws the request with an error and resets the engine. */
	retired = NULL;
	spin_lock(&device->irq_lock);
	drv_i915_request_fail(engine, session, EIO, &retired);
	spin_unlock(&device->irq_lock);
	assert(retired == submitted);
	drv_i915_request_complete_list(engine, retired);
	assert(fixture_completions == 1U);
	assert(fixture_completion_error == EIO);
	assert(submitted->state == I915_REQUEST_FREE);
	error = drv_i915_engine_reset(engine);
	assert(error == 0);
	assert(engine->reset_count == 1U);
	assert((fixture_reset_domains & GEN11_GRDOM_BLT) != 0U);
	assert((fixture_load32(fixture_regs + RING_RESET_CTL(I915_BCS0_BASE)) & RESET_CTL_REQUEST_RESET) == 0U);
	assert((fixture_load32(fixture_regs + RING_MI_MODE(I915_BCS0_BASE)) & STOP_RING) == 0U);
	assert(engine->status[I915_CSB_WRITE_INDEX] == I915_CSB_ENTRIES - 1U);
	assert(fixture_engine_hung[I915_ENGINE_BCS0] == 0U);

	/* A well-formed batch runs after the reset. */
	program[0] = MI_NOOP;
	program[1] = MI_BATCH_BUFFER_END;
	error = drv_i915_gem_write(batch, 0U, program, 2U * 4U);
	assert(error == 0);
	submitted = submit_batch(device, session, batch);
	fixture_run_engines();
	assert(fixture_completions == 2U);
	assert(fixture_completion_error == 0);
	assert(engine->active == NULL);

	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, batch);
	fixture_gpu_ops->close(fixture_gpu_private, session);
	detach();
}

/* Registers the driver, attaches the fake device and publishes the GPU node. */
static struct i915_device *
attach(void)
{
	struct i915_device *device;
	int error;

	error = drv_i915_pci_driver_register();
	assert(error == 0);
	assert(fixture_driver != NULL);
	assert(fixture_driver->id_count >= 18U);
	assert(fixture_driver->ids[5].vendor == 0x8086U);
	assert(fixture_driver->ids[5].device == 0x46a8U);
	error = fixture_driver->attach(fixture_pci_device, &fixture_driver->ids[5]);
	assert(error == 0);
	device = fixture_driver_data;
	assert(device != NULL);
	assert(fixture_service != NULL);
	error = fixture_service->publish(fixture_pci_device, fixture_service_argument);
	assert(error == 0);
	assert(fixture_gpu_ops != NULL);
	assert(fixture_gpu_private == device);
	return device;
}

/* Withdraws the node and detaches; every lease must be gone afterwards. */
static void
detach(void)
{
	int error;

	error = fixture_service->unpublish(fixture_pci_device, fixture_service_argument);
	assert(error == 0);
	assert(fixture_gpu_ops == NULL);
	error = fixture_driver->detach(fixture_pci_device, 0U);
	assert(error == 0);
	assert(fixture_driver_data == NULL);
	assert(fixture_maps == 0U);
	assert(fixture_irq_allocations == 0U);
	assert(fixture_irq_established == 0U);
	assert(fixture_enable_saved == 0U);
	assert(fixture_bus_master == 0U);
	assert(fixture_pool_live == 0U);
}

/* Attach programs the device in order and detach reverses every step. */
static void
test_lifecycle(void)
{
	struct i915_device *device;
	struct gpu_info info;
	void *session;
	int error;

	fixture_reset();
	device = attach();
	assert(device->product == 0x46a8U);
	assert(fixture_memory_enabled == 1U);
	assert(fixture_bus_master == 1U);
	assert(device->ggtt.entries == FIXTURE_REGS_BYTES / 8U);
	assert(device->irq_enabled == 1U);
	assert(fixture_load32(fixture_regs + GEN11_GFX_MSTR_IRQ) == GEN11_MASTER_IRQ);
	assert(fixture_load32(fixture_regs + FORCEWAKE_GT_GEN9) == ((FORCEWAKE_KERNEL << 16) | FORCEWAKE_KERNEL));
	assert(device->forcewake_count[0] == 1U && device->forcewake_count[1] == 1U);

	/* Info names the backend and its storage capabilities. */
	error = fixture_gpu_ops->open(fixture_gpu_private, &session);
	assert(error == 0);
	memset(&info, 0, sizeof(info));
	error = fixture_gpu_ops->get_info(fixture_gpu_private, session, &info);
	assert(error == 0);
	assert(info.capabilities == (GPU_CAP_RESOURCE | GPU_CAP_TRANSFER | GPU_CAP_COMMAND | GPU_CAP_NOTIFICATION | GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY | GPU_CAP_CAPSET));
	assert(info.max_resource_bytes == I915_MAX_RESOURCE_BYTES);
	assert(strcmp(info.driver_name, "i915") == 0);
	assert(((struct i915_session *)session)->identifier == 1U);
	assert(((struct i915_session *)session)->vm->created == 1U);
	fixture_gpu_ops->close(fixture_gpu_private, session);

	/* A second session gets the next identifier. */
	error = fixture_gpu_ops->open(fixture_gpu_private, &session);
	assert(error == 0);
	assert(((struct i915_session *)session)->identifier == 2U);
	fixture_gpu_ops->close(fixture_gpu_private, session);

	detach();
}

/* Objects are zeroed, bound into the session space, copied both ways and released. */
static void
test_resources(void)
{
	struct i915_device *device;
	struct gpu_resource_create request;
	struct i915_session *session;
	struct i915_gem_object *object;
	struct i915_gem_object *second;
	void *opaque;
	uint8_t pattern[4096];
	uint8_t readback[4096];
	uint64_t leaf;
	unsigned index;
	int error;

	fixture_reset();
	device = attach();
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	session = opaque;

	/* A 6000-byte request becomes two pages placed at the first 2 MiB slot. */
	memset(&request, 0, sizeof(request));
	request.usage = GPU_RESOURCE_USAGE_STORAGE;
	request.bytes = 6000U;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == 0);
	object = opaque;
	assert(object->bytes == 2U * I915_PAGE_BYTES);
	assert(object->pages == 2U);
	assert(object->slot == 1U);
	assert(object->va == I915_PPGTT_VA_START);
	assert(object->vm == session->vm);
	assert(device->object_count == 11U);
	for (index = 0U; index < 2U * I915_PAGE_BYTES; index++)
		assert(((uint8_t *)object->address)[index] == 0U);

	/* The private space resolves both pages and nothing beyond them. */
	leaf = drv_i915_ppgtt_lookup(session->vm, object->va);
	assert(leaf == ((uint64_t)object->run.paddr | GEN8_PAGE_PRESENT | GEN8_PAGE_RW));
	leaf = drv_i915_ppgtt_lookup(session->vm, object->va + I915_PAGE_BYTES);
	assert(leaf == (((uint64_t)object->run.paddr + I915_PAGE_BYTES) | GEN8_PAGE_PRESENT | GEN8_PAGE_RW));
	leaf = drv_i915_ppgtt_lookup(session->vm, object->va + 2U * I915_PAGE_BYTES);
	assert(leaf == session->vm->scratch_entry[0]);

	/* Write then read returns the same bytes at the same offset. */
	for (index = 0U; index < sizeof(pattern); index++)
		pattern[index] = (uint8_t)(index * 7U + 3U);
	error = fixture_gpu_ops->resource_write(fixture_gpu_private, session, object, 4096U - 100U, pattern, sizeof(pattern));
	assert(error == 0);
	memset(readback, 0, sizeof(readback));
	error = fixture_gpu_ops->resource_read(fixture_gpu_private, session, object, 4096U - 100U, readback, sizeof(readback));
	assert(error == 0);
	assert(memcmp(pattern, readback, sizeof(pattern)) == 0);
	assert(((uint8_t *)object->address)[4096U - 100U] == pattern[0]);

	/* Ranges past the end are refused without copying. */
	error = fixture_gpu_ops->resource_read(fixture_gpu_private, session, object, 2U * I915_PAGE_BYTES - 10U, readback, 11U);
	assert(error == EINVAL);
	error = fixture_gpu_ops->resource_write(fixture_gpu_private, session, object, 2U * I915_PAGE_BYTES, pattern, 1U);
	assert(error == EINVAL);
	error = fixture_gpu_ops->resource_read(fixture_gpu_private, session, object, 2U * I915_PAGE_BYTES, readback, 0U);
	assert(error == 0);

	/* A second object lands in the next 2 MiB slot with the next slot number. */
	request.bytes = I915_MAX_RESOURCE_BYTES;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == 0);
	second = opaque;
	assert(second->slot == 2U);
	assert(second->va == I915_PPGTT_VA_START + I915_PPGTT_VA_ALIGN);
	assert(second->pages == I915_MAX_RESOURCE_BYTES / I915_PAGE_BYTES);
	leaf = drv_i915_ppgtt_lookup(session->vm, second->va + I915_MAX_RESOURCE_BYTES - I915_PAGE_BYTES);
	assert(leaf == (((uint64_t)second->run.paddr + I915_MAX_RESOURCE_BYTES - I915_PAGE_BYTES) | GEN8_PAGE_PRESENT | GEN8_PAGE_RW));
	assert(session->resources == 2U);

	/* Destroying unmaps the object and frees its backing. */
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, object);
	assert(device->object_count == 11U);
	leaf = drv_i915_ppgtt_lookup(session->vm, I915_PPGTT_VA_START);
	assert(leaf == session->vm->scratch_entry[0]);
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, second);
	assert(device->object_count == 10U);
	assert(session->resources == 0U);

	fixture_gpu_ops->close(fixture_gpu_private, session);
	detach();
}

/* Invalid requests and exhaustion unwind without leaving state behind. */
static void
test_failures(void)
{
	struct gpu_resource_create request;
	struct i915_session *session;
	struct i915_gem_object *object;
	void *opaque;
	unsigned live;
	int error;

	fixture_reset();
	(void)attach();
	error = fixture_gpu_ops->open(fixture_gpu_private, &opaque);
	assert(error == 0);
	session = opaque;
	live = fixture_pool_live;

	/* Zero, oversized and non-storage requests are refused before allocation. */
	memset(&request, 0, sizeof(request));
	request.usage = GPU_RESOURCE_USAGE_STORAGE;
	request.bytes = 0U;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == EINVAL);
	request.bytes = I915_MAX_RESOURCE_BYTES + 1U;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == EINVAL);
	request.bytes = 4096U;
	request.usage = 0U;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == EINVAL);
	assert(fixture_pool_live == live);
	assert(session->next_slot == 1U);

	/* Pool exhaustion reports ENOMEM and leaves no partial object. */
	request.usage = GPU_RESOURCE_USAGE_STORAGE;
	request.bytes = I915_MAX_RESOURCE_BYTES;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == 0);
	object = opaque;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == ENOMEM);
	assert(session->resources == 1U);
	assert(session->next_slot == 2U);
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, object);

	/* A quarantined session keeps its object and its tables for the checked reset. */
	request.bytes = 4096U;
	error = fixture_gpu_ops->resource_create(fixture_gpu_private, session, &request, &opaque);
	assert(error == 0);
	object = opaque;
	session->quarantined = 1U;
	fixture_gpu_ops->resource_destroy(fixture_gpu_private, session, object);
	assert(object->quarantined == 1U);
	assert(((struct i915_device *)fixture_gpu_private)->quarantined_objects == 1U);
	assert(((struct i915_device *)fixture_gpu_private)->object_count == 11U);
	fixture_gpu_ops->close(fixture_gpu_private, session);
	assert(fixture_pool_live > live);

	/* Detach frees what quarantine retained once the device is reset. */
	detach();
}
