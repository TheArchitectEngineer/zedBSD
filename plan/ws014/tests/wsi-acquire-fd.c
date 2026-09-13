/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Tests production acquisition with independent fd readiness and native image ownership. */
#define WSI_SHARED_ENTRY legacy_shared_main
#include "wayland-wsi-swapchain.c"

#include <poll.h>
#include <signal.h>

/* One joined thread owns its public acquisition call and result storage. */
struct acquire_case {
	VkSwapchainKHR chain;
	uint64_t timeout;
	VkResult result;
	uint32_t image;
	int wake_fd;
};

/* The main test publishes native availability independently from production image states. */
static int fd_available;
/* Pipes model renderer and independent display descriptor lifetimes. */
static int renderer_pipe[2];
/* The display model owns a distinct fd so renderer and surface failures remain separate. */
static int display_pipe[2];
/* A finite handshake distinguishes sleeping calls from synchronous success. */
static unsigned fd_waits;
/* fence_mutex protects the number of calls registered at the actual wait boundary. */
static unsigned fd_entered;
/* The sequential main thread selects one deterministic pre-ppoll publication. */
static int fd_inject;
/* fence_mutex protects the independently retained wake fd numbers seen by concurrent calls. */
static int fd_seen[4];
/* The main thread retains this renderer namespace until all valid calls and native images retire. */
static struct vulkan_context fd_context;
/* Atomic creation counts distinguish real blocking from immediately reusable images. */
static unsigned fd_pipe_creations;

int __real_pipe2(int descriptors[2], int flags);
int __real_ppoll(struct pollfd *descriptors, nfds_t count, const struct timespec *timeout, const sigset_t *mask);
static int fd_descriptor(void *lease);
static VkBool32 fd_image_available(void *image);
static VkResult fd_progress(void *lease);
static void *fd_acquire(void *argument);
static void fd_await(unsigned count);
static VkSwapchainKHR fd_chain(VkSurfaceKHR surface);
static void fd_cleanup(VkSwapchainKHR chain);

/* Only native readiness is substituted; all swapchain image and error handling is production code. */
static const struct vulkan_wsi_platform_ops fd_platform = {
	native_capabilities, native_formats, native_modes,
	shared_claim, shared_release, NULL, native_wait, NULL,
	shared_import, shared_present, fd_progress, fd_image_available,
	shared_destroy_image, NULL, NULL, NULL, fd_descriptor
};

/*
 * Verifies independent wake tokens, exact timeout policy and no worker-dependent failure progress.
 */
int
main(void)
{
	VkDisplayPropertiesKHR output;
	VkSurfaceKHR surface_handle;
	struct vulkan_surface *surface;
	struct acquire_case cases[2];
	pthread_t threads[2];
	VkSwapchainKHR chain;
	VkResult status;
	uint32_t count;
	unsigned index;
	int result;
	int reused;

	/* The independent renderer model supplies ordinary object and allocation collaborators. */
	memset(&instance, 0, sizeof(instance));
	memset(&physical, 0, sizeof(physical));
	memset(&device, 0, sizeof(device));
	memset(&queue, 0, sizeof(queue));
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.pfnAllocation = test_allocate;
	callbacks.pfnFree = test_free;
	instance.object.kind = VULKAN_OBJECT_INSTANCE;
	physical.object.kind = VULKAN_OBJECT_PHYSICAL_DEVICE;
	physical.instance = &instance;
	physical.object.context = &fd_context;
	device.object.kind = VULKAN_OBJECT_DEVICE;
	device.object.context = &fd_context;
	device.physical = &physical;
	queue.object.kind = VULKAN_OBJECT_QUEUE;
	queue.device = &device;
	family.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT;
	physical.queue_families = &family;
	physical.queue_family_count = 1;
	physical.memory.memoryTypeCount = 1;
	physical.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	fd_context.capabilities = GPU_CAP_SHARE;
	result = pipe2(renderer_pipe, O_CLOEXEC | O_NONBLOCK);
	assert(result == 0);
	result = pipe2(display_pipe, O_CLOEXEC | O_NONBLOCK);
	assert(result == 0);
	fd_context.fd = renderer_pipe[0];
	pthread_mutex_init(&fd_context.mutex, NULL);
	pthread_mutex_init(&device.mutex, NULL);
	pthread_mutex_init(&queue.mutex, NULL);
	queues[0] = &queue;
	device.queues = queues;
	device.queue_count = 1;

	/* Every case owns a real swapchain and lease until its acquiring threads have joined. */
	count = 1;
	status = vkGetPhysicalDeviceDisplayPropertiesKHR((VkPhysicalDevice)&physical, &count, &output);
	assert(status == VK_INCOMPLETE);
	surface_handle = test_surface(0, output.display);
	surface = vulkan_wsi_surface(surface_handle);
	assert(surface != NULL);
	surface->platform = &fd_platform;

	/* Already reusable images do not allocate wake storage or enter any blocking syscall. */
	chain = fd_chain(surface_handle);
	__atomic_store_n(&fd_available, 1, __ATOMIC_RELEASE);
	status = vkAcquireNextImageKHR((VkDevice)&device, chain, UINT64_MAX, VK_NULL_HANDLE, (VkFence)1, &count);
	assert(status == VK_SUCCESS && fd_waits == 0 && fd_pipe_creations == 2);
	fd_cleanup(chain);

	/* A release occurring after predicate observation but before ppoll must leave a readable token. */
	chain = fd_chain(surface_handle);
	fd_inject = 1;
	status = vkAcquireNextImageKHR((VkDevice)&device, chain, UINT64_MAX, VK_NULL_HANDLE, (VkFence)1, &count);
	assert(status == VK_SUCCESS && fd_waits == 1 && acquire_waits == 0);
	fd_cleanup(chain);

	/* A finite direct deadline reaches ppoll unchanged rather than being divided into 10ms quanta. */
	chain = fd_chain(surface_handle);
	status = vkAcquireNextImageKHR((VkDevice)&device, chain, UINT64_C(500000000), VK_NULL_HANDLE, (VkFence)1, &count);
	assert(status == VK_TIMEOUT && fd_waits == 2 && acquire_waits == 0);
	fd_cleanup(chain);

	/* Two simultaneous callers retain distinct wake descriptors while waiting for one shared error. */
	chain = fd_chain(surface_handle);
	memset(cases, 0, sizeof(cases));
	for (index = 0; index < 2; index++) {
		cases[index].chain = chain;
		cases[index].timeout = UINT64_MAX;
		result = pthread_create(&threads[index], NULL, fd_acquire, &cases[index]);
		assert(result == 0);
	}

	/* Context loss wakes both pipes even though no presentation worker or native frame exists. */
	fd_await(2);
	assert(fd_seen[0] != fd_seen[1]);
	vulkan_context_error(&fd_context, VK_ERROR_DEVICE_LOST);
	for (index = 0; index < 2; index++) {
		result = pthread_join(threads[index], NULL);
		assert(result == 0 && cases[index].result == VK_ERROR_DEVICE_LOST);
	}

	/* Retired pipe numbers may be reused without a late publisher writing into the replacement fd. */
	reused = open("/dev/null", O_RDONLY | O_CLOEXEC);
	assert(reused >= 0);
	vulkan_context_error(&fd_context, VK_ERROR_DEVICE_LOST);
	result = fcntl(reused, F_GETFD);
	assert(result >= 0);
	close(reused);
	fd_cleanup(chain);

	/* A fresh modeled namespace separates the next independent kernel-error scenario. */
	__atomic_store_n(&fd_context.error, VK_SUCCESS, __ATOMIC_RELEASE);
	__atomic_store_n(&device.error, VK_SUCCESS, __ATOMIC_RELEASE);
	chain = fd_chain(surface_handle);
	memset(&cases[0], 0, sizeof(cases[0]));
	cases[0].chain = chain;
	cases[0].timeout = UINT64_MAX;
	fd_entered = 0;
	result = pthread_create(&threads[0], NULL, fd_acquire, &cases[0]);
	assert(result == 0);
	fd_await(1);
	close(renderer_pipe[1]);
	result = pthread_join(threads[0], NULL);
	assert(result == 0 && cases[0].result == VK_ERROR_DEVICE_LOST);
	fd_cleanup(chain);

	/* Every native allocation and lease retires after the last acquisition owner. */
	vkDestroySurfaceKHR((VkInstance)&instance, surface_handle, NULL);
	vulkan_wsi_instance_finish(&instance);
	close(renderer_pipe[0]);
	close(display_pipe[0]);
	close(display_pipe[1]);
	pthread_mutex_destroy(&queue.mutex);
	pthread_mutex_destroy(&device.mutex);
	pthread_mutex_destroy(&fd_context.mutex);
	assert(imported_images == destroyed_images);

	/* Succeeded: real direct acquisition used notification ownership rather than a monitor thread. */
	puts("Direct Acquire: immediate acquisition creates zero fds, pre-wait release, finite deadline, distinct waiter tokens, context error, GPU fd HUP and descriptor retirement PASS");
	return 0;
}

/*
 * Counts real pipe allocation without replacing its fd lifetime or readiness behavior.
 */
int
__wrap_pipe2(
	int descriptors[2],
	int flags)
{
	int error;

	/* The production direct wait uses private nonblocking descriptors without an exec inheritance gap. */
	assert(flags == (O_CLOEXEC | O_NONBLOCK));
	error = __real_pipe2(descriptors, flags);
	if (error != 0)
		return error;

	/* Succeeded: one additional blocked caller owns its two real fd references. */
	__atomic_fetch_add(&fd_pipe_creations, 1, __ATOMIC_RELAXED);
	return 0;
}

/*
 * Observes actual ppoll framing and injects one deterministic pre-sleep ownership transition.
 */
int
__wrap_ppoll(
	struct pollfd *descriptors,
	nfds_t count,
	const struct timespec *timeout,
	const sigset_t *mask)
{
	int result;

	/* Direct acquisition must ignore ordinary completion POLLIN retained by unrelated waiters. */
	assert(count == 3 && descriptors[0].fd == fd_context.fd && descriptors[0].events == 0);
	assert(descriptors[1].fd == display_pipe[0] && descriptors[1].events == POLLPRI);
	assert(descriptors[2].events == POLLIN && mask == NULL);
	result = pthread_mutex_trylock(&queue.mutex);
	assert(result == 0);
	pthread_mutex_unlock(&queue.mutex);
	result = pthread_mutex_trylock(&device.mutex);
	assert(result == 0);
	pthread_mutex_unlock(&device.mutex);
	result = pthread_mutex_trylock(&fd_context.mutex);
	assert(result == 0);
	pthread_mutex_unlock(&fd_context.mutex);

	/* The first injected release occurs exactly between the protected check and operating-system sleep. */
	__atomic_fetch_add(&fd_waits, 1, __ATOMIC_RELAXED);
	if (fd_inject != 0) {
		fd_inject = 0;
		__atomic_store_n(&fd_available, 1, __ATOMIC_RELEASE);
		vulkan_wsi_image_notify();
	}

	/* Thread registration is finite and independent from the production wake descriptor's contents. */
	pthread_mutex_lock(&fence_mutex);

	if (fd_entered < 4)
		fd_seen[fd_entered] = descriptors[2].fd;
	fd_entered++;
	pthread_cond_broadcast(&fence_condition);

	pthread_mutex_unlock(&fence_mutex);

	/* The real syscall proves that a publication before this call remains readable. */
	result = __real_ppoll(descriptors, count, timeout, mask);

	/* Preserve real interruption or descriptor failure for the production acquisition loop. */
	if (result < 0)
		return result;

	/* Succeeded: return the operating-system readiness result without fabricating a wake. */
	return result;
}

/* Returns the immutable display descriptor retained by the actual swapchain lease. */
static int
fd_descriptor(
	void *lease)
{
	assert(lease != NULL);

	/* Succeeded: independent node readiness remains separate from renderer errors. */
	return display_pipe[0];
}

/* Supplies native image ownership independently of application acquire state. */
static VkBool32
fd_image_available(
	void *image)
{
	int available;

	assert(image != NULL);
	available = __atomic_load_n(&fd_available, __ATOMIC_ACQUIRE);
	if (available == 0)
		return VK_FALSE;

	/* Succeeded: the native owner released this storage. */
	return VK_TRUE;
}

/* Models only native fd error interpretation; production acquisition owns all actual blocking. */
static VkResult
fd_progress(
	void *lease)
{
	struct pollfd descriptor;
	int result;

	assert(lease != NULL);
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.fd = fd_context.fd;
	result = poll(&descriptor, 1, 0);
	assert(result >= 0);
	if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
		vulkan_context_error(&fd_context, VK_ERROR_DEVICE_LOST);
		return VK_ERROR_DEVICE_LOST;
	}

	/* Succeeded: image availability is checked separately by the actual swapchain. */
	return VK_SUCCESS;
}

/* Executes one valid public acquisition while the main thread retains every referenced object. */
static void *
fd_acquire(
	void *argument)
{
	struct acquire_case *test;

	test = argument;
	test->result = vkAcquireNextImageKHR((VkDevice)&device, test->chain, test->timeout, VK_NULL_HANDLE, (VkFence)1, &test->image);

	/* Succeeded: pthread_join publishes the result to the independent observer. */
	return NULL;
}

/* Waits finitely until the expected acquisition calls have reached their actual fd boundary. */
static void
fd_await(
	unsigned count)
{
	struct timespec deadline;
	int result;

	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 3;
	pthread_mutex_lock(&fence_mutex);

	while (fd_entered < count) {
		result = pthread_cond_timedwait(&fence_condition, &fence_mutex, &deadline);
		assert(result == 0);
	}

	pthread_mutex_unlock(&fence_mutex);

	/* Succeeded: subsequent publication races only with the actual syscall registration. */
	return;
}

/* Creates ordinary GPU images while the native model retains them from acquisition. */
static VkSwapchainKHR
fd_chain(
	VkSurfaceKHR surface)
{
	VkSwapchainCreateInfoKHR create;
	VkSwapchainKHR chain;
	VkResult status;

	__atomic_store_n(&fd_available, 0, __ATOMIC_RELEASE);
	fd_entered = 0;
	create = test_create_info(surface, 3);
	status = vkCreateSwapchainKHR((VkDevice)&device, &create, NULL, &chain);
	assert(status == VK_SUCCESS);

	/* Succeeded: all imported images begin unavailable only because of the independent native hold. */
	return chain;
}

/* Destroys a joined case without relaxing the production swapchain lifetime contract. */
static void
fd_cleanup(
	VkSwapchainKHR chain)
{
	vkDestroySwapchainKHR((VkDevice)&device, chain, NULL);

	/* Succeeded: no acquisition thread borrows the retired lease or any of its fds. */
	return;
}
