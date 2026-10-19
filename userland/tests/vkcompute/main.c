/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * vkcompute: the smallest Vulkan compute test (ws269-p009).  It creates an
 * instance and a device on the first physical device, dispatches the fill
 * shader (data[x] = 3x + 7, local size 32) over one host-visible storage
 * buffer of 4096 words in 128 workgroups, waits on a fence, maps the memory
 * and counts the words that hold the expected value.  It prints one line:
 *
 *   vkcompute: <device>: PASS, 4096/4096 words
 *   vkcompute: <device>: FAIL, <n>/4096 words (first bad <i>: <value>, expected <value>)
 *   vkcompute: <device>: FAIL, <call> <VkResult>
 *   vkcompute: no Vulkan device (<call> <VkResult>)
 *
 * The exit status is 0 only on PASS.
 */

#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shader.h"

/* The shader's workgroup size (local_size_x in fill.comp). */
#define VKCOMPUTE_LOCAL_SIZE	32U

/* The workgroups dispatched; 128 is the most one submit accepts on PowerVR. */
#define VKCOMPUTE_GROUPS	128U

/* The words the dispatch writes. */
#define VKCOMPUTE_WORDS		(VKCOMPUTE_LOCAL_SIZE * VKCOMPUTE_GROUPS)

/* The shader's multiplier and addend. */
#define VKCOMPUTE_MULTIPLIER	3U
#define VKCOMPUTE_ADDEND	7U

/* The fence wait's limit in nanoseconds (5 s, above the driver's 2 s). */
#define VKCOMPUTE_WAIT_NS	5000000000ULL

/*
 * The Vulkan objects of one run.
 *
 * Every handle starts as VK_NULL_HANDLE and is destroyed by
 * vkcompute_destroy() whether or not the run got as far as creating it.
 */
struct vkcompute {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t family;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkShaderModule module;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkDescriptorPool pool;
	VkDescriptorSet set;
	VkCommandPool command_pool;
	VkCommandBuffer command;
	VkFence fence;
	char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
	const char *failed_call;
};

static VkResult vkcompute_open_device(struct vkcompute *run);
static VkResult vkcompute_create_buffer(struct vkcompute *run);
static VkResult vkcompute_create_pipeline(struct vkcompute *run);
static VkResult vkcompute_bind_buffer(struct vkcompute *run);
static VkResult vkcompute_submit(struct vkcompute *run);
static int vkcompute_verify(struct vkcompute *run);
static void vkcompute_destroy(struct vkcompute *run);

/*
 * Runs the fill shader once and prints the result line.
 */
int
main(
	void)
{
	struct vkcompute run;
	VkResult status;
	int failed;

	/* Starts with no objects, so the teardown can run from any point. */
	memset(&run, 0, sizeof(run));

	/* Opens the instance and the device; with none, nothing else can run. */
	status = vkcompute_open_device(&run);
	if (status != VK_SUCCESS && run.name[0] == '\0') {
		printf("vkcompute: no Vulkan device (%s %d)\n", run.failed_call, (int)status);
		vkcompute_destroy(&run);
		return 1;
	}

	/* Builds the buffer, the pipeline and the descriptor, then dispatches. */
	if (status == VK_SUCCESS)
		status = vkcompute_create_buffer(&run);
	if (status == VK_SUCCESS)
		status = vkcompute_create_pipeline(&run);
	if (status == VK_SUCCESS)
		status = vkcompute_bind_buffer(&run);
	if (status == VK_SUCCESS)
		status = vkcompute_submit(&run);

	/* Reports the call that failed before any word could be read back. */
	if (status != VK_SUCCESS) {
		printf("vkcompute: %s: FAIL, %s %d\n", run.name, run.failed_call, (int)status);
		vkcompute_destroy(&run);
		return 1;
	}

	/* Counts the words the shader wrote and prints the verdict. */
	failed = vkcompute_verify(&run);
	vkcompute_destroy(&run);

	/* Reports the verdict as the exit status. */
	if (failed != 0)
		return 1;

	/* Succeeded: every word holds 3x + 7. */
	return 0;
}

/* Creates the instance and the device on the first physical device's compute family. */
static VkResult
vkcompute_open_device(
	struct vkcompute *run)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance_info;
	VkPhysicalDeviceProperties properties;
	VkQueueFamilyProperties families[8];
	VkDeviceQueueCreateInfo queue_info;
	VkDeviceCreateInfo device_info;
	uint32_t count;
	uint32_t index;
	float priority;
	VkResult status;

	/* Creates a Vulkan 1.0 instance with no layer and no extension. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "vkcompute";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance_info, 0, sizeof(instance_info));
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application;
	run->failed_call = "vkCreateInstance";
	status = vkCreateInstance(&instance_info, NULL, &run->instance);
	if (status != VK_SUCCESS)
		return status;

	/* Takes the first physical device; VK_INCOMPLETE only means there are more. */
	count = 1;
	run->failed_call = "vkEnumeratePhysicalDevices";
	status = vkEnumeratePhysicalDevices(run->instance, &count, &run->physical);
	if (status != VK_SUCCESS && status != VK_INCOMPLETE)
		return status;
	if (count == 0)
		return VK_ERROR_INITIALIZATION_FAILED;

	/* Names the device in every later line. */
	vkGetPhysicalDeviceProperties(run->physical, &properties);
	memcpy(run->name, properties.deviceName, sizeof(run->name));
	run->name[sizeof(run->name) - 1] = '\0';

	/* Finds the first family with compute. */
	count = sizeof(families) / sizeof(families[0]);
	vkGetPhysicalDeviceQueueFamilyProperties(run->physical, &count, families);
	run->failed_call = "compute queue family";
	for (index = 0; index < count; index++) {
		if ((families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
			break;
	}

	/* Refuses a device without compute. */
	if (index == count)
		return VK_ERROR_FEATURE_NOT_PRESENT;

	/* Creates the device with one queue of that family. */
	run->family = index;
	priority = 1.0f;
	memset(&queue_info, 0, sizeof(queue_info));
	queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = run->family;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &priority;
	memset(&device_info, 0, sizeof(device_info));
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	run->failed_call = "vkCreateDevice";
	status = vkCreateDevice(run->physical, &device_info, NULL, &run->device);
	if (status != VK_SUCCESS)
		return status;

	/* Takes the queue the dispatch is submitted to. */
	vkGetDeviceQueue(run->device, run->family, 0, &run->queue);

	/* Succeeded: the device and its queue are open. */
	return VK_SUCCESS;
}

/* Creates the storage buffer in host-visible, host-coherent memory and clears it. */
static VkResult
vkcompute_create_buffer(
	struct vkcompute *run)
{
	VkBufferCreateInfo buffer_info;
	VkMemoryRequirements requirements;
	VkPhysicalDeviceMemoryProperties memory;
	VkMemoryAllocateInfo allocate_info;
	VkMemoryPropertyFlags wanted;
	uint32_t type;
	void *words;
	VkResult status;

	/* Creates a buffer of VKCOMPUTE_WORDS 32-bit words. */
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = VKCOMPUTE_WORDS * sizeof(uint32_t);
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	run->failed_call = "vkCreateBuffer";
	status = vkCreateBuffer(run->device, &buffer_info, NULL, &run->buffer);
	if (status != VK_SUCCESS)
		return status;

	/* Finds a type the buffer accepts that the CPU sees without flushes. */
	vkGetBufferMemoryRequirements(run->device, run->buffer, &requirements);
	vkGetPhysicalDeviceMemoryProperties(run->physical, &memory);
	wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (type = 0; type < memory.memoryTypeCount; type++) {
		if ((requirements.memoryTypeBits & (1U << type)) == 0)
			continue;
		if ((memory.memoryTypes[type].propertyFlags & wanted) == wanted)
			break;
	}

	/* Refuses a device with no such type. */
	run->failed_call = "host-coherent memory type";
	if (type == memory.memoryTypeCount)
		return VK_ERROR_FEATURE_NOT_PRESENT;

	/* Allocates the memory and binds the buffer to it. */
	memset(&allocate_info, 0, sizeof(allocate_info));
	allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = type;
	run->failed_call = "vkAllocateMemory";
	status = vkAllocateMemory(run->device, &allocate_info, NULL, &run->memory);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkBindBufferMemory";
	status = vkBindBufferMemory(run->device, run->buffer, run->memory, 0);
	if (status != VK_SUCCESS)
		return status;

	/* Fills the words with a value the shader never writes, so stale memory fails. */
	run->failed_call = "vkMapMemory";
	status = vkMapMemory(run->device, run->memory, 0, VK_WHOLE_SIZE, 0, &words);
	if (status != VK_SUCCESS)
		return status;
	memset(words, 0xa5, VKCOMPUTE_WORDS * sizeof(uint32_t));
	vkUnmapMemory(run->device, run->memory);

	/* Succeeded: the buffer is bound and holds the fill pattern. */
	return VK_SUCCESS;
}

/* Creates the shader module, the layouts and the compute pipeline. */
static VkResult
vkcompute_create_pipeline(
	struct vkcompute *run)
{
	VkShaderModuleCreateInfo module_info;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo set_info;
	VkPipelineLayoutCreateInfo layout_info;
	VkComputePipelineCreateInfo pipeline_info;
	VkResult status;

	/* Creates the module from the embedded SPIR-V. */
	memset(&module_info, 0, sizeof(module_info));
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = sizeof(vkcompute_fill_shader);
	module_info.pCode = vkcompute_fill_shader;
	run->failed_call = "vkCreateShaderModule";
	status = vkCreateShaderModule(run->device, &module_info, NULL, &run->module);
	if (status != VK_SUCCESS)
		return status;

	/* Describes set 0 as one storage buffer at binding 0. */
	memset(&binding, 0, sizeof(binding));
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_info.bindingCount = 1;
	set_info.pBindings = &binding;
	run->failed_call = "vkCreateDescriptorSetLayout";
	status = vkCreateDescriptorSetLayout(run->device, &set_info, NULL, &run->set_layout);
	if (status != VK_SUCCESS)
		return status;

	/* Creates the pipeline layout with that set and no push constant. */
	memset(&layout_info, 0, sizeof(layout_info));
	layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout_info.setLayoutCount = 1;
	layout_info.pSetLayouts = &run->set_layout;
	run->failed_call = "vkCreatePipelineLayout";
	status = vkCreatePipelineLayout(run->device, &layout_info, NULL, &run->pipeline_layout);
	if (status != VK_SUCCESS)
		return status;

	/* Creates the compute pipeline from the module's main. */
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = run->module;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = run->pipeline_layout;
	run->failed_call = "vkCreateComputePipelines";
	status = vkCreateComputePipelines(run->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &run->pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the pipeline is ready to bind. */
	return VK_SUCCESS;
}

/* Allocates the descriptor set and points it at the buffer. */
static VkResult
vkcompute_bind_buffer(
	struct vkcompute *run)
{
	VkDescriptorPoolSize size;
	VkDescriptorPoolCreateInfo pool_info;
	VkDescriptorSetAllocateInfo set_info;
	VkDescriptorBufferInfo buffer_info;
	VkWriteDescriptorSet write;
	VkResult status;

	/* Creates a pool for the one set. */
	memset(&size, 0, sizeof(size));
	size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	size.descriptorCount = 1;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &size;
	run->failed_call = "vkCreateDescriptorPool";
	status = vkCreateDescriptorPool(run->device, &pool_info, NULL, &run->pool);
	if (status != VK_SUCCESS)
		return status;

	/* Allocates the set. */
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_info.descriptorPool = run->pool;
	set_info.descriptorSetCount = 1;
	set_info.pSetLayouts = &run->set_layout;
	run->failed_call = "vkAllocateDescriptorSets";
	status = vkAllocateDescriptorSets(run->device, &set_info, &run->set);
	if (status != VK_SUCCESS)
		return status;

	/* Writes the whole buffer into binding 0. */
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.buffer = run->buffer;
	buffer_info.offset = 0;
	buffer_info.range = VK_WHOLE_SIZE;
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = run->set;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.pBufferInfo = &buffer_info;
	vkUpdateDescriptorSets(run->device, 1, &write, 0, NULL);

	/* Succeeded: the set names the buffer. */
	return VK_SUCCESS;
}

/* Records the dispatch, submits it with a fence and waits for the fence. */
static VkResult
vkcompute_submit(
	struct vkcompute *run)
{
	VkCommandPoolCreateInfo pool_info;
	VkCommandBufferAllocateInfo command_info;
	VkCommandBufferBeginInfo begin;
	VkMemoryBarrier barrier;
	VkFenceCreateInfo fence_info;
	VkSubmitInfo submit;
	VkResult status;

	/* Creates the command pool and one primary command buffer. */
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex = run->family;
	run->failed_call = "vkCreateCommandPool";
	status = vkCreateCommandPool(run->device, &pool_info, NULL, &run->command_pool);
	if (status != VK_SUCCESS)
		return status;
	memset(&command_info, 0, sizeof(command_info));
	command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_info.commandPool = run->command_pool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = 1;
	run->failed_call = "vkAllocateCommandBuffers";
	status = vkAllocateCommandBuffers(run->device, &command_info, &run->command);
	if (status != VK_SUCCESS)
		return status;

	/* Records bind, dispatch and the barrier that makes the writes visible to the host. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	run->failed_call = "vkBeginCommandBuffer";
	status = vkBeginCommandBuffer(run->command, &begin);
	if (status != VK_SUCCESS)
		return status;
	vkCmdBindPipeline(run->command, VK_PIPELINE_BIND_POINT_COMPUTE, run->pipeline);
	vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_COMPUTE, run->pipeline_layout, 0, 1, &run->set, 0, NULL);
	vkCmdDispatch(run->command, VKCOMPUTE_GROUPS, 1, 1);
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(
		run->command,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_HOST_BIT,
		0,
		1,
		&barrier,
		0,
		NULL,
		0,
		NULL);
	run->failed_call = "vkEndCommandBuffer";
	status = vkEndCommandBuffer(run->command);
	if (status != VK_SUCCESS)
		return status;

	/* Creates the fence the submit signals. */
	memset(&fence_info, 0, sizeof(fence_info));
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	run->failed_call = "vkCreateFence";
	status = vkCreateFence(run->device, &fence_info, NULL, &run->fence);
	if (status != VK_SUCCESS)
		return status;

	/* Submits the command buffer. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &run->command;
	run->failed_call = "vkQueueSubmit";
	status = vkQueueSubmit(run->queue, 1, &submit, run->fence);
	if (status != VK_SUCCESS)
		return status;

	/* Waits for the dispatch; VK_TIMEOUT is reported like any failure. */
	run->failed_call = "vkWaitForFences";
	status = vkWaitForFences(run->device, 1, &run->fence, VK_TRUE, VKCOMPUTE_WAIT_NS);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the dispatch has finished. */
	return VK_SUCCESS;
}

/* Counts the words that hold 3x + 7 and prints the verdict; returns 1 on failure. */
static int
vkcompute_verify(
	struct vkcompute *run)
{
	const uint32_t *words;
	void *mapped;
	uint32_t index;
	uint32_t expected;
	uint32_t good;
	uint32_t first_bad;
	VkResult status;

	/* Maps the memory the shader wrote. */
	status = vkMapMemory(run->device, run->memory, 0, VK_WHOLE_SIZE, 0, &mapped);
	if (status != VK_SUCCESS) {
		printf("vkcompute: %s: FAIL, vkMapMemory %d\n", run->name, (int)status);
		return 1;
	}

	/* Counts the good words and remembers the first bad one. */
	words = mapped;
	good = 0;
	first_bad = VKCOMPUTE_WORDS;
	for (index = 0; index < VKCOMPUTE_WORDS; index++) {
		expected = index * VKCOMPUTE_MULTIPLIER + VKCOMPUTE_ADDEND;
		if (words[index] == expected) {
			good++;
		} else if (first_bad == VKCOMPUTE_WORDS) {
			first_bad = index;
		}
	}

	/* Prints the verdict line. */
	if (good == VKCOMPUTE_WORDS) {
		printf("vkcompute: %s: PASS, %u/%u words\n", run->name, good, VKCOMPUTE_WORDS);
	} else {
		expected = first_bad * VKCOMPUTE_MULTIPLIER + VKCOMPUTE_ADDEND;
		printf("vkcompute: %s: FAIL, %u/%u words (first bad %u: 0x%08x, expected 0x%08x)\n",
		       run->name,
		       good,
		       VKCOMPUTE_WORDS,
		       first_bad,
		       words[first_bad],
		       expected);
	}
	vkUnmapMemory(run->device, run->memory);

	/* Reports a mismatch as a failure. */
	if (good != VKCOMPUTE_WORDS)
		return 1;

	/* Succeeded: every word matched. */
	return 0;
}

/* Destroys every object the run created, newest first. */
static void
vkcompute_destroy(
	struct vkcompute *run)
{
	/* Leaves the instance alone when no device was created. */
	if (run->device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(run->device);
		vkDestroyFence(run->device, run->fence, NULL);
		vkDestroyCommandPool(run->device, run->command_pool, NULL);
		vkDestroyDescriptorPool(run->device, run->pool, NULL);
		vkDestroyPipeline(run->device, run->pipeline, NULL);
		vkDestroyPipelineLayout(run->device, run->pipeline_layout, NULL);
		vkDestroyDescriptorSetLayout(run->device, run->set_layout, NULL);
		vkDestroyShaderModule(run->device, run->module, NULL);
		vkDestroyBuffer(run->device, run->buffer, NULL);
		vkFreeMemory(run->device, run->memory, NULL);
		vkDestroyDevice(run->device, NULL);
	}

	/* Destroys the instance last. */
	if (run->instance != VK_NULL_HANDLE)
		vkDestroyInstance(run->instance, NULL);
}
