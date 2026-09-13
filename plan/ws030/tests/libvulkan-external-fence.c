/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Reuse the independent native Vulkan peer with a separate reference-payload kernel oracle. */
#define main sync_regression_main
#include "libvulkan-sync.c"
#undef main
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <unistd.h>
#include <uapi/gpu-fence.h>

/* Kernel payload identity is independent of every native VkFence identity and fd number. */
struct kernel_payload {
	unsigned references;
	uint64_t generation;
	uint32_t state;
	int bound;
	uint64_t native;
};

/* The independent kernel peer serializes payload observations and strict job completion. */
static pthread_mutex_t kernel_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t kernel_condition = PTHREAD_COND_INITIALIZER;
static struct kernel_payload payloads[32];
static struct kernel_payload *descriptors[512];
static unsigned payload_count;
static unsigned fd_next = 100;
static unsigned kernel_waits;
static unsigned signals;
static unsigned rollbacks;
static int reject_bind;
static int stale_wait;

/* Kernel job reservations retain payload references independently of descriptor close or reuse. */
static struct kernel_payload *job_payloads[512];
/* Exportable fence creation must never request a guest completion thread. */
static unsigned worker_creations;

int fence_test_thread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*entry)(void *), void *argument);
static void kernel_jobs_progress(void);
int fence_test_ioctl(int fd, unsigned long operation, ...);
int fence_test_fcntl(int fd, int command, ...);
int fence_test_close(int fd);
int fence_test_poll(struct pollfd *fds, nfds_t count, int timeout);
static int kernel_operation(unsigned long operation, void *argument);
static int export_fd(struct VkDevice_T *device, VkFence fence);
static void import_fd(struct VkDevice_T *device, VkFence fence, int fd, VkFenceImportFlags flags);

/*
 * Rejects and counts any attempted per-fence worker creation.
 */
int
fence_test_thread_create(
	pthread_t *thread,
	const pthread_attr_t *attributes,
	void *(*entry)(void *),
	void *argument)
{
	/* No supported external-fence operation requires another userspace signaler. */
	(void)thread;
	(void)attributes;
	(void)entry;
	(void)argument;
	worker_creations++;

	/* A regression cannot pass merely because a host pthread happened to start. */
	return EAGAIN;
}

int
fence_test_ioctl(int fd, unsigned long operation, ...)
{
	va_list arguments;
	void *argument;
	int error;

	assert(fd == 61);
	va_start(arguments, operation);
	argument = va_arg(arguments, void *);
	va_end(arguments);
	pthread_mutex_lock(&kernel_mutex);
	error = kernel_operation(operation, argument);
	pthread_mutex_unlock(&kernel_mutex);
	return error;
}

static int
kernel_operation(unsigned long operation, void *argument)
{
	struct gpu_fence_create *create;
	struct gpu_fence_state *state;
	struct gpu_fence_bind *bind;
	struct gpu_job_reserve *reserve;
	unsigned job_index;
	struct timespec end;
	struct kernel_payload *payload;
	struct timespec deadline;
	int error;

	/* Reservation pins the shared payload before native work can be accepted. */
	if (operation == GPU_JOB_RESERVE) {
		reserve = argument;
		test_job_reject = reject_bind;
		error = sync_base_ioctl(61, operation, argument);
		if (error != 0)
			return error;

		/* Native-only jobs need no transferable payload reference. */
		if (reserve->fd >= 0) {
			payload = descriptors[reserve->fd];
			assert(payload != NULL && !payload->bound);
			assert(payload->state == GPU_FENCE_PENDING && payload->generation == reserve->generation);
			job_index = (unsigned)(reserve->sequence - 1001);
			job_payloads[job_index] = payload;
			payload->references++;
			payload->bound = 2;
		}
		return 0;
	}

	/* Strict host progress publishes success or error without any native status worker. */
	if (operation == GPU_JOB_COMMIT || operation == GPU_JOB_CANCEL || operation == GPU_COMMAND_WAIT) {
		error = sync_base_ioctl(61, operation, argument);
		kernel_jobs_progress();
		return error;
	}

	/* Payload queries observe independently completed kernel jobs before returning their state. */
	kernel_jobs_progress();

	if (operation == GPU_FENCE_CREATE) {
		create = argument;
		assert(create->fd == -1 && create->generation == 0);
		assert(create->flags == GPU_HANDLE_CLOEXEC);
		assert(payload_count < 32 && fd_next < 512);
		payload = &payloads[payload_count++];
		payload->references = 1;
		payload->generation = 1;
		payload->state = GPU_FENCE_PENDING;
		if (create->signaled)
			payload->state = GPU_FENCE_SIGNALED;

		create->fd = (int)fd_next;
		create->generation = 1;
		descriptors[fd_next++] = payload;
		return 0;
	}

	if (operation == GPU_FENCE_BIND) {
		bind = argument;
		assert(bind->fd >= 100 && bind->fd < 512);
		payload = descriptors[bind->fd];
		assert(payload != NULL && payload->generation == bind->generation);
		if (bind->flags & GPU_FENCE_BIND_RELEASE) {
			assert(payload->bound && payload->state == GPU_FENCE_PENDING);
			payload->bound = 0;
			rollbacks++;
			return 0;
		}

		if (reject_bind) {
			errno = ENOMEM;
			return -1;
		}

		assert(!payload->bound && payload->state == GPU_FENCE_PENDING);
		payload->bound = 1;
		return 0;
	}

	state = argument;
	assert(state->state == 0 && state->flags == 0);
	if (operation != GPU_FENCE_SIGNAL)
		assert(state->error == 0);
	if (operation != GPU_FENCE_WAIT)
		assert(state->timeout_ns == 0);
	if (state->fd < 100 || state->fd >= 512 || descriptors[state->fd] == NULL) {
		errno = EBADF;
		return -1;
	}

	payload = descriptors[state->fd];
	if (operation == GPU_FENCE_QUERY) {
		state->generation = payload->generation;
		state->state = payload->state;
		return 0;
	}

	/* A separate alias may reset then complete new work between QUERY and generation WAIT. */
	if (operation == GPU_FENCE_WAIT && stale_wait) {
		assert(!payload->bound && payload->state == GPU_FENCE_PENDING);
		stale_wait = 0;
		payload->generation++;
		payload->state = GPU_FENCE_SIGNALED;
		errno = ESTALE;
		return -1;
	}

	assert(state->generation == payload->generation);
	if (operation == GPU_FENCE_RESET) {
		assert(!payload->bound);
		payload->generation++;
		payload->state = GPU_FENCE_PENDING;
		state->generation = payload->generation;
		return 0;
	}

	if (operation == GPU_FENCE_SIGNAL) {
		assert(payload->bound);
		if (state->error == 0 && payload->native != 0) {
			pthread_mutex_lock(&peer_mutex);
			assert(peer.objects[payload->native].signaled);
			pthread_mutex_unlock(&peer_mutex);
		}

		payload->state = GPU_FENCE_SIGNALED;
		if (state->error != 0)
			payload->state = GPU_FENCE_ERROR;

		payload->bound = 0;
		signals++;
		pthread_cond_broadcast(&kernel_condition);
		return 0;
	}

	assert(operation == GPU_FENCE_WAIT);
	if (payload->state == GPU_FENCE_PENDING && state->timeout_ns == 0) {
		errno = EAGAIN;
		return -1;
	}

	clock_gettime(CLOCK_REALTIME, &end);
	end.tv_sec += 2;

	/* The fixture's host GPU oracle advances without production user threads. */
	while (payload->state == GPU_FENCE_PENDING) {
		clock_gettime(CLOCK_REALTIME, &deadline);
		if (deadline.tv_sec >= end.tv_sec) {
			errno = ETIMEDOUT;
			return -1;
		}

		/* Short condition intervals allow independent native event and alias operations. */
		deadline.tv_nsec += 1000000;
		if (deadline.tv_nsec >= 1000000000L) {
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000000000L;
		}

		kernel_waits++;
		error = pthread_cond_timedwait(&kernel_condition, &kernel_mutex, &deadline);
		assert(error == 0 || error == ETIMEDOUT);
		kernel_jobs_progress();
	}

	state->state = payload->state;
	return 0;
}

int
fence_test_fcntl(int fd, int command, ...)
{
	int duplicate;

	assert(command == F_DUPFD_CLOEXEC);
	pthread_mutex_lock(&kernel_mutex);
	assert(fd >= 100 && fd < 512 && descriptors[fd] != NULL);
	duplicate = (int)fd_next++;
	assert(duplicate < 512);
	descriptors[duplicate] = descriptors[fd];
	descriptors[duplicate]->references++;
	pthread_mutex_unlock(&kernel_mutex);
	return duplicate;
}

int
fence_test_close(int fd)
{
	pthread_mutex_lock(&kernel_mutex);
	assert(fd >= 100 && fd < 512 && descriptors[fd] != NULL);
	assert(descriptors[fd]->references > 0);
	descriptors[fd]->references--;
	descriptors[fd] = NULL;
	pthread_mutex_unlock(&kernel_mutex);
	return 0;
}

int
fence_test_poll(struct pollfd *fds, nfds_t count, int timeout)
{
	nfds_t index;
	struct timespec deadline;
	int ready;
	int error;

	(void)timeout;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 2;
	pthread_mutex_lock(&kernel_mutex);
	for (;;) {
		ready = 0;
		for (index = 0; index < count; index++) {
			fds[index].revents = 0;
			if (fds[index].fd == 61 || fds[index].fd < 0)
				continue;

			assert(descriptors[fds[index].fd] != NULL);
			if (descriptors[fds[index].fd]->state != GPU_FENCE_PENDING) {
				fds[index].revents = POLLIN;
				ready++;
			}
		}

		if (ready != 0)
			break;

		error = pthread_cond_timedwait(&kernel_condition, &kernel_mutex, &deadline);
		if (error != 0)
			break;
	}

	pthread_mutex_unlock(&kernel_mutex);
	return ready;
}

static int
export_fd(struct VkDevice_T *device, VkFence fence)
{
	VkFenceGetFdInfoKHR info;
	VkResult status;
	int fd;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
	info.fence = fence;
	info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	status = vkGetFenceFdKHR((VkDevice)device, &info, &fd);
	assert(status == VK_SUCCESS);
	return fd;
}

static void
import_fd(struct VkDevice_T *device, VkFence fence, int fd, VkFenceImportFlags flags)
{
	VkImportFenceFdInfoKHR info;
	VkResult status;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR;
	info.fence = fence;
	info.fd = fd;
	info.flags = flags;
	info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	status = vkImportFenceFdKHR((VkDevice)device, &info);
	assert(status == VK_SUCCESS);
}

int
main(void)
{
	struct vulkan_context context;
	struct VkDevice_T device;
	struct VkQueue_T queue;
	VkExportFenceCreateInfo export;
	VkFenceCreateInfo create;
	VkImportFenceFdInfoKHR invalid;
	VkFence source;
	VkFence alias;
	VkFence temporary;
	VkResult status;
	struct kernel_payload *shared;
	uint64_t generation;
	unsigned submits;
	unsigned index;
	int fd;
	int retained;

	memset(&context, 0, sizeof(context));
	memset(&device, 0, sizeof(device));
	memset(&queue, 0, sizeof(queue));
	context.fd = 61;
	context.capabilities = GPU_CAP_FENCE | GPU_CAP_JOB;
	queue.timeline_index = 1;
	device.object.context = &context;
	device.object.wire_id = 1000;
	queue.object.context = &context;
	queue.object.wire_id = 1001;
	queue.device = &device;
	pthread_mutex_init(&context.mutex, NULL);
	pthread_mutex_init(&device.mutex, NULL);
	pthread_mutex_init(&queue.mutex, NULL);
	memset(&export, 0, sizeof(export));
	export.sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO;
	export.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	create.pNext = &export;
	create.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	status = vkCreateFence((VkDevice)&device, &create, NULL, &source);
	assert(status == VK_SUCCESS);
	create.flags = 0;
	status = vkCreateFence((VkDevice)&device, &create, NULL, &alias);
	assert(status == VK_SUCCESS);
	fd = export_fd(&device, source);
	shared = descriptors[fd];
	retained = fence_test_fcntl(fd, F_DUPFD_CLOEXEC, 0);
	import_fd(&device, alias, fd, 0);
	status = vkGetFenceStatus((VkDevice)&device, alias);
	assert(status == VK_SUCCESS);
	generation = shared->generation;
	status = vkResetFences((VkDevice)&device, 1, &alias);
	assert(status == VK_SUCCESS && shared->generation == generation + 1);
	status = vkGetFenceStatus((VkDevice)&device, source);
	assert(status == VK_NOT_READY);
	/* A replaced generation wakes a retry, preserving the original deadline rather than losing the device. */
	stale_wait = 1;
	status = vkWaitForFences((VkDevice)&device, 1, &alias, VK_TRUE, UINT64_C(2000000000));
	assert(status == VK_SUCCESS && !stale_wait && context.error == VK_SUCCESS);
	status = vkResetFences((VkDevice)&device, 1, &alias);
	assert(status == VK_SUCCESS);
	shared->native = vulkan_sync_object((uint64_t)(uintptr_t)source)->object.wire_id;
	status = vkQueueSubmit((VkQueue)&queue, 0, NULL, source);
	assert(status == VK_SUCCESS);
	status = vkWaitForFences((VkDevice)&device, 1, &alias, VK_TRUE, UINT64_C(2000000000));
	assert(status == VK_SUCCESS && signals == 1);

	/* Alias reset must also invalidate the producer's old native signaled payload on next submit. */
	status = vkResetFences((VkDevice)&device, 1, &alias);
	assert(status == VK_SUCCESS);
	submits = peer.calls[37];
	status = vkQueueSubmit((VkQueue)&queue, 0, NULL, source);
	assert(status == VK_SUCCESS && peer.calls[37] == submits + 1);
	status = vkWaitForFences((VkDevice)&device, 1, &alias, VK_TRUE, UINT64_C(2000000000));
	assert(status == VK_SUCCESS && signals == 2);

	/* Temporary reset affects restored permanent state rather than the shared imported payload. */
	create.pNext = NULL;
	create.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	status = vkCreateFence((VkDevice)&device, &create, NULL, &temporary);
	assert(status == VK_SUCCESS);
	fd = export_fd(&device, source);
	import_fd(&device, temporary, fd, VK_FENCE_IMPORT_TEMPORARY_BIT);
	status = vkResetFences((VkDevice)&device, 1, &temporary);
	assert(status == VK_SUCCESS);
	status = vkGetFenceStatus((VkDevice)&device, temporary);
	assert(status == VK_NOT_READY);
	status = vkGetFenceStatus((VkDevice)&device, alias);
	assert(status == VK_SUCCESS);

	/* Native rejection releases its binding without exposing a false shared ERROR or generation. */
	status = vkResetFences((VkDevice)&device, 1, &alias);
	assert(status == VK_SUCCESS);
	generation = shared->generation;
	peer.fail_opcode = 18;
	peer.fail_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	status = vkQueueSubmit((VkQueue)&queue, 0, NULL, source);
	assert(status == VK_ERROR_OUT_OF_DEVICE_MEMORY);
	assert(!shared->bound && shared->state == GPU_FENCE_PENDING && shared->generation == generation);
	assert(rollbacks == 1);
	peer.fail_opcode = 0;
	peer.fail_result = VK_SUCCESS;

	/* Binding-allocation rejection precedes the native QueueSubmit side effect. */
	reject_bind = 1;
	submits = peer.calls[18];
	status = vkQueueSubmit((VkQueue)&queue, 0, NULL, source);
	assert(status == VK_ERROR_OUT_OF_DEVICE_MEMORY && peer.calls[18] == submits);
	reject_bind = 0;

	/* Invalid import leaves ownership with the application and preserves the existing payload. */
	memset(&invalid, 0, sizeof(invalid));
	invalid.sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR;
	invalid.fence = alias;
	invalid.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT;
	invalid.fd = 99;
	status = vkImportFenceFdKHR((VkDevice)&device, &invalid);
	assert(status == VK_ERROR_INVALID_EXTERNAL_HANDLE);
	status = vkQueueSubmit((VkQueue)&queue, 0, NULL, source);
	assert(status == VK_SUCCESS);
	status = vkWaitForFences((VkDevice)&device, 1, &alias, VK_TRUE, UINT64_C(2000000000));
	assert(status == VK_SUCCESS);

	/* A renderer DEVICE_LOST wake must publish kernel ERROR, never a successful shared fence. */
	status = vkResetFences((VkDevice)&device, 1, &alias);
	assert(status == VK_SUCCESS);
	test_job_error = EIO;
	status = vkQueueSubmit((VkQueue)&queue, 0, NULL, source);
	assert(status == VK_SUCCESS);
	status = vkWaitForFences((VkDevice)&device, 1, &alias, VK_TRUE, UINT64_C(2000000000));
	assert(status == VK_ERROR_DEVICE_LOST && shared->state == GPU_FENCE_ERROR);

	/* Exported references outlive all originating VkFence objects and close independently. */
	vkDestroyFence((VkDevice)&device, source, NULL);
	status = vkGetFenceStatus((VkDevice)&device, alias);
	assert(status == VK_ERROR_DEVICE_LOST);
	vkDestroyFence((VkDevice)&device, alias, NULL);
	vkDestroyFence((VkDevice)&device, temporary, NULL);
	assert(shared->references == 1 && descriptors[retained] == shared);
	assert(worker_creations == 0);
	fence_test_close(retained);
	for (index = 0; index < payload_count; index++)
		assert(payloads[index].references == 0 && !payloads[index].bound);

	pthread_mutex_destroy(&queue.mutex);
	pthread_mutex_destroy(&device.mutex);
	pthread_mutex_destroy(&context.mutex);
	puts("libvulkan external fence: references, aliases, temporary restore, actual native signal, lazy reset, bind/native failure rollback, native DEVICE_LOST error signal PASS");
	return 0;
}

/* Publishes only exact job results, then drops the reservation's independent strong reference. */
static void
kernel_jobs_progress(
	void)
{
	struct sync_test_job *job;
	struct kernel_payload *payload;
	unsigned index;

	/* Each kernel reservation can retire its payload hold exactly once. */
	for (index = 0; index < test_job_count; index++) {
		payload = job_payloads[index];
		if (payload == NULL)
			continue;

		/* Descriptor reuse cannot replace the object pinned when the generation was reserved. */
		job = &test_jobs[index];
		test_job_observe(job);
		if (!job->terminal && !job->consumed)
			continue;

		/* Native rejection leaves pending state and its generation unchanged. */
		assert(payload->generation == job->generation && payload->bound == 2);
		if (!job->terminal) {
			rollbacks++;
		} else if (job->error != 0) {
			payload->state = GPU_FENCE_ERROR;
		} else {
			/* Success requires the actual native fence named by this submission. */
			assert(job->native != 0 && peer.objects[job->native].signaled);
			payload->state = GPU_FENCE_SIGNALED;
			signals++;
		}

		/* Kernel completion releases its own strong hold after publishing the terminal payload. */
		payload->bound = 0;
		payload->references--;
		job_payloads[index] = NULL;
		pthread_cond_broadcast(&kernel_condition);
	}

	/* Succeeded: no userspace completion worker was needed to update shared payloads. */
	return;
}
