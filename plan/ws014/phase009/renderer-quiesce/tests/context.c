/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Tests actual host quiescence code with independent native idle and callback owners. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "vkr_context.c"

#ifdef NDEBUG
#error "Quiescence assertions must remain enabled"
#endif

static struct vkr_context context;
static struct vkr_instance instance;
static struct vkr_physical_device physical[2];
static struct vkr_physical_device *physical_pointers[2];
static struct vkr_device devices[3];
static struct vkr_queue queue;
static struct vkr_queue_sync pending;
static mtx_t fixture_mutex;
static cnd_t fixture_condition;
static unsigned native_calls;
static unsigned native_mask;
static unsigned native_allowed;
static VkResult native_result;
static unsigned callbacks;
static uint64_t last_fence;
static unsigned borrow_pending;
static unsigned fini_entered;
static unsigned fini_done;

static void prepare(void);
static void finish(void);
static void allow_native(void);
static void await_native(unsigned count);
static void join_worker(void);
static void failure_case(VkResult result);
static int finish_thread(void *argument);
static VkResult native_idle(VkDevice device);
static void retire(uint32_t context_id, uint32_t ring, uint64_t fence);

/* Diagnostics are not used as a correctness oracle. */
void
vkr_log(
	const char *format,
	...)
{
	(void)format;
}

/* An unexpected decoder allocation is a fixture failure, not a simulated successful decode. */
bool
vkr_cs_decoder_alloc_temp_internal(
	struct vkr_cs_decoder *decoder,
	size_t bytes)
{
	(void)decoder;
	(void)bytes;
	assert(0);
	return false;
}

/* A stopped decoder must return before touching its temporary decoding state. */
void
vkr_cs_decoder_reset(
	struct vkr_cs_decoder *decoder)
{
	(void)decoder;
	assert(0);
}

/* CPU0 tests cannot delegate completion proof to a GPU queue marker. */
bool
vkr_queue_sync_submit(
	struct vkr_queue *unused_queue,
	uint32_t flags,
	uint32_t ring,
	uint64_t fence)
{
	(void)unused_queue;
	(void)flags;
	(void)ring;
	(void)fence;
	assert(0);
	return false;
}

/* Drives real asynchronous host stop and CPU0 ordering through independently controlled native work. */
int
main(
	void)
{
	static const uint8_t command[16] = {0x51, 0x53, 0x42, 0x5a, 1};
	struct vkr_ring ring;
	struct timespec deadline;
	thrd_t closer;
	int error;

	/* Only the complete reserved command shape selects the private path. */
	assert(vkr_context_is_quiesce(command, sizeof(command)));
	assert(!vkr_context_is_quiesce(command, sizeof(command) - 1U));
	assert(!vkr_context_is_quiesce(command + 1U, sizeof(command) - 1U));

	/* Raw native work has no GPU_JOB or queue marker, but every real native device is still waited. */
	prepare();
	assert(vkr_context_submit_cmd(&context, command, sizeof(command)));
	assert(!vkr_context_submit_cmd(&context, command, sizeof(command) - 1U));
	assert(!vkr_context_submit_fence(&context, 1U, 1U, 20U));
	await_native(1U);
	assert(vkr_context_submit_fence(&context, 1U, 0U, UINT32_MAX));
	assert(vkr_context_submit_fence(&context, 1U, 0U, 0U));
	assert(callbacks == 0U);
	allow_native();
	join_worker();
	assert(native_calls == 3U && callbacks == 1U && last_fence == 0U);

	/* A later CPU0 watermark may advance only after the verified stop callback has finished. */
	assert(vkr_context_submit_fence(&context, 1U, 0U, 1U));
	assert(callbacks == 2U && last_fence == 1U);
	finish();

	/* Native idle cannot retire callback storage still borrowed by a delayed host queue worker. */
	prepare();
	borrow_pending = 1U;
	list_addtail(&pending.head, &queue.sync_thread.syncs);
	assert(vkr_context_quiesce_begin(&context));
	assert(vkr_context_submit_fence(&context, 1U, 0U, 41U));
	allow_native();
	await_native(3U);
	mtx_lock(&fixture_mutex);
	assert(callbacks == 0U);
	mtx_unlock(&fixture_mutex);
	mtx_lock(&queue.sync_thread.mutex);
	list_del(&pending.head);
	borrow_pending = 0U;
	cnd_broadcast(&queue.sync_thread.cond);
	mtx_unlock(&queue.sync_thread.mutex);
	join_worker();
	assert(callbacks == 1U && last_fence == 41U);
	finish();

	/* Every native wait error, including device loss, withholds the stop acknowledgement. */
	failure_case(VK_ERROR_OUT_OF_HOST_MEMORY);
	failure_case(VK_ERROR_OUT_OF_DEVICE_MEMORY);
	failure_case(VK_ERROR_DEVICE_LOST);

	/* Unsupported decoder rings never turn a later CPU0 marker into a false stop acknowledgement. */
	prepare();
	memset(&ring, 0, sizeof(ring));
	list_addtail(&ring.head, &context.rings);
	assert(vkr_context_quiesce_begin(&context));
	assert(!context.quiesce.started && context.quiesce.failed);
	assert(vkr_context_submit_fence(&context, 1U, 0U, 31U));
	assert(vkr_context_submit_fence(&context, 1U, 0U, 32U));
	assert(callbacks == 0U && native_calls == 0U);
	list_del(&ring.head);
	finish();

	/* Actual teardown join cannot return while the native idle worker still owns context storage. */
	prepare();
	assert(vkr_context_quiesce_begin(&context));
	await_native(1U);
	error = thrd_create(&closer, finish_thread, NULL);
	assert(error == thrd_success);
	mtx_lock(&fixture_mutex);
	while (fini_entered == 0U)
		cnd_wait(&fixture_condition, &fixture_mutex);
	timespec_get(&deadline, TIME_UTC);
	deadline.tv_nsec += 50000000;
	if (deadline.tv_nsec >= 1000000000) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000;
	}
	while (fini_done == 0U) {
		error = cnd_timedwait(&fixture_condition, &fixture_mutex, &deadline);
		if (error == thrd_busy)
			break;
	}
	assert(fini_done == 0U);
	mtx_unlock(&fixture_mutex);
	allow_native();
	thrd_join(closer, NULL);
	assert(fini_done == 1U);
	context.quiesce.started = false;
	mtx_destroy(&context.ring_mutex);
	cnd_destroy(&queue.sync_thread.cond);
	mtx_destroy(&queue.sync_thread.mutex);
	cnd_destroy(&fixture_condition);
	mtx_destroy(&fixture_mutex);

	puts("QUIESCE raw native jobs/all devices/CPU0 hold+wrap/OOM refusal/ring refusal/callback borrow/teardown join PASS");
	return 0;
}

/* Builds native device membership independently from the guest job and transport ledgers. */
static void
prepare(
	void)
{
	unsigned index;

	memset(&context, 0, sizeof(context));
	memset(&instance, 0, sizeof(instance));
	memset(physical, 0, sizeof(physical));
	memset(devices, 0, sizeof(devices));
	memset(&queue, 0, sizeof(queue));
	context.ctx_id = 17U;
	context.retire_fence = retire;
	context.instance = &instance;
	context.sync_queues[1] = &queue;
	instance.physical_device_count = 2U;
	instance.physical_devices = physical_pointers;
	assert(mtx_init(&context.quiesce.mutex, mtx_plain) == thrd_success);
	assert(mtx_init(&context.ring_mutex, mtx_plain) == thrd_success);
	assert(mtx_init(&fixture_mutex, mtx_plain) == thrd_success);
	assert(cnd_init(&fixture_condition) == thrd_success);
	assert(mtx_init(&queue.sync_thread.mutex, mtx_plain) == thrd_success);
	assert(cnd_init(&queue.sync_thread.cond) == thrd_success);
	list_inithead(&queue.sync_thread.syncs);
	list_inithead(&context.rings);
	for (index = 0U; index < 2U; index++) {
		physical_pointers[index] = &physical[index];
		list_inithead(&physical[index].devices);
	}
	for (index = 0U; index < 3U; index++) {
		devices[index].base.handle.device = (VkDevice)(uintptr_t)(0x1000U + index);
		devices[index].proc_table.DeviceWaitIdle = native_idle;
		list_inithead(&devices[index].queues);
		list_addtail(&devices[index].base.track_head, &physical[index % 2U].devices);
	}
	list_addtail(&queue.base.track_head, &devices[0].queues);
	native_calls = 0U;
	native_mask = 0U;
	native_allowed = 0U;
	native_result = VK_SUCCESS;
	callbacks = 0U;
	last_fence = 0U;
	borrow_pending = 0U;
	fini_entered = 0U;
	fini_done = 0U;
}

/* Releases only fixture state after the production native-access barrier is complete. */
static void
finish(
	void)
{
	vkr_context_quiesce_fini(&context);
	mtx_destroy(&context.ring_mutex);
	cnd_destroy(&queue.sync_thread.cond);
	mtx_destroy(&queue.sync_thread.mutex);
	cnd_destroy(&fixture_condition);
	mtx_destroy(&fixture_mutex);
}

/* Ends one independently controlled native wait without issuing any queue marker. */
static void
allow_native(
	void)
{
	mtx_lock(&fixture_mutex);
	native_allowed = 1U;
	cnd_broadcast(&fixture_condition);
	mtx_unlock(&fixture_mutex);
}

/* Waits until the production worker has entered the requested number of actual native calls. */
static void
await_native(
	unsigned count)
{
	mtx_lock(&fixture_mutex);
	while (native_calls < count)
		cnd_wait(&fixture_condition, &fixture_mutex);
	mtx_unlock(&fixture_mutex);
}

/* Joins once so later public CPU0 calls can be checked before final fixture cleanup. */
static void
join_worker(
	void)
{
	thrd_join(context.quiesce.thread, NULL);
	context.quiesce.started = false;
}

/* Tests native error classes without converting a wait failure into successful idle. */
static void
failure_case(
	VkResult result)
{
	prepare();
	native_result = result;
	assert(vkr_context_quiesce_begin(&context));
	assert(vkr_context_submit_fence(&context, 1U, 0U, 21U));
	allow_native();
	join_worker();
	assert(vkr_context_submit_fence(&context, 1U, 0U, 22U));
	assert(callbacks == 0U && native_calls == 1U);
	assert(context.quiesce.failed && !context.quiesce.done);
	finish();
}

/* Uses the exact teardown helper that production context_destroy calls before native object destruction. */
static int
finish_thread(
	void *argument)
{
	(void)argument;
	mtx_lock(&fixture_mutex);
	fini_entered = 1U;
	cnd_broadcast(&fixture_condition);
	mtx_unlock(&fixture_mutex);
	vkr_context_quiesce_fini(&context);
	mtx_lock(&fixture_mutex);
	fini_done = 1U;
	cnd_broadcast(&fixture_condition);
	mtx_unlock(&fixture_mutex);
	return 0;
}

/* Emulates independently pending raw native work using real host thread conditions. */
static VkResult
native_idle(
	VkDevice device)
{
	assert((uintptr_t)device >= 0x1000U && (uintptr_t)device <= 0x1002U);
	mtx_lock(&fixture_mutex);
	assert((native_mask & (1U << ((uintptr_t)device - 0x1000U))) == 0U);
	native_mask |= 1U << ((uintptr_t)device - 0x1000U);
	native_calls++;
	cnd_broadcast(&fixture_condition);
	while (native_allowed == 0U)
		cnd_wait(&fixture_condition, &fixture_mutex);
	mtx_unlock(&fixture_mutex);
	return native_result;
}

/* Rejects any completion watermark before native work and borrowed callbacks have both ended. */
static void
retire(
	uint32_t context_id,
	uint32_t ring,
	uint64_t fence)
{
	assert(context_id == 17U && ring == 0U);
	mtx_lock(&fixture_mutex);
	assert(native_calls == 3U && native_mask == 7U && native_allowed != 0U && borrow_pending == 0U);
	assert(native_result == VK_SUCCESS);
	callbacks++;
	last_fence = fence;
	mtx_unlock(&fixture_mutex);
}
