/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises production scene recording through an independent raw-pixel transfer oracle. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userland/desktop/wayland/scene.h"

/* Small unequal dimensions make full-output stride distinguishable from region width. */
#define TEST_WIDTH 67U
#define TEST_HEIGHT 43U

/* This fixture owns one fake bound buffer and observes actual recorded dependencies. */
static uint32_t cache_pixels[TEST_WIDTH * TEST_HEIGHT];
static uint32_t output_pixels[TEST_WIDTH * TEST_HEIGHT];
static unsigned fail_stage;
static unsigned buffer_destroys;
static unsigned memory_frees;
static unsigned recording_stage;
static unsigned restoring;
static VkImageLayout image_layout;

static void copy_region(const VkBufferImageCopy *region, uint32_t *destination, const uint32_t *source, unsigned restore);

/*
 * Checks old-cursor removal, partial scene updates, selected strides and allocation unwind.
 */
int
main(
	void)
{
	struct kwl_scene scene;
	VkRect2D region;
	VkResult result;
	unsigned index;
	unsigned before;

	/* Full capture starts without any published scene validity. */
	result = kwl_scene_open(&scene, VK_NULL_HANDLE, VK_NULL_HANDLE, TEST_WIDTH, TEST_HEIGHT);
	assert(result == VK_SUCCESS && scene.valid == 0U);
	for (index = 0U; index < TEST_WIDTH * TEST_HEIGHT; index++)
		output_pixels[index] = 0x80403000U + index;
	kwl_scene_copy(&scene, VK_NULL_HANDLE, (VkImage)(uintptr_t)output_pixels, NULL, 0U);
	assert(recording_stage == 0U && scene.valid == 0U);
	assert(memcmp(cache_pixels, output_pixels, sizeof(cache_pixels)) == 0);

	/* An older cursor disappears while pixels outside damage remain untouched. */
	region.offset.x = 13;
	region.offset.y = 9;
	region.extent.width = 11U;
	region.extent.height = 7U;
	output_pixels[10U * TEST_WIDTH + 15U] = 0xffffffffU;
	output_pixels[0] = 0x12345678U;
	kwl_scene_copy(&scene, VK_NULL_HANDLE, (VkImage)(uintptr_t)output_pixels, &region, 1U);
	assert(output_pixels[10U * TEST_WIDTH + 15U] == cache_pixels[10U * TEST_WIDTH + 15U]);
	assert(output_pixels[0] == 0x12345678U);

	/* A partial client update changes exactly the cached region, without capturing a newer cursor elsewhere. */
	output_pixels[11U * TEST_WIDTH + 16U] = 0x10203040U;
	kwl_scene_copy(&scene, VK_NULL_HANDLE, (VkImage)(uintptr_t)output_pixels, &region, 0U);
	assert(cache_pixels[11U * TEST_WIDTH + 16U] == 0x10203040U);
	assert(cache_pixels[0] == 0x80403000U);
	output_pixels[11U * TEST_WIDTH + 16U] = 0xffffffffU;
	kwl_scene_copy(&scene, VK_NULL_HANDLE, (VkImage)(uintptr_t)output_pixels, &region, 1U);
	assert(output_pixels[11U * TEST_WIDTH + 16U] == 0x10203040U);
	assert(recording_stage == 0U);
	kwl_scene_close(&scene, VK_NULL_HANDLE);
	assert(scene.buffer == VK_NULL_HANDLE && scene.memory == VK_NULL_HANDLE && !scene.valid);

	/* Memory and binding failures retire only the allocations actually acquired. */
	before = memory_frees;
	fail_stage = 2U;
	result = kwl_scene_open(&scene, VK_NULL_HANDLE, VK_NULL_HANDLE, TEST_WIDTH, TEST_HEIGHT);
	assert(result == VK_ERROR_OUT_OF_DEVICE_MEMORY && scene.buffer == VK_NULL_HANDLE);
	assert(memory_frees == before);
	fail_stage = 3U;
	result = kwl_scene_open(&scene, VK_NULL_HANDLE, VK_NULL_HANDLE, TEST_WIDTH, TEST_HEIGHT);
	assert(result == VK_ERROR_OUT_OF_DEVICE_MEMORY && scene.memory == VK_NULL_HANDLE);
	assert(memory_frees == before + 1U && buffer_destroys == 3U);

	/* Succeeded: this checks actual standard command recording, not physical GPU latency. */
	puts("scene-cache-host PASS");
	return 0;
}

/*
 * Creates the fake transfer buffer only for the production's exact four-byte scene size.
 */
VkResult
vkCreateBuffer(
	VkDevice device,
	const VkBufferCreateInfo *create,
	const VkAllocationCallbacks *allocator,
	VkBuffer *buffer)
{
	(void)device;
	(void)allocator;

	/* Both transfer directions use exactly one packed output allocation. */
	assert(create->size == sizeof(cache_pixels));
	assert(create->usage == (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
	*buffer = (VkBuffer)(uintptr_t)cache_pixels;

	/* Succeeded: the fixture's buffer exists. */
	return VK_SUCCESS;
}

/*
 * Supplies a nonzero memory-type ordinal to exclude assumptions about type zero.
 */
void
vkGetBufferMemoryRequirements(
	VkDevice device,
	VkBuffer buffer,
	VkMemoryRequirements *requirements)
{
	(void)device;
	(void)buffer;

	/* Only type two can back the transfer buffer. */
	memset(requirements, 0, sizeof(*requirements));
	requirements->size = sizeof(cache_pixels);
	requirements->memoryTypeBits = 4U;

	/* Succeeded: exact storage requirements are visible. */
	return;
}

/*
 * Advertises three actual memory types independently of any vendor profile.
 */
void
vkGetPhysicalDeviceMemoryProperties(
	VkPhysicalDevice physical,
	VkPhysicalDeviceMemoryProperties *properties)
{
	(void)physical;

	/* Type two exists in this fixture. */
	memset(properties, 0, sizeof(*properties));
	properties->memoryTypeCount = 3U;

	/* Succeeded: the cache can choose a permitted ordinal. */
	return;
}

/*
 * Models acquisition failure before memory becomes owned.
 */
VkResult
vkAllocateMemory(
	VkDevice device,
	const VkMemoryAllocateInfo *allocate,
	const VkAllocationCallbacks *allocator,
	VkDeviceMemory *memory)
{
	(void)device;
	(void)allocator;

	/* The cache selected the permitted allocation rather than type zero. */
	assert(allocate->memoryTypeIndex == 2U && allocate->allocationSize == sizeof(cache_pixels));
	if (fail_stage == 2U)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	*memory = (VkDeviceMemory)(uintptr_t)cache_pixels;

	/* Succeeded: buffer backing is acquired. */
	return VK_SUCCESS;
}

/*
 * Models a binding failure after backing becomes owned.
 */
VkResult
vkBindBufferMemory(
	VkDevice device,
	VkBuffer buffer,
	VkDeviceMemory memory,
	VkDeviceSize offset)
{
	(void)device;
	(void)buffer;
	(void)memory;

	/* Binding uses the whole allocation from its beginning. */
	assert(offset == 0U);
	if (fail_stage == 3U)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;

	/* Succeeded: the buffer has its backing. */
	return VK_SUCCESS;
}

/*
 * Counts acquired-buffer retirement, allowing Vulkan's null-handle no-op.
 */
void
vkDestroyBuffer(
	VkDevice device,
	VkBuffer buffer,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)allocator;

	/* Only an acquired buffer needs retirement. */
	if (buffer != VK_NULL_HANDLE)
		buffer_destroys++;

	/* Succeeded: the fixture no longer owns this buffer. */
	return;
}

/*
 * Counts independently acquired memory retirement.
 */
void
vkFreeMemory(
	VkDevice device,
	VkDeviceMemory memory,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)allocator;

	/* A failed allocation never creates a memory retirement. */
	if (memory != VK_NULL_HANDLE)
		memory_frees++;

	/* Succeeded: backing ownership is retired. */
	return;
}

/*
 * Validates the production's image transition and buffer ordering around each copy.
 */
void
vkCmdPipelineBarrier(
	VkCommandBuffer command,
	VkPipelineStageFlags source,
	VkPipelineStageFlags destination,
	VkDependencyFlags dependencies,
	uint32_t memories,
	const VkMemoryBarrier *memory,
	uint32_t buffers,
	const VkBufferMemoryBarrier *buffer,
	uint32_t images,
	const VkImageMemoryBarrier *image)
{
	(void)command;
	(void)dependencies;
	(void)memory;

	/* Every copy transitions one acquired output image without a queue-family change. */
	assert(memories == 0U && images == 1U);
	assert(image->srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
	assert(image->dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
	if (recording_stage == 0U) {
		assert(image->oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		assert(buffers == 1U && buffer->size == VK_WHOLE_SIZE);
		assert(destination == VK_PIPELINE_STAGE_TRANSFER_BIT);
		assert((source & VK_PIPELINE_STAGE_TRANSFER_BIT) != 0U);
		image_layout = image->newLayout;
		restoring = 0U;
		if (image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			restoring = 1U;
		assert(buffer->dstAccessMask == (restoring ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT));
		recording_stage = 1U;
	} else {
		assert(recording_stage == 2U && buffers == 0U);
		assert(image->oldLayout == image_layout && image->newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		assert(source == VK_PIPELINE_STAGE_TRANSFER_BIT && destination == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		assert(image->dstAccessMask == (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT));
		recording_stage = 0U;
	}

	/* Succeeded: transfer dependencies preserve the cursor LOAD pass. */
	return;
}

/*
 * Executes only the selected bytes of a recorded production readback.
 */
void
vkCmdCopyImageToBuffer(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout layout,
	VkBuffer buffer,
	uint32_t count,
	const VkBufferImageCopy *region)
{
	(void)command;
	(void)buffer;

	/* The image-to-buffer command follows its source transition. */
	assert(recording_stage == 1U && !restoring && count == 1U && layout == image_layout);
	copy_region(region, cache_pixels, (const uint32_t *)(uintptr_t)image, 0U);
	recording_stage = 2U;

	/* Succeeded: selected source pixels are now cached. */
	return;
}

/*
 * Executes only the selected bytes of a recorded production restoration.
 */
void
vkCmdCopyBufferToImage(
	VkCommandBuffer command,
	VkBuffer buffer,
	VkImage image,
	VkImageLayout layout,
	uint32_t count,
	const VkBufferImageCopy *region)
{
	(void)command;
	(void)buffer;

	/* The buffer-to-image command follows its destination transition. */
	assert(recording_stage == 1U && restoring && count == 1U && layout == image_layout);
	copy_region(region, (uint32_t *)(uintptr_t)image, cache_pixels, 1U);
	recording_stage = 2U;

	/* Succeeded: the old cursor has its unchanged scene beneath it again. */
	return;
}

/* Copies independent buffer and image rows using the actual recorded offsets and pitches. */
static void
copy_region(
	const VkBufferImageCopy *region,
	uint32_t *destination,
	const uint32_t *source,
	unsigned restore)
{
	size_t image_offset;
	size_t buffer_offset;
	size_t row;
	size_t column;

	/* The region's buffer stride stays the full output width, including partial damage. */
	assert(region->bufferRowLength == TEST_WIDTH && region->bufferImageHeight == TEST_HEIGHT);
	assert(region->imageExtent.depth == 1U && region->imageSubresource.layerCount == 1U);
	image_offset = (size_t)region->imageOffset.y * TEST_WIDTH + region->imageOffset.x;
	buffer_offset = (size_t)region->bufferOffset / sizeof(uint32_t);

	/* Preserves every row's unselected prefix and suffix independently. */
	for (row = 0U; row < region->imageExtent.height; row++) {
		for (column = 0U; column < region->imageExtent.width; column++) {
			if (restore) {
				destination[image_offset + row * TEST_WIDTH + column] = source[buffer_offset + row * region->bufferRowLength + column];
			} else {
				destination[buffer_offset + row * region->bufferRowLength + column] = source[image_offset + row * TEST_WIDTH + column];
			}
		}
	}

	/* Succeeded: exact recorded bytes changed without an implicit channel conversion. */
	return;
}
