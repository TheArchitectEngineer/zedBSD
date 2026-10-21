/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * vkmatrix: the parts of Vulkan the Keiland desktop draws with, case by
 * case, each on a 64x64 B8G8R8A8 offscreen image read back and compared
 * with the image worked out on the processor:
 *
 *   push4, push32, push128  push constants of 4, 32 and 128 bytes, read in
 *                           the vertex and the fragment stage, changed
 *                           between three draws
 *   batch                   the compositor's batch vertex: one binding of
 *                           R32G32, two R32G32B32A32 and one R32 SFLOAT
 *   nearest, linear         a combined image sampler (set 0, binding 0),
 *                           clamped to the edge
 *   two-textures            two descriptor sets bound in turn in one pass
 *   blend-over, blend-add   premultiplied alpha (ONE, ONE_MINUS_SRC_ALPHA)
 *                           and addition (ONE, ONE)
 *   draws-32, draws-64      32 and 64 draws in one render pass
 *   two-passes              a cleared pass, then one that loads it
 *   scissor-viewport        the dynamic scissor and viewport changed
 *                           between draws
 *   sample-bgra             a B8G8R8A8 texture (the target's format)
 *   sample-rendered         an image a render pass cleared, sampled by the
 *                           next pass (what a pass that loads reads)
 *   copy-64, copy-128       a buffer copied into an OPTIMAL image of 64-bit
 *                           (R16G16B16A16_SFLOAT) and 128-bit
 *                           (R32G32B32A32_SFLOAT) texels and back, every
 *                           byte compared
 *   render-f16              a pass on an R16G16B16A16_SFLOAT attachment:
 *                           cleared, one quad drawn, copied out, every
 *                           half float compared
 *   render-linear           a 1920x1080 LINEAR B8G8R8A8 attachment in
 *                           host-visible memory, cleared and one quad
 *                           drawn, read through its own mapping with its
 *                           row pitch, every pixel compared
 *   clear-64, clear-128     the wide images cleared by vkCmdClearColorImage
 *                           and copied out (the image-to-buffer leg)
 *   sample-linear-1180, sample-linear-65
 *                           a LINEAR R8G8B8A8 image of 1180x8 and 65x3
 *                           texels, written through its mapping, sampled
 *                           (nearest) over a quad
 *   copy-1080, copy-65      buffers of exactly 1920x1080 and 65x3 RGBA8
 *                           texels copied into an OPTIMAL image and back:
 *                           a transfer that touches past either buffer
 *                           faults on the board
 *
 * It prints one line a case and a total:
 *
 *   vkmatrix: <case>: PASS
 *   vkmatrix: <case>: FAIL(<call> <VkResult> | pixel (<x>,<y>) 0x<seen>, expected 0x<wanted>)
 *   vkmatrix: <passed>/<cases> passed
 *
 * The exit status is 0 only when every case passes.  An argument names
 * the one case to run; -r R runs the cases R times, and -p N runs them in
 * N processes at once, each with its own device (sessions rendering
 * concurrently), with lines prefixed vkmatrix[1] to vkmatrix[N].
 */

/* clock_gettime and fork under a strict C standard. */
#define _POSIX_C_SOURCE 200809L

#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shader.h"

/* The linear case's image: the display's size. */
#define VKMATRIX_LINEAR_WIDTH	1920U
#define VKMATRIX_LINEAR_HEIGHT	1080U

/* The perf cases' kinds and frames: a clear, a load, a full-screen flat quad, a full-screen textured quad. */
#define PERF_CLEAR		0
#define PERF_LOAD		1
#define PERF_FLAT		2
#define PERF_TEXTURE		3
#define PERF_FRAMES		20U

/* The load cases' passes over one 1920x1080 image. */
#define VKMATRIX_LOAD_FRAMES	60U

/* The offscreen image's side and pixels. */
#define VKMATRIX_SIZE		64U
#define VKMATRIX_PIXELS		(VKMATRIX_SIZE * VKMATRIX_SIZE)

/* The vertex buffer's bytes, room for every case's quads. */
#define VKMATRIX_VERTEX_BYTES	65536U

/* The fence wait's limit in nanoseconds (5 s, above the driver's 2 s). */
#define VKMATRIX_WAIT_NS	5000000000ULL

/* The most bytes of a failure's reason. */
#define VKMATRIX_REASON_MAX	256

/* The most pipelines and textures one case makes. */
#define VKMATRIX_PIPELINES_MAX	4
#define VKMATRIX_TEXTURES_MAX	2

/* The floats of the compositor's batch vertex: position 2, colour 4, extra 4, alpha 1. */
#define VKMATRIX_BATCH_FLOATS	11U

/* The vertex layouts the pipelines take. */
enum vkmatrix_layout {
	VKMATRIX_POSITION,
	VKMATRIX_BATCH,
	VKMATRIX_TEXTURE
};

/* The blends the pipelines take. */
enum vkmatrix_blend {
	VKMATRIX_OPAQUE,
	VKMATRIX_OVER,
	VKMATRIX_ADD
};

/* What one pipeline is built from. */
struct vkmatrix_shape {
	const uint32_t *vertex;
	size_t vertex_size;
	const uint32_t *fragment;
	size_t fragment_size;
	enum vkmatrix_layout layout;
	enum vkmatrix_blend blend;
	uint32_t push_size;
	VkShaderStageFlags push_stages;
	int texture;
};

/* One texture: the image, its memory, view and sampler, and the set that names it. */
struct vkmatrix_texture {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkSampler sampler;
	VkDescriptorSet set;
};

/*
 * The Vulkan objects of one run.  Every handle starts as VK_NULL_HANDLE
 * and is destroyed by vkmatrix_destroy() however far the run got.
 */
struct vkmatrix {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t family;
	VkCommandPool command_pool;
	VkCommandBuffer command;
	VkFence fence;
	VkImage target;
	VkDeviceMemory target_memory;
	VkImageView target_view;
	VkBuffer readback;
	VkDeviceMemory readback_memory;
	uint32_t *pixels;
	VkBuffer vertices;
	VkDeviceMemory vertex_memory;
	float *vertex_map;
	uint32_t vertex_floats;
	VkRenderPass clear_pass;
	VkRenderPass load_pass;
	VkFramebuffer framebuffer;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool pool;
	VkPipelineLayout layouts[VKMATRIX_PIPELINES_MAX];
	VkPipeline pipelines[VKMATRIX_PIPELINES_MAX];
	uint32_t pipeline_count;
	struct vkmatrix_texture textures[VKMATRIX_TEXTURES_MAX];
	uint32_t texture_count;
	VkFormat texture_format;
	int texture_linear;
	uint32_t texture_width;
	uint32_t texture_height;
	uint32_t wide_width;
	uint32_t wide_height;
	VkRenderPass pipeline_pass;
	VkImage wide_image;
	VkDeviceMemory wide_memory;
	VkImageView wide_view;
	VkRenderPass wide_pass;
	VkFramebuffer wide_framebuffer;
	VkBuffer wide_upload;
	VkDeviceMemory wide_upload_memory;
	VkBuffer wide_download;
	VkDeviceMemory wide_download_memory;
	uint32_t expected[VKMATRIX_PIXELS];
	int tolerance;
	const char *failed_call;
	char reason[VKMATRIX_REASON_MAX];
};

/* One case: its name and its work. */
struct vkmatrix_case {
	const char *name;
	VkResult (*run)(struct vkmatrix *run);
};

static int vkmatrix_run_cases(const char *name, unsigned long repeats, unsigned worker);
static VkResult vkmatrix_open(struct vkmatrix *run);
static VkResult vkmatrix_find_memory(struct vkmatrix *run, uint32_t allowed, VkMemoryPropertyFlags wanted, uint32_t *type);
static VkResult vkmatrix_buffer(struct vkmatrix *run, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buffer, VkDeviceMemory *memory, void **mapped);
static VkResult vkmatrix_target(struct vkmatrix *run);
static VkResult vkmatrix_passes(struct vkmatrix *run);
static VkResult vkmatrix_pipeline(struct vkmatrix *run, const struct vkmatrix_shape *shape, uint32_t *index);
static VkResult vkmatrix_texture(struct vkmatrix *run, uint32_t side, const uint32_t *texels, VkFilter filter);
static VkResult vkmatrix_begin(struct vkmatrix *run, int load, const float clear[4]);
static VkResult vkmatrix_finish(struct vkmatrix *run, int read);
static void vkmatrix_full_view(struct vkmatrix *run);
static uint32_t vkmatrix_quad(struct vkmatrix *run, float x0, float y0, float x1, float y1);
static uint32_t vkmatrix_texture_quad(struct vkmatrix *run, float x0, float y0, float x1, float y1);
static uint32_t vkmatrix_batch_quad(struct vkmatrix *run, float x0, float y0, float x1, float y1, const float colour[4], const float extra[4], float alpha);
static float vkmatrix_ndc(float pixel);
static float unit_clamp(float value);
static uint32_t vkmatrix_pack(const float colour[4]);
static void vkmatrix_fill(struct vkmatrix *run, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t value);
static int vkmatrix_compare(struct vkmatrix *run);
static void vkmatrix_release_case(struct vkmatrix *run);
static void vkmatrix_destroy(struct vkmatrix *run);
static VkResult case_push4(struct vkmatrix *run);
static VkResult case_push32(struct vkmatrix *run);
static VkResult case_push128(struct vkmatrix *run);
static VkResult case_batch(struct vkmatrix *run);
static VkResult case_nearest(struct vkmatrix *run);
static VkResult case_linear(struct vkmatrix *run);
static VkResult case_two_textures(struct vkmatrix *run);
static VkResult case_blend_over(struct vkmatrix *run);
static VkResult case_blend_add(struct vkmatrix *run);
static VkResult case_draws(struct vkmatrix *run, uint32_t draws);
static VkResult case_draws_32(struct vkmatrix *run);
static VkResult case_draws_64(struct vkmatrix *run);
static VkResult case_two_passes(struct vkmatrix *run);
static VkResult case_scissor_viewport(struct vkmatrix *run);
static VkResult case_sample_bgra(struct vkmatrix *run);
static VkResult case_sample_rendered(struct vkmatrix *run);
static VkResult wide_image(struct vkmatrix *run, VkFormat format, VkImageUsageFlags usage);
static VkResult wide_submit(struct vkmatrix *run);
static void wide_barrier(struct vkmatrix *run, VkImageLayout from, VkImageLayout to, VkAccessFlags source_access, VkAccessFlags destination_access, VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage);
static void wide_copy_out(struct vkmatrix *run);
static VkResult wide_compare(struct vkmatrix *run, const uint8_t *seen, const uint8_t *wanted, uint32_t texel_bytes);
static VkResult case_copy_wide(struct vkmatrix *run, VkFormat format, uint32_t texel_bytes);
static VkResult case_copy_64(struct vkmatrix *run);
static VkResult case_copy_128(struct vkmatrix *run);
static VkResult case_render_f16(struct vkmatrix *run);
static VkResult case_render_linear(struct vkmatrix *run);
static VkResult linear_target(struct vkmatrix *run, VkImageTiling tiling, VkSubresourceLayout *layout);
static VkResult case_load_1080(struct vkmatrix *run, uint32_t draws, VkImageTiling tiling, int textured);
static VkResult case_load_1080_textured(struct vkmatrix *run);
static VkResult perf_case(struct vkmatrix *run, int kind);
static VkResult case_perf_clear(struct vkmatrix *run);
static VkResult case_perf_load(struct vkmatrix *run);
static VkResult case_perf_flat(struct vkmatrix *run);
static VkResult case_perf_texture(struct vkmatrix *run);
static uint64_t perf_now_ns(void);
static VkResult load_1080_textures(struct vkmatrix *run, uint32_t *pipeline);
static VkResult case_load_1080_6(struct vkmatrix *run);
static VkResult case_load_1080_32(struct vkmatrix *run);
static VkResult case_load_1080_linear(struct vkmatrix *run);
static float load_1080_red(uint32_t frame, uint32_t draw);
static void load_1080_quad(uint32_t draw, float corners[4]);
static VkResult linear_check(struct vkmatrix *run, const VkSubresourceLayout *layout, const float quad[4]);
static VkResult case_clear_wide(struct vkmatrix *run, VkFormat format, uint32_t texel_bytes);
static VkResult case_clear_64(struct vkmatrix *run);
static VkResult case_clear_128(struct vkmatrix *run);
static VkResult case_sample_linear(struct vkmatrix *run, uint32_t width, uint32_t height);
static VkResult case_sample_linear_wide(struct vkmatrix *run);
static VkResult case_sample_linear_odd(struct vkmatrix *run);
static VkResult case_copy_1080(struct vkmatrix *run);
static VkResult case_copy_65(struct vkmatrix *run);

/* Every case, in the order they run. */
static const struct vkmatrix_case cases[] = {
	{ "push4", case_push4 },
	{ "push32", case_push32 },
	{ "push128", case_push128 },
	{ "batch", case_batch },
	{ "nearest", case_nearest },
	{ "linear", case_linear },
	{ "two-textures", case_two_textures },
	{ "blend-over", case_blend_over },
	{ "blend-add", case_blend_add },
	{ "draws-32", case_draws_32 },
	{ "draws-64", case_draws_64 },
	{ "two-passes", case_two_passes },
	{ "scissor-viewport", case_scissor_viewport },
	{ "sample-bgra", case_sample_bgra },
	{ "sample-rendered", case_sample_rendered },
	{ "copy-64", case_copy_64 },
	{ "copy-128", case_copy_128 },
	{ "render-f16", case_render_f16 },
	{ "render-linear", case_render_linear },
	{ "load-1080-6", case_load_1080_6 },
	{ "load-1080-32", case_load_1080_32 },
	{ "load-1080-linear", case_load_1080_linear },
	{ "load-1080-textured", case_load_1080_textured },
	{ "perf-clear", case_perf_clear },
	{ "perf-load", case_perf_load },
	{ "perf-flat", case_perf_flat },
	{ "perf-texture", case_perf_texture },
	{ "clear-64", case_clear_64 },
	{ "clear-128", case_clear_128 },
	{ "sample-linear-1180", case_sample_linear_wide },
	{ "sample-linear-65", case_sample_linear_odd },
	{ "copy-1080", case_copy_1080 },
	{ "copy-65", case_copy_65 },
};

/* The clear colour of every case's first pass: dark blue. */
static const float background[4] = { 0.0f, 0.0f, 0.5f, 1.0f };

int
main(
	int argc,
	char **argv)
{
	const char *name;
	unsigned long processes;
	unsigned long repeats;
	unsigned long index;
	char *end;
	pid_t child;
	pid_t done;
	int status;
	int failed;
	int arg;

	/* The options: -p N processes at once, -r R runs of the cases each, then the case's name. */
	processes = 1;
	repeats = 1;
	name = NULL;
	for (arg = 1; arg < argc; arg++) {
		if (strcmp(argv[arg], "-p") == 0 && arg + 1 < argc) {
			arg++;
			processes = strtoul(argv[arg], &end, 10);
			if (*end != '\0' || processes == 0 || processes > 16)
				return 2;
		} else if (strcmp(argv[arg], "-r") == 0 && arg + 1 < argc) {
			arg++;
			repeats = strtoul(argv[arg], &end, 10);
			if (*end != '\0' || repeats == 0)
				return 2;
		} else {
			name = argv[arg];
		}
	}

	/* One process: the cases here. */
	if (processes == 1) {
		failed = vkmatrix_run_cases(name, repeats, 0);
		return failed;
	}

	/* Several: each its own device in its own process, all at once (sessions rendering concurrently); nothing buffered is copied into the children. */
	fflush(stdout);
	for (index = 0; index < processes; index++) {
		child = fork();
		if (child < 0) {
			printf("vkmatrix: fork failed\n");
			return 1;
		}
		if (child == 0) {
			failed = vkmatrix_run_cases(name, repeats, (unsigned)index + 1U);
			fflush(stdout);
			_exit(failed);
		}
	}

	/* Every process's verdict. */
	failed = 0;
	for (index = 0; index < processes; index++) {
		done = wait(&status);
		if (done < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
			failed = 1;
	}
	printf("vkmatrix: %lu processes: %s\n", processes, failed ? "FAIL" : "PASS");
	if (failed)
		return 1;

	/* Succeeded: every process passed. */
	return 0;
}

/*
 * Opens a device and runs every case, or the one named, repeats times,
 * printing a line a case (with the process's number when it is one of
 * several) and a total; returns 1 on a failure.
 */
static int
vkmatrix_run_cases(
	const char *name,
	unsigned long repeats,
	unsigned worker)
{
	struct vkmatrix *run;
	VkResult status;
	unsigned long repeat;
	uint32_t index;
	uint32_t ran;
	uint32_t passed;
	int named;
	char prefix[32];

	/* The line's prefix. */
	snprintf(prefix, sizeof(prefix), "vkmatrix");
	if (worker != 0)
		snprintf(prefix, sizeof(prefix), "vkmatrix[%u]", worker);

	/* The run's objects (too large for a thread's stack). */
	run = calloc(1, sizeof(*run));
	if (run == NULL)
		return 1;

	/* The device and the objects every case shares. */
	status = vkmatrix_open(run);
	if (status == VK_SUCCESS)
		status = vkmatrix_target(run);
	if (status == VK_SUCCESS)
		status = vkmatrix_passes(run);
	if (status != VK_SUCCESS) {
		printf("%s: no Vulkan device (%s %d)\n", prefix, run->failed_call, (int)status);
		vkmatrix_destroy(run);
		free(run);
		return 1;
	}

	/* Each case (or the one named), its line, as many times as asked. */
	ran = 0;
	passed = 0;
	for (repeat = 0; repeat < repeats; repeat++) {
		for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
			/* Skips a case other than the one named. */
			if (name != NULL) {
				named = strcmp(name, cases[index].name);
				if (named != 0)
					continue;
			}

			run->failed_call = "";
			run->reason[0] = '\0';
			run->tolerance = 0;
			run->vertex_floats = 0;
			run->texture_format = VK_FORMAT_UNDEFINED;
			run->texture_linear = 0;
			run->texture_width = 0;
			run->texture_height = 0;
			run->wide_width = VKMATRIX_SIZE;
			run->wide_height = VKMATRIX_SIZE;
			run->pipeline_pass = VK_NULL_HANDLE;
			status = cases[index].run(run);
			ran++;
			if (status != VK_SUCCESS)
				printf("%s: %s: FAIL(%s %d)\n", prefix, cases[index].name, run->failed_call, (int)status);
			else if (vkmatrix_compare(run) != 0)
				printf("%s: %s: FAIL(%s)\n", prefix, cases[index].name, run->reason);
			else {
				printf("%s: %s: PASS\n", prefix, cases[index].name);
				passed++;
			}

			vkmatrix_release_case(run);
		}
	}

	/* The total. */
	printf("%s: %u/%u passed\n", prefix, (unsigned)passed, (unsigned)ran);
	vkmatrix_destroy(run);
	free(run);

	/* A run of no case, or with a failure, fails. */
	if (ran == 0 || passed != ran)
		return 1;

	/* Succeeded: every case that ran passed. */
	return 0;
}

/* Opens an instance and a device of one graphics queue on the first physical device. */
static VkResult
vkmatrix_open(
	struct vkmatrix *run)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance_info;
	VkPhysicalDevice physicals[4];
	VkQueueFamilyProperties families[8];
	VkDeviceQueueCreateInfo queue_info;
	VkDeviceCreateInfo device_info;
	VkCommandPoolCreateInfo pool_info;
	VkCommandBufferAllocateInfo command_info;
	VkFenceCreateInfo fence_info;
	uint32_t count;
	uint32_t index;
	float priority;
	VkResult status;

	/* The instance, Vulkan 1.0. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "vkmatrix";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance_info, 0, sizeof(instance_info));
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application;
	run->failed_call = "vkCreateInstance";
	status = vkCreateInstance(&instance_info, NULL, &run->instance);
	if (status != VK_SUCCESS)
		return status;

	/* The first physical device. */
	count = 4;
	run->failed_call = "vkEnumeratePhysicalDevices";
	status = vkEnumeratePhysicalDevices(run->instance, &count, physicals);
	if (status != VK_SUCCESS && status != VK_INCOMPLETE)
		return status;
	if (count == 0)
		return VK_ERROR_INITIALIZATION_FAILED;
	run->physical = physicals[0];

	/* The first family that draws. */
	count = 8;
	vkGetPhysicalDeviceQueueFamilyProperties(run->physical, &count, families);
	for (index = 0; index < count; index++) {
		if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
			break;
	}

	run->failed_call = "graphics queue family";
	if (index == count)
		return VK_ERROR_FEATURE_NOT_PRESENT;
	run->family = index;

	/* The device and its queue. */
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
	vkGetDeviceQueue(run->device, run->family, 0, &run->queue);

	/* The command pool, its one buffer, and the fence. */
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
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
	memset(&fence_info, 0, sizeof(fence_info));
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	run->failed_call = "vkCreateFence";
	status = vkCreateFence(run->device, &fence_info, NULL, &run->fence);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the device is open. */
	return VK_SUCCESS;
}

/* Finds a memory type among the allowed ones with the wanted properties. */
static VkResult
vkmatrix_find_memory(
	struct vkmatrix *run,
	uint32_t allowed,
	VkMemoryPropertyFlags wanted,
	uint32_t *type)
{
	VkPhysicalDeviceMemoryProperties properties;
	uint32_t index;

	/* The first allowed type with every wanted property. */
	vkGetPhysicalDeviceMemoryProperties(run->physical, &properties);
	for (index = 0; index < properties.memoryTypeCount; index++) {
		if ((allowed & (1U << index)) != 0 && (properties.memoryTypes[index].propertyFlags & wanted) == wanted) {
			*type = index;
			return VK_SUCCESS;
		}
	}

	/* None. */
	run->failed_call = "memory type";
	return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

/* Makes a host-visible, coherent buffer, bound and mapped. */
static VkResult
vkmatrix_buffer(
	struct vkmatrix *run,
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	VkBuffer *buffer,
	VkDeviceMemory *memory,
	void **mapped)
{
	VkBufferCreateInfo buffer_info;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkResult status;

	/* The buffer. */
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	run->failed_call = "vkCreateBuffer";
	status = vkCreateBuffer(run->device, &buffer_info, NULL, buffer);
	if (status != VK_SUCCESS)
		return status;

	/* Its memory, bound and mapped. */
	vkGetBufferMemoryRequirements(run->device, *buffer, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	status = vkmatrix_find_memory(run, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &allocate.memoryTypeIndex);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkAllocateMemory";
	status = vkAllocateMemory(run->device, &allocate, NULL, memory);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkBindBufferMemory";
	status = vkBindBufferMemory(run->device, *buffer, *memory, 0);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkMapMemory";
	status = vkMapMemory(run->device, *memory, 0, VK_WHOLE_SIZE, 0, mapped);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the buffer is mapped. */
	return VK_SUCCESS;
}

/* Makes the colour target, its view, the readback and the vertex buffers. */
static VkResult
vkmatrix_target(
	struct vkmatrix *run)
{
	VkImageCreateInfo image_info;
	VkImageViewCreateInfo view_info;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	void *mapped;
	VkResult status;

	/* The 64x64 B8G8R8A8 target. */
	memset(&image_info, 0, sizeof(image_info));
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
	image_info.extent.width = VKMATRIX_SIZE;
	image_info.extent.height = VKMATRIX_SIZE;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	run->failed_call = "vkCreateImage";
	status = vkCreateImage(run->device, &image_info, NULL, &run->target);
	if (status != VK_SUCCESS)
		return status;
	vkGetImageMemoryRequirements(run->device, run->target, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	status = vkmatrix_find_memory(run, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &allocate.memoryTypeIndex);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkAllocateMemory";
	status = vkAllocateMemory(run->device, &allocate, NULL, &run->target_memory);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkBindImageMemory";
	status = vkBindImageMemory(run->device, run->target, run->target_memory, 0);
	if (status != VK_SUCCESS)
		return status;

	/* Its view. */
	memset(&view_info, 0, sizeof(view_info));
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = run->target;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	run->failed_call = "vkCreateImageView";
	status = vkCreateImageView(run->device, &view_info, NULL, &run->target_view);
	if (status != VK_SUCCESS)
		return status;

	/* The readback and the vertex buffers. */
	status = vkmatrix_buffer(run, VKMATRIX_PIXELS * 4U, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &run->readback, &run->readback_memory, &mapped);
	if (status != VK_SUCCESS)
		return status;
	run->pixels = mapped;
	status = vkmatrix_buffer(run, VKMATRIX_VERTEX_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &run->vertices, &run->vertex_memory, &mapped);
	if (status != VK_SUCCESS)
		return status;
	run->vertex_map = mapped;

	/* Succeeded: the target and the buffers are made. */
	return VK_SUCCESS;
}

/*
 * Makes the two render passes (one that clears, one that loads; the same
 * attachment, so compatible), the framebuffer, the texture set layout and
 * its pool.
 */
static VkResult
vkmatrix_passes(
	struct vkmatrix *run)
{
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo pass_info;
	VkFramebufferCreateInfo framebuffer_info;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo set_info;
	VkDescriptorPoolSize pool_size;
	VkDescriptorPoolCreateInfo pool_info;
	VkResult status;
	int load;

	/* The two passes: the colour attachment stored and left for a transfer. */
	memset(&reference, 0, sizeof(reference));
	reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &reference;
	for (load = 0; load < 2; load++) {
		memset(&attachment, 0, sizeof(attachment));
		attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = load ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
		attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		memset(&pass_info, 0, sizeof(pass_info));
		pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		pass_info.attachmentCount = 1;
		pass_info.pAttachments = &attachment;
		pass_info.subpassCount = 1;
		pass_info.pSubpasses = &subpass;
		run->failed_call = "vkCreateRenderPass";
		status = vkCreateRenderPass(run->device, &pass_info, NULL, load ? &run->load_pass : &run->clear_pass);
		if (status != VK_SUCCESS)
			return status;
	}

	/* The framebuffer. */
	memset(&framebuffer_info, 0, sizeof(framebuffer_info));
	framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_info.renderPass = run->clear_pass;
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = &run->target_view;
	framebuffer_info.width = VKMATRIX_SIZE;
	framebuffer_info.height = VKMATRIX_SIZE;
	framebuffer_info.layers = 1;
	run->failed_call = "vkCreateFramebuffer";
	status = vkCreateFramebuffer(run->device, &framebuffer_info, NULL, &run->framebuffer);
	if (status != VK_SUCCESS)
		return status;

	/* The set layout: one combined image sampler for the fragment, as the compositor's. */
	memset(&binding, 0, sizeof(binding));
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_info.bindingCount = 1;
	set_info.pBindings = &binding;
	run->failed_call = "vkCreateDescriptorSetLayout";
	status = vkCreateDescriptorSetLayout(run->device, &set_info, NULL, &run->set_layout);
	if (status != VK_SUCCESS)
		return status;

	/* The pool, its sets freed one by one. */
	memset(&pool_size, 0, sizeof(pool_size));
	pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_size.descriptorCount = VKMATRIX_TEXTURES_MAX;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = VKMATRIX_TEXTURES_MAX;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;
	run->failed_call = "vkCreateDescriptorPool";
	status = vkCreateDescriptorPool(run->device, &pool_info, NULL, &run->pool);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the passes and the set layout are made. */
	return VK_SUCCESS;
}

/* Makes a pipeline (and its layout) of a shape, with a dynamic viewport and scissor. */
static VkResult
vkmatrix_pipeline(
	struct vkmatrix *run,
	const struct vkmatrix_shape *shape,
	uint32_t *index)
{
	static const VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkShaderModuleCreateInfo module_info;
	VkShaderModule modules[2];
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attributes[4];
	VkPipelineVertexInputStateCreateInfo input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineDynamicStateCreateInfo dynamic;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo layout_info;
	VkGraphicsPipelineCreateInfo pipeline_info;
	uint32_t slot;
	VkResult status;

	/* A free slot. */
	run->failed_call = "pipeline slots";
	if (run->pipeline_count >= VKMATRIX_PIPELINES_MAX)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	slot = run->pipeline_count;

	/* The layout: the push constant range, the texture's set. */
	memset(&range, 0, sizeof(range));
	range.stageFlags = shape->push_stages;
	range.size = shape->push_size;
	memset(&layout_info, 0, sizeof(layout_info));
	layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	if (shape->texture) {
		layout_info.setLayoutCount = 1;
		layout_info.pSetLayouts = &run->set_layout;
	}

	if (shape->push_size != 0) {
		layout_info.pushConstantRangeCount = 1;
		layout_info.pPushConstantRanges = &range;
	}

	run->failed_call = "vkCreatePipelineLayout";
	status = vkCreatePipelineLayout(run->device, &layout_info, NULL, &run->layouts[slot]);
	if (status != VK_SUCCESS)
		return status;
	run->pipeline_count++;

	/* The two shader modules. */
	modules[0] = VK_NULL_HANDLE;
	modules[1] = VK_NULL_HANDLE;
	memset(&module_info, 0, sizeof(module_info));
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = shape->vertex_size;
	module_info.pCode = shape->vertex;
	run->failed_call = "vkCreateShaderModule";
	status = vkCreateShaderModule(run->device, &module_info, NULL, &modules[0]);
	if (status == VK_SUCCESS) {
		module_info.codeSize = shape->fragment_size;
		module_info.pCode = shape->fragment;
		status = vkCreateShaderModule(run->device, &module_info, NULL, &modules[1]);
	}

	if (status != VK_SUCCESS) {
		if (modules[0] != VK_NULL_HANDLE)
			vkDestroyShaderModule(run->device, modules[0], NULL);
		return status;
	}

	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = modules[0];
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = modules[1];
	stages[1].pName = "main";

	/* The vertex input: one binding, the layout's attributes. */
	memset(&binding, 0, sizeof(binding));
	memset(attributes, 0, sizeof(attributes));
	memset(&input, 0, sizeof(input));
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	input.vertexBindingDescriptionCount = 1;
	input.pVertexBindingDescriptions = &binding;
	input.pVertexAttributeDescriptions = attributes;
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
	if (shape->layout == VKMATRIX_POSITION) {
		binding.stride = 2U * (uint32_t)sizeof(float);
		input.vertexAttributeDescriptionCount = 1;
	} else if (shape->layout == VKMATRIX_TEXTURE) {
		binding.stride = 4U * (uint32_t)sizeof(float);
		attributes[1].location = 1;
		attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
		attributes[1].offset = 2U * (uint32_t)sizeof(float);
		input.vertexAttributeDescriptionCount = 2;
	} else {
		binding.stride = VKMATRIX_BATCH_FLOATS * (uint32_t)sizeof(float);
		attributes[1].location = 1;
		attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributes[1].offset = 2U * (uint32_t)sizeof(float);
		attributes[2].location = 2;
		attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributes[2].offset = 6U * (uint32_t)sizeof(float);
		attributes[3].location = 3;
		attributes[3].format = VK_FORMAT_R32_SFLOAT;
		attributes[3].offset = 10U * (uint32_t)sizeof(float);
		input.vertexAttributeDescriptionCount = 4;
	}

	/* Triangle lists, no culling, one sample, the dynamic viewport and scissor. */
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamic_states;

	/* The blend. */
	memset(&blend_attachment, 0, sizeof(blend_attachment));
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	if (shape->blend != VKMATRIX_OPAQUE) {
		blend_attachment.blendEnable = VK_TRUE;
		blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend_attachment.dstColorBlendFactor = shape->blend == VKMATRIX_OVER ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
		blend_attachment.dstAlphaBlendFactor = blend_attachment.dstColorBlendFactor;
		blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
		blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}

	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blend_attachment;

	/* The pipeline, then the modules go. */
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = stages;
	pipeline_info.pVertexInputState = &input;
	pipeline_info.pInputAssemblyState = &assembly;
	pipeline_info.pViewportState = &viewport;
	pipeline_info.pRasterizationState = &raster;
	pipeline_info.pMultisampleState = &multisample;
	pipeline_info.pColorBlendState = &blend;
	pipeline_info.pDynamicState = &dynamic;
	pipeline_info.layout = run->layouts[slot];
	pipeline_info.renderPass = run->clear_pass;
	if (run->pipeline_pass != VK_NULL_HANDLE)
		pipeline_info.renderPass = run->pipeline_pass;
	run->failed_call = "vkCreateGraphicsPipelines";
	status = vkCreateGraphicsPipelines(run->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &run->pipelines[slot]);
	vkDestroyShaderModule(run->device, modules[0], NULL);
	vkDestroyShaderModule(run->device, modules[1], NULL);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the pipeline's slot. */
	*index = slot;
	return VK_SUCCESS;
}

/*
 * Makes a texture of side x side R8G8B8A8 texels (given as 0xAABBGGRR),
 * uploaded through a buffer, with a sampler of the filter clamped to the
 * edge, and its descriptor set.  With run->texture_linear it is a LINEAR
 * image of run->texture_width x run->texture_height texels written
 * through its mapping row by row at its row pitch.
 */
static VkResult
vkmatrix_texture(
	struct vkmatrix *run,
	uint32_t side,
	const uint32_t *texels,
	VkFilter filter)
{
	struct vkmatrix_texture *texture;
	VkImageCreateInfo image_info;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view_info;
	VkSamplerCreateInfo sampler_info;
	VkDescriptorSetAllocateInfo set_info;
	VkDescriptorImageInfo descriptor;
	VkWriteDescriptorSet write;
	VkCommandBufferBeginInfo begin;
	VkImageMemoryBarrier barrier;
	VkBufferImageCopy copy;
	VkSubmitInfo submit;
	VkBuffer upload;
	VkDeviceMemory upload_memory;
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	uint32_t width;
	uint32_t height;
	uint32_t row;
	void *mapped;
	VkResult status;

	/* The size: a square, or the case's own (a linear one, or a named OPTIMAL size). */
	width = side;
	height = side;
	if (run->texture_linear || run->texture_width != 0) {
		width = run->texture_width;
		height = run->texture_height;
	}

	/* A free slot. */
	run->failed_call = "texture slots";
	if (run->texture_count >= VKMATRIX_TEXTURES_MAX)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	texture = &run->textures[run->texture_count];
	run->texture_count++;

	/* The image and its memory. */
	memset(&image_info, 0, sizeof(image_info));
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = run->texture_format != VK_FORMAT_UNDEFINED ? run->texture_format : VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent.width = width;
	image_info.extent.height = height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (run->texture_linear) {
		image_info.tiling = VK_IMAGE_TILING_LINEAR;
		image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		image_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	}
	run->failed_call = "vkCreateImage";
	status = vkCreateImage(run->device, &image_info, NULL, &texture->image);
	if (status != VK_SUCCESS)
		return status;
	vkGetImageMemoryRequirements(run->device, texture->image, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	if (run->texture_linear) {
		status = vkmatrix_find_memory(run, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &allocate.memoryTypeIndex);
	} else {
		status = vkmatrix_find_memory(run, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &allocate.memoryTypeIndex);
	}
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkAllocateMemory";
	status = vkAllocateMemory(run->device, &allocate, NULL, &texture->memory);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkBindImageMemory";
	status = vkBindImageMemory(run->device, texture->image, texture->memory, 0);
	if (status != VK_SUCCESS)
		return status;

	/* A linear image: the rows through its mapping at its row pitch, then preinitialised -> shader read. */
	if (run->texture_linear) {
		memset(&subresource, 0, sizeof(subresource));
		subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkGetImageSubresourceLayout(run->device, texture->image, &subresource, &layout);
		run->failed_call = "vkMapMemory";
		status = vkMapMemory(run->device, texture->memory, 0, VK_WHOLE_SIZE, 0, &mapped);
		if (status != VK_SUCCESS)
			return status;
		for (row = 0; row < height; row++)
			memcpy((uint8_t *)mapped + layout.offset + row * layout.rowPitch, texels + (size_t)row * width, (size_t)width * 4U);
		vkUnmapMemory(run->device, texture->memory);
		memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkResetCommandBuffer(run->command, 0);
		vkBeginCommandBuffer(run->command, &begin);
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture->image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
		vkEndCommandBuffer(run->command);
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &run->command;
		vkResetFences(run->device, 1, &run->fence);
		run->failed_call = "vkQueueSubmit";
		status = vkQueueSubmit(run->queue, 1, &submit, run->fence);
		if (status == VK_SUCCESS) {
			run->failed_call = "vkWaitForFences";
			status = vkWaitForFences(run->device, 1, &run->fence, VK_TRUE, VKMATRIX_WAIT_NS);
		}
		if (status != VK_SUCCESS)
			return status;
	} else {
		/* The texels through a buffer: undefined -> transfer -> shader read. */
		status = vkmatrix_buffer(run, (VkDeviceSize)width * height * 4U, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &upload, &upload_memory, &mapped);
		if (status != VK_SUCCESS)
			return status;
		memcpy(mapped, texels, (size_t)width * height * 4U);
		memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkResetCommandBuffer(run->command, 0);
		vkBeginCommandBuffer(run->command, &begin);
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture->image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
		memset(&copy, 0, sizeof(copy));
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent.width = width;
		copy.imageExtent.height = height;
		copy.imageExtent.depth = 1;
		vkCmdCopyBufferToImage(run->command, upload, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
		vkEndCommandBuffer(run->command);
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &run->command;
		vkResetFences(run->device, 1, &run->fence);
		run->failed_call = "vkQueueSubmit";
		status = vkQueueSubmit(run->queue, 1, &submit, run->fence);
		if (status == VK_SUCCESS) {
			run->failed_call = "vkWaitForFences";
			status = vkWaitForFences(run->device, 1, &run->fence, VK_TRUE, VKMATRIX_WAIT_NS);
		}

		vkDestroyBuffer(run->device, upload, NULL);
		vkFreeMemory(run->device, upload_memory, NULL);
		if (status != VK_SUCCESS)
			return status;
	}

	/* The view and the sampler. */
	memset(&view_info, 0, sizeof(view_info));
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = texture->image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = run->texture_format != VK_FORMAT_UNDEFINED ? run->texture_format : VK_FORMAT_R8G8B8A8_UNORM;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	run->failed_call = "vkCreateImageView";
	status = vkCreateImageView(run->device, &view_info, NULL, &texture->view);
	if (status != VK_SUCCESS)
		return status;
	memset(&sampler_info, 0, sizeof(sampler_info));
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = filter;
	sampler_info.minFilter = filter;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.maxLod = 0.0f;
	run->failed_call = "vkCreateSampler";
	status = vkCreateSampler(run->device, &sampler_info, NULL, &texture->sampler);
	if (status != VK_SUCCESS)
		return status;

	/* The set that names them. */
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_info.descriptorPool = run->pool;
	set_info.descriptorSetCount = 1;
	set_info.pSetLayouts = &run->set_layout;
	run->failed_call = "vkAllocateDescriptorSets";
	status = vkAllocateDescriptorSets(run->device, &set_info, &texture->set);
	if (status != VK_SUCCESS)
		return status;
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.sampler = texture->sampler;
	descriptor.imageView = texture->view;
	descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = texture->set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &descriptor;
	vkUpdateDescriptorSets(run->device, 1, &write, 0, NULL);

	/* Succeeded: the texture and its set. */
	return VK_SUCCESS;
}

/* Begins the command buffer and a render pass (cleared to the colour, or loaded), the viewport and scissor whole. */
static VkResult
vkmatrix_begin(
	struct vkmatrix *run,
	int load,
	const float clear[4])
{
	VkCommandBufferBeginInfo begin;
	VkRenderPassBeginInfo pass;
	VkClearValue value;

	/* The command buffer, unless a pass of it is already recorded. */
	if (!load) {
		memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkResetCommandBuffer(run->command, 0);
		vkBeginCommandBuffer(run->command, &begin);
	}

	/* The pass over the whole target. */
	memset(&value, 0, sizeof(value));
	memcpy(value.color.float32, clear, sizeof(value.color.float32));
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = load ? run->load_pass : run->clear_pass;
	pass.framebuffer = run->framebuffer;
	pass.renderArea.extent.width = VKMATRIX_SIZE;
	pass.renderArea.extent.height = VKMATRIX_SIZE;
	pass.clearValueCount = 1;
	pass.pClearValues = &value;
	vkCmdBeginRenderPass(run->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkmatrix_full_view(run);

	/* Succeeded: the pass is open. */
	return VK_SUCCESS;
}

/* Sets the viewport and the scissor to the whole target. */
static void
vkmatrix_full_view(
	struct vkmatrix *run)
{
	VkViewport viewport;
	VkRect2D scissor;

	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)VKMATRIX_SIZE;
	viewport.height = (float)VKMATRIX_SIZE;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(run->command, 0, 1, &viewport);
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = VKMATRIX_SIZE;
	scissor.extent.height = VKMATRIX_SIZE;
	vkCmdSetScissor(run->command, 0, 1, &scissor);
}

/*
 * Ends the render pass and, when read, copies the target to the readback
 * buffer; submits and waits.
 */
static VkResult
vkmatrix_finish(
	struct vkmatrix *run,
	int read)
{
	VkImageMemoryBarrier barrier;
	VkBufferImageCopy copy;
	VkMemoryBarrier host;
	VkSubmitInfo submit;
	VkResult status;

	/* The pass's end. */
	vkCmdEndRenderPass(run->command);
	if (!read)
		return VK_SUCCESS;

	/* The target to a transfer source, copied, then made visible to the host. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = run->target;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = VKMATRIX_SIZE;
	copy.imageExtent.height = VKMATRIX_SIZE;
	copy.imageExtent.depth = 1;
	vkCmdCopyImageToBuffer(run->command, run->target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, run->readback, 1, &copy);
	memset(&host, 0, sizeof(host));
	host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL);
	vkEndCommandBuffer(run->command);

	/* The submit and its wait. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &run->command;
	vkResetFences(run->device, 1, &run->fence);
	run->failed_call = "vkQueueSubmit";
	status = vkQueueSubmit(run->queue, 1, &submit, run->fence);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkWaitForFences";
	status = vkWaitForFences(run->device, 1, &run->fence, VK_TRUE, VKMATRIX_WAIT_NS);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the target is in the readback buffer. */
	return VK_SUCCESS;
}

/* A pixel coordinate (0..64) as normalized device coordinate (-1..1). */
static float
vkmatrix_ndc(
	float pixel)
{
	return pixel / (float)(VKMATRIX_SIZE / 2U) - 1.0f;
}

/* Clamps a texture coordinate to the edge texels' centres (0..1), as the sampler clamps to the edge. */
static float
unit_clamp(
	float value)
{
	/* Below the first texel's centre. */
	if (value < 0.0f)
		return 0.0f;

	/* Past the last texel's centre. */
	if (value > 1.0f)
		return 1.0f;

	/* Succeeded: the coordinate is inside. */
	return value;
}

/* Appends a quad of positions over the pixel rectangle and returns its first vertex. */
static uint32_t
vkmatrix_quad(
	struct vkmatrix *run,
	float x0,
	float y0,
	float x1,
	float y1)
{
	const float corners[6][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
	uint32_t first;
	uint32_t index;

	/* Two triangles, two floats a vertex. */
	first = run->vertex_floats / 2U;
	for (index = 0; index < 6; index++) {
		run->vertex_map[run->vertex_floats++] = vkmatrix_ndc(corners[index][0]);
		run->vertex_map[run->vertex_floats++] = vkmatrix_ndc(corners[index][1]);
	}

	return first;
}

/* Appends a textured quad (position and coordinate 0..1) and returns its first vertex. */
static uint32_t
vkmatrix_texture_quad(
	struct vkmatrix *run,
	float x0,
	float y0,
	float x1,
	float y1)
{
	const float corners[6][4] = {
		{ x0, y0, 0.0f, 0.0f }, { x1, y0, 1.0f, 0.0f }, { x0, y1, 0.0f, 1.0f },
		{ x1, y0, 1.0f, 0.0f }, { x1, y1, 1.0f, 1.0f }, { x0, y1, 0.0f, 1.0f },
	};
	uint32_t first;
	uint32_t index;

	/* Two triangles, four floats a vertex. */
	first = run->vertex_floats / 4U;
	for (index = 0; index < 6; index++) {
		run->vertex_map[run->vertex_floats++] = vkmatrix_ndc(corners[index][0]);
		run->vertex_map[run->vertex_floats++] = vkmatrix_ndc(corners[index][1]);
		run->vertex_map[run->vertex_floats++] = corners[index][2];
		run->vertex_map[run->vertex_floats++] = corners[index][3];
	}

	return first;
}

/* Appends a quad of the compositor's batch vertices and returns its first vertex. */
static uint32_t
vkmatrix_batch_quad(
	struct vkmatrix *run,
	float x0,
	float y0,
	float x1,
	float y1,
	const float colour[4],
	const float extra[4],
	float alpha)
{
	const float corners[6][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
	uint32_t first;
	uint32_t index;
	uint32_t channel;

	/* Two triangles, eleven floats a vertex. */
	first = run->vertex_floats / VKMATRIX_BATCH_FLOATS;
	for (index = 0; index < 6; index++) {
		run->vertex_map[run->vertex_floats++] = vkmatrix_ndc(corners[index][0]);
		run->vertex_map[run->vertex_floats++] = vkmatrix_ndc(corners[index][1]);
		for (channel = 0; channel < 4; channel++)
			run->vertex_map[run->vertex_floats++] = colour[channel];
		for (channel = 0; channel < 4; channel++)
			run->vertex_map[run->vertex_floats++] = extra[channel];
		run->vertex_map[run->vertex_floats++] = alpha;
	}

	return first;
}

/* A colour (red, green, blue, alpha in 0..1) as the target's B8G8R8A8 word. */
static uint32_t
vkmatrix_pack(
	const float colour[4])
{
	uint32_t bytes[4];
	uint32_t index;
	float value;

	/* Each channel clamped and rounded to 8 bits. */
	for (index = 0; index < 4; index++) {
		value = colour[index];
		if (value < 0.0f)
			value = 0.0f;
		if (value > 1.0f)
			value = 1.0f;
		bytes[index] = (uint32_t)(value * 255.0f + 0.5f);
	}

	return bytes[2] | (bytes[1] << 8) | (bytes[0] << 16) | (bytes[3] << 24);
}

/* Sets the expected pixels of a rectangle. */
static void
vkmatrix_fill(
	struct vkmatrix *run,
	uint32_t x0,
	uint32_t y0,
	uint32_t x1,
	uint32_t y1,
	uint32_t value)
{
	uint32_t x;
	uint32_t y;

	for (y = y0; y < y1; y++) {
		for (x = x0; x < x1; x++)
			run->expected[y * VKMATRIX_SIZE + x] = value;
	}
}

/* Compares the readback with the expected image; 0 when every channel is within the tolerance. */
static int
vkmatrix_compare(
	struct vkmatrix *run)
{
	uint32_t index;
	uint32_t shift;
	int seen;
	int wanted;
	int difference;

	/* Each pixel, each channel. */
	for (index = 0; index < VKMATRIX_PIXELS; index++) {
		for (shift = 0; shift < 32; shift += 8) {
			seen = (int)((run->pixels[index] >> shift) & 0xffU);
			wanted = (int)((run->expected[index] >> shift) & 0xffU);
			difference = seen > wanted ? seen - wanted : wanted - seen;
			if (difference > run->tolerance) {
				snprintf(run->reason, sizeof(run->reason), "pixel (%u,%u) 0x%08x, expected 0x%08x",
					 (unsigned)(index % VKMATRIX_SIZE), (unsigned)(index / VKMATRIX_SIZE),
					 (unsigned)run->pixels[index], (unsigned)run->expected[index]);
				return 1;
			}
		}
	}

	/* Succeeded: the image is as expected. */
	return 0;
}

/* Destroys what one case made: its pipelines and textures. */
static void
vkmatrix_release_case(
	struct vkmatrix *run)
{
	struct vkmatrix_texture *texture;
	uint32_t index;

	/* Nothing may still run. */
	if (run->device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(run->device);

	/* The pipelines and their layouts. */
	for (index = 0; index < run->pipeline_count; index++) {
		if (run->pipelines[index] != VK_NULL_HANDLE)
			vkDestroyPipeline(run->device, run->pipelines[index], NULL);
		if (run->layouts[index] != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(run->device, run->layouts[index], NULL);
		run->pipelines[index] = VK_NULL_HANDLE;
		run->layouts[index] = VK_NULL_HANDLE;
	}

	run->pipeline_count = 0;

	/* The textures and their sets. */
	for (index = 0; index < run->texture_count; index++) {
		texture = &run->textures[index];
		if (texture->set != VK_NULL_HANDLE)
			vkFreeDescriptorSets(run->device, run->pool, 1, &texture->set);
		if (texture->sampler != VK_NULL_HANDLE)
			vkDestroySampler(run->device, texture->sampler, NULL);
		if (texture->view != VK_NULL_HANDLE)
			vkDestroyImageView(run->device, texture->view, NULL);
		if (texture->image != VK_NULL_HANDLE)
			vkDestroyImage(run->device, texture->image, NULL);
		if (texture->memory != VK_NULL_HANDLE)
			vkFreeMemory(run->device, texture->memory, NULL);
		memset(texture, 0, sizeof(*texture));
	}

	run->texture_count = 0;

	/* The wide cases' image, pass and buffers. */
	if (run->wide_framebuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(run->device, run->wide_framebuffer, NULL);
	if (run->wide_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(run->device, run->wide_pass, NULL);
	if (run->wide_view != VK_NULL_HANDLE)
		vkDestroyImageView(run->device, run->wide_view, NULL);
	if (run->wide_image != VK_NULL_HANDLE)
		vkDestroyImage(run->device, run->wide_image, NULL);
	if (run->wide_memory != VK_NULL_HANDLE)
		vkFreeMemory(run->device, run->wide_memory, NULL);
	if (run->wide_upload != VK_NULL_HANDLE)
		vkDestroyBuffer(run->device, run->wide_upload, NULL);
	if (run->wide_upload_memory != VK_NULL_HANDLE)
		vkFreeMemory(run->device, run->wide_upload_memory, NULL);
	if (run->wide_download != VK_NULL_HANDLE)
		vkDestroyBuffer(run->device, run->wide_download, NULL);
	if (run->wide_download_memory != VK_NULL_HANDLE)
		vkFreeMemory(run->device, run->wide_download_memory, NULL);
	run->wide_framebuffer = VK_NULL_HANDLE;
	run->wide_pass = VK_NULL_HANDLE;
	run->wide_view = VK_NULL_HANDLE;
	run->wide_image = VK_NULL_HANDLE;
	run->wide_memory = VK_NULL_HANDLE;
	run->wide_upload = VK_NULL_HANDLE;
	run->wide_upload_memory = VK_NULL_HANDLE;
	run->wide_download = VK_NULL_HANDLE;
	run->wide_download_memory = VK_NULL_HANDLE;
	run->pipeline_pass = VK_NULL_HANDLE;
}

/* Destroys everything the run made, in the reverse order. */
static void
vkmatrix_destroy(
	struct vkmatrix *run)
{
	/* What the last case left, then the shared objects. */
	vkmatrix_release_case(run);
	if (run->device != VK_NULL_HANDLE) {
		if (run->pool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(run->device, run->pool, NULL);
		if (run->set_layout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(run->device, run->set_layout, NULL);
		if (run->framebuffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer(run->device, run->framebuffer, NULL);
		if (run->load_pass != VK_NULL_HANDLE)
			vkDestroyRenderPass(run->device, run->load_pass, NULL);
		if (run->clear_pass != VK_NULL_HANDLE)
			vkDestroyRenderPass(run->device, run->clear_pass, NULL);
		if (run->vertices != VK_NULL_HANDLE)
			vkDestroyBuffer(run->device, run->vertices, NULL);
		if (run->vertex_memory != VK_NULL_HANDLE)
			vkFreeMemory(run->device, run->vertex_memory, NULL);
		if (run->readback != VK_NULL_HANDLE)
			vkDestroyBuffer(run->device, run->readback, NULL);
		if (run->readback_memory != VK_NULL_HANDLE)
			vkFreeMemory(run->device, run->readback_memory, NULL);
		if (run->target_view != VK_NULL_HANDLE)
			vkDestroyImageView(run->device, run->target_view, NULL);
		if (run->target != VK_NULL_HANDLE)
			vkDestroyImage(run->device, run->target, NULL);
		if (run->target_memory != VK_NULL_HANDLE)
			vkFreeMemory(run->device, run->target_memory, NULL);
		if (run->fence != VK_NULL_HANDLE)
			vkDestroyFence(run->device, run->fence, NULL);
		if (run->command_pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(run->device, run->command_pool, NULL);
		vkDestroyDevice(run->device, NULL);
	}

	if (run->instance != VK_NULL_HANDLE)
		vkDestroyInstance(run->instance, NULL);
}

/* Binds the vertex buffer and a pipeline. */
static void
vkmatrix_bind(
	struct vkmatrix *run,
	uint32_t pipeline)
{
	VkDeviceSize offset;

	offset = 0;
	vkCmdBindPipeline(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->pipelines[pipeline]);
	vkCmdBindVertexBuffers(run->command, 0, 1, &run->vertices, &offset);
}

/* push4: a 4-byte push constant read by the fragment, changed between three draws. */
static VkResult
case_push4(
	struct vkmatrix *run)
{
	static const float reds[3] = { 0.2f, 0.6f, 1.0f };
	struct vkmatrix_shape shape;
	float colour[4];
	uint32_t pipeline;
	uint32_t first;
	uint32_t index;
	VkResult status;

	/* The pipeline. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_push4_vert;
	shape.vertex_size = sizeof(vkmatrix_push4_vert);
	shape.fragment = vkmatrix_push4_frag;
	shape.fragment_size = sizeof(vkmatrix_push4_frag);
	shape.push_size = 4;
	shape.push_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
	status = vkmatrix_pipeline(run, &shape, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* Three quads side by side, each its own red. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	for (index = 0; index < 3; index++) {
		first = vkmatrix_quad(run, 4.0f + 20.0f * (float)index, 8.0f, 20.0f + 20.0f * (float)index, 40.0f);
		vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &reds[index]);
		vkCmdDraw(run->command, 6, 1, first, 0);
		colour[0] = reds[index];
		colour[1] = 0.0f;
		colour[2] = 0.0f;
		colour[3] = 1.0f;
		vkmatrix_fill(run, 4U + 20U * index, 8U, 20U + 20U * index, 40U, vkmatrix_pack(colour));
	}

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* Draws three quads with a push constant of an offset and a colour, read by the vertex and the fragment. */
static VkResult
case_push_sized(
	struct vkmatrix *run,
	const uint32_t *vertex,
	size_t vertex_size,
	const uint32_t *fragment,
	size_t fragment_size,
	uint32_t size)
{
	static const float colours[3][4] = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.4f, 0.0f, 1.0f }, { 0.8f, 0.8f, 0.2f, 1.0f } };
	struct vkmatrix_shape shape;
	float push[32];
	float colour[4];
	uint32_t pipeline;
	uint32_t first;
	uint32_t index;
	VkResult status;

	/* The pipeline. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vertex;
	shape.vertex_size = vertex_size;
	shape.fragment = fragment;
	shape.fragment_size = fragment_size;
	shape.push_size = size;
	shape.push_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	status = vkmatrix_pipeline(run, &shape, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* One quad at the top left, moved by each draw's offset; the colour at the range's end; a middle word for the 128-byte shader. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	first = vkmatrix_quad(run, 2.0f, 2.0f, 14.0f, 14.0f);
	for (index = 0; index < 3; index++) {
		memset(push, 0, sizeof(push));
		push[0] = (float)(index * 20U) / (float)(VKMATRIX_SIZE / 2U);
		push[1] = (float)(index * 16U) / (float)(VKMATRIX_SIZE / 2U);
		memcpy(&push[size / 4U - 4U], colours[index], sizeof(colours[index]));
		if (size == 128U)
			push[4 * 4 + 1] = 0.25f;
		vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, push);
		vkCmdDraw(run->command, 6, 1, first, 0);
		memcpy(colour, colours[index], sizeof(colour));
		if (size == 128U)
			colour[1] += 0.25f;
		vkmatrix_fill(run, 2U + index * 20U, 2U + index * 16U, 14U + index * 20U, 14U + index * 16U, vkmatrix_pack(colour));
	}

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* push32: the compositor's 32-byte push constant. */
static VkResult
case_push32(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the push constant case at this size. */
	status = case_push_sized(run, vkmatrix_push32_vert, sizeof(vkmatrix_push32_vert), vkmatrix_push32_frag, sizeof(vkmatrix_push32_frag), 32U);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* push128: a 128-byte push constant (the panel's 96 bytes fit in it). */
static VkResult
case_push128(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the push constant case at this size. */
	status = case_push_sized(run, vkmatrix_push128_vert, sizeof(vkmatrix_push128_vert), vkmatrix_push128_frag, sizeof(vkmatrix_push128_frag), 128U);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* batch: two quads of the compositor's batch vertex, every attribute used. */
static VkResult
case_batch(
	struct vkmatrix *run)
{
	static const float colours[2][4] = { { 1.0f, 0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f, 1.0f, 1.0f } };
	static const float extras[2][4] = { { 0.0f, 0.0f, 0.25f, 0.0f }, { 0.5f, 0.0f, 0.0f, 0.0f } };
	static const float alphas[2] = { 1.0f, 0.5f };
	struct vkmatrix_shape shape;
	float colour[4];
	uint32_t pipeline;
	uint32_t first;
	uint32_t index;
	uint32_t channel;
	VkResult status;

	/* The pipeline. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_batch_vert;
	shape.vertex_size = sizeof(vkmatrix_batch_vert);
	shape.fragment = vkmatrix_batch_frag;
	shape.fragment_size = sizeof(vkmatrix_batch_frag);
	shape.layout = VKMATRIX_BATCH;
	status = vkmatrix_pipeline(run, &shape, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* Two quads, each (colour.rgb * alpha, alpha) + extra. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	for (index = 0; index < 2; index++) {
		first = vkmatrix_batch_quad(run, 8.0f + 28.0f * (float)index, 8.0f, 28.0f + 28.0f * (float)index, 56.0f, colours[index], extras[index], alphas[index]);
		vkCmdDraw(run->command, 6, 1, first, 0);
		for (channel = 0; channel < 3; channel++)
			colour[channel] = colours[index][channel] * alphas[index] + extras[index][channel];
		colour[3] = alphas[index] + extras[index][3];
		vkmatrix_fill(run, 8U + 28U * index, 8U, 28U + 28U * index, 56U, vkmatrix_pack(colour));
	}

	run->tolerance = 1;

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* Makes the texture pipeline. */
static VkResult
texture_pipeline(
	struct vkmatrix *run,
	uint32_t *pipeline)
{
	struct vkmatrix_shape shape;
	VkResult status;

	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_texture_vert;
	shape.vertex_size = sizeof(vkmatrix_texture_vert);
	shape.fragment = vkmatrix_texture_frag;
	shape.fragment_size = sizeof(vkmatrix_texture_frag);
	shape.layout = VKMATRIX_TEXTURE;
	shape.texture = 1;

	/* Makes the pipeline of the shape. */
	status = vkmatrix_pipeline(run, &shape, pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the pipeline is made. */
	return VK_SUCCESS;
}

/* A texel word (0xAABBGGRR) as a colour. */
static void
texel_colour(
	uint32_t texel,
	float colour[4])
{
	uint32_t index;

	for (index = 0; index < 4; index++)
		colour[index] = (float)((texel >> (8U * index)) & 0xffU) / 255.0f;
}

/* nearest: a 4x4 texture on a 32x32 quad, each texel an 8x8 square. */
static VkResult
case_nearest(
	struct vkmatrix *run)
{
	uint32_t texels[16];
	float colour[4];
	uint32_t pipeline;
	uint32_t first;
	uint32_t index;
	VkResult status;

	/* The texture: sixteen colours. */
	for (index = 0; index < 16; index++)
		texels[index] = 0xff000000U | ((index * 16U) & 0xffU) | ((255U - index * 16U) << 8) | (((index % 4U) * 80U) << 16);
	status = vkmatrix_texture(run, 4, texels, VK_FILTER_NEAREST);
	if (status == VK_SUCCESS)
		status = texture_pipeline(run, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* The quad, and the squares expected. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &run->textures[0].set, 0, NULL);
	first = vkmatrix_texture_quad(run, 16.0f, 16.0f, 48.0f, 48.0f);
	vkCmdDraw(run->command, 6, 1, first, 0);
	for (index = 0; index < 16; index++) {
		texel_colour(texels[index], colour);
		vkmatrix_fill(run, 16U + (index % 4U) * 8U, 16U + (index / 4U) * 8U, 24U + (index % 4U) * 8U, 24U + (index / 4U) * 8U, vkmatrix_pack(colour));
	}

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* linear: a 2x2 texture on a 16x16 quad, filtered and clamped to the edge. */
static VkResult
case_linear(
	struct vkmatrix *run)
{
	static const uint32_t texels[4] = { 0xff0000ffU, 0xff00ff00U, 0xffff0000U, 0xffffffffU };
	float corners[4][4];
	float colour[4];
	float s;
	float t;
	float fx;
	float fy;
	uint32_t pipeline;
	uint32_t first;
	uint32_t x;
	uint32_t y;
	uint32_t channel;
	VkResult status;

	/* The texture and the pipeline. */
	status = vkmatrix_texture(run, 2, texels, VK_FILTER_LINEAR);
	if (status == VK_SUCCESS)
		status = texture_pipeline(run, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* The quad. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &run->textures[0].set, 0, NULL);
	first = vkmatrix_texture_quad(run, 24.0f, 24.0f, 40.0f, 40.0f);
	vkCmdDraw(run->command, 6, 1, first, 0);

	/* Each pixel: the bilinear mix of the four texels at its centre, clamped to the edge. */
	for (channel = 0; channel < 4; channel++)
		texel_colour(texels[channel], corners[channel]);
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++) {
			s = ((float)x + 0.5f) / 16.0f * 2.0f - 0.5f;
			t = ((float)y + 0.5f) / 16.0f * 2.0f - 0.5f;
			fx = unit_clamp(s);
			fy = unit_clamp(t);
			for (channel = 0; channel < 4; channel++) {
				colour[channel] = (corners[0][channel] * (1.0f - fx) + corners[1][channel] * fx) * (1.0f - fy) +
				    (corners[2][channel] * (1.0f - fx) + corners[3][channel] * fx) * fy;
			}

			run->expected[(24U + y) * VKMATRIX_SIZE + 24U + x] = vkmatrix_pack(colour);
		}
	}

	run->tolerance = 3;

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* two-textures: two sets, bound in turn between two draws of one pass. */
static VkResult
case_two_textures(
	struct vkmatrix *run)
{
	static const uint32_t red[1] = { 0xff0000ffU };
	static const uint32_t green[1] = { 0xff00ff00U };
	static const float red_colour[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	static const float green_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	uint32_t pipeline;
	uint32_t first;
	VkResult status;

	/* Two one-texel textures and the pipeline. */
	status = vkmatrix_texture(run, 1, red, VK_FILTER_NEAREST);
	if (status == VK_SUCCESS)
		status = vkmatrix_texture(run, 1, green, VK_FILTER_NEAREST);
	if (status == VK_SUCCESS)
		status = texture_pipeline(run, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* The left quad with the first set, the right with the second. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	first = vkmatrix_texture_quad(run, 4.0f, 4.0f, 28.0f, 60.0f);
	vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &run->textures[0].set, 0, NULL);
	vkCmdDraw(run->command, 6, 1, first, 0);
	first = vkmatrix_texture_quad(run, 36.0f, 4.0f, 60.0f, 60.0f);
	vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &run->textures[1].set, 0, NULL);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkmatrix_fill(run, 4, 4, 28, 60, vkmatrix_pack(red_colour));
	vkmatrix_fill(run, 36, 4, 60, 60, vkmatrix_pack(green_colour));

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* Draws one opaque quad, then two of a blend over it and over the background. */
static VkResult
case_blend(
	struct vkmatrix *run,
	enum vkmatrix_blend mode)
{
	static const float under[4] = { 0.0f, 0.6f, 0.0f, 1.0f };
	static const float over[4] = { 0.4f, 0.0f, 0.0f, 0.5f };
	struct vkmatrix_shape shape;
	float push[8];
	float colour[4];
	float base[4];
	uint32_t pipelines[2];
	uint32_t first;
	uint32_t channel;
	uint32_t pass;
	uint32_t left;
	uint32_t right;
	VkResult status;

	/* The opaque pipeline and the blending one, both of the 32-byte push constant. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_push32_vert;
	shape.vertex_size = sizeof(vkmatrix_push32_vert);
	shape.fragment = vkmatrix_push32_frag;
	shape.fragment_size = sizeof(vkmatrix_push32_frag);
	shape.push_size = 32;
	shape.push_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	status = vkmatrix_pipeline(run, &shape, &pipelines[0]);
	if (status != VK_SUCCESS)
		return status;
	shape.blend = mode;
	status = vkmatrix_pipeline(run, &shape, &pipelines[1]);
	if (status != VK_SUCCESS)
		return status;

	/* The green square, then the premultiplied red half-alpha band across it and the background. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	first = vkmatrix_quad(run, 8.0f, 8.0f, 40.0f, 56.0f);
	vkmatrix_bind(run, pipelines[0]);
	memset(push, 0, sizeof(push));
	memcpy(&push[4], under, sizeof(under));
	vkCmdPushConstants(run->command, run->layouts[pipelines[0]], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkmatrix_fill(run, 8, 8, 40, 56, vkmatrix_pack(under));
	first = vkmatrix_quad(run, 24.0f, 24.0f, 56.0f, 40.0f);
	vkmatrix_bind(run, pipelines[1]);
	memcpy(&push[4], over, sizeof(over));
	vkCmdPushConstants(run->command, run->layouts[pipelines[1]], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
	vkCmdDraw(run->command, 6, 1, first, 0);

	/* The band: over the green square (x 24..40), then over the background (x 40..56). */
	for (pass = 0; pass < 2; pass++) {
		/* The colour under the band and the band's columns in this pass. */
		if (pass == 0) {
			memcpy(base, under, sizeof(base));
			left = 24U;
			right = 40U;
		} else {
			memcpy(base, background, sizeof(base));
			left = 40U;
			right = 56U;
		}

		/* The blended colour. */
		for (channel = 0; channel < 4; channel++) {
			if (mode == VKMATRIX_OVER)
				colour[channel] = over[channel] + base[channel] * (1.0f - over[3]);
			else
				colour[channel] = over[channel] + base[channel];
		}

		/* The expected pixels of this part of the band. */
		vkmatrix_fill(run, left, 24U, right, 40U, vkmatrix_pack(colour));
	}

	run->tolerance = 1;

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* blend-over: premultiplied alpha, the compositor's blend. */
static VkResult
case_blend_over(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the blend case in this mode. */
	status = case_blend(run, VKMATRIX_OVER);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* blend-add: addition. */
static VkResult
case_blend_add(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the blend case in this mode. */
	status = case_blend(run, VKMATRIX_ADD);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* Draws a grid of the given number of squares in one pass, each its own colour by a push constant. */
static VkResult
case_draws(
	struct vkmatrix *run,
	uint32_t draws)
{
	struct vkmatrix_shape shape;
	float push[8];
	uint32_t pipeline;
	uint32_t first;
	uint32_t index;
	uint32_t columns;
	uint32_t side;
	uint32_t x;
	uint32_t y;
	VkResult status;

	/* The pipeline of the 32-byte push constant. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_push32_vert;
	shape.vertex_size = sizeof(vkmatrix_push32_vert);
	shape.fragment = vkmatrix_push32_frag;
	shape.fragment_size = sizeof(vkmatrix_push32_frag);
	shape.push_size = 32;
	shape.push_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	status = vkmatrix_pipeline(run, &shape, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* An 8-column grid of squares (8 or 4 rows), one quad moved by each draw's offset. */
	columns = 8;
	side = VKMATRIX_SIZE / columns;
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	first = vkmatrix_quad(run, 1.0f, 1.0f, (float)side - 1.0f, (float)side - 1.0f);
	for (index = 0; index < draws; index++) {
		x = index % columns;
		y = index / columns;
		memset(push, 0, sizeof(push));
		push[0] = (float)(x * side) / (float)(VKMATRIX_SIZE / 2U);
		push[1] = (float)(y * side) / (float)(VKMATRIX_SIZE / 2U);
		push[4] = (float)(index % 4U) / 3.0f;
		push[5] = (float)((index / 4U) % 4U) / 3.0f;
		push[6] = (float)(index / 16U) / 3.0f;
		push[7] = 1.0f;
		vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
		vkCmdDraw(run->command, 6, 1, first, 0);
		vkmatrix_fill(run, x * side + 1U, y * side + 1U, (x + 1U) * side - 1U, (y + 1U) * side - 1U, vkmatrix_pack(&push[4]));
	}

	run->tolerance = 1;

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* draws-32: 32 draws in one render pass. */
static VkResult
case_draws_32(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the many-draws case with this count. */
	status = case_draws(run, 32);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* draws-64: 64 draws in one render pass. */
static VkResult
case_draws_64(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the many-draws case with this count. */
	status = case_draws(run, 64);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* two-passes: a pass that clears and draws, then one that loads and draws, one submit. */
static VkResult
case_two_passes(
	struct vkmatrix *run)
{
	static const float first_colour[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	static const float second_colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
	struct vkmatrix_shape shape;
	float push[8];
	uint32_t pipeline;
	uint32_t first;
	VkResult status;

	/* The pipeline of the 32-byte push constant. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_push32_vert;
	shape.vertex_size = sizeof(vkmatrix_push32_vert);
	shape.fragment = vkmatrix_push32_frag;
	shape.fragment_size = sizeof(vkmatrix_push32_frag);
	shape.push_size = 32;
	shape.push_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	status = vkmatrix_pipeline(run, &shape, &pipeline);
	if (status != VK_SUCCESS)
		return status;
	first = vkmatrix_quad(run, 8.0f, 8.0f, 32.0f, 32.0f);
	memset(push, 0, sizeof(push));

	/* The first pass: cleared, the red square. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	memcpy(&push[4], first_colour, sizeof(first_colour));
	vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkmatrix_fill(run, 8, 8, 32, 32, vkmatrix_pack(first_colour));
	vkmatrix_finish(run, 0);

	/* The second: loaded, the green square moved by 24 pixels both ways, overlapping the red. */
	vkmatrix_begin(run, 1, background);
	vkmatrix_bind(run, pipeline);
	push[0] = 24.0f / (float)(VKMATRIX_SIZE / 2U);
	push[1] = 24.0f / (float)(VKMATRIX_SIZE / 2U);
	memcpy(&push[4], second_colour, sizeof(second_colour));
	vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkmatrix_fill(run, 32, 32, 56, 56, vkmatrix_pack(second_colour));

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* scissor-viewport: one quad over the whole target, drawn under three scissors and viewports. */
static VkResult
case_scissor_viewport(
	struct vkmatrix *run)
{
	static const float colours[3][4] = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } };
	struct vkmatrix_shape shape;
	VkViewport viewport;
	VkRect2D scissor;
	float push[8];
	uint32_t pipeline;
	uint32_t first;
	VkResult status;

	/* The pipeline of the 32-byte push constant. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_push32_vert;
	shape.vertex_size = sizeof(vkmatrix_push32_vert);
	shape.fragment = vkmatrix_push32_frag;
	shape.fragment_size = sizeof(vkmatrix_push32_frag);
	shape.push_size = 32;
	shape.push_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	status = vkmatrix_pipeline(run, &shape, &pipeline);
	if (status != VK_SUCCESS)
		return status;
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	first = vkmatrix_quad(run, 0.0f, 0.0f, (float)VKMATRIX_SIZE, (float)VKMATRIX_SIZE);
	memset(push, 0, sizeof(push));
	memset(&viewport, 0, sizeof(viewport));
	viewport.maxDepth = 1.0f;
	memset(&scissor, 0, sizeof(scissor));

	/* Red: the whole viewport, a scissor of x 4..20, y 4..60. */
	viewport.width = (float)VKMATRIX_SIZE;
	viewport.height = (float)VKMATRIX_SIZE;
	vkCmdSetViewport(run->command, 0, 1, &viewport);
	scissor.offset.x = 4;
	scissor.offset.y = 4;
	scissor.extent.width = 16;
	scissor.extent.height = 56;
	vkCmdSetScissor(run->command, 0, 1, &scissor);
	memcpy(&push[4], colours[0], sizeof(colours[0]));
	vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkmatrix_fill(run, 4, 4, 20, 60, vkmatrix_pack(colours[0]));

	/* Green: a viewport of x 24..40, y 8..24, the whole scissor. */
	viewport.x = 24.0f;
	viewport.y = 8.0f;
	viewport.width = 16.0f;
	viewport.height = 16.0f;
	vkCmdSetViewport(run->command, 0, 1, &viewport);
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = VKMATRIX_SIZE;
	scissor.extent.height = VKMATRIX_SIZE;
	vkCmdSetScissor(run->command, 0, 1, &scissor);
	memcpy(&push[4], colours[1], sizeof(colours[1]));
	vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkmatrix_fill(run, 24, 8, 40, 24, vkmatrix_pack(colours[1]));

	/* Yellow: a viewport of x 24..56, y 32..64 cut by a scissor of x 44..60, y 40..56. */
	viewport.x = 24.0f;
	viewport.y = 32.0f;
	viewport.width = 32.0f;
	viewport.height = 32.0f;
	vkCmdSetViewport(run->command, 0, 1, &viewport);
	scissor.offset.x = 44;
	scissor.offset.y = 40;
	scissor.extent.width = 16;
	scissor.extent.height = 16;
	vkCmdSetScissor(run->command, 0, 1, &scissor);
	memcpy(&push[4], colours[2], sizeof(colours[2]));
	vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkmatrix_fill(run, 44, 40, 56, 56, vkmatrix_pack(colours[2]));

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* sample-bgra: the nearest case with a B8G8R8A8 texture, the format the target and the compositor's images have. */
static VkResult
case_sample_bgra(
	struct vkmatrix *run)
{
	uint32_t texels[16];
	float colour[4];
	uint32_t pipeline;
	uint32_t first;
	uint32_t index;
	VkResult status;

	/* The texture: sixteen colours, each word 0xAARRGGBB as B8G8R8A8 holds it. */
	for (index = 0; index < 16; index++)
		texels[index] = 0xff000000U | ((index * 16U) & 0xffU) | ((255U - index * 16U) << 8) | (((index % 4U) * 80U) << 16);
	run->texture_format = VK_FORMAT_B8G8R8A8_UNORM;
	pipeline = 0;
	status = vkmatrix_texture(run, 4, texels, VK_FILTER_NEAREST);
	if (status == VK_SUCCESS)
		status = texture_pipeline(run, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* The quad, and the squares expected (blue the low byte). */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &run->textures[0].set, 0, NULL);
	first = vkmatrix_texture_quad(run, 16.0f, 16.0f, 48.0f, 48.0f);
	vkCmdDraw(run->command, 6, 1, first, 0);
	for (index = 0; index < 16; index++) {
		colour[0] = (float)((texels[index] >> 16) & 0xffU) / 255.0f;
		colour[1] = (float)((texels[index] >> 8) & 0xffU) / 255.0f;
		colour[2] = (float)(texels[index] & 0xffU) / 255.0f;
		colour[3] = 1.0f;
		vkmatrix_fill(run, 16U + (index % 4U) * 8U, 16U + (index / 4U) * 8U, 24U + (index % 4U) * 8U, 24U + (index / 4U) * 8U, vkmatrix_pack(colour));
	}

	/* Ends the pass, submits it and reads the image back. */
	status = vkmatrix_finish(run, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image was read back and compared. */
	return VK_SUCCESS;
}

/* Makes a 16x16 B8G8R8A8 image that is an attachment and a texture, its view, sampler and set, in the case's texture slot. */
static VkResult
rendered_texture(
	struct vkmatrix *run,
	struct vkmatrix_texture **result)
{
	struct vkmatrix_texture *texture;
	VkImageCreateInfo image_info;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view_info;
	VkSamplerCreateInfo sampler_info;
	VkDescriptorSetAllocateInfo set_info;
	VkDescriptorImageInfo descriptor;
	VkWriteDescriptorSet write;
	VkResult status;

	/* A free slot. */
	run->failed_call = "texture slots";
	if (run->texture_count >= VKMATRIX_TEXTURES_MAX)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	texture = &run->textures[run->texture_count];
	run->texture_count++;
	*result = texture;

	/* The image and its memory. */
	memset(&image_info, 0, sizeof(image_info));
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
	image_info.extent.width = 16;
	image_info.extent.height = 16;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	run->failed_call = "vkCreateImage";
	status = vkCreateImage(run->device, &image_info, NULL, &texture->image);
	if (status != VK_SUCCESS)
		return status;
	vkGetImageMemoryRequirements(run->device, texture->image, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	status = vkmatrix_find_memory(run, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &allocate.memoryTypeIndex);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkAllocateMemory";
	status = vkAllocateMemory(run->device, &allocate, NULL, &texture->memory);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkBindImageMemory";
	status = vkBindImageMemory(run->device, texture->image, texture->memory, 0);
	if (status != VK_SUCCESS)
		return status;

	/* The view and the sampler. */
	memset(&view_info, 0, sizeof(view_info));
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = texture->image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	run->failed_call = "vkCreateImageView";
	status = vkCreateImageView(run->device, &view_info, NULL, &texture->view);
	if (status != VK_SUCCESS)
		return status;
	memset(&sampler_info, 0, sizeof(sampler_info));
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_NEAREST;
	sampler_info.minFilter = VK_FILTER_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	run->failed_call = "vkCreateSampler";
	status = vkCreateSampler(run->device, &sampler_info, NULL, &texture->sampler);
	if (status != VK_SUCCESS)
		return status;

	/* The set that names them. */
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_info.descriptorPool = run->pool;
	set_info.descriptorSetCount = 1;
	set_info.pSetLayouts = &run->set_layout;
	run->failed_call = "vkAllocateDescriptorSets";
	status = vkAllocateDescriptorSets(run->device, &set_info, &texture->set);
	if (status != VK_SUCCESS)
		return status;
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.sampler = texture->sampler;
	descriptor.imageView = texture->view;
	descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = texture->set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &descriptor;
	vkUpdateDescriptorSets(run->device, 1, &write, 0, NULL);

	/* Succeeded: the image and its set. */
	return VK_SUCCESS;
}

/*
 * sample-rendered: a 16x16 B8G8R8A8 image a render pass clears, made a
 * texture and sampled by the next pass of the same command buffer, as a
 * pass that loads reads what the one before stored.
 */
static VkResult
case_sample_rendered(
	struct vkmatrix *run)
{
	static const float source_colour[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	struct vkmatrix_texture *texture;
	VkFramebufferCreateInfo framebuffer_info;
	VkFramebuffer framebuffer;
	VkRenderPassBeginInfo pass;
	VkClearValue value;
	VkCommandBufferBeginInfo begin;
	VkImageMemoryBarrier barrier;
	uint32_t pipeline;
	uint32_t first;
	VkResult status;

	/* The source image and the pipeline. */
	pipeline = 0;
	status = rendered_texture(run, &texture);
	if (status == VK_SUCCESS)
		status = texture_pipeline(run, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* The source's framebuffer. */
	memset(&framebuffer_info, 0, sizeof(framebuffer_info));
	framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_info.renderPass = run->clear_pass;
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = &texture->view;
	framebuffer_info.width = 16;
	framebuffer_info.height = 16;
	framebuffer_info.layers = 1;
	run->failed_call = "vkCreateFramebuffer";
	status = vkCreateFramebuffer(run->device, &framebuffer_info, NULL, &framebuffer);
	if (status != VK_SUCCESS)
		return status;

	/* The first pass: the source cleared, then made a texture. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer(run->command, 0);
	vkBeginCommandBuffer(run->command, &begin);
	memset(&value, 0, sizeof(value));
	memcpy(value.color.float32, source_colour, sizeof(value.color.float32));
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = run->clear_pass;
	pass.framebuffer = framebuffer;
	pass.renderArea.extent.width = 16;
	pass.renderArea.extent.height = 16;
	pass.clearValueCount = 1;
	pass.pClearValues = &value;
	vkCmdBeginRenderPass(run->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdEndRenderPass(run->command);
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = texture->image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

	/* The second pass of the same command buffer: the source sampled over a quad of the target. */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	memset(&value, 0, sizeof(value));
	memcpy(value.color.float32, background, sizeof(value.color.float32));
	pass.framebuffer = run->framebuffer;
	pass.renderArea.extent.width = VKMATRIX_SIZE;
	pass.renderArea.extent.height = VKMATRIX_SIZE;
	vkCmdBeginRenderPass(run->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkmatrix_full_view(run);
	vkmatrix_bind(run, pipeline);
	vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &texture->set, 0, NULL);
	first = vkmatrix_texture_quad(run, 16.0f, 16.0f, 48.0f, 48.0f);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkmatrix_fill(run, 16, 16, 48, 48, vkmatrix_pack(source_colour));
	run->tolerance = 1;
	status = vkmatrix_finish(run, 1);
	vkDeviceWaitIdle(run->device);
	vkDestroyFramebuffer(run->device, framebuffer, NULL);
	return status;
}

/* Makes the wide cases' 64x64 OPTIMAL image of the format, in device-local memory when there is some, and its view. */
static VkResult
wide_image(
	struct vkmatrix *run,
	VkFormat format,
	VkImageUsageFlags usage)
{
	VkImageCreateInfo image_info;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view_info;
	VkResult status;

	/* The image. */
	memset(&image_info, 0, sizeof(image_info));
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = format;
	image_info.extent.width = run->wide_width;
	image_info.extent.height = run->wide_height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = usage;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	run->failed_call = "vkCreateImage";
	status = vkCreateImage(run->device, &image_info, NULL, &run->wide_image);
	if (status != VK_SUCCESS)
		return status;

	/* Its memory: device-local if any type allows it, any type otherwise. */
	vkGetImageMemoryRequirements(run->device, run->wide_image, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	status = vkmatrix_find_memory(run, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &allocate.memoryTypeIndex);
	if (status != VK_SUCCESS)
		status = vkmatrix_find_memory(run, requirements.memoryTypeBits, 0, &allocate.memoryTypeIndex);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkAllocateMemory";
	status = vkAllocateMemory(run->device, &allocate, NULL, &run->wide_memory);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkBindImageMemory";
	status = vkBindImageMemory(run->device, run->wide_image, run->wide_memory, 0);
	if (status != VK_SUCCESS)
		return status;

	/* Its view. */
	memset(&view_info, 0, sizeof(view_info));
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = run->wide_image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	run->failed_call = "vkCreateImageView";
	status = vkCreateImageView(run->device, &view_info, NULL, &run->wide_view);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the image and its view. */
	return VK_SUCCESS;
}

/* Records a layout change of the wide image. */
static void
wide_barrier(
	struct vkmatrix *run,
	VkImageLayout from,
	VkImageLayout to,
	VkAccessFlags source_access,
	VkAccessFlags destination_access,
	VkPipelineStageFlags source_stage,
	VkPipelineStageFlags destination_stage)
{
	VkImageMemoryBarrier barrier;

	/* The whole colour image. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = run->wide_image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	barrier.oldLayout = from;
	barrier.newLayout = to;
	barrier.srcAccessMask = source_access;
	barrier.dstAccessMask = destination_access;
	vkCmdPipelineBarrier(run->command, source_stage, destination_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

/* Records the copy of the whole wide image (TRANSFER_SRC) into the download buffer and the barrier to the host. */
static void
wide_copy_out(
	struct vkmatrix *run)
{
	VkBufferImageCopy copy;
	VkMemoryBarrier host;

	/* The copy, tightly packed. */
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = run->wide_width;
	copy.imageExtent.height = run->wide_height;
	copy.imageExtent.depth = 1;
	vkCmdCopyImageToBuffer(run->command, run->wide_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, run->wide_download, 1, &copy);

	/* Transfer writes, then host reads. */
	memset(&host, 0, sizeof(host));
	host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL);
}

/* Ends the recording, submits it and waits for it. */
static VkResult
wide_submit(
	struct vkmatrix *run)
{
	VkSubmitInfo submit;
	VkResult status;

	/* The end, the submit, the wait. */
	run->failed_call = "vkEndCommandBuffer";
	status = vkEndCommandBuffer(run->command);
	if (status != VK_SUCCESS)
		return status;
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &run->command;
	vkResetFences(run->device, 1, &run->fence);
	run->failed_call = "vkQueueSubmit";
	status = vkQueueSubmit(run->queue, 1, &submit, run->fence);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkWaitForFences";
	status = vkWaitForFences(run->device, 1, &run->fence, VK_TRUE, VKMATRIX_WAIT_NS);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the work is done. */
	return VK_SUCCESS;
}

/*
 * Compares the texels read back with the wanted ones; on a difference it
 * names the first texel, every word seen and wanted there, the number of
 * texels that differ and which word positions differed anywhere, and fails
 * (VK_ERROR_UNKNOWN, the reason in the failed call), else it leaves the
 * shared target's comparison passing.
 */
static VkResult
wide_compare(
	struct vkmatrix *run,
	const uint8_t *seen,
	const uint8_t *wanted,
	uint32_t texel_bytes)
{
	uint32_t texel;
	uint32_t first;
	uint32_t bad;
	uint32_t positions;
	uint32_t word;
	uint32_t got;
	uint32_t want;
	size_t used;
	int differs;

	/* The count of texels that differ, the first of them, the word positions that differ. */
	bad = 0;
	first = run->wide_width * run->wide_height;
	positions = 0;
	for (texel = 0; texel < run->wide_width * run->wide_height; texel++) {
		/* A texel that matches says nothing. */
		differs = memcmp(seen + texel * texel_bytes, wanted + texel * texel_bytes, texel_bytes);
		if (differs == 0)
			continue;

		/* Counts it and remembers the first. */
		if (bad == 0)
			first = texel;
		bad++;

		/* The word positions that differ in it. */
		for (word = 0; word < texel_bytes / 4U; word++) {
			differs = memcmp(seen + texel * texel_bytes + word * 4U, wanted + texel * texel_bytes + word * 4U, 4);
			if (differs != 0)
				positions |= 1U << word;
		}
	}

	if (bad == 0) {
		/* Every texel matched: the shared target's comparison has nothing to say. */
		memcpy(run->expected, run->pixels, sizeof(run->expected));
		run->tolerance = 0;
		return VK_SUCCESS;
	}

	/* The first bad texel's words, seen/wanted, then the counts. */
	used = (size_t)snprintf(run->reason, sizeof(run->reason), "%u bad, words 0x%x, (%u,%u)",
				(unsigned)bad, (unsigned)positions, (unsigned)(first % run->wide_width), (unsigned)(first / run->wide_width));
	for (word = 0; word < texel_bytes / 4U && used < sizeof(run->reason); word++) {
		memcpy(&got, seen + first * texel_bytes + word * 4U, 4);
		memcpy(&want, wanted + first * texel_bytes + word * 4U, 4);
		used += (size_t)snprintf(run->reason + used, sizeof(run->reason) - used, " %08x/%08x", (unsigned)got, (unsigned)want);
	}

	run->failed_call = run->reason;
	return VK_ERROR_UNKNOWN;
}

/* clear-64 and clear-128: the image cleared by vkCmdClearColorImage, then copied out (the image-to-buffer leg alone). */
static VkResult
case_clear_wide(
	struct vkmatrix *run,
	VkFormat format,
	uint32_t texel_bytes)
{
	static const uint32_t words[4] = { 0x11223344U, 0x55667788U, 0x99aabbccU, 0xddeeff01U };
	static const uint16_t halves[4] = { 0x3400U, 0x3800U, 0x3a00U, 0x3c00U };
	VkCommandBufferBeginInfo begin;
	VkClearColorValue colour;
	VkImageSubresourceRange range;
	uint8_t *download;
	uint8_t *wanted;
	void *mapped;
	uint32_t texel;
	uint32_t byte;
	VkResult status;

	/* The image and the readback buffer. */
	status = wide_image(run, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	if (status != VK_SUCCESS)
		return status;
	status = vkmatrix_buffer(run, (VkDeviceSize)VKMATRIX_PIXELS * texel_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &run->wide_download, &run->wide_download_memory, &mapped);
	if (status != VK_SUCCESS)
		return status;
	download = mapped;
	memset(download, 0xa5, (size_t)VKMATRIX_PIXELS * texel_bytes);

	/* The colour: halves (0.25, 0.5, 0.75, 1) for 64 bits, the raw words for 128. */
	memset(&colour, 0, sizeof(colour));
	if (texel_bytes == 8) {
		colour.float32[0] = 0.25f;
		colour.float32[1] = 0.5f;
		colour.float32[2] = 0.75f;
		colour.float32[3] = 1.0f;
	} else {
		memcpy(colour.uint32, words, sizeof(words));
	}

	/* UNDEFINED -> TRANSFER_DST, the clear, -> TRANSFER_SRC, the copy out. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer(run->command, 0);
	vkBeginCommandBuffer(run->command, &begin);
	wide_barrier(run, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	memset(&range, 0, sizeof(range));
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1;
	range.layerCount = 1;
	vkCmdClearColorImage(run->command, run->wide_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
	wide_barrier(run, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	wide_copy_out(run);
	status = wide_submit(run);
	if (status != VK_SUCCESS)
		return status;

	/* The wanted texels: the colour in the format, every texel the same. */
	wanted = malloc((size_t)VKMATRIX_PIXELS * texel_bytes);
	if (wanted == NULL) {
		run->failed_call = "malloc";
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	for (texel = 0; texel < VKMATRIX_PIXELS; texel++) {
		byte = texel * texel_bytes;
		if (texel_bytes == 8)
			memcpy(wanted + byte, halves, sizeof(halves));
		else
			memcpy(wanted + byte, words, sizeof(words));
	}

	status = wide_compare(run, download, wanted, texel_bytes);
	free(wanted);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the cleared texels came back. */
	return VK_SUCCESS;
}

/* clear-64: R16G16B16A16_SFLOAT cleared and copied out. */
static VkResult
case_clear_64(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the wide-format case with this format. */
	status = case_clear_wide(run, VK_FORMAT_R16G16B16A16_SFLOAT, 8);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* clear-128: R32G32B32A32_SFLOAT cleared (raw words) and copied out. */
static VkResult
case_clear_128(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the wide-format case with this format. */
	status = case_clear_wide(run, VK_FORMAT_R32G32B32A32_SFLOAT, 16);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* copy-64 and copy-128: a pattern from a buffer into an OPTIMAL image of the format and back, every byte compared. */
static VkResult
case_copy_wide(
	struct vkmatrix *run,
	VkFormat format,
	uint32_t texel_bytes)
{
	VkCommandBufferBeginInfo begin;
	VkBufferImageCopy copy;
	uint8_t *upload;
	uint8_t *download;
	void *mapped;
	uint32_t byte;
	VkResult status;

	/* The image and the two buffers. */
	status = wide_image(run, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	if (status != VK_SUCCESS)
		return status;
	status = vkmatrix_buffer(run, (VkDeviceSize)run->wide_width * run->wide_height * texel_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &run->wide_upload, &run->wide_upload_memory, &mapped);
	if (status != VK_SUCCESS)
		return status;
	upload = mapped;
	status = vkmatrix_buffer(run, (VkDeviceSize)run->wide_width * run->wide_height * texel_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &run->wide_download, &run->wide_download_memory, &mapped);
	if (status != VK_SUCCESS)
		return status;
	download = mapped;

	/* Every byte different (any bit pattern: the copy moves the words as they are), the readback filled otherwise. */
	for (byte = 0; byte < run->wide_width * run->wide_height * texel_bytes; byte++) {
		upload[byte] = (uint8_t)(byte * 7U + byte / 251U);
		download[byte] = 0xa5;
	}

	/* UNDEFINED -> TRANSFER_DST, the copy in, -> TRANSFER_SRC, the copy out. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer(run->command, 0);
	vkBeginCommandBuffer(run->command, &begin);
	wide_barrier(run, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = run->wide_width;
	copy.imageExtent.height = run->wide_height;
	copy.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(run->command, run->wide_upload, run->wide_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	wide_barrier(run, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	wide_copy_out(run);
	status = wide_submit(run);
	if (status != VK_SUCCESS)
		return status;

	/* Every byte back. */
	status = wide_compare(run, download, upload, texel_bytes);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the texels went in and came back. */
	return VK_SUCCESS;
}

/* copy-64: 64-bit texels (R16G16B16A16_SFLOAT). */
static VkResult
case_copy_64(
	struct vkmatrix *run)
{
	VkResult status;

	/* The copy of eight bytes a texel. */
	status = case_copy_wide(run, VK_FORMAT_R16G16B16A16_SFLOAT, 8);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: every byte matched. */
	return VK_SUCCESS;
}

/* copy-128: 128-bit texels (R32G32B32A32_SFLOAT). */
static VkResult
case_copy_128(
	struct vkmatrix *run)
{
	VkResult status;

	/* The copy of sixteen bytes a texel. */
	status = case_copy_wide(run, VK_FORMAT_R32G32B32A32_SFLOAT, 16);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: every byte matched. */
	return VK_SUCCESS;
}

/*
 * render-f16: a pass on an R16G16B16A16_SFLOAT attachment cleared to
 * (0.25, 0.5, 0.75, 1), a quad of (0.625, 0, 0, 1) drawn over its middle,
 * the image copied out and every half float compared (each value exact
 * in a half float, so the pack's rounding does not matter).
 */
static VkResult
case_render_f16(
	struct vkmatrix *run)
{
	static const float clear[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	static const float red = 0.625f;
	static const uint16_t clear_halves[4] = { 0x3400, 0x3800, 0x3a00, 0x3c00 };
	static const uint16_t quad_halves[4] = { 0x3900, 0x0000, 0x0000, 0x3c00 };
	static uint16_t wanted[VKMATRIX_PIXELS * 4];
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo pass_info;
	VkFramebufferCreateInfo framebuffer_info;
	VkCommandBufferBeginInfo begin;
	VkRenderPassBeginInfo pass;
	VkClearValue value;
	struct vkmatrix_shape shape;
	void *mapped;
	uint32_t pipeline;
	uint32_t first;
	uint32_t x;
	uint32_t y;
	uint32_t channel;
	int inside;
	VkResult status;

	/* The attachment and the buffer it is copied into. */
	status = wide_image(run, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	if (status != VK_SUCCESS)
		return status;
	status = vkmatrix_buffer(run, (VkDeviceSize)VKMATRIX_PIXELS * 8U, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &run->wide_download, &run->wide_download_memory, &mapped);
	if (status != VK_SUCCESS)
		return status;
	memset(mapped, 0xa5, (size_t)VKMATRIX_PIXELS * 8U);

	/* The pass: cleared, stored, left for the copy. */
	memset(&attachment, 0, sizeof(attachment));
	attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	memset(&reference, 0, sizeof(reference));
	reference.attachment = 0;
	reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &reference;
	memset(&pass_info, 0, sizeof(pass_info));
	pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	pass_info.attachmentCount = 1;
	pass_info.pAttachments = &attachment;
	pass_info.subpassCount = 1;
	pass_info.pSubpasses = &subpass;
	run->failed_call = "vkCreateRenderPass";
	status = vkCreateRenderPass(run->device, &pass_info, NULL, &run->wide_pass);
	if (status != VK_SUCCESS)
		return status;
	memset(&framebuffer_info, 0, sizeof(framebuffer_info));
	framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_info.renderPass = run->wide_pass;
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = &run->wide_view;
	framebuffer_info.width = VKMATRIX_SIZE;
	framebuffer_info.height = VKMATRIX_SIZE;
	framebuffer_info.layers = 1;
	run->failed_call = "vkCreateFramebuffer";
	status = vkCreateFramebuffer(run->device, &framebuffer_info, NULL, &run->wide_framebuffer);
	if (status != VK_SUCCESS)
		return status;

	/* The push4 pipeline, made for this pass. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_push4_vert;
	shape.vertex_size = sizeof(vkmatrix_push4_vert);
	shape.fragment = vkmatrix_push4_frag;
	shape.fragment_size = sizeof(vkmatrix_push4_frag);
	shape.push_size = 4;
	shape.push_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
	run->pipeline_pass = run->wide_pass;
	status = vkmatrix_pipeline(run, &shape, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* The pass, the quad over the middle, the copy out. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer(run->command, 0);
	vkBeginCommandBuffer(run->command, &begin);
	memset(&value, 0, sizeof(value));
	memcpy(value.color.float32, clear, sizeof(value.color.float32));
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = run->wide_pass;
	pass.framebuffer = run->wide_framebuffer;
	pass.renderArea.extent.width = VKMATRIX_SIZE;
	pass.renderArea.extent.height = VKMATRIX_SIZE;
	pass.clearValueCount = 1;
	pass.pClearValues = &value;
	vkCmdBeginRenderPass(run->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkmatrix_full_view(run);
	vkmatrix_bind(run, pipeline);
	first = vkmatrix_quad(run, 16.0f, 16.0f, 48.0f, 48.0f);
	vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &red);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkCmdEndRenderPass(run->command);
	wide_copy_out(run);
	status = wide_submit(run);
	if (status != VK_SUCCESS)
		return status;

	/* The wanted half floats: the quad's inside, the clear outside. */
	for (y = 0; y < VKMATRIX_SIZE; y++) {
		for (x = 0; x < VKMATRIX_SIZE; x++) {
			inside = 0;
			if (x >= 16U && x < 48U && y >= 16U && y < 48U)
				inside = 1;
			for (channel = 0; channel < 4; channel++) {
				if (inside) {
					wanted[(y * VKMATRIX_SIZE + x) * 4U + channel] = quad_halves[channel];
				} else {
					wanted[(y * VKMATRIX_SIZE + x) * 4U + channel] = clear_halves[channel];
				}
			}
		}
	}

	/* Every half float. */
	status = wide_compare(run, mapped, (const uint8_t *)wanted, 8);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the clear and the quad, as half floats. */
	return VK_SUCCESS;
}

/*
 * sample-linear-*: a LINEAR R8G8B8A8 texture of width x height texels,
 * each a different colour, written through its mapping and sampled with
 * a nearest filter over the 32x32 quad; each pixel shows the texel its
 * centre falls in (no centre lies on a texel edge for these sizes).
 */
static VkResult
case_sample_linear(
	struct vkmatrix *run,
	uint32_t width,
	uint32_t height)
{
	uint32_t *texels;
	uint32_t texel;
	float colour[4];
	uint32_t pipeline;
	uint32_t first;
	uint32_t x;
	uint32_t y;
	VkResult status;

	/* The texels: red from x, green from y, blue mixing both. */
	texels = malloc((size_t)width * height * 4U);
	if (texels == NULL) {
		run->failed_call = "malloc";
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++)
			texels[y * width + x] = 0xff000000U | ((x * 37U) & 0xffU) | (((y * 71U + 13U) & 0xffU) << 8) | (((x * 5U + y * 29U) & 0xffU) << 16);
	}
	run->texture_linear = 1;
	run->texture_width = width;
	run->texture_height = height;
	pipeline = 0;
	status = vkmatrix_texture(run, 0, texels, VK_FILTER_NEAREST);
	if (status == VK_SUCCESS)
		status = texture_pipeline(run, &pipeline);
	if (status != VK_SUCCESS) {
		free(texels);
		return status;
	}

	/* The quad, and each pixel's texel (0xAABBGGRR). */
	vkmatrix_fill(run, 0, 0, VKMATRIX_SIZE, VKMATRIX_SIZE, vkmatrix_pack(background));
	vkmatrix_begin(run, 0, background);
	vkmatrix_bind(run, pipeline);
	vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &run->textures[0].set, 0, NULL);
	first = vkmatrix_texture_quad(run, 16.0f, 16.0f, 48.0f, 48.0f);
	vkCmdDraw(run->command, 6, 1, first, 0);
	for (y = 0; y < 32; y++) {
		for (x = 0; x < 32; x++) {
			texel = texels[((2U * y + 1U) * height / 64U) * width + (2U * x + 1U) * width / 64U];
			colour[0] = (float)(texel & 0xffU) / 255.0f;
			colour[1] = (float)((texel >> 8) & 0xffU) / 255.0f;
			colour[2] = (float)((texel >> 16) & 0xffU) / 255.0f;
			colour[3] = 1.0f;
			vkmatrix_fill(run, 16U + x, 16U + y, 17U + x, 17U + y, vkmatrix_pack(colour));
		}
	}
	free(texels);
	run->tolerance = 1;

	/* Ends the pass, submits it and reads the image back. */
	return vkmatrix_finish(run, 1);
}

/* sample-linear-1180: a linear row not a power of two (the desktop's client width). */
static VkResult
case_sample_linear_wide(
	struct vkmatrix *run)
{
	return case_sample_linear(run, 1180, 8);
}

/* sample-linear-65: an odd linear row and height. */
static VkResult
case_sample_linear_odd(
	struct vkmatrix *run)
{
	return case_sample_linear(run, 65, 3);
}

/*
 * render-linear: a 1920x1080 LINEAR B8G8R8A8 image in host-visible memory,
 * cleared and drawn with one quad, then read through its own mapping with
 * the row pitch vkGetImageSubresourceLayout reports, every pixel compared
 * (the image a display would scan out without a copy).
 */
static VkResult
case_render_linear(
	struct vkmatrix *run)
{
	static const float quad[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	VkSubresourceLayout layout;
	struct vkmatrix_shape shape;
	VkRenderPassBeginInfo pass;
	VkCommandBufferBeginInfo begin;
	VkClearValue value;
	VkViewport viewport;
	VkRect2D scissor;
	VkMemoryBarrier host;
	VkSubmitInfo submit;
	uint32_t pipeline;
	uint32_t first;
	VkResult status;

	/* The flat colour pipeline (push4: a red of the push constant, so the quad's colour is red). */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_push4_vert;
	shape.vertex_size = sizeof(vkmatrix_push4_vert);
	shape.fragment = vkmatrix_push4_frag;
	shape.fragment_size = sizeof(vkmatrix_push4_frag);
	shape.push_size = 4;
	shape.push_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
	status = vkmatrix_pipeline(run, &shape, &pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* The linear image, its memory, view and framebuffer, and its row layout. */
	status = linear_target(run, VK_IMAGE_TILING_LINEAR, &layout);
	if (status != VK_SUCCESS)
		return status;

	/* The command buffer. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer(run->command, 0);
	vkBeginCommandBuffer(run->command, &begin);

	/* The pass over the whole image, cleared to the background. */
	memset(&value, 0, sizeof(value));
	memcpy(value.color.float32, background, sizeof(value.color.float32));
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = run->clear_pass;
	pass.framebuffer = run->wide_framebuffer;
	pass.renderArea.extent.width = VKMATRIX_LINEAR_WIDTH;
	pass.renderArea.extent.height = VKMATRIX_LINEAR_HEIGHT;
	pass.clearValueCount = 1;
	pass.pClearValues = &value;
	vkCmdBeginRenderPass(run->command, &pass, VK_SUBPASS_CONTENTS_INLINE);

	/* The viewport over the whole image: the quad's units of 64 scale to it. */
	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)VKMATRIX_LINEAR_WIDTH;
	viewport.height = (float)VKMATRIX_LINEAR_HEIGHT;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(run->command, 0, 1, &viewport);
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = VKMATRIX_LINEAR_WIDTH;
	scissor.extent.height = VKMATRIX_LINEAR_HEIGHT;
	vkCmdSetScissor(run->command, 0, 1, &scissor);

	/* The quad over (16,16)-(48,48) of 64: pixels 480..1439 by 270..809. */
	vkmatrix_bind(run, pipeline);
	first = vkmatrix_quad(run, 16.0f, 16.0f, 48.0f, 48.0f);
	vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &quad[0]);
	vkCmdDraw(run->command, 6, 1, first, 0);
	vkCmdEndRenderPass(run->command);

	/* The colour writes, then host reads. */
	memset(&host, 0, sizeof(host));
	host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	host.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL);
	vkEndCommandBuffer(run->command);

	/* The submit and its wait. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &run->command;
	vkResetFences(run->device, 1, &run->fence);
	run->failed_call = "vkQueueSubmit";
	status = vkQueueSubmit(run->queue, 1, &submit, run->fence);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkWaitForFences";
	status = vkWaitForFences(run->device, 1, &run->fence, VK_TRUE, VKMATRIX_WAIT_NS);
	if (status != VK_SUCCESS)
		return status;

	/* Every pixel through the image's own mapping. */
	status = linear_check(run, &layout, quad);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the PBE stored the linear image row by row. */
	return VK_SUCCESS;
}

/* Makes the 1920x1080 image (LINEAR in host-visible coherent memory, or OPTIMAL), its view and framebuffer, and reads its row layout. */
static VkResult
linear_target(
	struct vkmatrix *run,
	VkImageTiling tiling,
	VkSubresourceLayout *layout)
{
	VkImageCreateInfo image_info;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view_info;
	VkFramebufferCreateInfo framebuffer_info;
	VkImageSubresource subresource;
	VkResult status;

	/* The image: a colour attachment the processor reads. */
	memset(&image_info, 0, sizeof(image_info));
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
	image_info.extent.width = VKMATRIX_LINEAR_WIDTH;
	image_info.extent.height = VKMATRIX_LINEAR_HEIGHT;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = tiling;
	image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (tiling == VK_IMAGE_TILING_OPTIMAL)
		image_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	run->failed_call = "vkCreateImage (linear)";
	status = vkCreateImage(run->device, &image_info, NULL, &run->wide_image);
	if (status != VK_SUCCESS)
		return status;

	/* Its memory: host-visible and coherent, as a scanout buffer is. */
	vkGetImageMemoryRequirements(run->device, run->wide_image, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	if (tiling == VK_IMAGE_TILING_LINEAR) {
		status = vkmatrix_find_memory(run, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &allocate.memoryTypeIndex);
	} else {
		status = vkmatrix_find_memory(run, requirements.memoryTypeBits, 0, &allocate.memoryTypeIndex);
	}
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkAllocateMemory (linear)";
	status = vkAllocateMemory(run->device, &allocate, NULL, &run->wide_memory);
	if (status != VK_SUCCESS)
		return status;
	run->failed_call = "vkBindImageMemory (linear)";
	status = vkBindImageMemory(run->device, run->wide_image, run->wide_memory, 0);
	if (status != VK_SUCCESS)
		return status;

	/* Its view. */
	memset(&view_info, 0, sizeof(view_info));
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = run->wide_image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	run->failed_call = "vkCreateImageView (linear)";
	status = vkCreateImageView(run->device, &view_info, NULL, &run->wide_view);
	if (status != VK_SUCCESS)
		return status;

	/* Its framebuffer in the shared clearing pass (same format, one sample). */
	memset(&framebuffer_info, 0, sizeof(framebuffer_info));
	framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_info.renderPass = run->clear_pass;
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = &run->wide_view;
	framebuffer_info.width = VKMATRIX_LINEAR_WIDTH;
	framebuffer_info.height = VKMATRIX_LINEAR_HEIGHT;
	framebuffer_info.layers = 1;
	run->failed_call = "vkCreateFramebuffer (linear)";
	status = vkCreateFramebuffer(run->device, &framebuffer_info, NULL, &run->wide_framebuffer);
	if (status != VK_SUCCESS)
		return status;

	/* Its rows' place and pitch. */
	memset(&subresource, 0, sizeof(subresource));
	subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vkGetImageSubresourceLayout(run->device, run->wide_image, &subresource, layout);

	/* Succeeded: the image is ready to render. */
	return VK_SUCCESS;
}

/*
 * Compares every pixel of the linear image, through its mapping, with
 * the clear colour outside the quad and the quad's colour inside; on a
 * difference it names the first pixel, the count and the row pitch.
 */
static VkResult
linear_check(
	struct vkmatrix *run,
	const VkSubresourceLayout *layout,
	const float quad[4])
{
	const uint8_t *bytes;
	const uint32_t *row;
	uint32_t inside_word;
	uint32_t outside_word;
	uint32_t wanted;
	uint32_t first_x;
	uint32_t first_y;
	uint32_t first_seen;
	uint32_t bad;
	uint32_t x;
	uint32_t y;
	void *mapped;
	VkResult status;

	/* The image's memory. */
	run->failed_call = "vkMapMemory (linear)";
	status = vkMapMemory(run->device, run->wide_memory, 0, VK_WHOLE_SIZE, 0, &mapped);
	if (status != VK_SUCCESS)
		return status;

	/* The two colours as the image stores them. */
	inside_word = vkmatrix_pack(quad);
	outside_word = vkmatrix_pack(background);

	/* Each pixel against its colour; the first wrong one is remembered. */
	bytes = (const uint8_t *)mapped + layout->offset;
	bad = 0;
	first_x = 0;
	first_y = 0;
	first_seen = 0;
	for (y = 0; y < VKMATRIX_LINEAR_HEIGHT; y++) {
		row = (const uint32_t *)(bytes + (size_t)y * layout->rowPitch);
		for (x = 0; x < VKMATRIX_LINEAR_WIDTH; x++) {
			/* The quad covers 480..1439 by 270..809. */
			wanted = outside_word;
			if (x >= 480U && x < 1440U && y >= 270U && y < 810U)
				wanted = inside_word;

			/* A wrong pixel. */
			if (row[x] != wanted) {
				if (bad == 0) {
					first_x = x;
					first_y = y;
					first_seen = row[x];
				}
				bad++;
			}
		}
	}
	vkUnmapMemory(run->device, run->wide_memory);

	/* Every pixel right: the shared target's comparison has nothing to say. */
	if (bad == 0) {
		memcpy(run->expected, run->pixels, sizeof(run->expected));
		run->tolerance = 0;
		return VK_SUCCESS;
	}

	/* Names what was wrong. */
	snprintf(run->reason, sizeof(run->reason), "%u bad pixels, first (%u,%u) 0x%08x, row pitch %u",
		 (unsigned)bad, (unsigned)first_x, (unsigned)first_y, (unsigned)first_seen, (unsigned)layout->rowPitch);
	run->failed_call = run->reason;
	return VK_ERROR_UNKNOWN;
}

/* copy-1080: a 1920x1080 RGBA8 image (1080 not a whole number of tiles) through buffers of exactly its size. */
static VkResult
case_copy_1080(
	struct vkmatrix *run)
{
	run->wide_width = 1920;
	run->wide_height = 1080;
	return case_copy_wide(run, VK_FORMAT_R8G8B8A8_UNORM, 4);
}

/* copy-65: a 65x3 RGBA8 image (an odd row smaller than a tile) through buffers of exactly its size. */
static VkResult
case_copy_65(
	struct vkmatrix *run)
{
	run->wide_width = 65;
	run->wide_height = 3;
	return case_copy_wide(run, VK_FORMAT_R8G8B8A8_UNORM, 4);
}

/*
 * load-1080-6, load-1080-32 and load-1080-linear: a 1920x1080 B8G8R8A8
 * attachment (OPTIMAL, or LINEAR for the last) cleared once, then drawn
 * in VKMATRIX_LOAD_FRAMES passes that each load it (LOAD, the driver's
 * full-screen load draw first) and draw 6 or 32 quads in a grid with a
 * red that changes every pass.  Each pass is submitted and waited for.
 * At the end every pixel holds the last pass's red inside a quad and the
 * clear colour outside: what the desktop's compositor does every frame.
 */
static VkResult
case_load_1080(
	struct vkmatrix *run,
	uint32_t draws,
	VkImageTiling tiling,
	int textured)
{
	VkSubresourceLayout layout;
	struct vkmatrix_shape shape;
	VkRenderPassBeginInfo pass;
	VkCommandBufferBeginInfo begin;
	VkClearValue value;
	VkViewport viewport;
	VkRect2D scissor;
	VkImageMemoryBarrier barrier;
	VkBufferImageCopy copy;
	VkMemoryBarrier host;
	VkSubmitInfo submit;
	const uint8_t *bytes;
	const uint32_t *row;
	uint32_t firsts[32];
	uint32_t wanted;
	uint32_t bad;
	uint32_t first_x;
	uint32_t first_y;
	uint32_t first_seen;
	uint32_t pipeline;
	uint32_t frame;
	uint32_t draw;
	uint32_t x;
	uint32_t y;
	int inside;
	float corners[4];
	float red;
	float colour[4];
	void *mapped;
	VkResult status;

	/* The flat red pipeline, or the textured one blending over (and its two textures). */
	if (textured) {
		status = load_1080_textures(run, &pipeline);
		if (status != VK_SUCCESS)
			return status;
	} else {
		memset(&shape, 0, sizeof(shape));
		shape.vertex = vkmatrix_push4_vert;
		shape.vertex_size = sizeof(vkmatrix_push4_vert);
		shape.fragment = vkmatrix_push4_frag;
		shape.fragment_size = sizeof(vkmatrix_push4_frag);
		shape.push_size = 4;
		shape.push_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
		status = vkmatrix_pipeline(run, &shape, &pipeline);
		if (status != VK_SUCCESS)
			return status;
	}

	/* The attachment, and for an OPTIMAL one a buffer to copy it out to. */
	status = linear_target(run, tiling, &layout);
	if (status != VK_SUCCESS)
		return status;
	if (tiling == VK_IMAGE_TILING_OPTIMAL) {
		status = vkmatrix_buffer(run, (VkDeviceSize)VKMATRIX_LINEAR_WIDTH * VKMATRIX_LINEAR_HEIGHT * 4U, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &run->wide_download, &run->wide_download_memory, &mapped);
		if (status != VK_SUCCESS)
			return status;
		layout.offset = 0;
		layout.rowPitch = (VkDeviceSize)VKMATRIX_LINEAR_WIDTH * 4U;
	}

	/* The quads, the same every pass. */
	for (draw = 0; draw < draws; draw++) {
		load_1080_quad(draw, corners);
		if (textured) {
			firsts[draw] = vkmatrix_texture_quad(run, corners[0], corners[1], corners[2], corners[3]);
		} else {
			firsts[draw] = vkmatrix_quad(run, corners[0], corners[1], corners[2], corners[3]);
		}
	}

	/* The passes: the first clears, the others load, each its own submission. */
	for (frame = 0; frame < VKMATRIX_LOAD_FRAMES; frame++) {
		/* The command buffer and the pass over the whole image. */
		memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkResetCommandBuffer(run->command, 0);
		vkBeginCommandBuffer(run->command, &begin);
		memset(&value, 0, sizeof(value));
		memcpy(value.color.float32, background, sizeof(value.color.float32));
		memset(&pass, 0, sizeof(pass));
		pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		pass.renderPass = run->clear_pass;
		if (frame != 0)
			pass.renderPass = run->load_pass;
		pass.framebuffer = run->wide_framebuffer;
		pass.renderArea.extent.width = VKMATRIX_LINEAR_WIDTH;
		pass.renderArea.extent.height = VKMATRIX_LINEAR_HEIGHT;
		pass.clearValueCount = 1;
		pass.pClearValues = &value;
		vkCmdBeginRenderPass(run->command, &pass, VK_SUBPASS_CONTENTS_INLINE);

		/* The viewport and scissor over the whole image. */
		memset(&viewport, 0, sizeof(viewport));
		viewport.width = (float)VKMATRIX_LINEAR_WIDTH;
		viewport.height = (float)VKMATRIX_LINEAR_HEIGHT;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(run->command, 0, 1, &viewport);
		memset(&scissor, 0, sizeof(scissor));
		scissor.extent.width = VKMATRIX_LINEAR_WIDTH;
		scissor.extent.height = VKMATRIX_LINEAR_HEIGHT;
		vkCmdSetScissor(run->command, 0, 1, &scissor);

		/* The quads with this pass's reds, or the first six of the 1024x256 texture and the rest of the 1024x1024 one. */
		vkmatrix_bind(run, pipeline);
		for (draw = 0; draw < draws; draw++) {
			if (textured) {
				if (draw == 0 || draw == 6U)
					vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &run->textures[draw == 0 ? 0 : 1].set, 0, NULL);
			} else {
				red = load_1080_red(frame, draw);
				vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &red);
			}
			vkCmdDraw(run->command, 6, 1, firsts[draw], 0);
		}
		vkCmdEndRenderPass(run->command);

		/* The last pass of an OPTIMAL image is copied out. */
		if (frame + 1U == VKMATRIX_LOAD_FRAMES && tiling == VK_IMAGE_TILING_OPTIMAL) {
			memset(&barrier, 0, sizeof(barrier));
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = run->wide_image;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
			memset(&copy, 0, sizeof(copy));
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.layerCount = 1;
			copy.imageExtent.width = VKMATRIX_LINEAR_WIDTH;
			copy.imageExtent.height = VKMATRIX_LINEAR_HEIGHT;
			copy.imageExtent.depth = 1;
			vkCmdCopyImageToBuffer(run->command, run->wide_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, run->wide_download, 1, &copy);
		}

		/* The writes, then host reads. */
		memset(&host, 0, sizeof(host));
		host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		host.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		vkCmdPipelineBarrier(run->command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL);
		vkEndCommandBuffer(run->command);

		/* The submit and its wait. */
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &run->command;
		vkResetFences(run->device, 1, &run->fence);
		run->failed_call = "vkQueueSubmit (load pass)";
		status = vkQueueSubmit(run->queue, 1, &submit, run->fence);
		if (status != VK_SUCCESS)
			return status;
		run->failed_call = "vkWaitForFences (load pass)";
		status = vkWaitForFences(run->device, 1, &run->fence, VK_TRUE, VKMATRIX_WAIT_NS);
		if (status != VK_SUCCESS)
			return status;
	}

	/* The pixels: the image's own memory, or the copy. */
	if (tiling == VK_IMAGE_TILING_LINEAR) {
		run->failed_call = "vkMapMemory (load)";
		status = vkMapMemory(run->device, run->wide_memory, 0, VK_WHOLE_SIZE, 0, &mapped);
		if (status != VK_SUCCESS)
			return status;
	}

	/* Each pixel against the last pass's reds and the clear colour. */
	bytes = (const uint8_t *)mapped + layout.offset;
	bad = 0;
	first_x = 0;
	first_y = 0;
	first_seen = 0;
	colour[1] = 0.0f;
	colour[2] = 0.0f;
	colour[3] = 1.0f;
	for (y = 0; y < VKMATRIX_LINEAR_HEIGHT; y++) {
		row = (const uint32_t *)(bytes + (size_t)y * layout.rowPitch);
		for (x = 0; x < VKMATRIX_LINEAR_WIDTH; x++) {
			/* The quad the pixel is in, if any, at 30 by 16.875 pixels a unit; a textured quad is not compared. */
			wanted = vkmatrix_pack(background);
			inside = 0;
			for (draw = 0; draw < draws; draw++) {
				load_1080_quad(draw, corners);
				if (x >= (uint32_t)(corners[0] * 30.0f) && x < (uint32_t)(corners[2] * 30.0f) &&
				    y >= (uint32_t)(corners[1] * 16.875f) && y < (uint32_t)(corners[3] * 16.875f)) {
					colour[0] = load_1080_red(VKMATRIX_LOAD_FRAMES - 1U, draw);
					wanted = vkmatrix_pack(colour);
					inside = 1;
				}
			}
			if (textured && inside)
				continue;

			/* A wrong pixel. */
			if (row[x] != wanted) {
				if (bad == 0) {
					first_x = x;
					first_y = y;
					first_seen = row[x];
				}
				bad++;
			}
		}
	}
	if (tiling == VK_IMAGE_TILING_LINEAR)
		vkUnmapMemory(run->device, run->wide_memory);

	/* Every pixel right: the shared target's comparison has nothing to say. */
	if (bad == 0) {
		memcpy(run->expected, run->pixels, sizeof(run->expected));
		run->tolerance = 0;
		return VK_SUCCESS;
	}

	/* Names what was wrong. */
	snprintf(run->reason, sizeof(run->reason), "%u bad pixels after %u passes, first (%u,%u) 0x%08x", (unsigned)bad, (unsigned)VKMATRIX_LOAD_FRAMES, (unsigned)first_x, (unsigned)first_y, (unsigned)first_seen);
	run->failed_call = run->reason;
	return VK_ERROR_UNKNOWN;
}

/* The red of one quad in one pass: changing with both, never the clear's. */
static float
load_1080_red(
	uint32_t frame,
	uint32_t draw)
{
	uint32_t level;

	/* A level from 40 to 239. */
	level = (frame * 7U + draw * 13U) % 200U + 40U;

	/* Succeeded: the red. */
	return (float)level / 255.0f;
}

/* The corners (x0, y0, x1, y1) of one quad of the grid, in units of 64 across and down: 8 a row, 4 rows. */
static void
load_1080_quad(
	uint32_t draw,
	float corners[4])
{
	/* Columns 8 units apart, rows 16 apart, each 4 by 8 units. */
	corners[0] = (float)((draw % 8U) * 8U + 2U);
	corners[1] = (float)(((draw / 8U) * 2U + 1U) * 8U);
	corners[2] = corners[0] + 4.0f;
	corners[3] = corners[1] + 8.0f;
}

/* load-1080-6: six quads a pass, OPTIMAL. */
static VkResult
case_load_1080_6(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the load case with six draws. */
	status = case_load_1080(run, 6U, VK_IMAGE_TILING_OPTIMAL, 0);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* load-1080-32: 32 quads a pass, OPTIMAL. */
static VkResult
case_load_1080_32(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the load case with 32 draws. */
	status = case_load_1080(run, 32U, VK_IMAGE_TILING_OPTIMAL, 0);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* load-1080-linear: six quads a pass, LINEAR in host-visible memory (a swapchain image). */
static VkResult
case_load_1080_linear(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the load case on a linear image. */
	status = case_load_1080(run, 6U, VK_IMAGE_TILING_LINEAR, 0);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/*
 * load-1080-textured: what the desktop's locking job draws over its
 * loaded 1080p target: 32 quads, the first six sampling a 1024x256
 * texture and the rest a 1024x1024 one (OPTIMAL, R8G8B8A8, linear
 * filtering), blended over (premultiplied), 60 passes.  Only the pixels
 * outside the quads are compared (the clear colour the loads keep).
 */
static VkResult
case_load_1080_textured(
	struct vkmatrix *run)
{
	VkResult status;

	/* Runs the load case with the textured draws. */
	status = case_load_1080(run, 32U, VK_IMAGE_TILING_OPTIMAL, 1);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* Makes the textured case's 1024x256 and 1024x1024 textures and its pipeline, blending over. */
static VkResult
load_1080_textures(
	struct vkmatrix *run,
	uint32_t *pipeline)
{
	struct vkmatrix_shape shape;
	uint32_t *texels;
	uint32_t index;
	VkResult status;

	/* Opaque texels of a pattern, enough for the larger texture. */
	texels = malloc((size_t)1024U * 1024U * 4U);
	if (texels == NULL) {
		run->failed_call = "malloc";
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	for (index = 0; index < 1024U * 1024U; index++)
		texels[index] = 0xff000000U | (index * 2654435761U >> 8);

	/* The text-atlas-like texture, then the icon-atlas-like one. */
	run->texture_width = 1024U;
	run->texture_height = 256U;
	status = vkmatrix_texture(run, 0, texels, VK_FILTER_LINEAR);
	if (status == VK_SUCCESS) {
		run->texture_height = 1024U;
		status = vkmatrix_texture(run, 0, texels, VK_FILTER_LINEAR);
	}
	free(texels);
	if (status != VK_SUCCESS)
		return status;

	/* The textured pipeline, blending over. */
	memset(&shape, 0, sizeof(shape));
	shape.vertex = vkmatrix_texture_vert;
	shape.vertex_size = sizeof(vkmatrix_texture_vert);
	shape.fragment = vkmatrix_texture_frag;
	shape.fragment_size = sizeof(vkmatrix_texture_frag);
	shape.layout = VKMATRIX_TEXTURE;
	shape.texture = 1;
	shape.blend = VKMATRIX_OVER;
	status = vkmatrix_pipeline(run, &shape, pipeline);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the textures and the pipeline. */
	return VK_SUCCESS;
}

/*
 * perf-clear, perf-load, perf-flat and perf-texture: the time of one
 * 1920x1080 OPTIMAL B8G8R8A8 pass, each submitted and waited for, over
 * PERF_FRAMES frames after one not counted: a clear only; a load only (the
 * driver's load draw); a clear and one full-screen flat quad; a clear and
 * one full-screen quad sampling a 1024x1024 OPTIMAL texture with linear
 * filtering.  Prints "vkmatrix: perf <kind>: <ms> ms a frame (min <ms>)".
 */
static VkResult
perf_case(
	struct vkmatrix *run,
	int kind)
{
	static const char *const names[4] = { "clear", "load", "flat", "texture" };
	VkSubresourceLayout layout;
	struct vkmatrix_shape shape;
	VkRenderPassBeginInfo pass;
	VkCommandBufferBeginInfo begin;
	VkClearValue value;
	VkViewport viewport;
	VkRect2D scissor;
	VkSubmitInfo submit;
	uint64_t started;
	uint64_t spent;
	uint64_t total;
	uint64_t least;
	uint32_t pipeline;
	uint32_t first;
	uint32_t frame;
	float red;
	VkResult status;

	/* The pipeline the kind draws with. */
	pipeline = 0;
	first = 0;
	if (kind == PERF_TEXTURE) {
		status = load_1080_textures(run, &pipeline);
		if (status != VK_SUCCESS)
			return status;
		first = vkmatrix_texture_quad(run, 0.0f, 0.0f, 64.0f, 64.0f);
	} else if (kind == PERF_FLAT) {
		memset(&shape, 0, sizeof(shape));
		shape.vertex = vkmatrix_push4_vert;
		shape.vertex_size = sizeof(vkmatrix_push4_vert);
		shape.fragment = vkmatrix_push4_frag;
		shape.fragment_size = sizeof(vkmatrix_push4_frag);
		shape.push_size = 4;
		shape.push_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
		status = vkmatrix_pipeline(run, &shape, &pipeline);
		if (status != VK_SUCCESS)
			return status;
		first = vkmatrix_quad(run, 0.0f, 0.0f, 64.0f, 64.0f);
	}

	/* The 1920x1080 attachment. */
	status = linear_target(run, VK_IMAGE_TILING_OPTIMAL, &layout);
	if (status != VK_SUCCESS)
		return status;

	/* The frames, the first not counted. */
	total = 0;
	least = UINT64_MAX;
	for (frame = 0; frame <= PERF_FRAMES; frame++) {
		/* The command buffer and the pass: cleared, or loaded for the load kind (after the first frame). */
		memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkResetCommandBuffer(run->command, 0);
		vkBeginCommandBuffer(run->command, &begin);
		memset(&value, 0, sizeof(value));
		memcpy(value.color.float32, background, sizeof(value.color.float32));
		memset(&pass, 0, sizeof(pass));
		pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		pass.renderPass = run->clear_pass;
		if (kind == PERF_LOAD && frame != 0)
			pass.renderPass = run->load_pass;
		pass.framebuffer = run->wide_framebuffer;
		pass.renderArea.extent.width = VKMATRIX_LINEAR_WIDTH;
		pass.renderArea.extent.height = VKMATRIX_LINEAR_HEIGHT;
		pass.clearValueCount = 1;
		pass.pClearValues = &value;
		vkCmdBeginRenderPass(run->command, &pass, VK_SUBPASS_CONTENTS_INLINE);

		/* The full-screen quad of the drawing kinds. */
		if (kind == PERF_FLAT || kind == PERF_TEXTURE) {
			memset(&viewport, 0, sizeof(viewport));
			viewport.width = (float)VKMATRIX_LINEAR_WIDTH;
			viewport.height = (float)VKMATRIX_LINEAR_HEIGHT;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(run->command, 0, 1, &viewport);
			memset(&scissor, 0, sizeof(scissor));
			scissor.extent.width = VKMATRIX_LINEAR_WIDTH;
			scissor.extent.height = VKMATRIX_LINEAR_HEIGHT;
			vkCmdSetScissor(run->command, 0, 1, &scissor);
			vkmatrix_bind(run, pipeline);
			if (kind == PERF_TEXTURE) {
				vkCmdBindDescriptorSets(run->command, VK_PIPELINE_BIND_POINT_GRAPHICS, run->layouts[pipeline], 0, 1, &run->textures[1].set, 0, NULL);
			} else {
				red = 1.0f;
				vkCmdPushConstants(run->command, run->layouts[pipeline], VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &red);
			}
			vkCmdDraw(run->command, 6, 1, first, 0);
		}
		vkCmdEndRenderPass(run->command);
		vkEndCommandBuffer(run->command);

		/* The submit and its wait, timed. */
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &run->command;
		vkResetFences(run->device, 1, &run->fence);
		started = perf_now_ns();
		run->failed_call = "vkQueueSubmit (perf)";
		status = vkQueueSubmit(run->queue, 1, &submit, run->fence);
		if (status != VK_SUCCESS)
			return status;
		run->failed_call = "vkWaitForFences (perf)";
		status = vkWaitForFences(run->device, 1, &run->fence, VK_TRUE, VKMATRIX_WAIT_NS);
		if (status != VK_SUCCESS)
			return status;
		spent = perf_now_ns() - started;

		/* Counted after the first frame. */
		if (frame != 0) {
			total += spent;
			if (spent < least)
				least = spent;
		}
	}

	/* The line, and nothing for the shared target's comparison to say. */
	printf("vkmatrix: perf %s: %.2f ms a frame (min %.2f) over %u frames of 1920x1080\n", names[kind],
	       (double)total / PERF_FRAMES / 1000000.0, (double)least / 1000000.0, (unsigned)PERF_FRAMES);
	(void)fflush(stdout);
	memcpy(run->expected, run->pixels, sizeof(run->expected));
	run->tolerance = 0;

	/* Succeeded: the timing is printed. */
	return VK_SUCCESS;
}

/* The monotonic clock in nanoseconds. */
static uint64_t
perf_now_ns(
	void)
{
	struct timespec now;
	int error;

	/* The clock; zero if it cannot be read. */
	error = clock_gettime(CLOCK_MONOTONIC, &now);
	if (error != 0)
		return 0;

	/* Succeeded: the time. */
	return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

/* perf-clear: a 1080p clear pass. */
static VkResult
case_perf_clear(
	struct vkmatrix *run)
{
	VkResult status;

	/* Times the kind. */
	status = perf_case(run, PERF_CLEAR);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* perf-load: a 1080p load pass. */
static VkResult
case_perf_load(
	struct vkmatrix *run)
{
	VkResult status;

	/* Times the kind. */
	status = perf_case(run, PERF_LOAD);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* perf-flat: a 1080p clear and one full-screen flat quad. */
static VkResult
case_perf_flat(
	struct vkmatrix *run)
{
	VkResult status;

	/* Times the kind. */
	status = perf_case(run, PERF_FLAT);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}

/* perf-texture: a 1080p clear and one full-screen textured quad. */
static VkResult
case_perf_texture(
	struct vkmatrix *run)
{
	VkResult status;

	/* Times the kind. */
	status = perf_case(run, PERF_TEXTURE);
	if (status != VK_SUCCESS)
		return status;

	/* Succeeded: the case ran. */
	return VK_SUCCESS;
}
