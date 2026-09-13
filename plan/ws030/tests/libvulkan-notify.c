/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Reuses the independent native Vulkan peer while adding a distinct kernel-notification oracle. */
#define main sync_regression_main
#include "libvulkan-sync.c"
#undef main
#include <assert.h>
#include <stdarg.h>
#include <poll.h>
#include <uapi/gpu.h>

/* Kernel wake identities are separate from actual native fence state. */
struct test_notification {
	uint64_t sequence;
	uint64_t fence;
	int complete;
	int consumed;
};

/* The fixture owns notification state for this one serial acceptance process. */
static struct test_notification notices[32];
static unsigned notice_count;
static unsigned blocking_waits;
static unsigned poll_waits;
static uint64_t next_fence;
static int saturate;
static int hold;
static int renderer_loss;
static struct VkQueue_T *test_queue;

int sync_test_ioctl(int fd, unsigned long operation, ...);
int sync_test_poll(struct pollfd *fds, nfds_t count, int timeout);
static void assert_wait_unlocked(void);

/*
 * Supplies independently delayed marker completion and ordinary marker backpressure.
 */
int
sync_test_ioctl(
	int fd,
	unsigned long operation,
	...)
{
	struct gpu_command_submit *submit;
	struct gpu_command_wait *wait;
	struct test_notification *notice;
	struct timespec pause;
	va_list arguments;
	void *argument;
	unsigned index;

	/* Kernel operations use the existing session without application object addresses. */
	assert(fd == 61);
	va_start(arguments, operation);
	argument = va_arg(arguments, void *);
	va_end(arguments);
	if (operation == GPU_COMMAND_SUBMIT) {
		submit = argument;
		assert(submit->bytes == 0 && submit->address == 0);
		assert(submit->timeline == 1 && submit->flags == GPU_COMMAND_CONTEXT_FENCE);
		if (saturate) {
			errno = EAGAIN;
			return -1;
		}

		assert(notice_count < 32);
		notice = &notices[notice_count++];
		notice->sequence = 100 + notice_count;
		notice->fence = next_fence;
		submit->sequence = notice->sequence;
		return 0;
	}

	/* Every marker can be consumed once, independently of native fence reset. */
	assert(operation == GPU_COMMAND_WAIT);
	wait = argument;
	notice = NULL;
	for (index = 0; index < notice_count; index++) {
		if (notices[index].sequence == wait->sequence && !notices[index].consumed)
			notice = &notices[index];
	}

	if (notice == NULL) {
		errno = ENOENT;
		return -1;
	}

	/* A pure observation does not advance the independent completion oracle. */
	if (!notice->complete && wait->timeout_ns == 0) {
		errno = EAGAIN;
		return -1;
	}

	if (!notice->complete) {
		assert_wait_unlocked();
		blocking_waits++;
		if (hold) {
			pause.tv_sec = 0;
			pause.tv_nsec = 1000000;
			nanosleep(&pause, NULL);
			errno = ETIMEDOUT;
			return -1;
		}

		/* A retirement wake can coexist with a native DEVICE_LOST result. */
		notice->complete = 1;
		if (renderer_loss) {
			peer.fail_opcode = 38;
			peer.fail_result = VK_ERROR_DEVICE_LOST;
			peer.objects[notice->fence].pending = 0;
			peer.objects[notice->fence].signaled = 1;
		} else {
			peer.objects[notice->fence].pending = 0;
			peer.objects[notice->fence].signaled = 1;
		}
	}

	wait->status = 0;
	if (wait->flags & GPU_WAIT_CONSUME)
		notice->consumed = 1;
	return 0;
}

/*
 * Completes the later of two requested fences to distinguish any-wait from first-wait.
 */
int
sync_test_poll(
	struct pollfd *fds,
	nfds_t count,
	int timeout)
{
	struct test_notification *notice;
	struct vulkan_sync additional;
	VkResult status;

	/* No producer lock may span a kernel completion wait. */
	assert_wait_unlocked();
	assert(count == 1 && fds[0].fd == 61 && (fds[0].events & POLLIN));
	assert(timeout > 0 || timeout == -1);
	poll_waits++;
	notice = &notices[notice_count - 1];
	notice->complete = 1;
	peer.objects[notice->fence].pending = 0;
	peer.objects[notice->fence].signaled = 1;
	/* Another submitter reaps notifications between observation and poll completion. */
	memset(&additional, 0, sizeof(additional));
	pthread_mutex_lock(&test_queue->mutex);
	pthread_mutex_lock(&test_queue->device->mutex);
	status = vulkan_sync_submit_notification(test_queue, &additional);
	pthread_mutex_unlock(&test_queue->device->mutex);
	pthread_mutex_unlock(&test_queue->mutex);
	assert(status == VK_SUCCESS);
	assert(!notice->consumed);
	fds[0].revents = POLLIN;
	return 1;
}

/*
 * Verifies notification semantics through real sync and queue entrypoints.
 */
int
main(
	void)
{
	struct vulkan_context context;
	struct VkDevice_T device;
	struct VkQueue_T queue;
	struct vulkan_notification *notification;
	VkFenceCreateInfo create;
	VkFence fences[2];
	VkResult status;
	unsigned before;
	unsigned index;

	/* Provide surrounding ordinary ownership while production creates both fences. */
	memset(&context, 0, sizeof(context));
	memset(&device, 0, sizeof(device));
	memset(&queue, 0, sizeof(queue));
	context.fd = 61;
	context.capabilities = GPU_CAP_NOTIFICATION;
	device.object.context = &context;
	device.object.wire_id = 1000;
	queue.device = &device;
	queue.object.context = &context;
	queue.object.wire_id = 1001;
	queue.timeline_index = 1;
	test_queue = &queue;
	pthread_mutex_init(&context.mutex, NULL);
	pthread_mutex_init(&device.mutex, NULL);
	pthread_mutex_init(&queue.mutex, NULL);
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	peer.event_blocked = 1;
	for (index = 0; index < 2; index++) {
		status = vkCreateFence((VkDevice)&device, &create, NULL, &fences[index]);
		assert(status == VK_SUCCESS);
		next_fence = vulkan_sync_object((uint64_t)fences[index])->object.wire_id;
		status = vkQueueSubmit((VkQueue)&queue, 0, NULL, fences[index]);
		assert(status == VK_SUCCESS);
		assert(vulkan_sync_object((uint64_t)fences[index])->notification != 0);
	}

	/* Zero and finite timeout preserve pending ownership without reporting completion. */
	status = vkWaitForFences((VkDevice)&device, 2, fences, VK_TRUE, 0);
	assert(status == VK_TIMEOUT && blocking_waits == 0);
	hold = 1;
	status = vkWaitForFences((VkDevice)&device, 1, fences, VK_TRUE, 1000000);
	assert(status == VK_TIMEOUT && !notices[0].consumed);
	hold = 0;
	before = blocking_waits;
	status = vkWaitForFences((VkDevice)&device, 2, fences, VK_TRUE, UINT64_MAX);
	assert(status == VK_SUCCESS && blocking_waits == before + 2);

	/* Any-fence waiting observes the second marker without blocking on the first. */
	status = vkResetFences((VkDevice)&device, 2, fences);
	assert(status == VK_SUCCESS);
	for (index = 0; index < 2; index++) {
		next_fence = vulkan_sync_object((uint64_t)fences[index])->object.wire_id;
		status = vkQueueSubmit((VkQueue)&queue, 0, NULL, fences[index]);
		assert(status == VK_SUCCESS);
	}

	status = vkWaitForFences((VkDevice)&device, 2, fences, VK_FALSE, UINT64_MAX);
	assert(status == VK_SUCCESS && poll_waits == 1);
	assert(!peer.objects[vulkan_sync_object((uint64_t)fences[0])->object.wire_id].signaled);
	status = vkWaitForFences((VkDevice)&device, 2, fences, VK_TRUE, UINT64_MAX);
	assert(status == VK_SUCCESS);

	/* Marker saturation never changes an accepted queue operation into failure. */
	status = vkResetFences((VkDevice)&device, 1, fences);
	assert(status == VK_SUCCESS);
	saturate = 1;
	status = vkQueueSubmit((VkQueue)&queue, 0, NULL, fences[0]);
	assert(status == VK_SUCCESS && vulkan_sync_object((uint64_t)fences[0])->notification == 0);
	peer.event_blocked = 0;
	status = vkWaitForFences((VkDevice)&device, 1, fences, VK_TRUE, 0);
	assert(status == VK_SUCCESS);
	saturate = 0;

	/* A kernel wake cannot override a failing actual Vulkan fence observation. */
	status = vkResetFences((VkDevice)&device, 1, fences);
	assert(status == VK_SUCCESS);
	peer.event_blocked = 1;
	next_fence = vulkan_sync_object((uint64_t)fences[0])->object.wire_id;
	status = vkQueueSubmit((VkQueue)&queue, 0, NULL, fences[0]);
	assert(status == VK_SUCCESS);
	renderer_loss = 1;
	status = vkWaitForFences((VkDevice)&device, 1, fences, VK_TRUE, UINT64_MAX);
	assert(status == VK_ERROR_DEVICE_LOST);

	/* Consume local ownership even after terminal renderer namespace loss. */
	vkDestroyFence((VkDevice)&device, fences[0], NULL);
	vkDestroyFence((VkDevice)&device, fences[1], NULL);
	while (context.notifications != NULL) {
		notification = context.notifications;
		context.notifications = notification->next;
		free(notification);
	}

	pthread_mutex_destroy(&queue.mutex);
	pthread_mutex_destroy(&device.mutex);
	pthread_mutex_destroy(&context.mutex);
	puts("libvulkan notifications: exact/all/any wake, timeout retention, saturated marker fallback, unlocked wait and native DEVICE_LOST PASS");
	return 0;
}

/* Confirms all locks required by independent producers remain available during WAIT. */
static void
assert_wait_unlocked(
	void)
{
	int error;

	error = pthread_mutex_trylock(&test_queue->mutex);
	assert(error == 0);
	pthread_mutex_unlock(&test_queue->mutex);
	error = pthread_mutex_trylock(&test_queue->device->mutex);
	assert(error == 0);
	pthread_mutex_unlock(&test_queue->device->mutex);
	error = pthread_mutex_trylock(&test_queue->device->object.context->mutex);
	assert(error == 0);
	pthread_mutex_unlock(&test_queue->device->object.context->mutex);
}
