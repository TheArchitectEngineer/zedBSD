/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Verifies a real Venus allocation after its producer process has exited.
 * The private WSI allocation helpers are linked directly into this test only.
 * Readback is an independent pixel oracle, never the WSI presentation path.
 */

#include "wsi-internal.h"
#include <uapi/gpu-display.h>

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SHARE_EDGE 32U
#define SHARE_BYTES (SHARE_EDGE * SHARE_EDGE * 4U)
#define SHARE_TIMEOUT 10000000000ULL
#define SHARE_EXTERNAL_FAMILY 0xfffffffeU

/*
 * One process owns all of these Vulkan objects and an independent renderer context.
 * The producer exports only its image allocation; no device or queue is inherited.
 */
struct share_context {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t family;
	VkCommandPool pool;
	VkCommandBuffer command;
	VkFence fence;
	VkImage image;
	VkDeviceMemory image_memory;
	VkBuffer readback;
	VkDeviceMemory readback_memory;
	struct gpu_image_descriptor descriptor;
	const char *operation;
};

static int share_displays(void);
static VkResult share_open(struct share_context *context);
static VkResult share_close(struct share_context *context);
static VkResult share_begin(struct share_context *context);
static VkResult share_submit(struct share_context *context);
static VkResult share_produce(struct share_context *context, int *descriptor);
static VkResult share_receive(struct share_context *context, int *descriptor);
static VkResult share_buffer(struct share_context *context);
static VkResult share_verify(struct share_context *context);
static void share_barrier(struct share_context *context, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t source_family, uint32_t destination_family, VkAccessFlags source_access, VkAccessFlags destination_access, VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage);
static int share_send_fd(int socket, int descriptor);
static int share_receive_fd(int socket, int *descriptor);
static int share_producer(int socket);

/*
 * Imports and checks pixels only after observing the producing process's exit.
 */
int
main(void)
{
	struct share_context context;
	VkResult result;
	VkResult cleanup;
	pid_t producer;
	pid_t reaped;
	int sockets[2];
	int descriptor;
	int child_status;
	int error;

	/* Native discovery reports actual QEMU timing metadata before either process creates Vulkan objects. */
	error = share_displays();
	if (error != 0) {
		perror("gpu-share-client: native display inventory");
		return 1;
	}

	/* Fork occurs before any Vulkan instance or renderer context exists. */
	error = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	if (error != 0) {
		perror("gpu-share-client: socketpair");
		return 1;
	}

	/* The child exclusively owns producer-side Vulkan objects. */
	producer = fork();
	if (producer < 0) {
		perror("gpu-share-client: fork");
		close(sockets[0]);
		close(sockets[1]);
		return 1;
	}

	/* Child exit follows explicit Vulkan cleanup and closure of its export fd. */
	if (producer == 0) {
		close(sockets[0]);
		error = share_producer(sockets[1]);
		close(sockets[1]);
		_exit(error);
	}

	/* SCM_RIGHTS is the only allocation authority carried between the processes. */
	close(sockets[1]);
	descriptor = -1;
	error = share_receive_fd(sockets[0], &descriptor);
	close(sockets[0]);

	/* A successful wait proves import happens after producer process teardown. */
	reaped = waitpid(producer, &child_status, 0);
	if (reaped != producer || error != 0) {
		fprintf(stderr, "GPU SHARE FAILED receive=%d wait=%d errno=%d\n", error, (int)reaped, errno);
		if (descriptor >= 0)
			close(descriptor);
		return 1;
	}

	/* A producer-side API or teardown failure is never replaced by a pixel comparison. */
	if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
		fprintf(stderr, "GPU SHARE FAILED producer status=%d\n", child_status);
		close(descriptor);
		return 1;
	}

	/* This fresh Vulkan instance creates an independent host renderer context. */
	memset(&context, 0, sizeof(context));
	result = share_open(&context);
	if (result == VK_SUCCESS)
		result = share_receive(&context, &descriptor);

	/* A failed import still leaves the input capability owned by this caller. */
	if (descriptor >= 0)
		close(descriptor);
	if (result != VK_SUCCESS)
		fprintf(stderr, "GPU SHARE FAILED receiver api=%s result=%d\n", context.operation, (int)result);

	/* Cleanup participates in the result even after the pixel oracle succeeds. */
	cleanup = share_close(&context);
	if (cleanup != VK_SUCCESS) {
		fprintf(stderr, "GPU SHARE FAILED receiver cleanup=%d\n", (int)cleanup);
		return 1;
	}

	/* An actual renderer or content failure prevents the terminal success record. */
	if (result != VK_SUCCESS)
		return 1;

	/* Succeeded: a new process used and released the producer's actual GPU allocation. */
	puts("GPU SHARE PASS producer-exit SCM_RIGHTS independent-context import GPU-copy pixels=1024 rgba=ff00ffff final-release");
	return 0;
}

/* Reports native output and mode metadata through the actual GPU ioctls without selecting scanout. */
static int
share_displays(void)
{
	struct gpu_display_info output;
	struct gpu_display_mode mode;
	uint32_t outputs;
	uint32_t modes;
	uint32_t index;
	uint32_t ordinal;
	int descriptor;
	int error;
	int saved;

	/* This diagnostic opens and closes its own context before either Vulkan process exists. */
	descriptor = open("/dev/gpu0", O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return -1;

	/* The count query has no synthetic framebuffer or hardcoded resolution source. */
	memset(&output, 0, sizeof(output));
	output.version = GPU_ABI_VERSION;
	output.size = sizeof(output);
	output.index = GPU_DISPLAY_COUNT_ONLY;
	error = ioctl(descriptor, GPU_DISPLAY_QUERY, &output);
	if (error != 0)
		goto cleanup;

	/* Virtio's protocol supports at most sixteen native scanouts. */
	outputs = output.count;
	if (outputs > 16U) {
		errno = EOVERFLOW;
		error = -1;
		goto cleanup;
	}

	/* Enumerate each actual connected output without changing its lease or selected image. */
	for (index = 0U; index < outputs; index++) {
		memset(&output, 0, sizeof(output));
		output.version = GPU_ABI_VERSION;
		output.size = sizeof(output);
		output.index = index;
		error = ioctl(descriptor, GPU_DISPLAY_QUERY, &output);
		if (error != 0)
			goto cleanup;
		printf("GPU DISPLAY index=%u id=%u connected=%u preferred=%ux%u refresh_millihz=%u physical_mm=%ux%u flags=%u\n", index, output.display_id, output.flags & GPU_DISPLAY_CONNECTED, output.preferred_width, output.preferred_height, output.refresh_millihz, output.physical_width_mm, output.physical_height_mm, output.flags);

		/* Disconnected outputs have no mode-validation or ownership contract to query. */
		if ((output.flags & GPU_DISPLAY_CONNECTED) == 0U)
			continue;

		/* Native mode count is obtained from the same observed topology generation. */
		memset(&mode, 0, sizeof(mode));
		mode.version = GPU_ABI_VERSION;
		mode.size = sizeof(mode);
		mode.display_id = output.display_id;
		mode.generation = output.generation;
		mode.operation = GPU_DISPLAY_MODE_ENUMERATE;
		mode.index = GPU_DISPLAY_COUNT_ONLY;
		error = ioctl(descriptor, GPU_DISPLAY_MODE, &mode);
		if (error != 0)
			goto cleanup;

		/* Eight EDID blocks contain fewer than sixty-four complete detailed timings. */
		modes = mode.count;
		if (modes == 0U || modes > 64U) {
			errno = EOVERFLOW;
			error = -1;
			goto cleanup;
		}

		/* Print the kernel's actual dimensions and nominal frequencies for the capture record. */
		for (ordinal = 0U; ordinal < modes; ordinal++) {
			memset(&mode, 0, sizeof(mode));
			mode.version = GPU_ABI_VERSION;
			mode.size = sizeof(mode);
			mode.display_id = output.display_id;
			mode.generation = output.generation;
			mode.operation = GPU_DISPLAY_MODE_ENUMERATE;
			mode.index = ordinal;
			error = ioctl(descriptor, GPU_DISPLAY_MODE, &mode);
			if (error != 0)
				goto cleanup;
			printf("GPU MODE display=%u index=%u width=%u height=%u refresh_millihz=%u\n", output.display_id, ordinal, mode.width, mode.height, mode.refresh_millihz);
		}
	}

cleanup:
	/* No diagnostic GPU context may be inherited by the later independent-process renderer check. */
	saved = errno;
	close(descriptor);
	fflush(stdout);
	errno = saved;
	if (error != 0)
		return -1;

	/* Succeeded: the record describes actual native discovery without acquiring a display lease. */
	return 0;
}

/* Creates one independent device, command buffer and bounded submission fence. */
static VkResult
share_open(
	struct share_context *context)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance;
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo device;
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo command;
	VkFenceCreateInfo fence;
	VkQueueFamilyProperties families[32];
	VkPhysicalDevice devices[8];
	VkResult result;
	float priority;
	uint32_t count;
	uint32_t index;

	/* No window system or display lease is involved in the renderer import oracle. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "gpu-share-client";
	application.apiVersion = VK_API_VERSION_1_0;

	/* Instance creation opens a GPU context owned solely by this process. */
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	context->operation = "vkCreateInstance";
	result = vkCreateInstance(&instance, NULL, &context->instance);
	if (result != VK_SUCCESS)
		return result;

	/* The bounded inventory rejects truncation instead of silently changing devices. */
	count = 8U;
	context->operation = "vkEnumeratePhysicalDevices";
	result = vkEnumeratePhysicalDevices(context->instance, &count, devices);
	if (result != VK_SUCCESS)
		return result;
	if (count == 0U)
		return VK_ERROR_INITIALIZATION_FAILED;
	context->physical = devices[0];

	/* Pick an actual graphics family rather than assuming queue family zero. */
	count = 32U;
	vkGetPhysicalDeviceQueueFamilyProperties(context->physical, &count, families);
	context->family = UINT32_MAX;
	for (index = 0U; index < count; index++) {
		/* A graphics queue provides the transfer commands exercised by this fixture. */
		if (families[index].queueCount != 0U &&
		    (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
			context->family = index;
			break;
		}
	}

	/* Unsupported queue inventories fail before a logical device is created. */
	if (context->family == UINT32_MAX)
		return VK_ERROR_FEATURE_NOT_PRESENT;

	/* One queue suffices to establish explicit external ownership transitions. */
	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = context->family;
	queue.queueCount = 1U;
	queue.pQueuePriorities = &priority;

	/* The private helper negotiates host external-memory details without a public guest extension. */
	memset(&device, 0, sizeof(device));
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.queueCreateInfoCount = 1U;
	device.pQueueCreateInfos = &queue;
	context->operation = "vkCreateDevice";
	result = vkCreateDevice(context->physical, &device, NULL, &context->device);
	if (result != VK_SUCCESS)
		return result;

	/* The returned queue belongs to this newly created logical device. */
	vkGetDeviceQueue(context->device, context->family, 0U, &context->queue);

	/* All fixture commands use the selected family and one primary buffer. */
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.queueFamilyIndex = context->family;
	context->operation = "vkCreateCommandPool";
	result = vkCreateCommandPool(context->device, &pool, NULL, &context->pool);
	if (result != VK_SUCCESS)
		return result;

	/* A pool owns its command buffer through normal Vulkan teardown. */
	memset(&command, 0, sizeof(command));
	command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command.commandPool = context->pool;
	command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command.commandBufferCount = 1U;
	context->operation = "vkAllocateCommandBuffers";
	result = vkAllocateCommandBuffers(context->device, &command, &context->command);
	if (result != VK_SUCCESS)
		return result;

	/* Every submitted command is observed with a finite GPU timeout. */
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	context->operation = "vkCreateFence";
	result = vkCreateFence(context->device, &fence, NULL, &context->fence);
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded: this process has its own renderer command namespace. */
	return VK_SUCCESS;
}

/* Releases all Vulkan ownership, including the imported allocation's kernel alias. */
static VkResult
share_close(
	struct share_context *context)
{
	VkResult result;

	/* A partially initialized context still releases every successfully created object. */
	result = VK_SUCCESS;
	if (context->device != VK_NULL_HANDLE) {
		/* Observe device completion before retiring command and memory ownership. */
		result = vkDeviceWaitIdle(context->device);

		/* Command buffers stop referring to the image before its destruction. */
		if (context->pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(context->device, context->pool, NULL);

		/* The completion fence owns no commands after device idle. */
		if (context->fence != VK_NULL_HANDLE)
			vkDestroyFence(context->device, context->fence, NULL);

		/* Resource objects retire before the allocations to which they were bound. */
		if (context->readback != VK_NULL_HANDLE)
			vkDestroyBuffer(context->device, context->readback, NULL);
		if (context->readback_memory != VK_NULL_HANDLE)
			vkFreeMemory(context->device, context->readback_memory, NULL);
		if (context->image != VK_NULL_HANDLE)
			vkDestroyImage(context->device, context->image, NULL);
		if (context->image_memory != VK_NULL_HANDLE)
			vkFreeMemory(context->device, context->image_memory, NULL);

		/* Destroying the device cannot be substituted by retaining the entire producer session. */
		vkDestroyDevice(context->device, NULL);
	}

	/* Instance teardown closes the final process-owned GPU session. */
	if (context->instance != VK_NULL_HANDLE)
		vkDestroyInstance(context->instance, NULL);

	/* A failed GPU completion is preserved even though local ownership was consumed. */
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded: this process owns no remaining Vulkan resource or context. */
	return VK_SUCCESS;
}

/* Begins the single one-time command buffer used by either process. */
static VkResult
share_begin(
	struct share_context *context)
{
	VkCommandBufferBeginInfo begin;
	VkResult result;

	/* Each independent process records only one submission in its fresh command buffer. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	context->operation = "vkBeginCommandBuffer";
	result = vkBeginCommandBuffer(context->command, &begin);
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded: image ownership and transfer commands may be recorded. */
	return VK_SUCCESS;
}

/* Submits the recorded GPU work and requires its actual completion fence. */
static VkResult
share_submit(
	struct share_context *context)
{
	VkSubmitInfo submit;
	VkResult result;

	/* Recording failures must be visible before any host queue submission. */
	context->operation = "vkEndCommandBuffer";
	result = vkEndCommandBuffer(context->command);
	if (result != VK_SUCCESS)
		return result;

	/* This fence, rather than socket progress, orders all subsequent allocation use. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &context->command;
	context->operation = "vkQueueSubmit";
	result = vkQueueSubmit(context->queue, 1U, &submit, context->fence);
	if (result != VK_SUCCESS)
		return result;

	/* A bounded timeout reports missing renderer completion as a test failure. */
	context->operation = "vkWaitForFences";
	result = vkWaitForFences(context->device, 1U, &context->fence, VK_TRUE, SHARE_TIMEOUT);
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded: all image writes or receiver copies completed on the real GPU. */
	return VK_SUCCESS;
}

/* Clears an exportable GPU image and releases its ownership to an external consumer. */
static VkResult
share_produce(
	struct share_context *context,
	int *descriptor)
{
	VkExtent2D extent;
	VkClearColorValue color;
	VkImageSubresourceRange range;
	VkResult result;

	/* Allocation and image creation use the real WSI helper, not a fixture-created host resource. */
	extent.width = SHARE_EDGE;
	extent.height = SHARE_EDGE;
	context->operation = "vulkan_wsi_shared_image_create";
	result = vulkan_wsi_shared_image_create(context->device, VK_FORMAT_R8G8B8A8_UNORM, extent, NULL, &context->image, &context->image_memory, descriptor, &context->descriptor);
	if (result != VK_SUCCESS)
		return result;

	/* The producing GPU command owns and initializes the entire pixel allocation. */
	result = share_begin(context);
	if (result != VK_SUCCESS)
		return result;
	share_barrier(context, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 0U, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	/* Exact endpoint channels allow a byte-exact independent pixel oracle. */
	memset(&color, 0, sizeof(color));
	color.float32[0] = 1.0f;
	color.float32[2] = 1.0f;
	color.float32[3] = 1.0f;
	memset(&range, 0, sizeof(range));
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1U;
	range.layerCount = 1U;
	vkCmdClearColorImage(context->command, context->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1U, &range);

	/* The next process acquires the same GENERAL-layout external allocation. */
	share_barrier(context, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, context->family, SHARE_EXTERNAL_FAMILY, VK_ACCESS_TRANSFER_WRITE_BIT, 0U, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	result = share_submit(context);
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded: exporting the fd cannot race the producer's GPU writes. */
	return VK_SUCCESS;
}

/* Imports the exited producer's allocation and checks a real GPU copy of its pixels. */
static VkResult
share_receive(
	struct share_context *context,
	int *descriptor)
{
	VkBufferImageCopy copy;
	VkBufferMemoryBarrier visible;
	VkResult result;

	/* This performs CTX_ATTACH, VkImportMemoryResourceInfoMESA, and VkImage binding. */
	context->operation = "vulkan_wsi_shared_image_import";
	result = vulkan_wsi_shared_image_import(context->device, *descriptor, NULL, &context->image, &context->image_memory, &context->descriptor);
	if (result != VK_SUCCESS)
		return result;

	/* The renderer alias must remain usable after the final transferable capability closes. */
	close(*descriptor);
	*descriptor = -1;

	/* Authoritative kernel metadata must describe the producer's exact image. */
	context->operation = "imported image metadata";
	if (context->descriptor.width != SHARE_EDGE ||
	    context->descriptor.height != SHARE_EDGE ||
	    context->descriptor.format != GPU_PIXEL_RGBA8888)
		return VK_ERROR_INITIALIZATION_FAILED;

	/* A separate visible buffer is only the final test oracle, not shared image storage. */
	result = share_buffer(context);
	if (result != VK_SUCCESS)
		return result;
	result = share_begin(context);
	if (result != VK_SUCCESS)
		return result;

	/* Import acquires external ownership before any receiver renderer command reads pixels. */
	share_barrier(context, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SHARE_EXTERNAL_FAMILY, context->family, 0U, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	/* The renderer copies the imported image into a distinct ordinary allocation. */
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = SHARE_EDGE;
	copy.imageExtent.height = SHARE_EDGE;
	copy.imageExtent.depth = 1U;
	vkCmdCopyImageToBuffer(context->command, context->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, context->readback, 1U, &copy);

	/* Host visibility follows the completed GPU write rather than coherent-memory assumptions. */
	memset(&visible, 0, sizeof(visible));
	visible.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	visible.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	visible.buffer = context->readback;
	visible.size = VK_WHOLE_SIZE;
	vkCmdPipelineBarrier(context->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0U, 0U, NULL, 1U, &visible, 0U, NULL);

	/* Complete receiver use returns the allocation to its external ownership domain. */
	share_barrier(context, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, context->family, SHARE_EXTERNAL_FAMILY, VK_ACCESS_TRANSFER_READ_BIT, 0U, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	result = share_submit(context);
	if (result != VK_SUCCESS)
		return result;

	/* The oracle cannot substitute a CPU-generated image for the actual imported GPU contents. */
	result = share_verify(context);
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded: this independent renderer consumed every expected producer pixel. */
	return VK_SUCCESS;
}

/* Allocates a separate host-visible destination for the receiver's GPU copy. */
static VkResult
share_buffer(
	struct share_context *context)
{
	VkBufferCreateInfo create;
	VkMemoryRequirements requirements;
	VkPhysicalDeviceMemoryProperties properties;
	VkMemoryAllocateInfo allocation;
	VkResult result;
	uint32_t type;
	uint32_t index;

	/* Readback storage is not the exported allocation and is never presented. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create.size = SHARE_BYTES;
	create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	context->operation = "vkCreateBuffer";
	result = vkCreateBuffer(context->device, &create, NULL, &context->readback);
	if (result != VK_SUCCESS)
		return result;

	/* Memory selection honors this actual buffer's supported memory types. */
	vkGetBufferMemoryRequirements(context->device, context->readback, &requirements);
	vkGetPhysicalDeviceMemoryProperties(context->physical, &properties);
	if (properties.memoryTypeCount > VK_MAX_MEMORY_TYPES)
		return VK_ERROR_INITIALIZATION_FAILED;

	/* Search only the actual bounded Vulkan memory-type inventory. */
	type = UINT32_MAX;
	for (index = 0U; index < properties.memoryTypeCount; index++) {
		/* The standard type mask excludes incompatible heaps even when they are visible. */
		if ((requirements.memoryTypeBits & (1U << index)) == 0U)
			continue;

		/* Explicit invalidation below handles either coherent or noncoherent visible memory. */
		if ((properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U) {
			type = index;
			break;
		}
	}

	/* A device without compatible readable memory cannot supply this oracle. */
	context->operation = "readback memory type";
	if (type == UINT32_MAX)
		return VK_ERROR_FEATURE_NOT_PRESENT;

	/* Allocate and bind the exact storage required by the receiver buffer. */
	memset(&allocation, 0, sizeof(allocation));
	allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = type;
	context->operation = "vkAllocateMemory readback";
	result = vkAllocateMemory(context->device, &allocation, NULL, &context->readback_memory);
	if (result != VK_SUCCESS)
		return result;

	/* The copy destination receives an ordinary Vulkan allocation in this context. */
	context->operation = "vkBindBufferMemory";
	result = vkBindBufferMemory(context->device, context->readback, context->readback_memory, 0U);
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded: the receiver owns an independently bound GPU copy destination. */
	return VK_SUCCESS;
}

/* Compares every GPU-copied pixel after explicit mapping and cache invalidation. */
static VkResult
share_verify(
	struct share_context *context)
{
	VkMappedMemoryRange range;
	VkResult result;
	void *mapping;
	const uint8_t *pixels;
	uint32_t offset;
	int mismatch;

	/* Mapping only the oracle buffer cannot make the shared image path CPU-dependent. */
	context->operation = "vkMapMemory readback";
	result = vkMapMemory(context->device, context->readback_memory, 0U, VK_WHOLE_SIZE, 0U, &mapping);
	if (result != VK_SUCCESS)
		return result;

	/* Whole-allocation invalidation satisfies nonCoherentAtomSize alignment without guessing it. */
	memset(&range, 0, sizeof(range));
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = context->readback_memory;
	range.size = VK_WHOLE_SIZE;
	context->operation = "vkInvalidateMappedMemoryRanges";
	result = vkInvalidateMappedMemoryRanges(context->device, 1U, &range);
	if (result != VK_SUCCESS) {
		vkUnmapMemory(context->device, context->readback_memory);
		return result;
	}

	/* Exact endpoint values detect stale, zero-filled and misbound allocation contents. */
	pixels = mapping;
	mismatch = 0;
	for (offset = 0U; offset < SHARE_BYTES; offset += 4U) {
		/* All 1024 independently copied pixels must contain producer-written magenta. */
		if (pixels[offset] != 255U ||
		    pixels[offset + 1U] != 0U ||
		    pixels[offset + 2U] != 255U ||
		    pixels[offset + 3U] != 255U) {
			fprintf(stderr, "GPU SHARE PIXEL FAILED index=%u rgba=%02x%02x%02x%02x\n", offset / 4U, pixels[offset], pixels[offset + 1U], pixels[offset + 2U], pixels[offset + 3U]);
			mismatch = 1;
			break;
		}
	}

	/* CPU mapping ownership ends before either success or mismatch is reported. */
	vkUnmapMemory(context->device, context->readback_memory);
	context->operation = "imported GPU pixel oracle";
	if (mismatch != 0)
		return VK_ERROR_INITIALIZATION_FAILED;

	/* Succeeded: every byte came from the same allocation filled by the exited producer. */
	return VK_SUCCESS;
}

/* Records one explicit ownership and layout transition for the shared image. */
static void
share_barrier(
	struct share_context *context,
	VkImageLayout old_layout,
	VkImageLayout new_layout,
	uint32_t source_family,
	uint32_t destination_family,
	VkAccessFlags source_access,
	VkAccessFlags destination_access,
	VkPipelineStageFlags source_stage,
	VkPipelineStageFlags destination_stage)
{
	VkImageMemoryBarrier barrier;

	/* Every transition covers exactly the image's sole color level and layer. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = source_access;
	barrier.dstAccessMask = destination_access;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = source_family;
	barrier.dstQueueFamilyIndex = destination_family;
	barrier.image = context->image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1U;
	barrier.subresourceRange.layerCount = 1U;
	vkCmdPipelineBarrier(context->command, source_stage, destination_stage, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);

	/* Succeeded: the barrier is recorded in the owning process's actual command stream. */
	return;
}

/* Sends one typed kernel capability through the ordinary AF_UNIX control-message path. */
static int
share_send_fd(
	int socket,
	int descriptor)
{
	union {
		struct cmsghdr alignment;
		unsigned char bytes[CMSG_SPACE(sizeof(int))];
	} control;
	struct msghdr message;
	struct cmsghdr *header;
	struct iovec vector;
	ssize_t sent;
	char payload;

	/* The byte makes stream delivery and its attached capability one receive operation. */
	payload = 'G';
	vector.iov_base = &payload;
	vector.iov_len = 1U;
	memset(&control, 0, sizeof(control));
	memset(&message, 0, sizeof(message));
	message.msg_iov = &vector;
	message.msg_iovlen = 1U;
	message.msg_control = control.bytes;
	message.msg_controllen = sizeof(control.bytes);
	header = (struct cmsghdr *)(void *)control.bytes;
	header->cmsg_level = SOL_SOCKET;
	header->cmsg_type = SCM_RIGHTS;
	header->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(header), &descriptor, sizeof(descriptor));

	/* The kernel retains the capability even if the sender closes its local fd immediately. */
	sent = sendmsg(socket, &message, 0);
	if (sent != 1)
		return -1;

	/* Succeeded: the socket message owns an independent allocation reference. */
	return 0;
}

/* Receives exactly one capability and rejects truncation or unrelated ancillary data. */
static int
share_receive_fd(
	int socket,
	int *descriptor)
{
	union {
		struct cmsghdr alignment;
		unsigned char bytes[CMSG_SPACE(sizeof(int))];
	} control;
	struct msghdr message;
	struct cmsghdr *header;
	struct iovec vector;
	ssize_t received;
	char payload;

	/* A zeroed receive record exposes actual returned metadata rather than stale stack bytes. */
	payload = 0;
	vector.iov_base = &payload;
	vector.iov_len = 1U;
	memset(&control, 0, sizeof(control));
	memset(&message, 0, sizeof(message));
	message.msg_iov = &vector;
	message.msg_iovlen = 1U;
	message.msg_control = control.bytes;
	message.msg_controllen = sizeof(control.bytes);
	received = recvmsg(socket, &message, MSG_CMSG_CLOEXEC);
	if (received != 1)
		return -1;

	/* The sender's one rights message has a fixed payload, level, type and length. */
	if (message.msg_controllen < CMSG_LEN(sizeof(int)))
		return -1;
	header = (struct cmsghdr *)(void *)control.bytes;
	if (header->cmsg_level != SOL_SOCKET ||
	    header->cmsg_type != SCM_RIGHTS ||
	    header->cmsg_len != CMSG_LEN(sizeof(int)))
		return -1;
	memcpy(descriptor, CMSG_DATA(header), sizeof(*descriptor));

	/* A received descriptor is still returned for cleanup when the accompanying byte is invalid. */
	if (message.msg_flags != 0 || payload != 'G' || *descriptor < 0)
		return -1;

	/* Succeeded: the receiver owns a capability in its independent descriptor namespace. */
	return 0;
}

/* Finishes producer rendering and destroys the full source context before process exit. */
static int
share_producer(
	int socket)
{
	struct share_context context;
	VkResult result;
	VkResult cleanup;
	int descriptor;
	int sent;

	/* Only this child creates the image and its source renderer namespace. */
	memset(&context, 0, sizeof(context));
	descriptor = -1;
	sent = -1;
	result = share_open(&context);
	if (result == VK_SUCCESS)
		result = share_produce(&context, &descriptor);

	/* A failed GPU operation sends no misleading success capability to the receiver. */
	if (result != VK_SUCCESS) {
		fprintf(stderr, "GPU SHARE FAILED producer api=%s result=%d\n", context.operation, (int)result);
	} else {
		/* Sending after fence completion establishes a real cross-process ownership boundary. */
		sent = share_send_fd(socket, descriptor);
		if (sent != 0)
			perror("gpu-share-client: sendmsg");
	}

	/* The fd and Vulkan memory have separate references; both source references are consumed. */
	if (descriptor >= 0)
		close(descriptor);
	cleanup = share_close(&context);
	if (cleanup != VK_SUCCESS) {
		fprintf(stderr, "GPU SHARE FAILED producer cleanup=%d\n", (int)cleanup);
		return 1;
	}

	/* Process success requires rendering, capability transfer and complete source cleanup. */
	if (result != VK_SUCCESS || sent != 0)
		return 1;

	/* Succeeded: only the socket or receiver retains the actual image allocation. */
	return 0;
}
