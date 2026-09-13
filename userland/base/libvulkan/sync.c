/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Implements native synchronization with completed WSI payloads and unlocked waits.
 */

#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <poll.h>
#include <uapi/gpu.h>
#include "sync-internal.h"

#define VULKAN_SYNC_POLL_NS UINT64_C(1000000)

static VkResult sync_create(struct VkDevice_T *device, enum vulkan_object_kind kind, uint32_t opcode, uint32_t destroy_opcode, uint32_t structure, VkFlags flags, const VkAllocationCallbacks *allocator, struct vulkan_sync **result);
static void sync_destroy(struct VkDevice_T *device, struct vulkan_sync *sync, uint32_t opcode, const VkAllocationCallbacks *allocator);
static VkResult sync_event_operation(VkDevice device, VkEvent event, uint32_t opcode);
static VkResult sync_notifications_reap(struct vulkan_context *context, unsigned *retired);
static VkResult sync_notification_wait(struct vulkan_context *context, uint64_t sequence, uint64_t timeout_ns);
static VkResult sync_notification_poll(struct vulkan_context *context, uint64_t timeout_ns, struct pollfd *descriptors, uint32_t count);
static void sync_release_storage(struct vulkan_object *object);
static VkResult sync_fences_wait(struct VkDevice_T *device, uint32_t count, const VkFence *fences, VkBool32 all, uint64_t timeout_ns, struct vulkan_notification **pins, struct pollfd *descriptors);
static void sync_notifications_unpin(struct vulkan_context *context, uint32_t count, struct vulkan_notification **pins);

/*
 * Creates a fence with the application's requested initial native state.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(
	VkDevice device,
	const VkFenceCreateInfo *pCreateInfo,
	const VkAllocationCallbacks *pAllocator,
	VkFence *pFence)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;
	uint64_t handle;
	VkResult status;

	/* Allocate one fence using this call's effective allocation policy. */
	owner = vulkan_device(device);
	status = sync_create(owner, VULKAN_OBJECT_FENCE, VULKAN_OPCODE_vkCreateFence, VULKAN_OPCODE_vkDestroyFence, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, pCreateInfo->flags, pAllocator, &sync);
	if (status != VK_SUCCESS)
		return status;

	/* Complete exportable kernel payload and worker ownership before exposing this fence. */
	status = vulkan_external_fence_create(owner, sync, pCreateInfo);
	if (status != VK_SUCCESS) {
		sync_destroy(owner, sync, VULKAN_OPCODE_vkDestroyFence, pAllocator);
		return status;
	}

	/* Publish the standard ABI handle only after native creation succeeds. */
	handle = vulkan_nondispatchable_handle(&sync->object);
	*pFence = (VkFence)(uintptr_t)handle;

	/* Succeeded: the fence retains its requested signaled or unsignaled state. */
	return VK_SUCCESS;
}

/*
 * Destroys an externally synchronized fence and its saved allocation.
 */
VKAPI_ATTR void VKAPI_CALL
vkDestroyFence(
	VkDevice device,
	VkFence fence,
	const VkAllocationCallbacks *pAllocator)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;

	/* The destruction call may supply compatible callbacks with different userdata. */
	owner = vulkan_device(device);
	sync = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	sync_destroy(owner, sync, VULKAN_OPCODE_vkDestroyFence, pAllocator);

	/* Succeeded: null fences are ignored and ordinary fence ownership is consumed. */
	return;
}

/*
 * Resets native fences and completed WSI payloads in one device transaction.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkResetFences(
	VkDevice device,
	uint32_t fenceCount,
	const VkFence *pFences)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;
	struct vulkan_writer writer;
	struct vulkan_reader reader;
	VkResult status;
	uint32_t index;

	/* Encode every native fence even if its active payload came from acquisition. */
	owner = vulkan_device(device);

	/* Worker completion cannot be joined while retaining the device mutex it needs. */
	for (index = 0; index < fenceCount; index++) {
		sync = vulkan_sync_object((uint64_t)(uintptr_t)pFences[index]);
		vulkan_external_fence_quiesce(sync);
	}

	vulkan_writer_init_for_object(&writer, &owner->object);
	vulkan_command_begin(&writer, VULKAN_OPCODE_vkResetFences);
	vulkan_write_u64(&writer, owner->object.wire_id);
	vulkan_write_u32(&writer, fenceCount);
	vulkan_write_u64(&writer, fenceCount);
	for (index = 0; index < fenceCount; index++) {
		sync = vulkan_sync_object((uint64_t)(uintptr_t)pFences[index]);
		vulkan_write_u64(&writer, sync->object.wire_id);
	}

	/* Commit software reset only after the host accepted the native reset. */
	pthread_mutex_lock(&owner->mutex);

	status = vulkan_command_execute(owner->object.context, &writer, 8, &reader, VK_TRUE);
	if (status == VK_SUCCESS) {
		/* A reset fence no longer carries acquisition completion to host waiters. */
		for (index = 0; index < fenceCount; index++) {
			sync = vulkan_sync_object((uint64_t)(uintptr_t)pFences[index]);
			status = vulkan_external_fence_reset_locked(owner, sync);
			if (status != VK_SUCCESS)
				break;

			sync->notification = 0;
			sync->software_signaled = VK_FALSE;
		}
	}

	/* Preserve device loss for future local completion observations. */
	vulkan_sync_device_error(owner, status);

	pthread_mutex_unlock(&owner->mutex);

	/* Release independent reply and encoding storage after the transaction. */
	vulkan_reader_finish(&reader);
	vulkan_writer_finish(&writer);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: all requested fences now have unsignaled payloads. */
	return VK_SUCCESS;
}

/*
 * Observes a native or completed acquisition fence without a GPU wait.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkGetFenceStatus(
	VkDevice device,
	VkFence fence)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;
	VkResult status;

	/* Resolve the ordinary fence object before its brief payload transaction. */
	owner = vulkan_device(device);
	sync = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	pthread_mutex_lock(&owner->mutex);

	status = vulkan_sync_device_status_locked(owner);
	if (status == VK_SUCCESS) {
		/* Software completion is valid only after WSI released the actual image. */
		if (sync->external != NULL) {
			status = vulkan_external_fence_status_locked(owner, sync);
		} else if (sync->software_signaled) {
			status = VK_SUCCESS;
		} else {
			status = vulkan_sync_status_locked(owner, sync, VULKAN_OPCODE_vkGetFenceStatus);
		}
	}

	/* Shared producer failure follows the same sticky device-loss rule as native failure. */
	vulkan_sync_device_error(owner, status);

	pthread_mutex_unlock(&owner->mutex);

	/* Preserve native NOT_READY and device-loss results without coercion. */
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: this fence's actual active payload is complete. */
	return VK_SUCCESS;
}

/*
 * Waits for the requested fence condition without retaining submission locks.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(
	VkDevice device,
	uint32_t fenceCount,
	const VkFence *pFences,
	VkBool32 waitAll,
	uint64_t timeout)
{
	struct VkDevice_T *owner;
	VkResult status;

	/* Use the same fence semantics for public callers and internal idle markers. */
	owner = vulkan_device(device);
	status = vulkan_fences_wait(owner, fenceCount, pFences, waitAll, timeout);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the caller's all-or-any fence condition is satisfied. */
	return VK_SUCCESS;
}

/*
 * Creates one unsignaled native binary semaphore with no acquisition payload.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSemaphore(
	VkDevice device,
	const VkSemaphoreCreateInfo *pCreateInfo,
	const VkAllocationCallbacks *pAllocator,
	VkSemaphore *pSemaphore)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;
	uint64_t handle;
	VkResult status;

	/* Create the native payload before allowing queue or WSI use. */
	owner = vulkan_device(device);
	status = sync_create(owner, VULKAN_OBJECT_SEMAPHORE, VULKAN_OPCODE_vkCreateSemaphore, VULKAN_OPCODE_vkDestroySemaphore, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, pCreateInfo->flags, pAllocator, &sync);
	if (status != VK_SUCCESS)
		return status;

	/* Expose the standard non-dispatchable handle rather than its wire identity. */
	handle = vulkan_nondispatchable_handle(&sync->object);
	*pSemaphore = (VkSemaphore)(uintptr_t)handle;

	/* Succeeded: the ordinary binary semaphore is initially unsignaled. */
	return VK_SUCCESS;
}

/*
 * Destroys a binary semaphore after all submitted uses have completed.
 */
VKAPI_ATTR void VKAPI_CALL
vkDestroySemaphore(
	VkDevice device,
	VkSemaphore semaphore,
	const VkAllocationCallbacks *pAllocator)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;

	/* Honor compatible destruction callbacks while consuming this semaphore. */
	owner = vulkan_device(device);
	sync = vulkan_sync_object((uint64_t)(uintptr_t)semaphore);
	sync_destroy(owner, sync, VULKAN_OPCODE_vkDestroySemaphore, pAllocator);

	/* Succeeded: the semaphore's local and native ownership is consumed. */
	return;
}

/*
 * Creates an initially reset event shared by ordinary host and GPU operations.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateEvent(
	VkDevice device,
	const VkEventCreateInfo *pCreateInfo,
	const VkAllocationCallbacks *pAllocator,
	VkEvent *pEvent)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;
	uint64_t handle;
	VkResult status;

	/* Keep event state native so command buffers observe later host changes. */
	owner = vulkan_device(device);
	status = sync_create(owner, VULKAN_OBJECT_EVENT, VULKAN_OPCODE_vkCreateEvent, VULKAN_OPCODE_vkDestroyEvent, VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, pCreateInfo->flags, pAllocator, &sync);
	if (status != VK_SUCCESS)
		return status;

	/* Publish only a successfully created event to the application. */
	handle = vulkan_nondispatchable_handle(&sync->object);
	*pEvent = (VkEvent)(uintptr_t)handle;

	/* Succeeded: the native event begins in its reset state. */
	return VK_SUCCESS;
}

/*
 * Destroys an event after the application has synchronized its outstanding uses.
 */
VKAPI_ATTR void VKAPI_CALL
vkDestroyEvent(
	VkDevice device,
	VkEvent event,
	const VkAllocationCallbacks *pAllocator)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;

	/* Honor the effective destruction allocator, including null-handle cleanup. */
	owner = vulkan_device(device);
	sync = vulkan_sync_object((uint64_t)(uintptr_t)event);
	sync_destroy(owner, sync, VULKAN_OPCODE_vkDestroyEvent, pAllocator);

	/* Succeeded: the application no longer owns this event. */
	return;
}

/*
 * Reads the actual host-visible event state without consuming it.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkGetEventStatus(
	VkDevice device,
	VkEvent event)
{
	VkResult status;

	/* Preserve EVENT_SET and EVENT_RESET as the event's meaningful answers. */
	status = sync_event_operation(device, event, VULKAN_OPCODE_vkGetEventStatus);
	if (status < 0)
		return status;

	/* Succeeded: report the native event state rather than a cached guess. */
	return status;
}

/*
 * Sets a native event so already submitted GPU waits can make progress.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkSetEvent(
	VkDevice device,
	VkEvent event)
{
	VkResult status;

	/* Execute this short host operation independently of GPU fence waiters. */
	status = sync_event_operation(device, event, VULKAN_OPCODE_vkSetEvent);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the native event is signaled for subsequent dependency checks. */
	return VK_SUCCESS;
}

/*
 * Resets a native event after the application has synchronized prior use.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkResetEvent(
	VkDevice device,
	VkEvent event)
{
	VkResult status;

	/* Keep host reset visible to the same event used by command buffers. */
	status = sync_event_operation(device, event, VULKAN_OPCODE_vkResetEvent);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the native event is reset. */
	return VK_SUCCESS;
}

/*
 * Signals acquisition completion without placing work behind a blocked queue.
 */
VkResult
vulkan_wsi_acquire_signal(
	struct VkDevice_T *device,
	VkSemaphore semaphore,
	VkFence fence)
{
	struct vulkan_sync *acquire_semaphore;
	struct vulkan_sync *acquire_fence;
	VkResult status;

	/* Resolve optional ordinary synchronization objects before taking ownership. */
	acquire_semaphore = vulkan_sync_object((uint64_t)(uintptr_t)semaphore);
	acquire_fence = vulkan_sync_object((uint64_t)(uintptr_t)fence);
	vulkan_external_fence_quiesce(acquire_fence);
	pthread_mutex_lock(&device->mutex);

	status = vulkan_sync_device_status_locked(device);
	if (status == VK_SUCCESS)
		status = vulkan_external_fence_acquire_locked(device, acquire_fence);

	if (status == VK_SUCCESS) {
		/* Queue wait encoding consumes this already completed payload exactly once. */
		if (acquire_semaphore != NULL)
			acquire_semaphore->software_signaled = VK_TRUE;

		/* Fence observation retains completion until an explicit fence reset. */
		if (acquire_fence != NULL)
			acquire_fence->software_signaled = VK_TRUE;
	}

	pthread_mutex_unlock(&device->mutex);

	/* A lost device cannot acquire a newly successful local completion. */
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: native display release is represented by ordinary sync payloads. */
	return VK_SUCCESS;
}

/*
 * Adds an optional exact-queue completion wake after successful native submission.
 */
VkResult
vulkan_sync_submit_notification(
	struct VkQueue_T *queue,
	struct vulkan_sync *sync)
{
	struct vulkan_context *context;
	struct vulkan_notification *notification;
	struct vulkan_notification *cursor;
	struct gpu_command_submit submit;
	VkResult status;
	unsigned retired;
	unsigned count;
	int error;
	int saved_error;

	/* Notifications optimize observation and never replace the native fence payload. */
	context = queue->device->object.context;
	sync->notification = 0;
	if ((context->capabilities & GPU_CAP_NOTIFICATION) == 0)
		return VK_SUCCESS;

	/* All marker creation is serialized with decoder transactions on this session. */
	pthread_mutex_lock(&context->mutex);

	status = sync_notifications_reap(context, &retired);
	if (status != VK_SUCCESS) {
		pthread_mutex_unlock(&context->mutex);
		return status;
	}

	/* Reserve one kernel record for the decoder that permits future submissions. */
	count = 0;
	for (cursor = context->notifications; cursor != NULL; cursor = cursor->next)
		count++;

	if (count >= GPU_SUBMIT_MAX - 1U) {
		pthread_mutex_unlock(&context->mutex);
		return VK_SUCCESS;
	}

	/* Optional observation storage must never turn accepted GPU work into an OOM failure. */
	notification = calloc(1, sizeof(*notification));
	if (notification == NULL) {
		pthread_mutex_unlock(&context->mutex);
		return VK_SUCCESS;
	}

	/* Queue ownership keeps this marker before every later submission to the same queue. */
	memset(&submit, 0, sizeof(submit));
	submit.version = GPU_ABI_VERSION;
	submit.size = sizeof(submit);
	submit.flags = GPU_COMMAND_CONTEXT_FENCE;
	submit.timeline = queue->timeline_index;
	error = ioctl(context->fd, GPU_COMMAND_SUBMIT, &submit);
	saved_error = errno;
	if (error != 0) {
		free(notification);
		pthread_mutex_unlock(&context->mutex);

		/* Saturation must not wait for work that a future valid submission will unblock. */
		if (saved_error == EAGAIN || saved_error == ENOMEM)
			return VK_SUCCESS;

		__atomic_store_n(&context->error, VK_ERROR_DEVICE_LOST, __ATOMIC_RELEASE);
		return VK_ERROR_DEVICE_LOST;
	}

	/* A successful kernel acceptance always supplies a nonzero lifetime identity. */
	if (submit.sequence == 0) {
		free(notification);
		pthread_mutex_unlock(&context->mutex);
		__atomic_store_n(&context->error, VK_ERROR_DEVICE_LOST, __ATOMIC_RELEASE);
		return VK_ERROR_DEVICE_LOST;
	}

	notification->sequence = submit.sequence;
	notification->next = context->notifications;
	context->notifications = notification;
	sync->notification = submit.sequence;

	pthread_mutex_unlock(&context->mutex);

	/* Succeeded: this optional wake belongs to the submitted fence's current payload. */
	return VK_SUCCESS;
}

/*
 * Polls native and acquisition fences with the caller's exact timeout policy.
 */
VkResult
vulkan_fences_wait(
	struct VkDevice_T *device,
	uint32_t count,
	const VkFence *fences,
	VkBool32 all,
	uint64_t timeout_ns)
{
	struct vulkan_notification **pins;
	struct pollfd *descriptors;
	size_t descriptor_count;
	VkResult status;

	/* Any-wait pins keep another observer from consuming its level-triggered wake. */
	pins = NULL;
	descriptors = NULL;
	if (!all && count != 0) {
		pins = calloc(count, sizeof(*pins));
		if (pins == NULL)
			return VK_ERROR_OUT_OF_HOST_MEMORY;

		/* Keep allocation overflow separate from pointer-array allocation success. */
		descriptor_count = count;
		if (descriptor_count == SIZE_MAX) {
			free(pins);
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		descriptor_count++;
		descriptors = calloc(descriptor_count, sizeof(*descriptors));
		if (descriptors == NULL) {
			free(pins);
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
	}

	/* One cleanup boundary retires every pin after success, timeout or device loss. */
	status = sync_fences_wait(device, count, fences, all, timeout_ns, pins, descriptors);
	sync_notifications_unpin(device->object.context, count, pins);
	free(pins);
	free(descriptors);

	/* A terminal shared dependency invalidates later local-success observations too. */
	if (status == VK_ERROR_DEVICE_LOST) {
		pthread_mutex_lock(&device->mutex);

		vulkan_sync_device_error(device, status);

		pthread_mutex_unlock(&device->mutex);
	}

	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the requested payload condition was observed without losing a wake. */
	return VK_SUCCESS;
}

/*
 * Converts an ordinary non-dispatchable sync handle into its retained object.
 */
struct vulkan_sync *
vulkan_sync_object(
	uint64_t handle)
{
	struct vulkan_object *object;

	/* Keep all standard handle representation changes in the common helper. */
	object = vulkan_nondispatchable_object(handle);

	/* Succeeded: the common object is the first member of every sync allocation. */
	return (struct vulkan_sync *)object;
}

/*
 * Observes context loss before accepting a locally completed synchronization payload.
 */
VkResult
vulkan_sync_device_status_locked(
	struct VkDevice_T *device)
{
	VkResult status;

	/* Device-local loss and transport loss both invalidate subsequent completion. */
	if (device->error != VK_SUCCESS)
		return device->error;

	/* Other API families publish context failure without taking this device mutex. */
	status = __atomic_load_n(&device->object.context->error, __ATOMIC_ACQUIRE);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: no earlier device or shared transport failure is hidden. */
	return VK_SUCCESS;
}

/*
 * Executes one native status or event operation while its caller owns the device.
 */
VkResult
vulkan_sync_status_locked(
	struct VkDevice_T *device,
	struct vulkan_sync *sync,
	uint32_t opcode)
{
	struct vulkan_writer writer;
	struct vulkan_reader reader;
	VkResult status;

	/* Encode only renderer identities, never application pointers or handles. */
	vulkan_writer_init_for_object(&writer, &sync->object);
	vulkan_command_begin(&writer, opcode);
	vulkan_write_u64(&writer, device->object.wire_id);
	vulkan_write_u64(&writer, sync->object.wire_id);
	status = vulkan_command_execute(device->object.context, &writer, 8, &reader, VK_TRUE);
	vulkan_sync_device_error(device, status);

	/* Both independent buffers expire after this short transaction. */
	vulkan_reader_finish(&reader);
	vulkan_writer_finish(&writer);
	if (status < 0)
		return status;

	/* Succeeded: preserve the native status, including an unsignaled payload. */
	return status;
}

/*
 * Records device loss before a later local payload can report success.
 */
void
vulkan_sync_device_error(
	struct VkDevice_T *device,
	VkResult status)
{
	/* Host allocation pressure does not by itself invalidate the Vulkan device. */
	if (status == VK_ERROR_DEVICE_LOST) {
		device->error = status;
		__atomic_store_n(&device->object.context->error, status, __ATOMIC_RELEASE);
	}

	/* Succeeded: device-loss state remains sticky for all sync observers. */
	return;
}

/*
 * Samples a monotonic nanosecond clock without retaining Vulkan locks.
 */
VkResult
vulkan_sync_clock(
	uint64_t *nanoseconds)
{
	struct timespec sample;
	int error;

	/* A failed timing source cannot safely implement a Vulkan timeout. */
	error = clock_gettime(CLOCK_MONOTONIC, &sample);
	if (error != 0)
		return VK_ERROR_DEVICE_LOST;

	/* Guard the nanosecond conversion before multiplying an arbitrary epoch. */
	if ((uint64_t)sample.tv_sec > UINT64_MAX / UINT64_C(1000000000))
		return VK_ERROR_DEVICE_LOST;

	/* Bound the final fractional second independently of the multiplication. */
	*nanoseconds = (uint64_t)sample.tv_sec * UINT64_C(1000000000);
	if ((uint64_t)sample.tv_nsec > UINT64_MAX - *nanoseconds)
		return VK_ERROR_DEVICE_LOST;

	/* Succeeded: expose one monotonic sample with nanosecond timeout units. */
	*nanoseconds += (uint64_t)sample.tv_nsec;
	return VK_SUCCESS;
}

/*
 * Yields between nonblocking observations while leaving all Vulkan locks free.
 */
VkResult
vulkan_sync_pause(
	uint64_t nanoseconds)
{
	struct timespec delay;
	int error;

	/* Polling delays never exceed one quantum, even for indefinite Vulkan waits. */
	if (nanoseconds > VULKAN_SYNC_POLL_NS)
		nanoseconds = VULKAN_SYNC_POLL_NS;

	/* Let an interrupt shorten this pause because the caller rechecks its deadline. */
	delay.tv_sec = 0;
	delay.tv_nsec = (long)nanoseconds;
	error = nanosleep(&delay, NULL);
	if (error != 0) {
		/* An ordinary signal is an opportunity to observe completion again. */
		if (errno != EINTR)
			return VK_ERROR_DEVICE_LOST;
	}

	/* Succeeded: the caller can perform another short completion observation. */
	return VK_SUCCESS;
}

/*
 * Waits for the private native fence without consulting its shared kernel payload.
 */
VkResult
vulkan_sync_wait_native(
	struct VkDevice_T *device,
	struct vulkan_sync *sync)
{
	VkResult status;
	uint64_t sequence;

	/* Only the renderer's actual fence status can authorize a shared successful signal. */
	for (;;) {
		pthread_mutex_lock(&device->mutex);

		status = vulkan_sync_device_status_locked(device);
		if (status == VK_SUCCESS)
			status = vulkan_sync_status_locked(device, sync, VULKAN_OPCODE_vkGetFenceStatus);

		sequence = sync->notification;

		pthread_mutex_unlock(&device->mutex);

		if (status == VK_SUCCESS)
			break;

		if (status != VK_NOT_READY)
			return status;

		/* Exact queue completion wakes have no device, queue or context lock held. */
		if (sequence != 0) {
			status = sync_notification_wait(device->object.context, sequence, UINT64_MAX);
		} else {
			status = vulkan_sync_pause(VULKAN_SYNC_POLL_NS);
		}

		if (status < 0)
			return status;
	}

	/* Succeeded: the native payload, not notification retirement, proved successful GPU work. */
	return VK_SUCCESS;
}

/* Observes native payloads while retaining any-wait notification ownership. */
static VkResult
sync_fences_wait(
	struct VkDevice_T *device,
	uint32_t count,
	const VkFence *fences,
	VkBool32 all,
	uint64_t timeout_ns,
	struct vulkan_notification **pins,
	struct pollfd *descriptors)
{
	uint64_t started;
	uint64_t now;
	uint64_t delay;
	uint64_t elapsed;
	uint32_t index;
	uint32_t completed;
	uint32_t descriptor_count;
	VkBool32 external_pending;
	uint64_t sequence;
	uint64_t first_sequence;
	VkBool32 missing;
	VkBool32 first_pending;
	struct vulkan_sync *sync;
	struct vulkan_sync *first_external;
	struct vulkan_notification *notification;
	struct vulkan_context *context;
	VkResult status;

	/* Keep candidate records under the same context ledger as every consumer. */
	context = device->object.context;

	/* Measure one deadline before the first observation, including transaction time. */
	status = vulkan_sync_clock(&started);
	if (status != VK_SUCCESS)
		return status;

	/* Give other host threads an opportunity to submit and signal between polls. */
	for (;;) {
		/* A fresh candidate snapshot covers each new observation and its following poll. */
		sync_notifications_unpin(context, count, pins);

		/* Each individual observation releases its device and transport locks. */
		completed = 0;
		first_sequence = 0;
		first_external = NULL;
		first_pending = VK_TRUE;
		missing = VK_FALSE;
		descriptor_count = 1;
		for (index = 0; index < count; index++) {
			/* Pin before observing status, closing the observe-to-poll consumption race. */
			if (pins != NULL) {
				pthread_mutex_lock(&device->mutex);

				sync = vulkan_sync_object((uint64_t)(uintptr_t)fences[index]);
				sequence = sync->notification;
				pthread_mutex_lock(&context->mutex);

				/* A previously consumed marker requires only the native observation below. */
				for (notification = context->notifications; notification != NULL; notification = notification->next) {
					if (notification->sequence == sequence) {
						notification->waiters++;
						pins[index] = notification;
						break;
					}
				}

				pthread_mutex_unlock(&context->mutex);

				pthread_mutex_unlock(&device->mutex);
			}

			status = vkGetFenceStatus((VkDevice)device, fences[index]);
			if (status < 0)
				return status;

			/* Count only completed native or acquisition payloads. */
			if (status == VK_SUCCESS) {
				completed++;

				/* Any-fence waiting ends at the first successful observation. */
				if (!all)
					break;
			} else {
				/* Snapshot the optional wake under the same mutex as fence reset and submit. */
				pthread_mutex_lock(&device->mutex);

				sync = vulkan_sync_object((uint64_t)(uintptr_t)fences[index]);
				sequence = sync->notification;
				external_pending = VK_FALSE;
				if (sync->external != NULL) {
					external_pending = VK_TRUE;
					if (descriptors != NULL) {
						descriptors[descriptor_count].fd = vulkan_external_fence_descriptor(sync);
						descriptors[descriptor_count].events = POLLIN;
						descriptors[descriptor_count].revents = 0;
						descriptor_count++;
					}
				}

				pthread_mutex_unlock(&device->mutex);

				/* All waits may block on one required fence; any waits need every candidate. */
				if (first_pending) {
					first_sequence = sequence;
					if (sync->external != NULL)
						first_external = sync;

					first_pending = VK_FALSE;
				}

				if (sequence == 0 && !external_pending)
					missing = VK_TRUE;

				/* Without a retained record, bounded native observation is the safe fallback. */
				if (pins != NULL && pins[index] == NULL && !external_pending)
					missing = VK_TRUE;
			}
		}

		/* All-fence and any-fence conditions have separate debugger decisions. */
		if (all) {
			/* Every requested fence has been observed complete. */
			if (completed == count)
				break;
		} else {
			/* At least one observed completion satisfies an any-fence wait. */
			if (completed != 0)
				break;
		}

		/* A zero timeout performs exactly one observation pass. */
		if (timeout_ns == 0)
			return VK_TIMEOUT;

		/* Bound ordinary waits without imposing a finite limit on UINT64_MAX. */
		delay = UINT64_MAX;
		if (timeout_ns != UINT64_MAX) {
			/* Count wire processing against the original finite deadline. */
			status = vulkan_sync_clock(&now);
			if (status != VK_SUCCESS)
				return status;

			/* Unsigned subtraction also expires a clock that moves backward. */
			elapsed = now - started;
			if (elapsed >= timeout_ns)
				return VK_TIMEOUT;

			/* Never deliberately sleep beyond the remaining caller deadline. */
			delay = timeout_ns - elapsed;
		}

		/* Exact queue markers wake normal waits without recurring status transactions. */
		if (all && first_external != NULL) {
			status = vulkan_external_fence_wait(device, first_external, delay);
		} else if (all && first_sequence != 0) {
			status = sync_notification_wait(device->object.context, first_sequence, delay);
		} else if (!all && !missing) {
			status = sync_notification_poll(device->object.context, delay, descriptors, descriptor_count);
		} else {
			/* Optional marker saturation retains the original nonblocking status fallback. */
			if (delay > VULKAN_SYNC_POLL_NS)
				delay = VULKAN_SYNC_POLL_NS;

			status = vulkan_sync_pause(delay);
		}

		/* A transport wake never bypasses the next actual VkFenceStatus/error observation. */
		if (status < 0)
			return status;
	}

	/* Succeeded: the requested fence condition was observed complete. */
	return VK_SUCCESS;
}

/* Reclaims completed kernel records without interpreting them as Vulkan results. */
static VkResult
sync_notifications_reap(
	struct vulkan_context *context,
	unsigned *retired)
{
	struct vulkan_notification **link;
	struct vulkan_notification *notification;
	struct gpu_command_wait wait;
	int error;

	/* The context mutex keeps each pending sequence on exactly one local ledger. */
	*retired = 0;
	link = &context->notifications;
	while (*link != NULL) {
		notification = *link;
		if (notification->waiters != 0) {
			link = &notification->next;
			continue;
		}

		memset(&wait, 0, sizeof(wait));
		wait.version = GPU_ABI_VERSION;
		wait.size = sizeof(wait);
		wait.sequence = notification->sequence;
		wait.flags = GPU_WAIT_CONSUME;
		error = ioctl(context->fd, GPU_COMMAND_WAIT, &wait);
		if (error != 0) {
			/* Pending or interrupted observations preserve their exact kernel record. */
			if (errno == EAGAIN || errno == EINTR) {
				link = &notification->next;
				continue;
			}

			__atomic_store_n(&context->error, VK_ERROR_DEVICE_LOST, __ATOMIC_RELEASE);
			return VK_ERROR_DEVICE_LOST;
		}

		/* Once consumed, a missing local sequence means observe its actual fence again. */
		*link = notification->next;
		free(notification);
		(*retired)++;
		if (wait.status != 0) {
			__atomic_store_n(&context->error, VK_ERROR_DEVICE_LOST, __ATOMIC_RELEASE);
			return VK_ERROR_DEVICE_LOST;
		}
	}

	/* Succeeded: only still-pending completion records remain on the local ledger. */
	return VK_SUCCESS;
}

/* Releases pins only after their native observation and blocking poll have finished. */
static void
sync_notifications_unpin(
	struct vulkan_context *context,
	uint32_t count,
	struct vulkan_notification **pins)
{
	uint32_t index;

	/* All-fence waits use exact sequence WAIT and need no level-triggered record pins. */
	if (pins == NULL)
		return;

	/* The ledger cannot retire an entry while a waiter owns its pointer. */
	pthread_mutex_lock(&context->mutex);

	/* Retire every candidate before a later pass selects fresh active payloads. */
	for (index = 0; index < count; index++) {
		if (pins[index] != NULL) {
			pins[index]->waiters--;
			pins[index] = NULL;
		}
	}

	pthread_mutex_unlock(&context->mutex);

	/* Succeeded: ordinary marker reclamation can consume these entries again. */
	return;
}

/* Waits for one exact marker without retaining submission or context locks. */
static VkResult
sync_notification_wait(
	struct vulkan_context *context,
	uint64_t sequence,
	uint64_t timeout_ns)
{
	struct vulkan_notification *notification;
	struct gpu_command_wait wait;
	VkResult status;
	unsigned retired;
	int error;
	int saved_error;

	/* Another observer may have consumed this marker while retaining the native fence. */
	pthread_mutex_lock(&context->mutex);

	status = sync_notifications_reap(context, &retired);
	notification = context->notifications;
	while (notification != NULL) {
		if (notification->sequence == sequence)
			break;

		notification = notification->next;
	}

	pthread_mutex_unlock(&context->mutex);

	if (status != VK_SUCCESS)
		return status;

	/* A consumed marker is only a reason to perform another native fence observation. */
	if (notification == NULL)
		return VK_SUCCESS;

	/* WAIT does not consume so concurrent waiters may independently observe completion. */
	memset(&wait, 0, sizeof(wait));
	wait.version = GPU_ABI_VERSION;
	wait.size = sizeof(wait);
	wait.sequence = sequence;
	wait.timeout_ns = timeout_ns;
	error = ioctl(context->fd, GPU_COMMAND_WAIT, &wait);
	saved_error = errno;
	if (error != 0) {
		/* A different observer can consume completion between the snapshot and WAIT. */
		if (saved_error == ENOENT || saved_error == EINTR)
			return VK_SUCCESS;

		/* Caller timeout never cancels GPU work or destroys a pending record. */
		if (saved_error == ETIMEDOUT || saved_error == EAGAIN)
			return VK_TIMEOUT;

		__atomic_store_n(&context->error, VK_ERROR_DEVICE_LOST, __ATOMIC_RELEASE);
		return VK_ERROR_DEVICE_LOST;
	}

	/* A transport fault invalidates the context, independently of renderer fence payloads. */
	if (wait.status != 0) {
		__atomic_store_n(&context->error, VK_ERROR_DEVICE_LOST, __ATOMIC_RELEASE);
		return VK_ERROR_DEVICE_LOST;
	}

	/* Succeeded: the caller must still query the actual native fence result. */
	return VK_SUCCESS;
}

/* Waits for any unread completion while allowing all producers to make progress. */
static VkResult
sync_notification_poll(
	struct vulkan_context *context,
	uint64_t timeout_ns,
	struct pollfd *descriptors,
	uint32_t count)
{
	uint32_t index;
	VkResult status;
	uint64_t milliseconds;
	unsigned retired;
	int timeout;
	int error;

	/* Consume unrelated completed markers so level-triggered poll cannot spin on them. */
	pthread_mutex_lock(&context->mutex);

	status = sync_notifications_reap(context, &retired);

	pthread_mutex_unlock(&context->mutex);

	if (status != VK_SUCCESS)
		return status;

	/* A completion just consumed may already satisfy one of the caller's native fences. */
	if (retired != 0)
		return VK_SUCCESS;

	/* Poll uses milliseconds, rounded up without overflowing the finite nanosecond input. */
	timeout = -1;
	if (timeout_ns != UINT64_MAX) {
		milliseconds = timeout_ns / UINT64_C(1000000);
		if (timeout_ns % UINT64_C(1000000) != 0)
			milliseconds++;

		timeout = 2147483647;
		if (milliseconds < 2147483647U)
			timeout = (int)milliseconds;
	}

	/* No library mutex is retained while the kernel waits for any completed GPU record. */
	memset(&descriptors[0], 0, sizeof(descriptors[0]));
	descriptors[0].fd = context->fd;
	descriptors[0].events = POLLIN;
	error = poll(descriptors, count, timeout);
	if (error < 0) {
		if (errno == EINTR)
			return VK_SUCCESS;

		return VK_ERROR_DEVICE_LOST;
	}

	/* A timeout is not a GPU fault and leaves every pending sequence available. */
	if (error == 0)
		return VK_TIMEOUT;

	/* An error on any requested dependency invalidates the corresponding fence wait. */
	for (index = 0; index < count; index++) {
		if (descriptors[index].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			__atomic_store_n(&context->error, VK_ERROR_DEVICE_LOST, __ATOMIC_RELEASE);
			return VK_ERROR_DEVICE_LOST;
		}
	}

	/* Succeeded: native status observation decides which requested fence completed. */
	return VK_SUCCESS;
}

/* Leaves pending kernel ownership on the context ledger during implicit destruction. */
static void
sync_release_storage(
	struct vulkan_object *object)
{
	struct vulkan_sync *sync;
	struct vulkan_context *context;
	VkResult status;
	unsigned retired;

	/* Optional markers hold no application object pointers after this identity is freed. */
	sync = (struct vulkan_sync *)object;
	vulkan_external_fence_finish(sync);
	if (sync->notification == 0)
		return;

	context = object->context;
	sync->notification = 0;
	pthread_mutex_lock(&context->mutex);

	status = sync_notifications_reap(context, &retired);
	(void)status;

	pthread_mutex_unlock(&context->mutex);

	/* Succeeded: context close can reclaim even a still-pending detached marker. */
	return;
}

/* Create one flags-only native sync object before publishing its local identity. */
static VkResult
sync_create(
	struct VkDevice_T *device,
	enum vulkan_object_kind kind,
	uint32_t opcode,
	uint32_t destroy_opcode,
	uint32_t structure,
	VkFlags flags,
	const VkAllocationCallbacks *allocator,
	struct vulkan_sync **result)
{
	struct vulkan_object *object;
	struct vulkan_writer writer;
	struct vulkan_reader reader;
	uint64_t present;
	uint64_t returned;
	VkResult status;

	/* Retain one callback-owned local object through every creation rollback. */
	status = vulkan_object_alloc(sizeof(struct vulkan_sync), sizeof(uint64_t), kind, &device->object, device->object.context, allocator, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, &object);
	if (status != VK_SUCCESS)
		return status;

	/* Implicit device destruction also retires optional notification bookkeeping. */
	object->release_storage = sync_release_storage;

	/* Reserve a never-reused renderer identity before encoding its output pointer. */
	status = vulkan_object_reserve_id(object);
	if (status != VK_SUCCESS) {
		vulkan_object_free(object);
		return status;
	}

	/* The three core flags-only create structures share this explicit wire shape. */
	vulkan_writer_init_for_object(&writer, object);
	vulkan_command_begin(&writer, opcode);
	vulkan_write_u64(&writer, device->object.wire_id);
	vulkan_write_u64(&writer, 1);
	vulkan_write_u32(&writer, structure);
	vulkan_write_u64(&writer, 0);
	vulkan_write_u32(&writer, flags);
	vulkan_write_u64(&writer, 0);
	vulkan_write_u64(&writer, 1);
	vulkan_write_u64(&writer, object->wire_id);
	status = vulkan_command_execute(device->object.context, &writer, 24, &reader, VK_TRUE);
	if (status != VK_SUCCESS) {
		status = vulkan_reply_finish(device->object.context, &reader, status);
		vulkan_writer_finish(&writer);
		vulkan_object_free(object);
		return status;
	}

	/* A successful host creation must echo the reserved output identity. */
	present = vulkan_read_u64(&reader);
	returned = vulkan_read_u64(&reader);
	status = reader.error;
	if (status == VK_SUCCESS) {
		/* A missing required pointer is a protocol failure, not a null object. */
		if (present != 1)
			status = VK_ERROR_DEVICE_LOST;

		/* A foreign identity must never be exposed as this local object. */
		if (returned != object->wire_id)
			status = VK_ERROR_DEVICE_LOST;
	}

	/* Protocol loss is shared so no software completion can mask malformed creation. */
	if (status == VK_ERROR_DEVICE_LOST)
		__atomic_store_n(&device->object.context->error, status, __ATOMIC_RELEASE);

	/* Native ownership already exists; use its normal destruction for rollback. */
	vulkan_reader_finish(&reader);
	vulkan_writer_finish(&writer);
	if (status != VK_SUCCESS) {
		sync_destroy(device, (struct vulkan_sync *)object, destroy_opcode, NULL);
		return status;
	}

	/* Link the complete object only after both host state and reply are valid. */
	status = vulkan_object_publish(object);
	if (status != VK_SUCCESS) {
		sync_destroy(device, (struct vulkan_sync *)object, destroy_opcode, NULL);
		return status;
	}

	/* Succeeded: the caller receives one fully owned ordinary sync object. */
	*result = (struct vulkan_sync *)object;
	return VK_SUCCESS;
}

/* Release a flags-only native synchronization object and its saved allocator. */
static void
sync_destroy(
	struct VkDevice_T *device,
	struct vulkan_sync *sync,
	uint32_t opcode,
	const VkAllocationCallbacks *allocator)
{
	struct vulkan_writer writer;
	struct vulkan_reader reader;
	VkResult status;

	/* Vulkan destruction accepts the null handle during partial initialization. */
	if (sync == NULL)
		return;

	/* Join before native destruction because the worker still proves completion on this fence. */
	vulkan_external_fence_quiesce(sync);

	/* Keep native destruction and software payload retirement in one transaction. */
	vulkan_writer_init_for_object(&writer, &sync->object);
	if (allocator != NULL) {
		writer.allocator.callbacks = *allocator;
		writer.allocator.has_callbacks = VK_TRUE;
	}

	/* Encode the native destruction after selecting this call's command allocator. */
	vulkan_command_begin(&writer, opcode);
	vulkan_write_u64(&writer, device->object.wire_id);
	vulkan_write_u64(&writer, sync->object.wire_id);
	vulkan_write_u64(&writer, 0);
	pthread_mutex_lock(&device->mutex);

	status = vulkan_command_execute(device->object.context, &writer, 4, &reader, VK_FALSE);
	vulkan_sync_device_error(device, status);

	pthread_mutex_unlock(&device->mutex);

	/* Void destruction consumes local ownership even after device loss. */
	vulkan_reader_finish(&reader);
	vulkan_writer_finish(&writer);
	vulkan_object_free_with_allocator(&sync->object, allocator);

	/* Succeeded: no local sync allocation remains owned by the application. */
	return;
}

/* Apply a native host event operation without holding locks while a GPU waits. */
static VkResult
sync_event_operation(
	VkDevice device,
	VkEvent event,
	uint32_t opcode)
{
	struct VkDevice_T *owner;
	struct vulkan_sync *sync;
	VkResult status;

	/* Resolve the event's ordinary ownership record before the short transaction. */
	owner = vulkan_device(device);
	sync = vulkan_sync_object((uint64_t)(uintptr_t)event);
	pthread_mutex_lock(&owner->mutex);

	status = vulkan_sync_device_status_locked(owner);
	if (status == VK_SUCCESS)
		status = vulkan_sync_status_locked(owner, sync, opcode);

	pthread_mutex_unlock(&owner->mutex);

	/* Preserve native event answers, including SET and RESET statuses. */
	if (status < 0)
		return status;

	/* Succeeded: the event operation completed or reported its actual state. */
	return status;
}
