/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises actual shared-image swapchain acquisition with the established
 * independent renderer fixture. The real Wayland peer is tested separately.
 */

#define vkCreateFence legacy_create_fence
#define vkDestroyFence legacy_destroy_fence
#define vkResetFences legacy_reset_fences
#define vkGetFenceFdKHR legacy_get_fence_fd
#define vulkan_context_lock legacy_context_lock
#define vulkan_context_unlock legacy_context_unlock
#define main legacy_discovery_main
#define vulkan_fences_wait legacy_fences_wait
#define vkCmdPipelineBarrier legacy_pipeline_barrier
#include "../../ws030/tests/wsi-discovery.c"
#undef vkCmdPipelineBarrier
#undef main
#undef vkCreateFence
#undef vkDestroyFence
#undef vkResetFences
#undef vkGetFenceFdKHR
#undef vulkan_context_lock
#undef vulkan_context_unlock
#undef vulkan_fences_wait

#include <uapi/gpu-fence.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

/* Retains one native import until its lease is destroyed. */
struct shared_test_image {
	struct shared_test_image *next;
	unsigned index;
	struct shared_test_lease *lease;
	int busy;
};

/* Tracks imported native objects independently of the actual swapchain states. */
struct shared_test_lease {
	struct shared_test_image *images;
};

/* Counts actual GPU-copy records and native presentations in this finite fixture. */
static unsigned gpu_copies;
/* Counts scaled GPU image operations after their independently checked opaque clear. */
static unsigned gpu_blits;
/* Counts opaque black GPU clears for the finite rectangle-composition oracle. */
static unsigned gpu_clears;
/* Counts native callbacks; the test reads it only while the fence gate blocks work or after queue idle. */
static unsigned native_presents;
/* Protects the finite producer gate and the idle-thread handshake observations. */
static pthread_mutex_t fence_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Wakes the finite gate and idle observers whenever their protected handshake changes. */
static pthread_cond_t fence_condition = PTHREAD_COND_INITIALIZER;
/* Keeps actual GPU completion pending until the main test explicitly opens the protected gate. */
static int fence_blocked;
/* Records that an accepted worker reached the independently blocked producer fence. */
static int fence_entered;
/* Records that the idle probe began before the main test releases producer completion. */
static int idle_entered;
/* Records completed queue drain under fence_mutex, distinct from enqueue receipt. */
static int idle_finished;
/* At most three concurrent jobs in this test own independently modeled shared generations. */
static VkFence cached_fences[8];
/* Retains every exported host descriptor number so teardown can prove exact closure. */
static int cached_fds[8];
/* Models each cached payload generation independently from the production job metadata. */
static uint64_t cached_generations[8];
/* Counts fence slots created by the real WSI, with a finite three-job concurrency oracle. */
static unsigned cached_count;
/* Counts actual descriptor exports; cache reuse must not export again for later frames. */
static unsigned exported_fences;
/* Counts native callbacks which independently validated a live exact-generation producer fd. */
static unsigned synchronized_frames;
/* Selects an ordinary shared-fence capability profile before any production object is created. */
static int shared_fence_test;

/* Records external acquire barriers only after an earlier successful submission. */
static unsigned external_acquires;

/* Records release barriers transferring completed GPU writes to the consumer domain. */
static unsigned external_releases;

/* Counts native imports so image indices are predictable independently of WSI state. */
static unsigned imported_images;

/* Counts native object retirement for the final ownership audit. */
static unsigned destroyed_images;

/* Simulates exactly the compositor's release event for one imported image. */
static int release_index = -1;

/* Keeps pacing progress distinct from image availability in the test oracle. */
static unsigned frame_progress;

/* Injects a native OOM after the queue has already consumed its waits. */
static int native_oom;

/* Injects terminal surface loss while otherwise reusable images still exist. */
static int surface_lost;

/* A failed enqueue must retry an image whose native layout is still undefined. */
static VkImageLayout expected_shared_layout;

/* One creation attempt rejects its second imported image; no per-frame retry may follow. */
static VkResult import_failure;

/* Counts backend import attempts independently from successful alias ownership. */
static unsigned import_attempts;

/* Counts copied-route setup, which must happen once before presentation. */
static unsigned copy_preparations;

/* Selects the separately exercised native copied fallback rather than the normal GPU route. */
static int fallback_test;

/* A selected physical requirement travels through the real shared-image allocation helper. */
static unsigned placement_profile;

/* Counts physical-condition allocation attempts separately from native import attempts. */
static unsigned placement_attempts;

/* An independently selected allocator refusal distinguishes unsupported backing from OOM. */
static VkResult placement_failure;

static void test_fallback(struct vulkan_surface *surface, VkSurfaceKHR surface_handle);
static VkResult fallback_prepare(void *private_lease, VkFormat format, VkExtent2D extent);
static VkResult fallback_placement(void *private_lease, struct gpu_placement *placement);
static VkResult shared_claim(struct vulkan_surface *surface, struct VkDevice_T *gpu, void **result);
static VkResult shared_release(void *private_lease);
static VkResult shared_import(void *private_lease, int fd, const struct gpu_image_descriptor *descriptor, void **result);
static VkResult shared_present(void *private_lease, void *private_image, VkPresentModeKHR mode, uint64_t *sequence);
static VkResult shared_progress(void *private_lease);
static VkBool32 shared_available(void *private_image);
static void shared_destroy_image(void *private_image);
static void *idle_probe(void *argument);
static VkResult shared_present_sync(void *private_lease, void *private_image, VkPresentModeKHR mode, uint64_t *sequence, int wait_fd, uint64_t generation);

/* Substitutes only the native compositor boundary; swapchain code remains real. */
static const struct vulkan_wsi_platform_ops shared_platform = {
	native_capabilities, native_formats, native_modes,
	shared_claim, shared_release, NULL, native_wait, NULL,
	shared_import, shared_present, shared_progress, shared_available, shared_destroy_image, NULL, shared_present_sync, NULL
};

/* Supplies both routes so real swapchain creation must choose and retain one. */
static const struct vulkan_wsi_platform_ops fallback_platform = {
	native_capabilities, native_formats, native_modes,
	shared_claim, shared_release, native_present, native_wait, NULL,
	shared_import, shared_present, shared_progress, shared_available,
	shared_destroy_image, fallback_prepare, shared_present_sync, fallback_placement
};

/*
 * Verifies real shared-image acquisition, external barriers and failure rollback.
 */
int
main(
	int argc,
	char **argv)
{
	struct vulkan_context context;
	VkDisplayPropertiesKHR display_properties;
	VkSurfaceKHR surface_handle;
	struct vulkan_surface *surface;
	VkSwapchainCreateInfoKHR create;
	VkSwapchainKHR chain;
	VkPresentInfoKHR present;
	VkResult per_chain;
	VkResult error;
	uint32_t count;
	uint32_t indices[3];
	uint32_t unused;
	uint32_t index;
	unsigned signals_before;
	unsigned submits_before;
	pthread_t idle_thread;
	struct timespec pause;
	VkDisplayPresentInfoKHR rectangles;
	int status;
	int variant;

	/* The alternate fixture mode advertises actual shared payload metadata to the production WSI path. */
	shared_fence_test = 0;
	fallback_test = 0;
	if (argc > 1) {
		/* Each finite process runs one ordinary public capability profile. */
		variant = strcmp(argv[1], "shared-fence");
		if (variant == 0)
			shared_fence_test = 1;
		else
			fallback_test = 1;
	}

	/* Initializes the original renderer mock's ordinary object/allocator contracts. */
	memset(&instance, 0, sizeof(instance));
	memset(&physical, 0, sizeof(physical));
	memset(&device, 0, sizeof(device));
	memset(&queue, 0, sizeof(queue));
	memset(&context, 0, sizeof(context));
	instance.object.kind = VULKAN_OBJECT_INSTANCE;
	physical.object.kind = VULKAN_OBJECT_PHYSICAL_DEVICE;
	physical.instance = &instance;
	physical.object.context = &context;
	device.object.kind = VULKAN_OBJECT_DEVICE;
	device.object.context = &context;
	device.physical = &physical;
	queue.object.kind = VULKAN_OBJECT_QUEUE;
	queue.device = &device;
	queue.family = 0;
	family.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT;
	physical.queue_families = &family;
	physical.queue_family_count = 1;
	physical.memory.memoryTypeCount = 1;
	physical.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	/* Explicit copied fallback requires separately advertised host-visible staging support. */
	if (fallback_test != 0) {
		physical.memory.memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}

	context.capabilities = GPU_CAP_SHARE;
	if (shared_fence_test != 0)
		context.capabilities |= GPU_CAP_FENCE;
	context.fd = 123;
	pthread_mutex_init(&context.mutex, NULL);
	queues[0] = &queue;
	device.queues = queues;
	device.queue_count = 1;
	pthread_mutex_init(&device.mutex, NULL);
	pthread_mutex_init(&queue.mutex, NULL);
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.pfnAllocation = test_allocate;
	callbacks.pfnFree = test_free;
	instance.object.allocator.has_callbacks = VK_TRUE;
	instance.object.allocator.callbacks = callbacks;
	device.object.allocator = instance.object.allocator;

	/* Creates real standard discovery/surface objects before selecting the native backend. */
	count = 1;
	error = vkGetPhysicalDeviceDisplayPropertiesKHR((VkPhysicalDevice)&physical, &count, &display_properties);
	assert(error == VK_INCOMPLETE);
	surface_handle = test_surface(0, display_properties.display);
	surface = vulkan_wsi_surface(surface_handle);
	assert(surface != NULL);
	/* The copied-only variant uses the same production discovery and swapchain implementation. */
	if (fallback_test != 0) {
		test_fallback(surface, surface_handle);
		return 0;
	}

	surface->platform = &shared_platform;
	create = test_create_info(surface_handle, 3);
	error = vkCreateSwapchainKHR((VkDevice)&device, &create, &callbacks, &chain);
	assert(error == VK_SUCCESS);
	assert(imported_images == 3);

	/* Acquires every application image before presenting any to the native owner. */
	for (index = 0; index < 3; index++) {
		error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0, VK_NULL_HANDLE, (VkFence)1, &indices[index]);
		assert(error == VK_SUCCESS);
		assert(indices[index] == index);
	}

	/* An enqueue failure leaves both application ownership and external layout unchanged. */
	memset(&present, 0, sizeof(present));
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.swapchainCount = 1;
	present.pSwapchains = &chain;
	present.pImageIndices = &indices[0];
	present.pResults = &per_chain;
	expected_waits = 0;
	expected_shared_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	fail_submit = 1;
	submits_before = submissions;
	error = vkQueuePresentKHR((VkQueue)&queue, &present);
	assert(error == VK_ERROR_OUT_OF_HOST_MEMORY);
	assert(submissions == submits_before);
	fail_submit = 0;
	fence_blocked = 1;
	error = vkQueuePresentKHR((VkQueue)&queue, &present);
	assert(error == VK_SUCCESS);
	assert(per_chain == VK_SUCCESS);

	/* Sends the remaining independently acquired images into compositor ownership. */
	for (index = 1; index < 3; index++) {
		present.pImageIndices = &indices[index];
		error = vkQueuePresentKHR((VkQueue)&queue, &present);
		assert(error == VK_SUCCESS);
	}

	/* Three accepted jobs return while the actual producer fence remains blocked. */
	pthread_mutex_lock(&fence_mutex);
	while (fence_entered == 0)
		pthread_cond_wait(&fence_condition, &fence_mutex);
	assert(native_presents == 0);
	pthread_mutex_unlock(&fence_mutex);

	/* Borrowed caller arrays may change immediately; queued jobs must retain independent indices and results. */
	indices[0] = 91U;
	indices[1] = 92U;
	indices[2] = 93U;
	per_chain = VK_ERROR_UNKNOWN;
	/* A separate caller must remain blocked in queue idle while the producer gate is closed. */
	status = pthread_create(&idle_thread, NULL, idle_probe, NULL);
	assert(status == 0);
	pthread_mutex_lock(&fence_mutex);
	while (idle_entered == 0)
		pthread_cond_wait(&fence_condition, &fence_mutex);
	pthread_mutex_unlock(&fence_mutex);
	pause.tv_sec = 0;
	pause.tv_nsec = 5000000L;
	nanosleep(&pause, NULL);
	pthread_mutex_lock(&fence_mutex);
	assert(idle_finished == 0);
	fence_blocked = 0;
	pthread_cond_broadcast(&fence_condition);
	pthread_mutex_unlock(&fence_mutex);
	/* Joining makes all independently observed native callback counts stable for inspection. */
	status = pthread_join(idle_thread, NULL);
	assert(status == 0);
	assert(native_presents == 3);
	assert(cached_count == 3U);

	/* Shared payloads add fd/generation validation without changing the number of cache slots. */
	if (shared_fence_test != 0)
		assert(exported_fences == 3U && synchronized_frames == 3U);
	assert(per_chain == VK_ERROR_UNKNOWN);

	/* Presented state alone is insufficient while the compositor retains every image. */
	signals_before = acquire_signals;
	error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0, VK_NULL_HANDLE, (VkFence)1, &unused);
	assert(error == VK_NOT_READY);
	error = vkAcquireNextImageKHR((VkDevice)&device, chain, 1000000, VK_NULL_HANDLE, (VkFence)1, &unused);
	assert(error == VK_TIMEOUT);
	assert(acquire_signals == signals_before);

	/* Frame callbacks advance pacing without authorizing reuse of any GPU allocation. */
	frame_progress++;
	error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0, VK_NULL_HANDLE, (VkFence)1, &unused);
	assert(error == VK_NOT_READY);
	assert(acquire_signals == signals_before);

	/* One compositor release permits exactly that image to be reacquired and rewritten. */
	release_index = 1;
	error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0, VK_NULL_HANDLE, (VkFence)1, &unused);
	assert(error == VK_SUCCESS);
	assert(unused == 1);
	assert(acquire_signals == signals_before + 1);
	expected_shared_layout = VK_IMAGE_LAYOUT_GENERAL;
	present.pImageIndices = &unused;
	memset(&rectangles, 0, sizeof(rectangles));
	rectangles.sType = VK_STRUCTURE_TYPE_DISPLAY_PRESENT_INFO_KHR;
	rectangles.srcRect.offset.x = 10;
	rectangles.srcRect.offset.y = 20;
	rectangles.srcRect.extent.width = 100U;
	rectangles.srcRect.extent.height = 80U;
	rectangles.dstRect.offset.x = 30;
	rectangles.dstRect.offset.y = 40;
	rectangles.dstRect.extent.width = 200U;
	rectangles.dstRect.extent.height = 160U;
	present.pNext = &rectangles;
	error = vkQueuePresentKHR((VkQueue)&queue, &present);
	assert(error == VK_SUCCESS);
	assert(external_acquires == 1);
	assert(copies == 0);
	assert(gpu_copies == 4);
	assert(gpu_blits == 1 && gpu_clears == 1);
	assert(external_releases == 5);

	error = vulkan_wsi_queue_idle(&queue);
	assert(error == VK_SUCCESS);
	present.pNext = NULL;

	/* A native allocation failure after submission cannot promise unchanged waits. */
	release_index = 2;
	error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0, VK_NULL_HANDLE, (VkFence)1, &unused);
	assert(error == VK_SUCCESS);
	native_oom = 1;
	error = vkQueuePresentKHR((VkQueue)&queue, &present);
	assert(error == VK_SUCCESS);
	assert(per_chain == VK_SUCCESS);
	error = vulkan_wsi_queue_idle(&queue);
	assert(error == VK_ERROR_DEVICE_LOST);
	assert(per_chain == VK_SUCCESS);
	error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0, VK_NULL_HANDLE, (VkFence)1, &unused);
	assert(error == VK_ERROR_DEVICE_LOST);
	assert(device.error == VK_ERROR_DEVICE_LOST);
	assert(context.error == VK_ERROR_DEVICE_LOST);
	vkDestroySwapchainKHR((VkDevice)&device, chain, &callbacks);
	assert(imported_images == destroyed_images);
	vkDestroySurfaceKHR((VkInstance)&instance, surface_handle, &callbacks);
	vulkan_wsi_device_finish(&device);
	vulkan_wsi_instance_finish(&instance);
	assert(allocations == releases);
	assert(device.object.first_child == NULL);
	pthread_mutex_destroy(&device.mutex);
	pthread_mutex_destroy(&queue.mutex);
	pthread_mutex_destroy(&device.object.context->mutex);
	assert(cached_count == 3U);

	/* Shared payloads add fd/generation validation without changing the number of cache slots. */
	if (shared_fence_test != 0) {
		assert(exported_fences == 3U && synchronized_frames == 5U);
		/* Locate the independently modeled capability corresponding to the borrowed production identity. */
	for (index = 0U; index < cached_count; index++) {
			/* Reused private fence descriptors survive jobs and close only with device teardown. */
			status = fcntl(cached_fds[index], F_GETFD);
			assert(status == -1 && errno == EBADF);
		}
	}

	/* Succeeded: the shared path uses GPU commands and preserves ownership rollback. */
	puts("PASS shared WSI swapchain: no readback, external barriers, three owned async jobs, delayed producer fence, idle drain, GPU crop/scale, release-gated acquire, late OOM");
	return 0;
}

/*
 * Supplies independently modeled format capabilities to the actual shared-image creator.
 */
VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties(
	VkPhysicalDevice gpu,
	VkFormat format,
	VkFormatProperties *properties)
{
	/* The fixture explicitly supports one linear sampled format without host visibility. */
	assert(gpu == (VkPhysicalDevice)&physical);
	assert(format == VK_FORMAT_R8G8B8A8_UNORM);
	memset(properties, 0, sizeof(*properties));
	properties->linearTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
	properties->optimalTilingFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT;

	/* Succeeded: the actual helper can request its transfer-capable linear image. */
	return;
}

/*
 * Supplies the actual image's row layout independently of WSI's descriptor builder.
 */
VKAPI_ATTR void VKAPI_CALL
vkGetImageSubresourceLayout(
	VkDevice gpu,
	VkImage image,
	const VkImageSubresource *subresource,
	VkSubresourceLayout *layout)
{
	/* The fixture image obeys the queried geometry and one color subresource. */
	assert(gpu == (VkDevice)&device);
	assert(vulkan_image(image) != NULL);
	assert(subresource->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	memset(layout, 0, sizeof(*layout));
	layout->rowPitch = 1280;
	layout->size = 1280 * 240;

	/* Succeeded: no packed-pixel fallback was needed by the real descriptor builder. */
	return;
}

/*
 * Models the renderer's shared memory allocation through the established object fixture.
 */
VkResult
vulkan_memory_allocate(
	VkDevice gpu,
	const VkMemoryAllocateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkBool32 shared,
	VkDeviceMemory *memory)
{
	VkResult error;

	/* The actual shared-image helper must request explicitly exportable storage. */
	assert(shared == VK_TRUE);
	error = vkAllocateMemory(gpu, info, allocator, memory);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: the shared image still owns ordinary Vulkan memory independently. */
	return VK_SUCCESS;
}

/*
 * Verifies physical conditions reaching shared-image allocation without replacing swapchain routing.
 */
VkResult
vulkan_memory_allocate_placed(
	VkDevice gpu,
	const VkMemoryAllocateInfo *info,
	const VkAllocationCallbacks *allocator,
	const struct gpu_placement *placement,
	VkDeviceMemory *memory)
{
	VkResult error;

	/* Physical metadata must arrive before any image allocation capability is exported. */
	if (placement_profile != 0U) {
		assert(placement != NULL);
		assert(placement->flags == (GPU_PLACEMENT_DMA32 | GPU_PLACEMENT_COHERENT));
		assert(placement->max_dma_address == UINT32_MAX);
		assert(placement->alignment == 0U && placement->reserved == 0U);
		placement_attempts++;

		/* An allocator can refuse physical requirements without pretending an image was created. */
		if (placement_failure != VK_SUCCESS)
			return placement_failure;
	}

	/* Existing object ownership remains supplied by the independent renderer allocation peer. */
	error = vulkan_memory_allocate(gpu, info, allocator, VK_TRUE, memory);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: this shared image retains the same ordinary allocation and fd cleanup contract. */
	return VK_SUCCESS;
}

/*
 * Models only the fd-export boundary without pretending to export real GPU memory.
 */
VkResult
vulkan_memory_image_fd(
	struct VkDevice_T *gpu,
	VkDeviceMemory memory,
	struct gpu_image_descriptor *descriptor,
	int *fd)
{
	struct vulkan_memory *allocation;

	/* Immutable allocation size comes from the actual image requirement fixture. */
	assert(gpu == &device);
	allocation = vulkan_memory(memory);
	assert(allocation != NULL);
	descriptor->allocation_bytes = allocation->bytes;
	descriptor->memory_type = 0;
	descriptor->device_id = 1;
	*fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
	assert(*fd >= 0);

	/* Succeeded: the native boundary owns only the descriptor duplication contract. */
	return VK_SUCCESS;
}

/*
 * Checks external resource ownership barriers against independently chosen expectations.
 */
VKAPI_ATTR void VKAPI_CALL
vkCmdPipelineBarrier(
	VkCommandBuffer command,
	VkPipelineStageFlags source_stage,
	VkPipelineStageFlags target_stage,
	VkDependencyFlags flags,
	uint32_t memory_count,
	const VkMemoryBarrier *memories,
	uint32_t buffer_count,
	const VkBufferMemoryBarrier *buffers,
	uint32_t image_count,
	const VkImageMemoryBarrier *images)
{
	(void)source_stage;
	(void)target_stage;

	/* Copied fallback obeys the existing independent staging and host-access barrier oracle. */
	if (fallback_test != 0) {
		legacy_pipeline_barrier(command, source_stage, target_stage, flags, memory_count, memories, buffer_count, buffers, image_count, images);
		return;
	}

	/* The shared path requires image barriers and no host readback buffer barriers. */
	assert(command != VK_NULL_HANDLE);
	assert(flags == 0);
	assert(memory_count == 0);
	assert(memories == NULL);
	assert(buffer_count == 0);
	assert(buffers == NULL);
	/* A GPU clear is ordered before the blit by one transfer-write destination barrier. */
	if (image_count == 1U) {
		assert(gpu_clears == 1U && gpu_blits == 0U);
		assert(images[0].srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
		assert(images[0].dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
		return;
	}

	assert(image_count == 2);
	assert(images[0].srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
	assert(images[0].dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);

	/* The first barrier acquires only a previously exported and released image. */
	if (images[0].oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
		assert(images[1].oldLayout == expected_shared_layout);
		assert(images[1].newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		/* First use has no previous external owner; enqueue rollback preserves that. */
		if (expected_shared_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
			assert(images[1].srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
			assert(images[1].dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
		} else {
			assert(images[1].srcQueueFamilyIndex == 0xfffffffeU);
			assert(images[1].dstQueueFamilyIndex == queue.family);
			external_acquires++;
		}
	} else {
		/* Every completed copy restores the app image and releases external storage. */
		assert(images[0].newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		assert(images[1].oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		assert(images[1].newLayout == VK_IMAGE_LAYOUT_GENERAL);
		assert(images[1].srcQueueFamilyIndex == queue.family);
		assert(images[1].dstQueueFamilyIndex == 0xfffffffeU);
		external_releases++;
	}

	/* Succeeded: independently checked barriers represent a valid producer handoff. */
	return;
}

/*
 * Checks the GPU-only image copy without mapping or fabricating image pixels.
 */
VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImage(
	VkCommandBuffer command,
	VkImage source,
	VkImageLayout source_layout,
	VkImage destination,
	VkImageLayout destination_layout,
	uint32_t count,
	const VkImageCopy *regions)
{
	/* The image pair must remain distinct ordinary GPU image identities. */
	assert(command != VK_NULL_HANDLE);
	assert(source != destination);
	assert(vulkan_image(source) != NULL);
	assert(vulkan_image(destination) != NULL);
	assert(source_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	assert(destination_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	assert(count == 1);
	assert(regions[0].extent.width == 320);
	assert(regions[0].extent.height == 240);
	gpu_copies++;

	/* Succeeded: no CPU readback or upload participates in this mock GPU boundary. */
	return;
}

/*
 * A finite producer gate proves that enqueue receipt is not native presentation completion.
 */
VkResult
vulkan_fences_wait(
	struct VkDevice_T *gpu,
	uint32_t count,
	const VkFence *fences,
	VkBool32 all,
	uint64_t timeout)
{
	struct timespec deadline;
	int status;

	assert(gpu == &device && count == 1U && fences[0] != VK_NULL_HANDLE && all != VK_FALSE);
	assert(timeout != 0U);
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 5;
	pthread_mutex_lock(&fence_mutex);
	fence_entered = 1;
	pthread_cond_broadcast(&fence_condition);
	/* A finite deadline makes a missing enqueue/wakeup fail instead of hanging the test process. */
	while (fence_blocked != 0) {
		/* Only the test's explicit completion release may finish this producer wait successfully. */
		status = pthread_cond_timedwait(&fence_condition, &fence_mutex, &deadline);
		assert(status == 0);
	}

	pthread_mutex_unlock(&fence_mutex);

	/* Succeeded: actual producer completion now permits native image consumption. */
	return VK_SUCCESS;
}

/*
 * The GPU destination receives black surround independently from the source image.
 */
VKAPI_ATTR void VKAPI_CALL
vkCmdClearColorImage(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout layout,
	const VkClearColorValue *color,
	uint32_t count,
	const VkImageSubresourceRange *ranges)
{
	assert(command != VK_NULL_HANDLE && image != VK_NULL_HANDLE);
	assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && count == 1U);
	assert(ranges[0].aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	assert(color->float32[0] == 0.0f && color->float32[1] == 0.0f);
	assert(color->float32[2] == 0.0f && color->float32[3] == 1.0f);
	gpu_clears++;

	/* Succeeded: the GPU clear matches the opaque copied fallback background. */
	return;
}

/*
 * Exact crop and scaled destination coordinates are checked without CPU composition.
 */
VKAPI_ATTR void VKAPI_CALL
vkCmdBlitImage(
	VkCommandBuffer command,
	VkImage source,
	VkImageLayout source_layout,
	VkImage destination,
	VkImageLayout destination_layout,
	uint32_t count,
	const VkImageBlit *regions,
	VkFilter filter)
{
	assert(command != VK_NULL_HANDLE && source != destination && count == 1U);
	assert(source_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && destination_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	assert(filter == VK_FILTER_NEAREST && gpu_clears == 1U);
	assert(regions[0].srcOffsets[0].x == 10 && regions[0].srcOffsets[0].y == 20);
	assert(regions[0].srcOffsets[1].x == 110 && regions[0].srcOffsets[1].y == 100);
	assert(regions[0].dstOffsets[0].x == 30 && regions[0].dstOffsets[0].y == 40);
	assert(regions[0].dstOffsets[1].x == 230 && regions[0].dstOffsets[1].y == 200);
	assert(regions[0].srcOffsets[1].z == 1 && regions[0].dstOffsets[1].z == 1);
	gpu_blits++;

	/* Succeeded: the GPU operation preserves the independently chosen crop and scale coordinates. */
	return;
}

/*
 * Every concurrent slot acquires one ordinary fence, optionally carrying the standard export request.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(
	VkDevice gpu,
	const VkFenceCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkFence *fence)
{
	const VkExportFenceCreateInfo *export;
	VkResult error;

	if (shared_fence_test != 0) {
		export = info->pNext;
		assert(export != NULL && export->sType == VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO);
		assert(export->handleTypes == VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT);
	}

	error = legacy_create_fence(gpu, info, allocator, fence);
	if (error != VK_SUCCESS)
		return error;
	assert(cached_count < 8U);
	cached_fences[cached_count] = *fence;
	cached_fds[cached_count] = -1;
	cached_generations[cached_count] = 1U;
	cached_count++;

	/* Succeeded: a new native fence starts with one independently modeled payload generation. */
	return VK_SUCCESS;
}

/*
 * Slot reuse advances the shared generation without allocating another completion object.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkResetFences(
	VkDevice gpu,
	uint32_t count,
	const VkFence *fences)
{
	unsigned index;

	assert(gpu == (VkDevice)&device && count == 1U);
	/* Locate the independently modeled capability corresponding to the borrowed production identity. */
	for (index = 0U; index < cached_count; index++) {
		/* Reset advances only the reused native fence's permanent shared payload. */
		if (cached_fences[index] == fences[0])
			break;
	}

	assert(index < cached_count);
	/* Each reuse creates a distinct pending generation without another native fence allocation. */
	cached_generations[index]++;

	/* Succeeded: later native requests must carry this exact updated generation. */
	return VK_SUCCESS;
}

/*
 * Export returns one real host fd so premature close is independently observable.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkGetFenceFdKHR(
	VkDevice gpu,
	const VkFenceGetFdInfoKHR *info,
	int *fd)
{
	unsigned index;

	assert(gpu == (VkDevice)&device && shared_fence_test != 0);
	assert(info->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT);
	/* Locate the independently modeled capability corresponding to the borrowed production identity. */
	for (index = 0U; index < cached_count; index++) {
		/* A public export names one existing native fence rather than creating a new one. */
		if (cached_fences[index] == info->fence)
			break;
	}

	assert(index < cached_count && cached_fds[index] == -1);
	cached_fds[index] = open("/dev/null", O_RDONLY | O_CLOEXEC);
	assert(cached_fds[index] >= 0);
	exported_fences++;
	*fd = cached_fds[index];

	/* Succeeded: the production queue slot owns this independently inspectable host descriptor. */
	return VK_SUCCESS;
}

/*
 * Native generation query is the only raw GPU operation modeled by this WSI-level fixture.
 */
int
ioctl(
	int fd,
	unsigned long command,
	...)
{
	struct gpu_fence_state *state;
	va_list arguments;
	unsigned index;

	assert(fd == 123 && command == GPU_FENCE_QUERY && shared_fence_test != 0);
	va_start(arguments, command);
	state = va_arg(arguments, struct gpu_fence_state *);
	va_end(arguments);
	assert(state->generation == 0U);
	/* Locate the independently modeled capability corresponding to the borrowed production identity. */
	for (index = 0U; index < cached_count; index++) {
		/* Exact descriptor identity resolves to the independently advanced kernel generation. */
		if (cached_fds[index] == state->fd)
			break;
	}

	assert(index < cached_count);
	state->generation = cached_generations[index];

	/* Succeeded: production captures the actual modeled generation rather than guessing it. */
	return 0;
}

/*
 * Shared generation observation serializes with the renderer's ordinary admission mutex.
 */
void
vulkan_context_lock(
	struct vulkan_context *context)
{
	assert(shared_fence_test != 0 && context->fd == 123);
	assert(pthread_mutex_lock(&context->mutex) == 0);
	return;
}

/*
 * Raw dependency observation cannot retain renderer admission into native presentation.
 */
void
vulkan_context_unlock(
	struct vulkan_context *context)
{
	assert(pthread_mutex_unlock(&context->mutex) == 0);
	return;
}

/*
 * Device teardown destroys every cached fence exactly once after the WSI worker has joined.
 */
VKAPI_ATTR void VKAPI_CALL
vkDestroyFence(
	VkDevice gpu,
	VkFence fence,
	const VkAllocationCallbacks *allocator)
{
	legacy_destroy_fence(gpu, fence, allocator);
	return;
}

/* Allocates an independent native lease for each actual swapchain object. */
static VkResult
shared_claim(
	struct vulkan_surface *surface,
	struct VkDevice_T *gpu,
	void **result)
{
	struct shared_test_lease *lease;

	(void)surface;

	/* Native lease ownership is independent of the renderer's common object registry. */
	assert(gpu == &device);
	lease = calloc(1, sizeof(*lease));
	assert(lease != NULL);
	*result = lease;

	/* Succeeded: the actual swapchain now owns a private native image list. */
	return VK_SUCCESS;
}

/* Releases every native image only when its owning swapchain lease retires. */
static VkResult
shared_release(
	void *private_lease)
{
	struct shared_test_lease *lease;
	struct shared_test_image *image;

	/* Counts all imported images independently of GPU resource destruction. */
	lease = private_lease;
	while (lease->images != NULL) {
		image = lease->images;
		lease->images = image->next;
		free(image);
		destroyed_images++;
	}

	free(lease);

	/* Succeeded: no native buffer outlives its actual swapchain lease. */
	return VK_SUCCESS;
}

/* Imports a descriptor into a separately tracked compositor-owned resource. */
static VkResult
shared_import(
	void *private_lease,
	int fd,
	const struct gpu_image_descriptor *descriptor,
	void **result)
{
	struct shared_test_lease *lease;
	struct shared_test_image *image;
	int flags;

	/* Reject only the second allocation, proving partial aliases retire before fallback allocation. */
	import_attempts++;
	if (import_failure != VK_SUCCESS && import_attempts == 2U)
		return import_failure;

	/* The production helper supplies a live fd and a renderer-derived row layout. */
	lease = private_lease;
	flags = fcntl(fd, F_GETFD);
	assert(flags >= 0);
	assert(descriptor->stride == 1280);
	assert(descriptor->allocation_bytes == 1280 * 240);
	image = calloc(1, sizeof(*image));
	assert(image != NULL);
	image->lease = lease;
	image->index = imported_images;
	imported_images++;
	image->next = lease->images;
	lease->images = image;
	*result = image;

	/* Succeeded: native state is initially available until its first presentation. */
	return VK_SUCCESS;
}

/* Retains a presentation image independently of queue-submit completion. */
static VkResult
shared_present(
	void *private_lease,
	void *private_image,
	VkPresentModeKHR mode,
	uint64_t *sequence)
{
	struct shared_test_image *image;

	/* Native rejection here occurs after actual WSI has consumed submission waits. */
	assert(private_lease != NULL);
	assert(mode == VK_PRESENT_MODE_FIFO_KHR);
	if (native_oom)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	/* The compositor owns the image until the distinct release event is observed. */
	image = private_image;
	assert(!image->busy);
	image->busy = 1;
	native_presents++;
	*sequence = frame_progress + 1;

	/* Succeeded: frame pacing and image availability remain separate state. */
	return VK_SUCCESS;
}

/* Applies only an explicitly injected compositor release to native image ownership. */
static VkResult
shared_progress(
	void *private_lease)
{
	struct shared_test_lease *lease;
	struct shared_test_image *image;

	/* A failed compositor prevents acquisition even if an image was otherwise free. */
	if (surface_lost)
		return VK_ERROR_SURFACE_LOST_KHR;

	/* Releases exactly one requested native image, preserving all others. */
	lease = private_lease;
	for (image = lease->images; image != NULL; image = image->next) {
		if ((int)image->index == release_index) {
			image->busy = 0;
			release_index = -1;
		}
	}

	/* Succeeded: unrelated frame progress did not alter any allocation hold. */
	return VK_SUCCESS;
}

/* Reports only the native compositor's independently retained allocation state. */
static VkBool32
shared_available(
	void *private_image)
{
	struct shared_test_image *image;

	/* The actual acquisition algorithm must combine this with application ownership. */
	image = private_image;
	if (image->busy)
		return VK_FALSE;

	/* Succeeded: this allocation is no longer retained by the compositor. */
	return VK_TRUE;
}

/* Idle must retain its caller until all previously accepted native jobs have completed. */
static void *
idle_probe(
	void *argument)
{
	VkResult error;

	(void)argument;
	pthread_mutex_lock(&fence_mutex);
	idle_entered = 1;
	pthread_cond_broadcast(&fence_condition);
	pthread_mutex_unlock(&fence_mutex);
	error = vulkan_wsi_queue_idle(&queue);
	assert(error == VK_SUCCESS);
	pthread_mutex_lock(&fence_mutex);
	idle_finished = 1;
	pthread_mutex_unlock(&fence_mutex);

	/* Succeeded: queue idle returned only after every accepted native job retired. */
	return NULL;
}

/* Native image destruction precedes renderer image retirement and the owning lease's final release. */
static void
shared_destroy_image(
	void *private_image)
{
	struct shared_test_image *image;
	struct shared_test_image **link;

	/* Remove only the exact alias owned by this chain before freeing the wrapper. */
	image = private_image;
	link = &image->lease->images;
	/* The independent lease list proves that this native alias was successfully imported once. */
	while (*link != image)
		link = &(*link)->next;
	*link = image->next;
	free(image);
	destroyed_images++;

	/* Succeeded: native alias ownership is retired before the source allocation is freed. */
	return;
}

/* A native synchronized adapter borrows a live fd for exactly the verified producer generation. */
static VkResult
shared_present_sync(
	void *private_lease,
	void *private_image,
	VkPresentModeKHR mode,
	uint64_t *sequence,
	int wait_fd,
	uint64_t generation)
{
	unsigned index;
	VkResult error;
	int status;

	/* The worker must not close or replace the exported descriptor before the native dependency consumes it. */
	assert(shared_fence_test != 0);
	status = fcntl(wait_fd, F_GETFD);
	assert(status >= 0);
	/* Locate the independently modeled capability corresponding to the borrowed production identity. */
	for (index = 0U; index < cached_count; index++) {
		/* Native presentation may use only the exact exported descriptor, not another slot. */
		if (cached_fds[index] == wait_fd)
			break;
	}

	assert(index < cached_count && generation == cached_generations[index]);
	synchronized_frames++;
	/* Ordinary native ownership rules still apply after the explicit dependency check. */
	error = shared_present(private_lease, private_image, mode, sequence);
	if (error != VK_SUCCESS)
		return error;

	/* Succeeded: this native frame used the current live producer generation. */
	return VK_SUCCESS;
}

/* Exercises a partial GPU import rollback and a stable copied route chosen once at creation. */
static void
test_fallback(
	struct vulkan_surface *surface,
	VkSurfaceKHR surface_handle)
{
	VkSwapchainCreateInfoKHR create;
	VkSwapchainKHR chain;
	VkPresentInfoKHR present;
	VkResult error;
	uint32_t index;
	uint32_t frame;

	/* Physical allocation OOM remains an error before any import or copy-route preparation. */
	surface->platform = &fallback_platform;
	create = test_create_info(surface_handle, 3U);
	placement_profile = 1U;
	placement_failure = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	error = vkCreateSwapchainKHR((VkDevice)&device, &create, &callbacks, &chain);
	assert(error == VK_ERROR_OUT_OF_DEVICE_MEMORY && chain == VK_NULL_HANDLE);
	assert(placement_attempts == 1U && import_attempts == 0U && copy_preparations == 0U);

	/* Unsupported physical backing selects the explicitly supported copy route during creation. */
	placement_failure = VK_ERROR_FEATURE_NOT_PRESENT;
	error = vkCreateSwapchainKHR((VkDevice)&device, &create, &callbacks, &chain);
	assert(error == VK_SUCCESS && placement_attempts == 2U);
	assert(import_attempts == 0U && copy_preparations == 1U);

	/* Later physical support does not trigger a new allocation or import while presenting this chain. */
	placement_failure = VK_SUCCESS;
	error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0U, VK_NULL_HANDLE, (VkFence)1, &index);
	assert(error == VK_SUCCESS);
	memset(&present, 0, sizeof(present));
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.swapchainCount = 1U;
	present.pSwapchains = &chain;
	present.pImageIndices = &index;
	expected_waits = 0U;
	error = vkQueuePresentKHR((VkQueue)&queue, &present);
	assert(error == VK_SUCCESS);
	error = vulkan_wsi_queue_idle(&queue);
	assert(error == VK_SUCCESS);
	assert(placement_attempts == 2U && import_attempts == 0U && copy_preparations == 1U);
	vkDestroySwapchainKHR((VkDevice)&device, chain, &callbacks);

	/* The remaining scenarios independently exercise import-time failure after allocation succeeds. */
	placement_profile = 0U;
	copy_preparations = 0U;
	copies = 0U;
	native_presents = 0U;

	/* An operational import error must abort instead of silently changing the presentation route. */
	surface->platform = &fallback_platform;
	create = test_create_info(surface_handle, 3U);
	import_failure = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	error = vkCreateSwapchainKHR((VkDevice)&device, &create, &callbacks, &chain);
	assert(error == VK_ERROR_OUT_OF_DEVICE_MEMORY);
	assert(chain == VK_NULL_HANDLE);
	assert(import_attempts == 2U);
	assert(imported_images == 1U && destroyed_images == 1U);
	assert(copy_preparations == 0U);

	/* Unsupported import after one success must retire all partial native aliases before choosing copy. */
	import_attempts = 0U;
	import_failure = VK_ERROR_FORMAT_NOT_SUPPORTED;
	error = vkCreateSwapchainKHR((VkDevice)&device, &create, &callbacks, &chain);
	assert(error == VK_SUCCESS);
	assert(import_attempts == 2U);
	assert(imported_images == 2U && destroyed_images == 2U);
	assert(copy_preparations == 1U);

	/* Later backend support changes cannot renegotiate this already established swapchain route. */
	import_failure = VK_SUCCESS;
	memset(&present, 0, sizeof(present));
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.swapchainCount = 1U;
	present.pSwapchains = &chain;
	present.pImageIndices = &index;
	expected_waits = 0U;

	/* Repeated frames use only copied staging and the one native allocation prepared during creation. */
	for (frame = 0U; frame < 2U; frame++) {
		/* Each presentation consumes an independently acquired public image. */
		error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0U, VK_NULL_HANDLE, (VkFence)1, &index);
		assert(error == VK_SUCCESS);

		/* The owned worker must finish its actual producer fence before exposing copied bytes. */
		error = vkQueuePresentKHR((VkQueue)&queue, &present);
		assert(error == VK_SUCCESS);
		error = vulkan_wsi_queue_idle(&queue);
		assert(error == VK_SUCCESS);
	}

	/* Native import was attempted only during creation, with no GPU-only commands on the chosen copy route. */
	assert(import_attempts == 2U && copy_preparations == 1U);
	assert(copies == 2U && native_presents == 2U);
	assert(gpu_copies == 0U && gpu_blits == 0U && gpu_clears == 0U);

	/* Ordinary public destruction retires all aliases, staging, workers, surfaces and allocation callbacks. */
	vkDestroySwapchainKHR((VkDevice)&device, chain, &callbacks);
	vkDestroySurfaceKHR((VkInstance)&instance, surface_handle, &callbacks);
	vulkan_wsi_device_finish(&device);
	vulkan_wsi_instance_finish(&instance);
	assert(imported_images == destroyed_images);
	assert(allocations == releases && device.object.first_child == NULL);
	pthread_mutex_destroy(&device.mutex);
	pthread_mutex_destroy(&queue.mutex);
	pthread_mutex_destroy(&device.object.context->mutex);

	/* Succeeded: only unsupported import selected a fully initialized, persistent copied route. */
	puts("PASS WSI route: partial native import cleanup, OOM abort, unsupported fallback at creation, no per-frame retry, exact teardown");
	return;
}

/* Supplies explicit physical requirements before the real WSI creates a shared image. */
static VkResult
fallback_placement(
	void *private_lease,
	struct gpu_placement *placement)
{
	(void)private_lease;

	/* These limits are independent from the allocator's selected result. */
	memset(placement, 0, sizeof(*placement));
	if (placement_profile != 0U) {
		placement->flags = GPU_PLACEMENT_DMA32 | GPU_PLACEMENT_COHERENT;
		placement->max_dma_address = UINT32_MAX;
	}

	/* Succeeded: the owned stack snapshot remains live through synchronous image construction. */
	return VK_SUCCESS;
}

/* Records copied storage preparation after all unsuccessful native import aliases have retired. */
static VkResult
fallback_prepare(
	void *private_lease,
	VkFormat format,
	VkExtent2D extent)
{
	/* This native callback validates preparation timing independently of production route state. */
	assert(private_lease != NULL);
	assert(format == VK_FORMAT_R8G8B8A8_UNORM);
	assert(extent.width == 320U && extent.height == 240U);
	assert(imported_images == destroyed_images);
	copy_preparations++;

	/* Succeeded: no later presentation is allowed to allocate or renegotiate native storage. */
	return VK_SUCCESS;
}
