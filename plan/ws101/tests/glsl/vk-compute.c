/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws101-p008: runs compute shaders zedBSD's GLSL compiler made on the
 * host's Vulkan (lavapipe) and compares the buffers they leave with what
 * C computes from the same inputs.
 *
 *   vk-compute DIR       DIR holds the pass/ shaders of this directory
 *
 * Each test compiles and links its shader with the compiler, binds a
 * host-visible buffer at each binding the program uses (a storage block's
 * at GLSL_STORAGE_FIRST_BINDING + its binding, the default uniform block's
 * at 0 with the uniforms at the std140 offsets the link gave), dispatches
 * and reads the buffers back.  Nothing here is evidence about the i915
 * executor; it shows that the SPIR-V means what the GLSL means.
 */

#include "../../../../userland/desktop/libglesv2/glsl/glsl.h"

#include <vulkan/vulkan.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The most buffers a test binds, and the bytes of each. */
#define RUN_MAX_BUFFERS		6U
#define RUN_BUFFER_BYTES	65536U

/* The descriptor binding of the default uniform block. */
#define RUN_UNIFORM_BINDING	0U

/* The Vulkan objects every test shares. */
struct run_context {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkPhysicalDeviceMemoryProperties memory;
	VkDevice device;
	VkQueue queue;
	uint32_t family;
	VkCommandPool pool;
};

/* One buffer of a test: its binding, whether it is the uniform block, and its words. */
struct run_buffer {
	uint32_t binding;
	int uniform;
	uint32_t words[RUN_BUFFER_BYTES / 4U];
	VkBuffer buffer;
	VkDeviceMemory memory;
	void *mapped;
};

/* A test's dispatch: its buffers and its groups. */
struct run_test {
	struct run_buffer buffers[RUN_MAX_BUFFERS];
	unsigned count;
	uint32_t groups[3];
};

static struct run_context run_context;
static const char *run_dir;

static int run_init(void);
static struct run_buffer *run_storage(struct run_test *test, unsigned binding, uint32_t fill);
static struct run_buffer *run_uniform(struct run_test *test);
static int run_dispatch(const char *name, struct run_test *test);
static int run_buffer_make(struct run_buffer *buffer);
static void run_buffer_free(struct run_buffer *buffer);
static int test_add(void);
static int test_ids(void);
static int test_atomic(void);
static int test_shared(void);
static int test_reduce(void);
static int test_length(void);
static int test_layout(void);
static int test_noct(void);
static int test_noct_ops(void);
static int test_loopret(void);

int
main(
	int argc,
	char **argv)
{
	int failures;

	/* The directory, and the device. */
	if (argc != 2) {
		fprintf(stderr, "usage: vk-compute DIR\n");
		return 2;
	}
	run_dir = argv[1];
	if (run_init() != 0) {
		printf("vk-compute: FAIL no Vulkan device with a compute queue\n");
		return 1;
	}

	/* Every test. */
	failures = 0;
	failures += test_add();
	failures += test_ids();
	failures += test_atomic();
	failures += test_shared();
	failures += test_reduce();
	failures += test_length();
	failures += test_layout();
	failures += test_noct();
	failures += test_noct_ops();
	failures += test_loopret();
	if (failures != 0) {
		printf("vk-compute: FAIL %d test(s)\n", failures);
		return 1;
	}
	printf("vk-compute: every shader computes on the host's Vulkan what C computes\n");
	return 0;
}

/* Makes the instance, the first device's compute queue and a command pool. */
static int
run_init(void)
{
	static const float priority = 1.0f;
	VkApplicationInfo application;
	VkInstanceCreateInfo instance_info;
	VkDeviceQueueCreateInfo queue_info;
	VkDeviceCreateInfo device_info;
	VkCommandPoolCreateInfo pool_info;
	VkQueueFamilyProperties families[16];
	uint32_t count;
	uint32_t index;

	/* The instance. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance_info, 0, sizeof(instance_info));
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application;
	if (vkCreateInstance(&instance_info, NULL, &run_context.instance) != VK_SUCCESS)
		return -1;

	/* The first physical device, and a queue family with compute. */
	count = 1U;
	if (vkEnumeratePhysicalDevices(run_context.instance, &count, &run_context.physical) != VK_SUCCESS && count == 0U)
		return -1;
	if (count == 0U)
		return -1;
	vkGetPhysicalDeviceMemoryProperties(run_context.physical, &run_context.memory);
	count = 16U;
	vkGetPhysicalDeviceQueueFamilyProperties(run_context.physical, &count, families);
	for (index = 0U; index < count; index++) {
		if ((families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U)
			break;
	}
	if (index == count)
		return -1;
	run_context.family = index;

	/* The device and its queue. */
	memset(&queue_info, 0, sizeof(queue_info));
	queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = run_context.family;
	queue_info.queueCount = 1U;
	queue_info.pQueuePriorities = &priority;
	memset(&device_info, 0, sizeof(device_info));
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount = 1U;
	device_info.pQueueCreateInfos = &queue_info;
	if (vkCreateDevice(run_context.physical, &device_info, NULL, &run_context.device) != VK_SUCCESS)
		return -1;
	vkGetDeviceQueue(run_context.device, run_context.family, 0U, &run_context.queue);

	/* The command pool. */
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex = run_context.family;
	if (vkCreateCommandPool(run_context.device, &pool_info, NULL, &run_context.pool) != VK_SUCCESS)
		return -1;
	return 0;
}

/* Adds a storage buffer at a GL binding, every word `fill`. */
static struct run_buffer *
run_storage(
	struct run_test *test,
	unsigned binding,
	uint32_t fill)
{
	struct run_buffer *buffer;
	unsigned index;

	buffer = &test->buffers[test->count++];
	buffer->binding = GLSL_STORAGE_FIRST_BINDING + binding;
	buffer->uniform = 0;
	for (index = 0U; index < RUN_BUFFER_BYTES / 4U; index++)
		buffer->words[index] = fill;
	return buffer;
}

/* Adds the default uniform block's buffer, all zero. */
static struct run_buffer *
run_uniform(
	struct run_test *test)
{
	struct run_buffer *buffer;

	buffer = &test->buffers[test->count++];
	buffer->binding = RUN_UNIFORM_BINDING;
	buffer->uniform = 1;
	memset(buffer->words, 0, sizeof(buffer->words));
	return buffer;
}

/*
 * Compiles DIR/NAME.comp, dispatches it over the test's buffers and reads
 * them back into their words.  Returns 0, or 1 with the reason printed.
 */
static int
run_dispatch(
	const char *name,
	struct run_test *test)
{
	VkDescriptorSetLayoutBinding bindings[RUN_MAX_BUFFERS];
	VkDescriptorSetLayoutCreateInfo layout_info;
	VkPipelineLayoutCreateInfo pipeline_layout_info;
	VkShaderModuleCreateInfo module_info;
	VkComputePipelineCreateInfo pipeline_info;
	VkDescriptorPoolSize sizes[2];
	VkDescriptorPoolCreateInfo pool_info;
	VkDescriptorSetAllocateInfo set_info;
	VkDescriptorBufferInfo buffer_infos[RUN_MAX_BUFFERS];
	VkWriteDescriptorSet writes[RUN_MAX_BUFFERS];
	VkCommandBufferAllocateInfo command_info;
	VkCommandBufferBeginInfo begin;
	VkSubmitInfo submit;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout pipeline_layout;
	VkShaderModule module;
	VkPipeline pipeline;
	VkDescriptorPool pool;
	VkDescriptorSet set;
	VkCommandBuffer command;
	struct glsl_program program;
	struct glsl_shader *shader;
	char path[512];
	char *source;
	char *log;
	FILE *file;
	long size;
	unsigned index;
	int status;

	/* The shader, compiled and linked. */
	snprintf(path, sizeof(path), "%s/%s.comp", run_dir, name);
	file = fopen(path, "rb");
	if (file == NULL) {
		printf("%s: FAIL cannot read %s\n", name, path);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	source = calloc((size_t)size + 1U, 1U);
	if (source == NULL || fread(source, 1U, (size_t)size, file) != (size_t)size) {
		fclose(file);
		printf("%s: FAIL cannot read %s\n", name, path);
		return 1;
	}
	fclose(file);
	shader = glsl_compile(GLSL_STAGE_COMPUTE, source, 100U, &log);
	free(source);
	if (shader == NULL) {
		printf("%s: FAIL compile: %s\n", name, log != NULL ? log : "?");
		free(log);
		return 1;
	}
	free(log);
	status = glsl_link_compute(shader, &program, &log);
	glsl_shader_free(shader);
	if (status != 0) {
		printf("%s: FAIL link: %s\n", name, log != NULL ? log : "?");
		free(log);
		return 1;
	}

	/* The buffers and their bindings. */
	for (index = 0U; index < test->count; index++) {
		if (run_buffer_make(&test->buffers[index]) != 0) {
			printf("%s: FAIL buffer\n", name);
			glsl_program_free(&program);
			return 1;
		}
		memset(&bindings[index], 0, sizeof(bindings[index]));
		bindings[index].binding = test->buffers[index].binding;
		bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		if (test->buffers[index].uniform)
			bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[index].descriptorCount = 1U;
		bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}

	/* The layouts, the module and the pipeline. */
	memset(&layout_info, 0, sizeof(layout_info));
	layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_info.bindingCount = test->count;
	layout_info.pBindings = bindings;
	vkCreateDescriptorSetLayout(run_context.device, &layout_info, NULL, &set_layout);
	memset(&pipeline_layout_info, 0, sizeof(pipeline_layout_info));
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1U;
	pipeline_layout_info.pSetLayouts = &set_layout;
	vkCreatePipelineLayout(run_context.device, &pipeline_layout_info, NULL, &pipeline_layout);
	memset(&module_info, 0, sizeof(module_info));
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = program.words[GLSL_STAGE_COMPUTE] * 4U;
	module_info.pCode = program.code[GLSL_STAGE_COMPUTE];
	vkCreateShaderModule(run_context.device, &module_info, NULL, &module);
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = module;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = pipeline_layout;
	if (vkCreateComputePipelines(run_context.device, VK_NULL_HANDLE, 1U, &pipeline_info, NULL, &pipeline) != VK_SUCCESS) {
		printf("%s: FAIL the pipeline was not made\n", name);
		glsl_program_free(&program);
		return 1;
	}
	glsl_program_free(&program);

	/* The set. */
	sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	sizes[0].descriptorCount = RUN_MAX_BUFFERS;
	sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	sizes[1].descriptorCount = 1U;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = 1U;
	pool_info.poolSizeCount = 2U;
	pool_info.pPoolSizes = sizes;
	vkCreateDescriptorPool(run_context.device, &pool_info, NULL, &pool);
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_info.descriptorPool = pool;
	set_info.descriptorSetCount = 1U;
	set_info.pSetLayouts = &set_layout;
	vkAllocateDescriptorSets(run_context.device, &set_info, &set);
	for (index = 0U; index < test->count; index++) {
		buffer_infos[index].buffer = test->buffers[index].buffer;
		buffer_infos[index].offset = 0U;
		buffer_infos[index].range = VK_WHOLE_SIZE;
		memset(&writes[index], 0, sizeof(writes[index]));
		writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[index].dstSet = set;
		writes[index].dstBinding = test->buffers[index].binding;
		writes[index].descriptorCount = 1U;
		writes[index].descriptorType = bindings[index].descriptorType;
		writes[index].pBufferInfo = &buffer_infos[index];
	}
	vkUpdateDescriptorSets(run_context.device, test->count, writes, 0U, NULL);

	/* The dispatch. */
	memset(&command_info, 0, sizeof(command_info));
	command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_info.commandPool = run_context.pool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = 1U;
	vkAllocateCommandBuffers(run_context.device, &command_info, &command);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(command, &begin);
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0U, 1U, &set, 0U, NULL);
	vkCmdDispatch(command, test->groups[0], test->groups[1], test->groups[2]);
	vkEndCommandBuffer(command);
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &command;
	vkQueueSubmit(run_context.queue, 1U, &submit, VK_NULL_HANDLE);
	vkQueueWaitIdle(run_context.queue);

	/* The words back, and everything given back. */
	for (index = 0U; index < test->count; index++) {
		memcpy(test->buffers[index].words, test->buffers[index].mapped, RUN_BUFFER_BYTES);
		run_buffer_free(&test->buffers[index]);
	}
	vkFreeCommandBuffers(run_context.device, run_context.pool, 1U, &command);
	vkDestroyDescriptorPool(run_context.device, pool, NULL);
	vkDestroyPipeline(run_context.device, pipeline, NULL);
	vkDestroyShaderModule(run_context.device, module, NULL);
	vkDestroyPipelineLayout(run_context.device, pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(run_context.device, set_layout, NULL);
	return 0;
}

/* Makes a host-visible, coherent buffer holding the words. */
static int
run_buffer_make(
	struct run_buffer *buffer)
{
	VkBufferCreateInfo info;
	VkMemoryAllocateInfo allocate;
	VkMemoryRequirements requirements;
	VkMemoryPropertyFlags wanted;
	uint32_t type;

	/* The buffer. */
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = RUN_BUFFER_BYTES;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	if (vkCreateBuffer(run_context.device, &info, NULL, &buffer->buffer) != VK_SUCCESS)
		return -1;

	/* Its memory: host visible and coherent. */
	vkGetBufferMemoryRequirements(run_context.device, buffer->buffer, &requirements);
	wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (type = 0U; type < run_context.memory.memoryTypeCount; type++) {
		if ((requirements.memoryTypeBits & (1U << type)) != 0U &&
		    (run_context.memory.memoryTypes[type].propertyFlags & wanted) == wanted)
			break;
	}
	if (type == run_context.memory.memoryTypeCount)
		return -1;
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = type;
	if (vkAllocateMemory(run_context.device, &allocate, NULL, &buffer->memory) != VK_SUCCESS)
		return -1;
	vkBindBufferMemory(run_context.device, buffer->buffer, buffer->memory, 0U);

	/* The words written into it. */
	vkMapMemory(run_context.device, buffer->memory, 0U, RUN_BUFFER_BYTES, 0U, &buffer->mapped);
	memcpy(buffer->mapped, buffer->words, RUN_BUFFER_BYTES);
	return 0;
}

/* Gives a buffer back. */
static void
run_buffer_free(
	struct run_buffer *buffer)
{
	vkUnmapMemory(run_context.device, buffer->memory);
	vkDestroyBuffer(run_context.device, buffer->buffer, NULL);
	vkFreeMemory(run_context.device, buffer->memory, NULL);
}

/* add.comp: c[i] = a[i] + b[i] + k for i below n (uniforms n at 0, k at 4); the others untouched. */
static int
test_add(void)
{
	static struct run_test test;
	struct run_buffer *a;
	struct run_buffer *b;
	struct run_buffer *c;
	struct run_buffer *u;
	uint32_t i;
	uint32_t want;

	memset(&test, 0, sizeof(test));
	a = run_storage(&test, 0U, 0U);
	b = run_storage(&test, 1U, 0U);
	c = run_storage(&test, 2U, 0xdeadbeefU);
	u = run_uniform(&test);
	for (i = 0U; i < 1024U; i++) {
		a->words[i] = i * 2654435761U;
		b->words[i] = i ^ 0x5a5a5a5aU;
	}
	u->words[0] = 1000U;
	u->words[1] = 77U;
	test.groups[0] = 16U;
	test.groups[1] = 1U;
	test.groups[2] = 1U;
	if (run_dispatch("add", &test) != 0)
		return 1;
	for (i = 0U; i < 1100U; i++) {
		want = 0xdeadbeefU;
		if (i < 1000U)
			want = a->words[i] + b->words[i] + 77U;
		if (c->words[i] != want) {
			printf("add: FAIL c[%u] = 0x%08x, expected 0x%08x\n", i, c->words[i], want);
			return 1;
		}
	}
	printf("add: 1000 of 1024 invocations add, the rest return; uniforms and three storage blocks\n");
	return 0;
}

/* ids.comp: every built-in of a (3, 2, 2) dispatch of (4, 2, 3) groups, gl_WorkGroupSize among them. */
static int
test_ids(void)
{
	static struct run_test test;
	struct run_buffer *o;
	uint32_t want[13];
	uint32_t x, y, z;
	uint32_t linear;
	uint32_t k;

	memset(&test, 0, sizeof(test));
	o = run_storage(&test, 3U, 0xdeadbeefU);
	test.groups[0] = 3U;
	test.groups[1] = 2U;
	test.groups[2] = 2U;
	if (run_dispatch("ids", &test) != 0)
		return 1;
	for (z = 0U; z < 6U; z++) {
		for (y = 0U; y < 4U; y++) {
			for (x = 0U; x < 12U; x++) {
				linear = x + 12U * (y + 4U * z);
				want[0] = x % 4U;
				want[1] = y % 2U;
				want[2] = z % 3U;
				want[3] = want[0] + 4U * want[1] + 8U * want[2];
				want[4] = x / 4U;
				want[5] = y / 2U;
				want[6] = z / 3U;
				want[7] = 3U;
				want[8] = 2U;
				want[9] = 2U;
				want[10] = x;
				want[11] = y;
				want[12] = z;
				for (k = 0U; k < 13U; k++) {
					if (o->words[linear * 13U + k] != want[k]) {
						printf("ids: FAIL (%u %u %u) word %u = %u, expected %u\n", x, y, z, k, o->words[linear * 13U + k], want[k]);
						return 1;
					}
				}
			}
		}
	}
	printf("ids: 288 invocations' built-ins and gl_WorkGroupSize\n");
	return 0;
}

/* atomic.comp: every buffer atomic of 1000 of 1024 invocations, and each old counter value once. */
static int
test_atomic(void)
{
	static struct run_test test;
	struct run_buffer *v;
	struct run_buffer *h;
	struct run_buffer *old;
	struct run_buffer *u;
	uint32_t want[27];
	uint8_t seen[1000];
	uint32_t n, i, x;
	int32_t low, high;

	memset(&test, 0, sizeof(test));
	v = run_storage(&test, 0U, 0U);
	h = run_storage(&test, 1U, 0U);
	old = run_storage(&test, 2U, 0xdeadbeefU);
	u = run_uniform(&test);
	n = 1000U;
	u->words[0] = n;
	for (i = 0U; i < 1024U; i++)
		v->words[i] = (i * 2654435761U) % 5000U;
	h->words[17] = 0x7fffffffU;
	h->words[18] = 0x80000000U;
	h->words[19] = 0xffffffffU;
	h->words[21] = 0xffffffffU;
	test.groups[0] = 16U;
	test.groups[1] = 1U;
	test.groups[2] = 1U;
	if (run_dispatch("atomic", &test) != 0)
		return 1;
	memset(want, 0, sizeof(want));
	low = INT_MAX;
	high = INT_MIN;
	want[19] = 0xffffffffU;
	want[21] = 0xffffffffU;
	for (i = 0U; i < n; i++) {
		x = v->words[i];
		want[x % 16U]++;
		want[16] += x;
		if ((int32_t)x - 1000 < low)
			low = (int32_t)x - 1000;
		if ((int32_t)x - 1000 > high)
			high = (int32_t)x - 1000;
		if (x < want[19])
			want[19] = x;
		if (x > want[20])
			want[20] = x;
		want[21] &= x | 0xffff0000U;
		want[22] |= x;
		want[23] ^= x;
	}
	want[17] = (uint32_t)low;
	want[18] = (uint32_t)high;
	want[24] = 7U;
	want[25] = 1U;
	want[26] = n;
	for (i = 0U; i < 27U; i++) {
		if (h->words[i] != want[i]) {
			printf("atomic: FAIL word %u = 0x%08x, expected 0x%08x\n", i, h->words[i], want[i]);
			return 1;
		}
	}
	memset(seen, 0, sizeof(seen));
	for (i = 0U; i < n; i++) {
		if (old->words[i] >= n || seen[old->words[i]] != 0U) {
			printf("atomic: FAIL old[%u] = %u\n", i, old->words[i]);
			return 1;
		}
		seen[old->words[i]] = 1U;
	}
	if (old->words[n] != 0xdeadbeefU) {
		printf("atomic: FAIL an invocation past n wrote\n");
		return 1;
	}
	printf("atomic: the histogram, sum, int and uint min and max, and, or, xor, exchange, compare-exchange and old values\n");
	return 0;
}

/* shared.comp: a 64 x 64 matrix transposed through 8 x 8 tiles of shared memory. */
static int
test_shared(void)
{
	static struct run_test test;
	struct run_buffer *m;
	struct run_buffer *t;
	uint32_t r, c;

	memset(&test, 0, sizeof(test));
	m = run_storage(&test, 0U, 0U);
	t = run_storage(&test, 1U, 0xdeadbeefU);
	for (r = 0U; r < 4096U; r++)
		m->words[r] = r * 40503U + 1U;
	test.groups[0] = 8U;
	test.groups[1] = 8U;
	test.groups[2] = 1U;
	if (run_dispatch("shared", &test) != 0)
		return 1;
	for (r = 0U; r < 64U; r++) {
		for (c = 0U; c < 64U; c++) {
			if (t->words[r * 64U + c] != m->words[c * 64U + r]) {
				printf("shared: FAIL t[%u][%u]\n", r, c);
				return 1;
			}
		}
	}
	printf("shared: a 64 x 64 matrix transposed through shared memory across a barrier\n");
	return 0;
}

/* reduce.comp: each group's 64 words summed by a tree with barriers, and the shared counter of 64 arrivals. */
static int
test_reduce(void)
{
	static struct run_test test;
	struct run_buffer *v;
	struct run_buffer *s;
	uint32_t g, i, sum;

	memset(&test, 0, sizeof(test));
	v = run_storage(&test, 0U, 0U);
	s = run_storage(&test, 1U, 0xdeadbeefU);
	for (i = 0U; i < 64U * 32U; i++)
		v->words[i] = (i * 2246822519U) >> 12;
	test.groups[0] = 32U;
	test.groups[1] = 1U;
	test.groups[2] = 1U;
	if (run_dispatch("reduce", &test) != 0)
		return 1;
	for (g = 0U; g < 32U; g++) {
		sum = 0U;
		for (i = 0U; i < 64U; i++)
			sum += v->words[g * 64U + i];
		if (s->words[g * 2U] != sum || s->words[g * 2U + 1U] != 64U) {
			printf("reduce: FAIL group %u: %u and %u, expected %u and 64\n", g, s->words[g * 2U], s->words[g * 2U + 1U], sum);
			return 1;
		}
	}
	printf("reduce: 32 groups summed by a tree in shared memory, 6 barriers, a shared atomic counter\n");
	return 0;
}

/* length.comp: the run-time arrays' lengths in 64 KiB buffers, and a sized array's. */
static int
test_length(void)
{
	static struct run_test test;
	struct run_buffer *w;
	struct run_buffer *o;

	memset(&test, 0, sizeof(test));
	w = run_storage(&test, 0U, 0U);
	(void)run_storage(&test, 1U, 0U);
	o = run_storage(&test, 2U, 0U);
	test.groups[0] = 1U;
	test.groups[1] = 1U;
	test.groups[2] = 1U;
	if (run_dispatch("length", &test) != 0)
		return 1;
	if (o->words[0] != (RUN_BUFFER_BYTES - 4U) / 4U || o->words[1] != RUN_BUFFER_BYTES / 12U || o->words[2] != 4U || w->words[0] != 5U) {
		printf("length: FAIL %u %u %u %u\n", o->words[0], o->words[1], o->words[2], w->words[0]);
		return 1;
	}
	printf("length: run-time arrays of words after a header and of 12-byte structs, a sized array\n");
	return 0;
}

/* layout.comp: the std430 offsets of a vec3, a float, a vec2 array, a mat2, a struct array, a bool and an ivec4. */
static int
test_layout(void)
{
	static struct run_test test;
	static const struct {
		unsigned word;
		uint32_t value;
	} want[] = {
		{ 0U, 0x3f000000U }, { 1U, 0x3f000000U }, { 2U, 0x3f000000U }, { 3U, 0x40200000U },
		{ 6U, 0x40c00000U }, { 7U, 0x40e00000U },
		{ 10U, 0x3f800000U }, { 11U, 0x40000000U }, { 12U, 0x40400000U }, { 13U, 0x40800000U },
		{ 20U, 0x3f800000U }, { 21U, 0x40000000U }, { 22U, 0x40400000U }, { 23U, 0x40800000U },
		{ 24U, 1U },
		{ 28U, 7U }, { 29U, 6U }, { 30U, 4U }, { 31U, 1U },
		{ 4U, 0xdeadbeefU }, { 8U, 0xdeadbeefU }, { 16U, 0xdeadbeefU }
	};
	struct run_buffer *l;
	unsigned index;

	memset(&test, 0, sizeof(test));
	l = run_storage(&test, 0U, 0xdeadbeefU);
	test.groups[0] = 1U;
	test.groups[1] = 1U;
	test.groups[2] = 1U;
	if (run_dispatch("layout", &test) != 0)
		return 1;
	for (index = 0U; index < sizeof(want) / sizeof(want[0]); index++) {
		if (l->words[want[index].word] != want[index].value) {
			printf("layout: FAIL word %u = 0x%08x, expected 0x%08x\n", want[index].word, l->words[want[index].word], want[index].value);
			return 1;
		}
	}
	printf("layout: std430 offsets of a vec3, a float, a vec2 array, a mat2, a struct array, a bool and an ivec4\n");
	return 0;
}

/* noct.comp: Noct's kernel shape -- a lane guard, raw words, signed division and remainder, a result word's atomicAdd. */
static int
test_noct(void)
{
	static struct run_test test;
	struct run_buffer *in;
	struct run_buffer *out;
	struct run_buffer *scalar;
	struct run_buffer *result;
	uint32_t trip, i, v4, v6, v9, v10, v11, v13, sum;
	int32_t remainder;

	memset(&test, 0, sizeof(test));
	in = run_storage(&test, 0U, 0U);
	out = run_storage(&test, 1U, 0xdeadbeefU);
	scalar = run_storage(&test, 2U, 0U);
	result = run_storage(&test, 3U, 0U);
	trip = 300U;
	for (i = 0U; i < trip; i++)
		in->words[i] = i * 2654435761U;
	scalar->words[0] = 7U;
	scalar->words[1] = 0U;
	scalar->words[2] = trip;
	test.groups[0] = 5U;
	test.groups[1] = 1U;
	test.groups[2] = 1U;
	if (run_dispatch("noct", &test) != 0)
		return 1;
	sum = 0U;
	for (i = 0U; i < 320U; i++) {
		if (i >= trip) {
			if (out->words[i] != 0xdeadbeefU) {
				printf("noct: FAIL lane %u past the trip wrote\n", i);
				return 1;
			}
			continue;
		}
		v4 = in->words[i] * 0x0019660DU;
		v6 = v4 + 0x3C6EF35FU;
		v9 = v6 ^ (v6 >> 13);
		v10 = (uint32_t)((int32_t)v9 / 7);
		remainder = (int32_t)v9 % 7;
		v11 = (uint32_t)remainder;
		v13 = (int32_t)v10 < (int32_t)v11 ? v10 : v11;
		sum += v11;
		if (out->words[i] != v13) {
			printf("noct: FAIL lane %u = %u, expected %u\n", i, out->words[i], v13);
			return 1;
		}
	}
	if (result->words[0] != sum) {
		printf("noct: FAIL result %u, expected %u\n", result->words[0], sum);
		return 1;
	}
	printf("noct: 300 lanes of Noct's kernel shape and its result word's sum; 20 lanes past the trip change nothing\n");
	return 0;
}

/* noct-ops.comp: Noct's other shapes -- the result word read, arithmetic, shifts, bit operations, comparisons, selections. */
static int
test_noct_ops(void)
{
	static struct run_test test;
	struct run_buffer *in;
	struct run_buffer *out;
	struct run_buffer *scalar;
	struct run_buffer *result;
	uint32_t trip, i, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v20, flags, sum;
	int32_t quotient;
	int32_t remainder;

	memset(&test, 0, sizeof(test));
	in = run_storage(&test, 0U, 0U);
	out = run_storage(&test, 1U, 0xdeadbeefU);
	scalar = run_storage(&test, 2U, 0U);
	result = run_storage(&test, 3U, 0U);
	trip = 200U;
	for (i = 0U; i < trip; i++)
		in->words[i] = i * 747796405U + 2891336453U;
	scalar->words[0] = 13U;
	scalar->words[2] = trip;
	result->words[0] = 123456789U;
	test.groups[0] = 4U;
	test.groups[1] = 1U;
	test.groups[2] = 1U;
	if (run_dispatch("noct-ops", &test) != 0)
		return 1;
	sum = 0U;
	for (i = 0U; i < 256U; i++) {
		if (i >= trip) {
			if (out->words[i] != 0xdeadbeefU) {
				printf("noct-ops: FAIL lane %u past the trip wrote\n", i);
				return 1;
			}
			continue;
		}
		v1 = in->words[i];
		v2 = 123456789U;
		v3 = 13U;
		v4 = v1 + v2;
		v5 = v4 - v3;
		v6 = v5 * 0x9E3779B1U;
		v7 = v6 / v3;
		v8 = v6 % v3;
		v9 = v6 << 3U;
		v10 = v6 >> 7U;
		v11 = (v9 & v10) | (v9 ^ v8);
		quotient = (int32_t)v11 / (int32_t)v3;
		remainder = (int32_t)v11 % (int32_t)v3;
		v12 = (uint32_t)quotient;
		v13 = (uint32_t)remainder;
		v20 = (int32_t)v12 < (int32_t)v13 ? v12 : v13;
		flags = 0U;
		if ((int32_t)v12 <= (int32_t)v7)
			flags |= 1U;
		if ((int32_t)v11 > (int32_t)v1)
			flags |= 2U;
		if ((int32_t)v11 >= (int32_t)v2)
			flags |= 4U;
		if (v8 == v13)
			flags |= 8U;
		if (v7 != v12)
			flags |= 16U;
		sum += flags;
		if (out->words[i] != (v20 ^ flags)) {
			printf("noct-ops: FAIL lane %u = 0x%08x, expected 0x%08x\n", i, out->words[i], v20 ^ flags);
			return 1;
		}
	}
	if (result->words[1] != sum || result->words[0] != 123456789U) {
		printf("noct-ops: FAIL result words %u %u, expected 123456789 %u\n", result->words[0], result->words[1], sum);
		return 1;
	}
	printf("noct-ops: 200 lanes of Noct's other shapes and the result word's sum\n");
	return 0;
}

/* loopret.comp: o[i] is 1 + 2 + ... + (i % 8), each invocation returning from inside its loop. */
static int
test_loopret(void)
{
	static struct run_test test;
	struct run_buffer *o;
	uint32_t i, want;

	memset(&test, 0, sizeof(test));
	o = run_storage(&test, 0U, 0U);
	test.groups[0] = 2U;
	test.groups[1] = 1U;
	test.groups[2] = 1U;
	if (run_dispatch("loopret", &test) != 0)
		return 1;
	for (i = 0U; i < 128U; i++) {
		want = (i % 8U) * (i % 8U + 1U) / 2U;
		if (o->words[i] != want) {
			printf("loopret: FAIL o[%u] = %u, expected %u\n", i, o->words[i], want);
			return 1;
		}
	}
	printf("loopret: 128 invocations each returning from inside its loop\n");
	return 0;
}

