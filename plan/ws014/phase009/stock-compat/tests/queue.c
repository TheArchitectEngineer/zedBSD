/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Observes fixed stock renderer queue dispatch and completion information loss.
 * Only native Vulkan operations are scripted; production retirement is linked unchanged.
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "vkr_queue.c"

#ifdef NDEBUG
#error "The strict completion fixture requires active assertions"
#endif

/* One isolated queue and its native collaborators live throughout each scenario. */
static struct vkr_context fixture_context;
static struct vkr_device fixture_device;
static struct vkr_physical_device fixture_physical;
static struct vkr_queue fixture_queue;
static struct vkr_fence fixture_fence;

/* Scripted native outcomes and observed callback identities form the independent oracle. */
static VkResult fixture_submit_result;
static VkResult fixture_wait_results[3];
static unsigned fixture_wait_count;
static unsigned fixture_wait_cursor;
static unsigned fixture_callbacks;
static uint64_t fixture_last_id;
static unsigned fixture_native_submits;

/* The ordering case supplies a later already-ready fence behind a pending sparse fence. */
static unsigned fixture_ordered;

static VkResult fixture_create(VkDevice device, const VkFenceCreateInfo *info, const VkAllocationCallbacks *allocator, VkFence *result);
static void fixture_init(void);
static void fixture_cleanup(void);
static void fixture_dispatch(unsigned sparse, VkResult status, unsigned has_fence);
static void fixture_case(unsigned sparse, VkResult submit_result, VkResult wait_result);
static void fixture_retire(uint32_t context, uint32_t ring, uint64_t id);
static VkResult fixture_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence);
static VkResult fixture_sparse(VkQueue queue, uint32_t count, const VkBindSparseInfo *binds, VkFence fence);
static VkResult fixture_wait(VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all, uint64_t timeout);

/*
 * Accepts production diagnostics without turning their text into a success oracle.
 */
void
vkr_log(
	const char *format,
	...)
{
	(void)format;
}

/*
 * Checks actual submission fences, failed retirement and teardown ordering.
 */
int
main(
	void)
{
	fixture_case(0U, VK_SUCCESS, VK_SUCCESS);
	fixture_case(1U, VK_SUCCESS, VK_SUCCESS);
	fixture_case(0U, VK_SUCCESS, VK_ERROR_DEVICE_LOST);
	fixture_case(0U, VK_SUCCESS, VK_ERROR_OUT_OF_HOST_MEMORY);
	puts("STOCK QUEUE separate empty-submit fence and status-free SUCCESS/DEVICE_LOST/OOM retirement OBSERVED");
	return 0;
}

/* Initializes only the real renderer state needed by queue completion. */
static void
fixture_init(
	void)
{
	int error;

	/* Native object identities deliberately differ from guest wrapper addresses. */
	memset(&fixture_context, 0, sizeof(fixture_context));
	memset(&fixture_device, 0, sizeof(fixture_device));
	memset(&fixture_queue, 0, sizeof(fixture_queue));
	memset(&fixture_fence, 0, sizeof(fixture_fence));
	fixture_context.ctx_id = 17U;
	fixture_context.retire_fence = fixture_retire;
	memset(&fixture_physical, 0, sizeof(fixture_physical));
	fixture_device.physical_device = &fixture_physical;
	fixture_device.proc_table.CreateFence = fixture_create;
	fixture_device.base.handle.device = (VkDevice)(uintptr_t)0x1020U;
	fixture_device.proc_table.QueueSubmit = fixture_submit;
	fixture_device.proc_table.QueueBindSparse = fixture_sparse;
	fixture_device.proc_table.WaitForFences = fixture_wait;
	fixture_queue.base.type = VK_OBJECT_TYPE_QUEUE;
	fixture_queue.base.id = 21U;
	fixture_queue.base.handle.queue = (VkQueue)(uintptr_t)0x2030U;
	fixture_queue.context = &fixture_context;
	fixture_queue.device = &fixture_device;
	fixture_fence.base.type = VK_OBJECT_TYPE_FENCE;
	fixture_fence.base.id = 31U;
	fixture_fence.base.handle.fence = (VkFence)(uintptr_t)0xfaceU;

	/* Real mutexes and lists execute the production synchronization paths. */
	error = mtx_init(&fixture_device.free_sync_mutex, mtx_plain);
	assert(error == thrd_success);
	error = mtx_init(&fixture_queue.vk_mutex, mtx_plain);
	assert(error == thrd_success);
	error = mtx_init(&fixture_queue.sync_thread.mutex, mtx_plain);
	assert(error == thrd_success);
	error = cnd_init(&fixture_queue.sync_thread.cond);
	assert(error == thrd_success);
	list_inithead(&fixture_device.free_syncs);
	list_inithead(&fixture_queue.sync_thread.syncs);
	fixture_wait_count = 1U;
	fixture_wait_cursor = 0U;
	fixture_callbacks = 0U;
	fixture_last_id = 0U;
	fixture_native_submits = 0U;
	fixture_ordered = 0U;
	fixture_wait_results[0] = VK_SUCCESS;
}

/* Releases independent fixture storage after all production fence accesses have ended. */
static void
fixture_cleanup(
	void)
{
	struct vkr_queue_sync *sync;
	bool empty;

	/* Every accepted record must have retired into the bookkeeping-only free list. */
	assert(list_is_empty(&fixture_queue.sync_thread.syncs));
	while (true) {
		/* The production pool owns only bookkeeping after every borrowed native access ended. */
		empty = list_is_empty(&fixture_device.free_syncs);
		if (empty)
			break;

		/* Release one fixture-owned record without touching its former native fence. */
		sync = LIST_ENTRY(struct vkr_queue_sync, fixture_device.free_syncs.next, head);
		assert(sync->fence == (VkFence)(uintptr_t)0xdeadU);
		list_del(&sync->head);
		free(sync);
	}

	/* No production owner retains these native synchronization primitives. */
	cnd_destroy(&fixture_queue.sync_thread.cond);
	mtx_destroy(&fixture_queue.sync_thread.mutex);
	mtx_destroy(&fixture_queue.vk_mutex);
	mtx_destroy(&fixture_device.free_sync_mutex);
}

/* Sends a real renderer dispatch through handle replacement and its native result handoff. */
static void
fixture_dispatch(
	unsigned sparse,
	VkResult status,
	unsigned has_fence)
{
	struct vn_command_vkQueueSubmit submit;
	struct vn_command_vkQueueBindSparse bind;
	VkFence fence;

	/* The production replacement functions resolve the supplied wrapper to its native handle. */
	fixture_submit_result = status;
	fence = VK_NULL_HANDLE;
	if (has_fence != 0U)
		fence = (VkFence)(uintptr_t)&fixture_fence;

	/* Exercise each actual dispatcher, including rejected submissions. */
	if (sparse != 0U) {
		memset(&bind, 0, sizeof(bind));
		bind.queue = (VkQueue)(uintptr_t)&fixture_queue;
		bind.fence = fence;
		vkr_dispatch_vkQueueBindSparse(NULL, &bind);
		assert(bind.ret == status);
	} else {
		memset(&submit, 0, sizeof(submit));
		submit.queue = (VkQueue)(uintptr_t)&fixture_queue;
		submit.fence = fence;
		vkr_dispatch_vkQueueSubmit(NULL, &submit);
		assert(submit.ret == status);
	}
}

/* Checks one independent successful or failed native result without accepting a retirement shortcut. */
static void
fixture_case(
	unsigned sparse,
	VkResult submit_result,
	VkResult wait_result)
{
	bool accepted;

	/* Actual application and marker fences have independently chosen different native IDs. */
	fixture_init();
	fixture_dispatch(sparse, submit_result, 1U);
	fixture_wait_results[0] = wait_result;
	accepted = vkr_queue_sync_submit(&fixture_queue, 1U, 3U, 41U);
	assert(accepted);
	vkr_queue_thread(&fixture_queue);
	assert(fixture_wait_cursor == 1U);
	assert(fixture_callbacks == 1U);
	assert(fixture_last_id == 41U);
	assert(fixture_native_submits == 2U);
	fixture_cleanup();
}

/* Records the same callback QEMU converts to a successful fenced response. */
static void
fixture_retire(
	uint32_t context,
	uint32_t ring,
	uint64_t id)
{
	assert(context == 17U);
	assert(ring == 3U);
	/* Queue-local retirement must preserve the successful prefix across sparse and ordinary work. */
	if (fixture_ordered != 0U) {
		assert(fixture_wait_cursor == fixture_callbacks + 2U);
		assert(id == 91U + fixture_callbacks);
	} else {
		assert(fixture_wait_cursor == fixture_wait_count);
	}

	fixture_callbacks++;
	fixture_last_id = id;
}

/* Supplies a native submit result while checking independently chosen handle values. */
static VkResult
fixture_submit(
	VkQueue queue,
	uint32_t count,
	const VkSubmitInfo *submits,
	VkFence fence)
{
	assert(queue == (VkQueue)(uintptr_t)0x2030U);
	assert(count == 0U);
	assert(submits == NULL);
	if (fixture_ordered != 0U)
		assert(fence == (VkFence)(uintptr_t)0xfaceU || fence == (VkFence)(uintptr_t)0xf00dU);
	else
		assert(fence == (VkFence)(uintptr_t)0xdeadU || fence == (VkFence)(uintptr_t)0xfaceU);
	fixture_native_submits++;

	/* A scripted rejection must remain distinct from successful native acceptance. */
	if (fixture_submit_result != VK_SUCCESS)
		return fixture_submit_result;

	/* Succeeded: the real dispatch may retain this supplied native fence. */
	return VK_SUCCESS;
}

/* Supplies the actual sparse submission result without inserting any ordinary queue work. */
static VkResult
fixture_sparse(
	VkQueue queue,
	uint32_t count,
	const VkBindSparseInfo *binds,
	VkFence fence)
{
	VkResult status;

	/* The sparse native call uses the same independent identity and acceptance oracle. */
	assert(binds == NULL);
	status = fixture_submit(queue, count, NULL, fence);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the actual sparse fence belongs to this accepted operation. */
	return VK_SUCCESS;
}

/* Returns scripted native completion only after checking the exact submitted fence. */
static VkResult
fixture_wait(
	VkDevice device,
	uint32_t count,
	const VkFence *fences,
	VkBool32 all,
	uint64_t timeout)
{
	VkResult status;

	/* The marker must borrow the actual submission fence, never its own substitute. */
	assert(device == (VkDevice)(uintptr_t)0x1020U);
	assert(count == 1U);
	if (fixture_ordered != 0U && fixture_wait_cursor == 2U) {
		assert(fences[0] == (VkFence)(uintptr_t)0xf00dU);
		assert(fixture_callbacks == 1U);
		assert(fixture_last_id == 91U);
	} else {
		assert(fences[0] == (VkFence)(uintptr_t)0xdeadU);
		assert(fixture_callbacks == 0U);
	}
	assert(all == VK_TRUE);
	assert(timeout == UINT64_C(3000000000));
	assert(fixture_wait_cursor < fixture_wait_count);
	status = fixture_wait_results[fixture_wait_cursor++];

	/* Finish this bounded direct worker invocation after its last scripted observation. */
	if (fixture_wait_cursor == fixture_wait_count)
		fixture_queue.sync_thread.join = true;

	/* Pending and failed observations must retain their exact independent scripted outcome. */
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the production worker may now retire the actual submitted fence. */
	return VK_SUCCESS;
}

/* Gives the stock marker a fence identity that differs from the actual native submission. */
static VkResult
fixture_create(
	VkDevice device,
	const VkFenceCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkFence *result)
{
	assert(device == (VkDevice)(uintptr_t)0x1020U);
	assert(info->sType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
	assert(allocator == NULL);
	*result = (VkFence)(uintptr_t)0xdeadU;
	return VK_SUCCESS;
}
