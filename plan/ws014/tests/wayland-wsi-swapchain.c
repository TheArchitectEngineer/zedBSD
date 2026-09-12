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

#define main legacy_discovery_main
#define vkCmdPipelineBarrier legacy_pipeline_barrier
#include "../../ws030/tests/wsi-discovery.c"
#undef vkCmdPipelineBarrier
#undef main

#include <fcntl.h>
#include <unistd.h>

/* Retains one native import until its lease is destroyed. */
struct shared_test_image {
	struct shared_test_image *next;
	unsigned index;
	int busy;
};

/* Tracks imported native objects independently of the actual swapchain states. */
struct shared_test_lease {
	struct shared_test_image *images;
};

/* Counts actual GPU-copy records and native presentations in this finite fixture. */
static unsigned gpu_copies;

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

static VkResult shared_claim(struct vulkan_surface *surface, struct VkDevice_T *gpu, void **result);
static VkResult shared_release(void *private_lease);
static VkResult shared_import(void *private_lease, int fd, const struct gpu_image_descriptor *descriptor, void **result);
static VkResult shared_present(void *private_lease, void *private_image, VkPresentModeKHR mode, uint64_t *sequence);
static VkResult shared_progress(void *private_lease);
static VkBool32 shared_available(void *private_image);

/* Substitutes only the native compositor boundary; swapchain code remains real. */
static const struct vulkan_wsi_platform_ops shared_platform = {
	native_capabilities, native_formats, native_modes,
	shared_claim, shared_release, NULL, native_wait, NULL,
	shared_import, shared_present, shared_progress, shared_available
};

/*
 * Verifies real shared-image acquisition, external barriers and failure rollback.
 */
int
main(void)
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
	context.capabilities = GPU_CAP_SHARE;
	queues[0] = &queue;
	device.queues = queues;
	device.queue_count = 1;
	pthread_mutex_init(&device.mutex, NULL);
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
	error = vkQueuePresentKHR((VkQueue)&queue, &present);
	assert(error == VK_SUCCESS);
	assert(per_chain == VK_SUCCESS);

	/* Sends the remaining independently acquired images into compositor ownership. */
	for (index = 1; index < 3; index++) {
		present.pImageIndices = &indices[index];
		error = vkQueuePresentKHR((VkQueue)&queue, &present);
		assert(error == VK_SUCCESS);
	}

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
	error = vkQueuePresentKHR((VkQueue)&queue, &present);
	assert(error == VK_SUCCESS);
	assert(external_acquires == 1);
	assert(copies == 0);
	assert(gpu_copies == 5);
	assert(external_releases == 5);

	/* A native allocation failure after submission cannot promise unchanged waits. */
	release_index = 2;
	error = vkAcquireNextImageKHR((VkDevice)&device, chain, 0, VK_NULL_HANDLE, (VkFence)1, &unused);
	assert(error == VK_SUCCESS);
	native_oom = 1;
	error = vkQueuePresentKHR((VkQueue)&queue, &present);
	assert(error == VK_ERROR_DEVICE_LOST);
	assert(per_chain == VK_ERROR_DEVICE_LOST);
	assert(device.error == VK_ERROR_DEVICE_LOST);
	assert(context.error == VK_ERROR_DEVICE_LOST);
	vkDestroySwapchainKHR((VkDevice)&device, chain, &callbacks);
	assert(imported_images == destroyed_images);
	vkDestroySurfaceKHR((VkInstance)&instance, surface_handle, &callbacks);
	vulkan_wsi_instance_finish(&instance);
	assert(allocations == releases);
	assert(device.object.first_child == NULL);
	pthread_mutex_destroy(&device.mutex);

	/* Succeeded: the shared path uses GPU commands and preserves ownership rollback. */
	puts("PASS shared WSI swapchain: no readback, external barriers, enqueue rollback, release-gated acquire, timeout, consumed-wait OOM");
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
	properties->linearTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

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

	/* The shared path requires image barriers and no host readback buffer barriers. */
	assert(command != VK_NULL_HANDLE);
	assert(flags == 0);
	assert(memory_count == 0);
	assert(memories == NULL);
	assert(buffer_count == 0);
	assert(buffers == NULL);
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

	/* The production helper supplies a live fd and a renderer-derived row layout. */
	lease = private_lease;
	flags = fcntl(fd, F_GETFD);
	assert(flags >= 0);
	assert(descriptor->stride == 1280);
	assert(descriptor->allocation_bytes == 1280 * 240);
	image = calloc(1, sizeof(*image));
	assert(image != NULL);
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
