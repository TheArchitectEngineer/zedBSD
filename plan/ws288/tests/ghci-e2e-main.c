/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The end-to-end test of the GHCI driver: libvulkan's API, through the
 * driver's Vulkan executor and the CP firmware, to the RTL of the KC705's
 * GPU in Verilator (ghci-e2e.h).
 *
 *   ghci-e2e CASE...
 *
 * The cases, in the order given:
 *
 *   fill            vkCmdFillBuffer on device-local and host-visible
 *                   buffers, overlapping fills, read back by a copy
 *   copy            vkCmdCopyBuffer with several regions between
 *                   host-visible and device-local buffers
 *   compute=SPV     a compute pipeline of the module SPV (the test's
 *                   scale.comp) dispatched over 1024 invocations, its
 *                   storage buffer read back by a copy
 *   vkdemo=PPM      one frame of vkdemo's offscreen renderer (the
 *                   textured cuboid at 1000 ms), written to PPM for the
 *                   independent oracle
 *
 * Every case prints "PASS e2e.<case>" or "FAIL e2e.<case>: ..." and its
 * GPU cycles, the firmware's packet cycles, the µop words and the round
 * trips the fake core measured; the run ends with "PASS ghci-e2e" when
 * every case passed.  vkdemo's pixels are judged by the oracle the
 * script runs (vkdemo_oracle.py), not here.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "ghci-e2e.h"
#include "link.h"
#include "renderer.h"

/* The bytes of the buffers of the transfer cases. */
#define TRANSFER_BYTES			65536U

/* The invocations of the compute case, and its workgroup's size (scale.comp's local_size_x). */
#define COMPUTE_COUNT			1024U
#define COMPUTE_GROUP			64U

/* The words the fills write. */
#define FILL_WHOLE			0x12345678U
#define FILL_PART			0xcafef00dU

/* The vkdemo frame's shader time. */
#define VKDEMO_TIME_MS			1000U

/* Waits for a fence for at most this many nanoseconds of simulated time. */
#define FENCE_TIMEOUT_NS		60000000000ULL

/*
 * The Vulkan objects the transfer and compute cases share: the instance,
 * the device and its queue, a command pool with one buffer, and a fence.
 * Made by vulkan_open(), released by vulkan_close().
 */
struct vulkan {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkPhysicalDeviceMemoryProperties memory;
	VkDevice device;
	VkQueue queue;
	uint32_t family;
	VkCommandPool pool;
	VkCommandBuffer commands;
	VkFence fence;
};

/* One buffer with its own memory, mapped when host-visible. */
struct buffer {
	VkBuffer buffer;
	VkDeviceMemory memory;
	void *mapped;
};

/* The counts of a case's start, which its report subtracts. */
struct measure {
	struct atg_link_stats link;
	struct e2e_core_stats core;
};

/* The count of the cases that failed, which decides the run's result. */
static unsigned cases_failed;

static int vulkan_open(struct vulkan *vulkan);
static void vulkan_close(struct vulkan *vulkan);
static int buffer_make(struct vulkan *vulkan, VkDeviceSize bytes, VkBufferUsageFlags usage, int host, struct buffer *buffer);
static void buffer_drop(struct vulkan *vulkan, struct buffer *buffer);
static int record_begin(struct vulkan *vulkan);
static int record_submit(struct vulkan *vulkan);
static void barrier(struct vulkan *vulkan, VkAccessFlags source, VkAccessFlags destination, VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage);
static int case_run(const char *name);
static int case_fill(void);
static int case_copy(void);
static int case_compute(const char *path);
static int case_vkdemo(const char *path);
static void measure_start(struct measure *measure);
static void measure_report(const char *name, const struct measure *measure);
static uint32_t *module_read(const char *path, size_t *bytes);

/*
 * Runs the cases named on the command line on one start of the board.
 */
int
main(
	int argc,
	char **argv)
{
	struct measure measure;
	const char *name;
	int index;
	int error;

	/* Lines reach a log as they are printed: a run is long. */
	setvbuf(stdout, NULL, _IOLBF, 0);

	/* The board and the driver on it. */
	measure_start(&measure);
	error = e2e_board_start();
	if (error != 0) {
		printf("FAIL ghci-e2e: the board did not start\n");
		return 1;
	}
	measure_report("start", &measure);

	/* Each case in turn. */
	for (index = 1; index < argc; index++) {
		name = argv[index];
		measure_start(&measure);
		error = case_run(name);
		if (error != 0)
			cases_failed++;
		measure_report(name, &measure);
	}

	/* The detach and the end of the simulation. */
	e2e_board_stop();

	/* A failed case fails the run. */
	if (cases_failed != 0U) {
		printf("FAIL ghci-e2e: %u cases failed\n", cases_failed);
		return 1;
	}

	/* Succeeded: every case passed. */
	printf("PASS ghci-e2e\n");
	return 0;
}

/* Runs the case a command-line word names: 0 when it passed. */
static int
case_run(
	const char *name)
{
	int fill;
	int copy;
	int compute;
	int vkdemo;
	int error;

	/* Which case the word names (a nonzero comparison: not that case). */
	fill = strcmp(name, "fill");
	copy = strcmp(name, "copy");
	compute = strncmp(name, "compute=", 8U);
	vkdemo = strncmp(name, "vkdemo=", 7U);

	/* The case, with what follows its '=' when it takes a path. */
	if (fill == 0) {
		error = case_fill();
	} else if (copy == 0) {
		error = case_copy();
	} else if (compute == 0) {
		error = case_compute(name + 8);
	} else if (vkdemo == 0) {
		error = case_vkdemo(name + 7);
	} else {
		printf("FAIL ghci-e2e: unknown case %s\n", name);
		error = 1;
	}

	/* A failed case. */
	if (error != 0)
		return 1;

	/* Succeeded: the case passed. */
	return 0;
}

/* Makes the instance, the device and its queue, the command buffer and the fence of the transfer cases. */
static int
vulkan_open(
	struct vulkan *vulkan)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance_info;
	VkDeviceQueueCreateInfo queue_info;
	VkDeviceCreateInfo device_info;
	VkCommandPoolCreateInfo pool_info;
	VkCommandBufferAllocateInfo buffer_info;
	VkFenceCreateInfo fence_info;
	VkQueueFamilyProperties families[8];
	uint32_t count;
	float priority;
	VkResult status;

	memset(vulkan, 0, sizeof(*vulkan));

	/* A Vulkan 1.0 instance. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "ghci-e2e";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance_info, 0, sizeof(instance_info));
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application;
	status = vkCreateInstance(&instance_info, NULL, &vulkan->instance);
	if (status != VK_SUCCESS) {
		printf("  vkCreateInstance: %d\n", (int)status);
		return 1;
	}

	/* The GPU: the only physical device. */
	count = 1U;
	status = vkEnumeratePhysicalDevices(vulkan->instance, &count, &vulkan->physical);
	if ((status != VK_SUCCESS && status != VK_INCOMPLETE) || count == 0U) {
		printf("  vkEnumeratePhysicalDevices: %d, %u devices\n", (int)status, count);
		return 1;
	}
	vkGetPhysicalDeviceMemoryProperties(vulkan->physical, &vulkan->memory);

	/* A queue family with graphics, compute and transfer. */
	count = 8U;
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan->physical, &count, families);
	for (vulkan->family = 0U; vulkan->family < count; vulkan->family++) {
		if ((families[vulkan->family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
		    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
			break;
	}
	if (vulkan->family == count) {
		printf("  no queue family with graphics and compute\n");
		return 1;
	}

	/* The device with one queue. */
	priority = 1.0f;
	memset(&queue_info, 0, sizeof(queue_info));
	queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = vulkan->family;
	queue_info.queueCount = 1U;
	queue_info.pQueuePriorities = &priority;
	memset(&device_info, 0, sizeof(device_info));
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount = 1U;
	device_info.pQueueCreateInfos = &queue_info;
	status = vkCreateDevice(vulkan->physical, &device_info, NULL, &vulkan->device);
	if (status != VK_SUCCESS) {
		printf("  vkCreateDevice: %d\n", (int)status);
		return 1;
	}
	vkGetDeviceQueue(vulkan->device, vulkan->family, 0U, &vulkan->queue);

	/* The command pool and its buffer, recorded again by every submission. */
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = vulkan->family;
	status = vkCreateCommandPool(vulkan->device, &pool_info, NULL, &vulkan->pool);
	if (status != VK_SUCCESS) {
		printf("  vkCreateCommandPool: %d\n", (int)status);
		return 1;
	}
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	buffer_info.commandPool = vulkan->pool;
	buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	buffer_info.commandBufferCount = 1U;
	status = vkAllocateCommandBuffers(vulkan->device, &buffer_info, &vulkan->commands);
	if (status != VK_SUCCESS) {
		printf("  vkAllocateCommandBuffers: %d\n", (int)status);
		return 1;
	}

	/* The fence each submission signals. */
	memset(&fence_info, 0, sizeof(fence_info));
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	status = vkCreateFence(vulkan->device, &fence_info, NULL, &vulkan->fence);
	if (status != VK_SUCCESS) {
		printf("  vkCreateFence: %d\n", (int)status);
		return 1;
	}

	/* Succeeded: the objects are made. */
	return 0;
}

/* Releases what vulkan_open() made, after the device is idle. */
static void
vulkan_close(
	struct vulkan *vulkan)
{
	/* The device's work, then its objects, then the device and the instance. */
	if (vulkan->device != VK_NULL_HANDLE) {
		(void)vkDeviceWaitIdle(vulkan->device);
		if (vulkan->fence != VK_NULL_HANDLE)
			vkDestroyFence(vulkan->device, vulkan->fence, NULL);
		if (vulkan->pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(vulkan->device, vulkan->pool, NULL);
		vkDestroyDevice(vulkan->device, NULL);
	}
	if (vulkan->instance != VK_NULL_HANDLE)
		vkDestroyInstance(vulkan->instance, NULL);
	memset(vulkan, 0, sizeof(*vulkan));
}

/* Makes a buffer with its own memory: host-visible and coherent (mapped), or device-local. */
static int
buffer_make(
	struct vulkan *vulkan,
	VkDeviceSize bytes,
	VkBufferUsageFlags usage,
	int host,
	struct buffer *buffer)
{
	VkBufferCreateInfo buffer_info;
	VkMemoryAllocateInfo memory_info;
	VkMemoryRequirements requirements;
	VkMemoryPropertyFlags wanted;
	uint32_t type;
	VkResult status;

	memset(buffer, 0, sizeof(*buffer));

	/* The buffer. */
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = bytes;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	status = vkCreateBuffer(vulkan->device, &buffer_info, NULL, &buffer->buffer);
	if (status != VK_SUCCESS) {
		printf("  vkCreateBuffer: %d\n", (int)status);
		return 1;
	}

	/* A memory type it may live in with the properties asked. */
	vkGetBufferMemoryRequirements(vulkan->device, buffer->buffer, &requirements);
	wanted = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	if (host)
		wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (type = 0U; type < vulkan->memory.memoryTypeCount; type++) {
		if ((requirements.memoryTypeBits & (1U << type)) != 0U &&
		    (vulkan->memory.memoryTypes[type].propertyFlags & wanted) == wanted)
			break;
	}
	if (type == vulkan->memory.memoryTypeCount) {
		printf("  no memory type for the buffer\n");
		return 1;
	}

	/* The memory, bound, and mapped when the host sees it. */
	memset(&memory_info, 0, sizeof(memory_info));
	memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_info.allocationSize = requirements.size;
	memory_info.memoryTypeIndex = type;
	status = vkAllocateMemory(vulkan->device, &memory_info, NULL, &buffer->memory);
	if (status != VK_SUCCESS) {
		printf("  vkAllocateMemory: %d\n", (int)status);
		return 1;
	}
	status = vkBindBufferMemory(vulkan->device, buffer->buffer, buffer->memory, 0U);
	if (status != VK_SUCCESS) {
		printf("  vkBindBufferMemory: %d\n", (int)status);
		return 1;
	}
	if (host) {
		status = vkMapMemory(vulkan->device, buffer->memory, 0U, VK_WHOLE_SIZE, 0U, &buffer->mapped);
		if (status != VK_SUCCESS) {
			printf("  vkMapMemory: %d\n", (int)status);
			return 1;
		}
	}

	/* Succeeded: the buffer is bound. */
	return 0;
}

/* Releases a buffer and its memory. */
static void
buffer_drop(
	struct vulkan *vulkan,
	struct buffer *buffer)
{
	/* The buffer before its memory. */
	if (buffer->buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(vulkan->device, buffer->buffer, NULL);
	if (buffer->memory != VK_NULL_HANDLE)
		vkFreeMemory(vulkan->device, buffer->memory, NULL);
	memset(buffer, 0, sizeof(*buffer));
}

/* Starts recording the command buffer for one submission. */
static int
record_begin(
	struct vulkan *vulkan)
{
	VkCommandBufferBeginInfo begin;
	VkResult status;

	/* One submission, then the buffer is recorded again. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	status = vkBeginCommandBuffer(vulkan->commands, &begin);
	if (status != VK_SUCCESS) {
		printf("  vkBeginCommandBuffer: %d\n", (int)status);
		return 1;
	}

	/* Succeeded: recording. */
	return 0;
}

/* Ends the recording, submits it with the fence and waits for the fence. */
static int
record_submit(
	struct vulkan *vulkan)
{
	VkSubmitInfo submit;
	VkResult status;

	/* The end of the recording. */
	status = vkEndCommandBuffer(vulkan->commands);
	if (status != VK_SUCCESS) {
		printf("  vkEndCommandBuffer: %d\n", (int)status);
		return 1;
	}

	/* The submission, with the fence. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &vulkan->commands;
	status = vkQueueSubmit(vulkan->queue, 1U, &submit, vulkan->fence);
	if (status != VK_SUCCESS) {
		printf("  vkQueueSubmit: %d\n", (int)status);
		return 1;
	}

	/* The work is done when the fence is signaled. */
	status = vkWaitForFences(vulkan->device, 1U, &vulkan->fence, VK_TRUE, FENCE_TIMEOUT_NS);
	if (status != VK_SUCCESS) {
		printf("  vkWaitForFences: %d\n", (int)status);
		return 1;
	}
	status = vkResetFences(vulkan->device, 1U, &vulkan->fence);
	if (status != VK_SUCCESS) {
		printf("  vkResetFences: %d\n", (int)status);
		return 1;
	}
	status = vkResetCommandBuffer(vulkan->commands, 0U);
	if (status != VK_SUCCESS) {
		printf("  vkResetCommandBuffer: %d\n", (int)status);
		return 1;
	}

	/* Succeeded: the submission's work is done. */
	return 0;
}

/* Records a global memory barrier between two stages. */
static void
barrier(
	struct vulkan *vulkan,
	VkAccessFlags source,
	VkAccessFlags destination,
	VkPipelineStageFlags source_stage,
	VkPipelineStageFlags destination_stage)
{
	VkMemoryBarrier memory;

	/* What the earlier commands wrote is visible to the later ones. */
	memset(&memory, 0, sizeof(memory));
	memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory.srcAccessMask = source;
	memory.dstAccessMask = destination;
	vkCmdPipelineBarrier(vulkan->commands, source_stage, destination_stage, 0U, 1U, &memory, 0U, NULL, 0U, NULL);
}

/*
 * Fills a device-local buffer whole, then a part of it again, and a
 * host-visible buffer directly; copies the device-local one out and
 * compares both with what the fills wrote.
 */
static int
case_fill(void)
{
	struct vulkan vulkan;
	struct buffer local;
	struct buffer host;
	struct buffer readback;
	const uint32_t *words;
	uint32_t expected;
	unsigned index;
	unsigned wrong;
	int error;
	VkBufferCopy region;

	/* The device and three buffers. */
	error = vulkan_open(&vulkan);
	if (error == 0)
		error = buffer_make(&vulkan, TRANSFER_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, &local);
	if (error == 0)
		error = buffer_make(&vulkan, TRANSFER_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1, &host);
	if (error == 0)
		error = buffer_make(&vulkan, TRANSFER_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1, &readback);
	if (error != 0) {
		printf("FAIL e2e.fill: the objects\n");
		vulkan_close(&vulkan);
		return 1;
	}
	memset(host.mapped, 0xa5, TRANSFER_BYTES);
	memset(readback.mapped, 0xa5, TRANSFER_BYTES);

	/*
	 * The fills: the device-local buffer whole, then its second 4 KiB
	 * again; the host-visible one from 1 KiB to its end; then the copy of
	 * the device-local one to the readback buffer.
	 */
	error = record_begin(&vulkan);
	if (error == 0) {
		vkCmdFillBuffer(vulkan.commands, local.buffer, 0U, VK_WHOLE_SIZE, FILL_WHOLE);
		barrier(&vulkan, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		vkCmdFillBuffer(vulkan.commands, local.buffer, 4096U, 4096U, FILL_PART);
		vkCmdFillBuffer(vulkan.commands, host.buffer, 1024U, TRANSFER_BYTES - 1024U, FILL_PART);
		barrier(&vulkan, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		region.srcOffset = 0U;
		region.dstOffset = 0U;
		region.size = TRANSFER_BYTES;
		vkCmdCopyBuffer(vulkan.commands, local.buffer, readback.buffer, 1U, &region);
		barrier(&vulkan, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
		error = record_submit(&vulkan);
	}
	if (error != 0) {
		printf("FAIL e2e.fill: the submission\n");
		vulkan_close(&vulkan);
		return 1;
	}

	/* The device-local buffer as the copy read it: the whole fill, its second 4 KiB filled again. */
	wrong = 0U;
	words = readback.mapped;
	for (index = 0U; index < TRANSFER_BYTES / 4U; index++) {
		expected = FILL_WHOLE;
		if (index >= 1024U && index < 2048U)
			expected = FILL_PART;
		if (words[index] != expected) {
			if (wrong < 4U)
				printf("  device-local word %u: %08x, expected %08x\n", index, words[index], expected);
			wrong++;
		}
	}

	/* The host-visible buffer: its first 1 KiB as the host left it, the rest filled. */
	words = host.mapped;
	for (index = 0U; index < TRANSFER_BYTES / 4U; index++) {
		expected = FILL_PART;
		if (index < 256U)
			expected = 0xa5a5a5a5U;
		if (words[index] != expected) {
			if (wrong < 8U)
				printf("  host-visible word %u: %08x, expected %08x\n", index, words[index], expected);
			wrong++;
		}
	}

	/* The objects go. */
	buffer_drop(&vulkan, &readback);
	buffer_drop(&vulkan, &host);
	buffer_drop(&vulkan, &local);
	vulkan_close(&vulkan);

	/* Every word as the fills wrote it. */
	if (wrong != 0U) {
		printf("FAIL e2e.fill: %u words differ\n", wrong);
		return 1;
	}

	/* Succeeded: the fills are right. */
	printf("PASS e2e.fill: %u words\n", 2U * TRANSFER_BYTES / 4U);
	return 0;
}

/*
 * Copies a host-visible pattern into a device-local buffer in three
 * regions (the second overlapping none, the third at an odd word), then
 * the device-local buffer back out in two regions, and compares.
 */
static int
case_copy(void)
{
	struct vulkan vulkan;
	struct buffer source;
	struct buffer local;
	struct buffer readback;
	VkBufferCopy into[3];
	VkBufferCopy out[2];
	uint32_t *pattern;
	uint32_t *words;
	uint32_t expected[TRANSFER_BYTES / 4U];
	unsigned index;
	unsigned region;
	unsigned wrong;
	int error;

	/* The device and three buffers. */
	error = vulkan_open(&vulkan);
	if (error == 0)
		error = buffer_make(&vulkan, TRANSFER_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1, &source);
	if (error == 0)
		error = buffer_make(&vulkan, TRANSFER_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, &local);
	if (error == 0)
		error = buffer_make(&vulkan, TRANSFER_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1, &readback);
	if (error != 0) {
		printf("FAIL e2e.copy: the objects\n");
		vulkan_close(&vulkan);
		return 1;
	}

	/* The pattern: every word different. */
	pattern = source.mapped;
	for (index = 0U; index < TRANSFER_BYTES / 4U; index++)
		pattern[index] = 0x9e3779b9U * (index + 1U);
	memset(readback.mapped, 0, TRANSFER_BYTES);

	/* The regions in: [0, 8 KiB) to 16 KiB, [8 KiB, 12 KiB) to 0, 100 words from word 5001 to word 3. */
	into[0].srcOffset = 0U;
	into[0].dstOffset = 16384U;
	into[0].size = 8192U;
	into[1].srcOffset = 8192U;
	into[1].dstOffset = 0U;
	into[1].size = 4096U;
	into[2].srcOffset = 4U * 5001U;
	into[2].dstOffset = 4U * 3U;
	into[2].size = 4U * 100U;

	/* The regions out: [0, 16 KiB) and [16 KiB, 32 KiB), swapped. */
	out[0].srcOffset = 0U;
	out[0].dstOffset = 16384U;
	out[0].size = 16384U;
	out[1].srcOffset = 16384U;
	out[1].dstOffset = 0U;
	out[1].size = 16384U;

	/* The device-local buffer starts as zeros, then the copies in, then the copies out. */
	error = record_begin(&vulkan);
	if (error == 0) {
		vkCmdFillBuffer(vulkan.commands, local.buffer, 0U, VK_WHOLE_SIZE, 0U);
		barrier(&vulkan, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		vkCmdCopyBuffer(vulkan.commands, source.buffer, local.buffer, 3U, into);
		barrier(&vulkan, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		vkCmdCopyBuffer(vulkan.commands, local.buffer, readback.buffer, 2U, out);
		barrier(&vulkan, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
		error = record_submit(&vulkan);
	}
	if (error != 0) {
		printf("FAIL e2e.copy: the submission\n");
		vulkan_close(&vulkan);
		return 1;
	}

	/* What the device-local buffer holds after the copies in, by the same regions on the host. */
	memset(expected, 0, sizeof(expected));
	for (region = 0U; region < 3U; region++)
		memcpy((uint8_t *)expected + into[region].dstOffset, (uint8_t *)pattern + into[region].srcOffset, into[region].size);

	/* The readback against it, through the swapped regions out. */
	wrong = 0U;
	words = readback.mapped;
	for (index = 0U; index < 8192U; index++) {
		if (words[index] != expected[(index + 4096U) % 8192U]) {
			if (wrong < 4U)
				printf("  readback word %u: %08x, expected %08x\n", index, words[index], expected[(index + 4096U) % 8192U]);
			wrong++;
		}
	}
	for (index = 8192U; index < TRANSFER_BYTES / 4U; index++) {
		if (words[index] != 0U) {
			if (wrong < 4U)
				printf("  readback word %u past the regions: %08x\n", index, words[index]);
			wrong++;
		}
	}

	/* The objects go. */
	buffer_drop(&vulkan, &readback);
	buffer_drop(&vulkan, &local);
	buffer_drop(&vulkan, &source);
	vulkan_close(&vulkan);

	/* Every word as the copies put it. */
	if (wrong != 0U) {
		printf("FAIL e2e.copy: %u words differ\n", wrong);
		return 1;
	}

	/* Succeeded: the copies are right. */
	printf("PASS e2e.copy: %u words\n", TRANSFER_BYTES / 4U);
	return 0;
}

/*
 * Dispatches scale.comp over COMPUTE_COUNT invocations: out[i] =
 * in[i] * 3 + (i ^ 0x5a), the input host-visible, the output
 * device-local and copied out.
 */
static int
case_compute(
	const char *path)
{
	struct vulkan vulkan;
	struct buffer input;
	struct buffer output;
	struct buffer readback;
	VkShaderModuleCreateInfo module_info;
	VkDescriptorSetLayoutBinding bindings[2];
	VkDescriptorSetLayoutCreateInfo set_layout_info;
	VkPipelineLayoutCreateInfo layout_info;
	VkComputePipelineCreateInfo pipeline_info;
	VkDescriptorPoolSize pool_size;
	VkDescriptorPoolCreateInfo pool_info;
	VkDescriptorSetAllocateInfo set_info;
	VkDescriptorBufferInfo buffer_infos[2];
	VkWriteDescriptorSet writes[2];
	VkShaderModule module;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout layout;
	VkPipeline pipeline;
	VkDescriptorPool pool;
	VkDescriptorSet set;
	VkBufferCopy region;
	VkResult status;
	uint32_t *code;
	uint32_t *values;
	uint32_t *words;
	uint32_t expected;
	size_t code_bytes;
	unsigned index;
	unsigned wrong;
	int error;

	/* The module's SPIR-V. */
	code = module_read(path, &code_bytes);
	if (code == NULL) {
		printf("FAIL e2e.compute: no module %s\n", path);
		return 1;
	}

	/* The device and the buffers. */
	error = vulkan_open(&vulkan);
	if (error == 0)
		error = buffer_make(&vulkan, 4U * COMPUTE_COUNT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1, &input);
	if (error == 0)
		error = buffer_make(&vulkan, 4U * COMPUTE_COUNT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, &output);
	if (error == 0)
		error = buffer_make(&vulkan, 4U * COMPUTE_COUNT, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1, &readback);
	if (error != 0) {
		printf("FAIL e2e.compute: the buffers\n");
		vulkan_close(&vulkan);
		free(code);
		return 1;
	}
	values = input.mapped;
	for (index = 0U; index < COMPUTE_COUNT; index++)
		values[index] = 7U * index + 11U;
	memset(readback.mapped, 0, 4U * COMPUTE_COUNT);

	/* The module, the layouts and the pipeline: binding 0 the input, binding 1 the output. */
	memset(&module_info, 0, sizeof(module_info));
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = code_bytes;
	module_info.pCode = code;
	status = vkCreateShaderModule(vulkan.device, &module_info, NULL, &module);
	free(code);
	if (status != VK_SUCCESS) {
		printf("FAIL e2e.compute: vkCreateShaderModule %d\n", (int)status);
		vulkan_close(&vulkan);
		return 1;
	}
	memset(bindings, 0, sizeof(bindings));
	for (index = 0U; index < 2U; index++) {
		bindings[index].binding = index;
		bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[index].descriptorCount = 1U;
		bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	memset(&set_layout_info, 0, sizeof(set_layout_info));
	set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout_info.bindingCount = 2U;
	set_layout_info.pBindings = bindings;
	status = vkCreateDescriptorSetLayout(vulkan.device, &set_layout_info, NULL, &set_layout);
	if (status != VK_SUCCESS) {
		printf("FAIL e2e.compute: vkCreateDescriptorSetLayout %d\n", (int)status);
		vulkan_close(&vulkan);
		return 1;
	}
	memset(&layout_info, 0, sizeof(layout_info));
	layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout_info.setLayoutCount = 1U;
	layout_info.pSetLayouts = &set_layout;
	status = vkCreatePipelineLayout(vulkan.device, &layout_info, NULL, &layout);
	if (status != VK_SUCCESS) {
		printf("FAIL e2e.compute: vkCreatePipelineLayout %d\n", (int)status);
		vulkan_close(&vulkan);
		return 1;
	}
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = module;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = layout;
	status = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1U, &pipeline_info, NULL, &pipeline);
	if (status != VK_SUCCESS) {
		printf("FAIL e2e.compute: vkCreateComputePipelines %d\n", (int)status);
		vulkan_close(&vulkan);
		return 1;
	}

	/* The descriptor set naming the two buffers. */
	pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_size.descriptorCount = 2U;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = 1U;
	pool_info.poolSizeCount = 1U;
	pool_info.pPoolSizes = &pool_size;
	status = vkCreateDescriptorPool(vulkan.device, &pool_info, NULL, &pool);
	if (status != VK_SUCCESS) {
		printf("FAIL e2e.compute: vkCreateDescriptorPool %d\n", (int)status);
		vulkan_close(&vulkan);
		return 1;
	}
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_info.descriptorPool = pool;
	set_info.descriptorSetCount = 1U;
	set_info.pSetLayouts = &set_layout;
	status = vkAllocateDescriptorSets(vulkan.device, &set_info, &set);
	if (status != VK_SUCCESS) {
		printf("FAIL e2e.compute: vkAllocateDescriptorSets %d\n", (int)status);
		vulkan_close(&vulkan);
		return 1;
	}
	buffer_infos[0].buffer = input.buffer;
	buffer_infos[1].buffer = output.buffer;
	memset(writes, 0, sizeof(writes));
	for (index = 0U; index < 2U; index++) {
		buffer_infos[index].offset = 0U;
		buffer_infos[index].range = VK_WHOLE_SIZE;
		writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[index].dstSet = set;
		writes[index].dstBinding = index;
		writes[index].descriptorCount = 1U;
		writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[index].pBufferInfo = &buffer_infos[index];
	}
	vkUpdateDescriptorSets(vulkan.device, 2U, writes, 0U, NULL);

	/* The dispatch, then the copy of the output to the readback buffer. */
	error = record_begin(&vulkan);
	if (error == 0) {
		barrier(&vulkan, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		vkCmdBindPipeline(vulkan.commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(vulkan.commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0U, 1U, &set, 0U, NULL);
		vkCmdDispatch(vulkan.commands, COMPUTE_COUNT / COMPUTE_GROUP, 1U, 1U);
		barrier(&vulkan, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		region.srcOffset = 0U;
		region.dstOffset = 0U;
		region.size = 4U * COMPUTE_COUNT;
		vkCmdCopyBuffer(vulkan.commands, output.buffer, readback.buffer, 1U, &region);
		barrier(&vulkan, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
		error = record_submit(&vulkan);
	}

	/* Every invocation's word. */
	wrong = 0U;
	words = readback.mapped;
	for (index = 0U; error == 0 && index < COMPUTE_COUNT; index++) {
		expected = values[index] * 3U + (index ^ 0x5aU);
		if (words[index] != expected) {
			if (wrong < 4U)
				printf("  word %u: %08x, expected %08x\n", index, words[index], expected);
			wrong++;
		}
	}

	/* The objects go. */
	vkDestroyPipeline(vulkan.device, pipeline, NULL);
	vkDestroyDescriptorPool(vulkan.device, pool, NULL);
	vkDestroyPipelineLayout(vulkan.device, layout, NULL);
	vkDestroyDescriptorSetLayout(vulkan.device, set_layout, NULL);
	vkDestroyShaderModule(vulkan.device, module, NULL);
	buffer_drop(&vulkan, &readback);
	buffer_drop(&vulkan, &output);
	buffer_drop(&vulkan, &input);
	vulkan_close(&vulkan);

	/* The submission ran, and every word is the module's. */
	if (error != 0) {
		printf("FAIL e2e.compute: the submission\n");
		return 1;
	}
	if (wrong != 0U) {
		printf("FAIL e2e.compute: %u words differ\n", wrong);
		return 1;
	}

	/* Succeeded: the dispatch is right. */
	printf("PASS e2e.compute: %u invocations\n", COMPUTE_COUNT);
	return 0;
}

/*
 * Renders one frame of vkdemo's offscreen renderer and writes it out; the
 * script's oracle judges the pixels.
 */
static int
case_vkdemo(
	const char *path)
{
	struct measure frame;
	char digest[65];
	int error;

	/* The renderer's objects: the device, the texture's upload, the pipeline. */
	error = vkdemo_initialize(0U, 1, 1);
	if (error != 0) {
		printf("FAIL e2e.vkdemo: initialization at %s (%d)\n", vkdemo_error_operation(), (int)vkdemo_error_code());
		(void)vkdemo_close();
		return 1;
	}

	/* One frame: the recording, the submission, the wait and the readback. */
	measure_start(&frame);
	error = vkdemo_render(VKDEMO_TIME_MS, 1U, digest);
	measure_report("vkdemo frame", &frame);
	if (error != 0) {
		printf("FAIL e2e.vkdemo: the frame at %s (%d)\n", vkdemo_error_operation(), (int)vkdemo_error_code());
		(void)vkdemo_close();
		return 1;
	}

	/* The frame for the oracle. */
	error = vkdemo_write_frame(path);
	if (error != 0) {
		printf("FAIL e2e.vkdemo: the frame could not be written to %s\n", path);
		(void)vkdemo_close();
		return 1;
	}

	/* The renderer's objects go. */
	error = vkdemo_close();
	if (error != 0) {
		printf("FAIL e2e.vkdemo: the close at %s (%d)\n", vkdemo_error_operation(), (int)vkdemo_error_code());
		return 1;
	}

	/* Succeeded: the frame is written (the oracle decides). */
	printf("PASS e2e.vkdemo: frame %s sha256 %s\n", path, digest);
	return 0;
}

/* Takes the counts at a case's start. */
static void
measure_start(
	struct measure *measure)
{
	atg_link_stats(&measure->link);
	e2e_core_stats(&measure->core);
}

/* Prints what a case took: the GPU's cycles, the firmware's, the µops, the round trips. */
static void
measure_report(
	const char *name,
	const struct measure *measure)
{
	struct atg_link_stats link;
	struct e2e_core_stats core;
	uint64_t commands;
	uint64_t jobs;
	uint64_t command_average;
	uint64_t job_average;

	/* The counts now, and the averages of the round trips. */
	atg_link_stats(&link);
	e2e_core_stats(&core);
	commands = core.commands - measure->core.commands;
	jobs = core.jobs - measure->core.jobs;
	command_average = 0U;
	if (commands != 0U)
		command_average = (core.command_cycles - measure->core.command_cycles) / commands;
	job_average = 0U;
	if (jobs != 0U)
		job_average = (core.job_cycles - measure->core.job_cycles) / jobs;

	/* One line of the totals, and the round trips. */
	printf("  measure %s: %llu cycles, firmware in packets %llu, µop words %llu, register reads %llu writes %llu, stray %llu\n",
	    name,
	    (unsigned long long)(link.cycles - measure->link.cycles),
	    (unsigned long long)(link.fw_packet_cycles - measure->link.fw_packet_cycles),
	    (unsigned long long)(link.uop_words - measure->link.uop_words),
	    (unsigned long long)(link.register_reads - measure->link.register_reads),
	    (unsigned long long)(link.register_writes - measure->link.register_writes),
	    (unsigned long long)(link.stray_accesses - measure->link.stray_accesses));
	if (commands != 0U || jobs != 0U) {
		printf("  measure %s: %llu transactions, %llu cycles each on average; %llu jobs, %llu cycles each on average\n",
		    name,
		    (unsigned long long)commands,
		    (unsigned long long)command_average,
		    (unsigned long long)jobs,
		    (unsigned long long)job_average);
	}
	fflush(stdout);
}

/* Reads a SPIR-V module from a file into words the caller frees, or NULL. */
static uint32_t *
module_read(
	const char *path,
	size_t *bytes)
{
	FILE *file;
	uint32_t *words;
	long length;
	size_t got;

	/* The file and its length. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	(void)fseek(file, 0L, SEEK_END);
	length = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);
	if (length <= 0 || (length % 4) != 0) {
		fclose(file);
		return NULL;
	}

	/* Its words. */
	words = malloc((size_t)length);
	if (words == NULL) {
		fclose(file);
		return NULL;
	}
	got = fread(words, 1U, (size_t)length, file);
	fclose(file);
	if (got != (size_t)length) {
		free(words);
		return NULL;
	}

	/* Succeeded: the module. */
	*bytes = (size_t)length;
	return words;
}
