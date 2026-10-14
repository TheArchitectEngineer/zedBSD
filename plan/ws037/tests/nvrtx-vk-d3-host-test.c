/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the nvrtx Vulkan executor's objects (ws037-p007 stage D3).
 * The commands are written as libvulkan writes them: its wire writer, its
 * command prefixes (resources-generated.inc, memory.c) and its own codec's
 * structure encoders.  The node's side is faked: the blobs, the storage it
 * attaches, the executor's GPU memory and the runs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/nvrtx/nvrtx-vk-objects.h"
#include "userland/desktop/libvulkan/internal.h"
#include "userland/desktop/libvulkan/codec.h"

/* The reply blob's id, where its replies start, and the device's identity. */
#define TEST_REPLY_BLOB		1U
#define TEST_REPLY_AT		16U
#define TEST_DEVICE		0x102U

static unsigned failures;
static unsigned checks;
static uint32_t reply_words[2048];
static unsigned allocations;
static unsigned detached;
static void *detached_backing;
static uint8_t storage[0x20000];
static int backing_token;
static int import_token;

static void check(int ok, const char *what);
static void frame_begin(struct vulkan_writer *writer);
static int frame_run(struct nvrtx_vk_session *session, struct vulkan_writer *writer);
static uint32_t reply_u32(unsigned *at);
static uint64_t reply_u64(unsigned *at);
static void command_begin(struct vulkan_writer *writer, uint32_t opcode);
static void write_tail(struct vulkan_writer *writer, uint64_t identity);
static void write_allocate(struct vulkan_writer *writer, uint64_t identity, uint64_t bytes, uint32_t imported);
static void write_image(struct vulkan_writer *writer, uint64_t identity, uint32_t format, uint32_t width, uint32_t height, uint32_t levels);
static void write_buffer(struct vulkan_writer *writer, uint64_t identity, uint64_t bytes);
static void write_pair(struct vulkan_writer *writer, uint32_t opcode, uint64_t first, uint64_t second);
static void write_bind(struct vulkan_writer *writer, uint32_t opcode, uint64_t resource, uint64_t memory, uint64_t offset);
static void test_memory(void);
static void test_views(void);

void *
kern_malloc(size_t bytes)
{
	allocations++;
	return malloc(bytes);
}

void *
kern_calloc(size_t count, size_t bytes)
{
	allocations++;
	return calloc(count, bytes);
}

void
kern_free(void *pointer)
{
	if (pointer != NULL)
		allocations--;
	free(pointer);
}

void
kern_logf(const char *format, ...)
{
	(void)format;
}

/* libvulkan's handles are its objects' addresses (objects.c). */
uint64_t
vulkan_object_wire_id(const struct vulkan_object *object)
{
	if (object == NULL)
		return 0;
	return object->wire_id;
}

struct vulkan_object *
vulkan_nondispatchable_object(uint64_t handle)
{
	return (struct vulkan_object *)(uintptr_t)handle;
}

/* The one blob: the reply array. */
int
nvrtx_vk_blob(void *owner, uint32_t resource_id, uint8_t **cpu, uint64_t *bytes)
{
	(void)owner;
	if (resource_id != TEST_REPLY_BLOB)
		return ENOENT;
	*cpu = (uint8_t *)reply_words;
	*bytes = sizeof(reply_words);
	return 0;
}

uint32_t
nvrtx_vk_device_id(void *owner)
{
	(void)owner;
	return 0x1f02U;
}

uint64_t
nvrtx_vk_heap_bytes(void *owner)
{
	(void)owner;
	return 240ULL << 20;
}

void
nvrtx_vk_memory_detach(void *owner, void *backing, uint64_t va, uint64_t bytes)
{
	(void)owner;
	(void)va;
	(void)bytes;
	detached++;
	detached_backing = backing;
}

/* Resource 7 is a blob of 0x10000 bytes the session can import. */
int
nvrtx_vk_memory_import(void *owner, uint32_t resource_id, uint64_t bytes, uint8_t **cpu, uint64_t *va, void **backing)
{
	(void)owner;
	if (resource_id != 7U)
		return ENOENT;
	if (bytes > 0x10000U)
		return EINVAL;
	*cpu = storage + 0x10000U;
	*va = 0x31000000ULL;
	*backing = &import_token;
	return 0;
}

int
nvrtx_vk_gpu_alloc(void *owner, uint64_t bytes, void **token, uint8_t **cpu, uint64_t *va)
{
	(void)owner;
	*cpu = calloc(1U, (size_t)bytes);
	if (*cpu == NULL)
		return ENOMEM;
	*token = *cpu;
	*va = 0x38000000ULL;
	return 0;
}

void
nvrtx_vk_gpu_free(void *owner, void *token)
{
	(void)owner;
	free(token);
}

int
nvrtx_vk_gpu_run(void *owner, uint64_t va, uint32_t bytes)
{
	(void)owner;
	(void)va;
	(void)bytes;
	return 0;
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	test_memory();
	test_views();
	printf("%s nvrtx vk d3: %u checks, %u failures\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
	return failures == 0 ? 0 : 1;
}

static void
check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* Starts a frame: the reply stream is the reply blob from TEST_REPLY_AT. */
static void
frame_begin(struct vulkan_writer *writer)
{
	vulkan_writer_init(writer);
	memset(reply_words, 0, sizeof(reply_words));
	vulkan_write_u32(writer, GPU_OP_SET_REPLY_STREAM);
	vulkan_write_u32(writer, 0);
	vulkan_write_u64(writer, 1);
	vulkan_write_u32(writer, TEST_REPLY_BLOB);
	vulkan_write_u64(writer, TEST_REPLY_AT);
	vulkan_write_u64(writer, sizeof(reply_words) - TEST_REPLY_AT);
}

/* Runs the frame through the transport and the real dispatcher. */
static int
frame_run(struct nvrtx_vk_session *session, struct vulkan_writer *writer)
{
	int error;

	error = nvrtx_vk_stream_execute(session, writer->data, (uint32_t)writer->bytes, nvrtx_vk_dispatch);
	vulkan_writer_finish(writer);
	return error;
}

static uint32_t
reply_u32(unsigned *at)
{
	uint32_t word;

	word = reply_words[*at];
	(*at)++;
	return word;
}

static uint64_t
reply_u64(unsigned *at)
{
	uint64_t low;
	uint64_t high;

	low = reply_u32(at);
	high = reply_u32(at);
	return low | (high << 32);
}

/* A command's header as vulkan_command_begin() writes it, appended to the frame (that one starts the writer over). */
static void
command_begin(struct vulkan_writer *writer, uint32_t opcode)
{
	vulkan_write_u32(writer, opcode);
	vulkan_write_u32(writer, GPU_OP_FLAG_REPLY);
}

/* The create's tail (vulkan_object_create_complete): no allocator, the output present, the identity. */
static void
write_tail(struct vulkan_writer *writer, uint64_t identity)
{
	vulkan_write_u64(writer, 0);
	vulkan_write_u64(writer, 1);
	vulkan_write_u64(writer, identity);
}

/* vkAllocateMemory as memory.c writes it, with an import when imported is nonzero. */
static void
write_allocate(struct vulkan_writer *writer, uint64_t identity, uint64_t bytes, uint32_t imported)
{
	command_begin(writer, GPU_OP_ALLOCATE_MEMORY);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, 1);
	vulkan_write_u32(writer, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
	if (imported != 0U) {
		vulkan_write_u64(writer, 1);
		vulkan_write_u32(writer, GPU_OP_RECORD_IMPORT_RESOURCE);
		vulkan_write_u64(writer, 0);
		vulkan_write_u32(writer, imported);
	} else {
		vulkan_write_u64(writer, 0);
	}
	vulkan_write_u64(writer, bytes);
	vulkan_write_u32(writer, 0);
	write_tail(writer, identity);
}

/* vkCreateImage as resources-generated.inc writes it. */
static void
write_image(struct vulkan_writer *writer, uint64_t identity, uint32_t format, uint32_t width, uint32_t height, uint32_t levels)
{
	VkImageCreateInfo info;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = (VkFormat)format;
	info.extent.width = width;
	info.extent.height = height;
	info.extent.depth = 1;
	info.mipLevels = levels;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	command_begin(writer, GPU_OP_CREATE_IMAGE);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, 1);
	vulkan_encode_VkImageCreateInfo(writer, &info);
	write_tail(writer, identity);
}

/* vkCreateBuffer as resources-generated.inc writes it. */
static void
write_buffer(struct vulkan_writer *writer, uint64_t identity, uint64_t bytes)
{
	VkBufferCreateInfo info;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = bytes;
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	command_begin(writer, GPU_OP_CREATE_BUFFER);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, 1);
	vulkan_encode_VkBufferCreateInfo(writer, &info);
	write_tail(writer, identity);
}

/* A command of [device][first][second]: a destroy (second the allocator) or a requirements query (second the output). */
static void
write_pair(struct vulkan_writer *writer, uint32_t opcode, uint64_t first, uint64_t second)
{
	command_begin(writer, opcode);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, first);
	vulkan_write_u64(writer, second);
}

/* vkBindBufferMemory and vkBindImageMemory: [device][resource][memory][offset]. */
static void
write_bind(struct vulkan_writer *writer, uint32_t opcode, uint64_t resource, uint64_t memory, uint64_t offset)
{
	command_begin(writer, opcode);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, resource);
	vulkan_write_u64(writer, memory);
	vulkan_write_u64(writer, offset);
}

/*
 * Memory, its attached storage, images and buffers bound to it, their
 * requirements and layout, and the storage going back once the last
 * holder lets go.
 */
static void
test_memory(void)
{
	struct nvrtx_vk_session *session;
	struct vulkan_writer client;
	struct nvrtx_vk_image *image;
	struct nvrtx_vk_buffer *buffer;
	uint64_t va;
	uint8_t *cpu;
	unsigned before;
	unsigned at;
	int error;

	before = allocations;
	check(nvrtx_vk_session_open(NULL, &session) == 0, "memory: session opened");

	/* An allocation, an image of 100x10 (rows of 512 bytes), its requirements, a buffer of 1000 bytes and its requirements. */
	frame_begin(&client);
	write_allocate(&client, 0x200, 0x10000, 0);
	write_image(&client, 0x201, VK_FORMAT_B8G8R8A8_UNORM, 100, 10, 1);
	write_pair(&client, GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS, 0x201, 1);
	write_buffer(&client, 0x202, 1000);
	write_pair(&client, GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS, 0x202, 1);
	write_image(&client, 0x203, VK_FORMAT_B8G8R8A8_UNORM, 100, 10, 2);
	write_image(&client, 0x204, VK_FORMAT_R16G16B16A16_SFLOAT, 100, 10, 1);
	error = frame_run(session, &client);
	check(error == 0, "memory: the creates ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_ALLOCATE_MEMORY && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x200,
	    "memory: allocation reply");
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x201,
	    "memory: image reply");
	check(reply_u32(&at) == GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS && reply_u64(&at) == 1 && reply_u64(&at) == 5120 &&
	    reply_u64(&at) == 256 && reply_u32(&at) == 1, "memory: image requirements (rows of 512, 256 aligned)");
	check(reply_u32(&at) == GPU_OP_CREATE_BUFFER && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x202,
	    "memory: buffer reply");
	check(reply_u32(&at) == GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS && reply_u64(&at) == 1 && reply_u64(&at) == 1024 &&
	    reply_u64(&at) == 256 && reply_u32(&at) == 1, "memory: buffer requirements (rounded to 256)");
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE && reply_u32(&at) == (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT,
	    "memory: an image of two levels is refused by name");
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE && reply_u32(&at) == (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT,
	    "memory: an image of a float format is refused by name");
	image = nvrtx_vk_payload(session, I915_VK_OBJ_IMAGE, 0x201);
	check(image != NULL && image->pitch == 512 && image->bytes == 5120, "memory: the image's layout");

	/* The node attaches the blob's storage once, of the allocation's size. */
	check(nvrtx_vk_memory_attach(session, 0x299, 0x10000, storage, 0x30000000ULL, &backing_token) == EINVAL,
	    "memory: no attach to an unknown allocation");
	check(nvrtx_vk_memory_attach(session, 0x200, 0x8000, storage, 0x30000000ULL, &backing_token) == EINVAL,
	    "memory: no attach of another size");
	check(nvrtx_vk_memory_attach(session, 0x200, 0x10000, storage, 0x30000000ULL, &backing_token) == 0,
	    "memory: the blob's storage attached");
	check(nvrtx_vk_memory_attach(session, 0x200, 0x10000, storage, 0x30000000ULL, &backing_token) == EINVAL,
	    "memory: attached once");

	/* Binds: the image at 0, the buffer at 5120; again, off the boundary, past the end, and to unknown memory fail. */
	frame_begin(&client);
	write_bind(&client, GPU_OP_BIND_IMAGE_MEMORY, 0x201, 0x200, 0);
	write_bind(&client, GPU_OP_BIND_IMAGE_MEMORY, 0x201, 0x200, 0);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x202, 0x200, 100);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x202, 0x200, 0x10000 - 512);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x202, 0x299, 0);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x202, 0x200, 5120);
	command_begin(&client, GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 0x201);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, VK_IMAGE_ASPECT_COLOR_BIT);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 1);
	command_begin(&client, GPU_OP_GET_DEVICE_MEMORY_COMMITMENT);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 0x200);
	vulkan_write_u64(&client, 1);
	error = frame_run(session, &client);
	check(error == 0, "memory: the binds ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_BIND_IMAGE_MEMORY && reply_u32(&at) == VK_SUCCESS, "memory: image bound");
	check(reply_u32(&at) == GPU_OP_BIND_IMAGE_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: an image is bound once");
	check(reply_u32(&at) == GPU_OP_BIND_BUFFER_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: off the boundary");
	check(reply_u32(&at) == GPU_OP_BIND_BUFFER_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: past the end");
	check(reply_u32(&at) == GPU_OP_BIND_BUFFER_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: unknown memory");
	check(reply_u32(&at) == GPU_OP_BIND_BUFFER_MEMORY && reply_u32(&at) == VK_SUCCESS, "memory: buffer bound");
	check(reply_u32(&at) == GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT && reply_u64(&at) == 1 && reply_u64(&at) == 0 &&
	    reply_u64(&at) == 5120 && reply_u64(&at) == 512, "memory: the image's layout (offset, size, row pitch)");
	at += 4U;
	check(reply_u32(&at) == GPU_OP_GET_DEVICE_MEMORY_COMMITMENT && reply_u64(&at) == 1 && reply_u64(&at) == 0x10000,
	    "memory: committed once attached");
	buffer = nvrtx_vk_payload(session, I915_VK_OBJ_BUFFER, 0x202);
	check(buffer != NULL && nvrtx_vk_buffer_address(buffer, &va, &cpu) == 0 && va == 0x30000000ULL + 5120 && cpu == storage + 5120,
	    "memory: the buffer's addresses");
	check(image != NULL && nvrtx_vk_image_address(image, &va, &cpu) == 0 && va == 0x30000000ULL && cpu == storage,
	    "memory: the image's addresses");

	/* The allocation freed first lives on while the image and the buffer hold it. */
	frame_begin(&client);
	write_pair(&client, GPU_OP_FREE_MEMORY, 0x200, 0);
	write_pair(&client, GPU_OP_DESTROY_IMAGE, 0x201, 0);
	error = frame_run(session, &client);
	check(error == 0 && detached == 0, "memory: freed memory still held by the buffer");
	frame_begin(&client);
	write_pair(&client, GPU_OP_DESTROY_BUFFER, 0x202, 0);
	error = frame_run(session, &client);
	check(error == 0 && detached == 1 && detached_backing == &backing_token, "memory: the storage goes back with the last holder");

	/* An import takes the blob's storage at once; an unknown resource is refused. */
	frame_begin(&client);
	write_allocate(&client, 0x210, 0x10000, 7);
	write_allocate(&client, 0x211, 0x10000, 9);
	error = frame_run(session, &client);
	check(error == 0, "memory: the imports ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_ALLOCATE_MEMORY && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x210,
	    "memory: import accepted");
	check(reply_u32(&at) == GPU_OP_ALLOCATE_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: import of an unknown resource refused");
	check(nvrtx_vk_memory_attach(session, 0x210, 0x10000, storage, 0x30000000ULL, &backing_token) == EINVAL,
	    "memory: an import has its storage already");

	/* Closing the session gives the import's storage back and frees everything. */
	check(nvrtx_vk_session_close(&session) == 0 && detached == 2 && detached_backing == &import_token, "memory: session closed");
	check(allocations == before, "memory: nothing leaked");
}

/* Views of images and samplers. */
static void
test_views(void)
{
	struct nvrtx_vk_session *session;
	struct vulkan_writer client;
	struct vulkan_object image;
	struct vulkan_object unknown;
	struct nvrtx_vk_view *view;
	VkImageViewCreateInfo info;
	VkSamplerCreateInfo sampler;
	unsigned before;
	unsigned at;
	int error;

	before = allocations;
	check(nvrtx_vk_session_open(NULL, &session) == 0, "views: session opened");
	memset(&image, 0, sizeof(image));
	image.wire_id = 0x301;
	memset(&unknown, 0, sizeof(unknown));
	unknown.wire_id = 0x399;

	/* An image, a view of it (blue and red swapped), a view of an unknown image, a sampler. */
	frame_begin(&client);
	write_image(&client, 0x301, VK_FORMAT_R8G8B8A8_UNORM, 64, 64, 1);
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = (VkImage)(uintptr_t)&image;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.components.r = VK_COMPONENT_SWIZZLE_B;
	info.components.b = VK_COMPONENT_SWIZZLE_R;
	info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.layerCount = 1;
	command_begin(&client, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkImageViewCreateInfo(&client, &info);
	write_tail(&client, 0x302);
	info.image = (VkImage)(uintptr_t)&unknown;
	command_begin(&client, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkImageViewCreateInfo(&client, &info);
	write_tail(&client, 0x303);
	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	command_begin(&client, GPU_OP_CREATE_SAMPLER);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkSamplerCreateInfo(&client, &sampler);
	write_tail(&client, 0x304);
	error = frame_run(session, &client);
	check(error == 0, "views: the creates ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE && reply_u32(&at) == VK_SUCCESS, "views: image");
	at += 4U;
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE_VIEW && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x302,
	    "views: view");
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE_VIEW && reply_u32(&at) != VK_SUCCESS, "views: view of an unknown image refused");
	check(reply_u32(&at) == GPU_OP_CREATE_SAMPLER && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x304,
	    "views: sampler");
	view = nvrtx_vk_payload(session, I915_VK_OBJ_IMAGE_VIEW, 0x302);
	check(view != NULL && view->swizzle[0] == VK_COMPONENT_SWIZZLE_B && view->swizzle[1] == VK_COMPONENT_SWIZZLE_G &&
	    view->swizzle[2] == VK_COMPONENT_SWIZZLE_R && view->swizzle[3] == VK_COMPONENT_SWIZZLE_A, "views: the swizzle, identity resolved");

	/* The image destroyed first lives on in its view. */
	frame_begin(&client);
	write_pair(&client, GPU_OP_DESTROY_IMAGE, 0x301, 0);
	error = frame_run(session, &client);
	check(error == 0 && view->image != NULL && ((struct nvrtx_vk_image *)view->image->payload)->width == 64, "views: the view holds its image");
	frame_begin(&client);
	write_pair(&client, GPU_OP_DESTROY_IMAGE_VIEW, 0x302, 0);
	write_pair(&client, GPU_OP_DESTROY_SAMPLER, 0x304, 0);
	write_pair(&client, GPU_OP_DESTROY_SAMPLER, 0x399, 0);
	error = frame_run(session, &client);
	check(error == 0, "views: destroyed (an unknown identity too)");

	check(nvrtx_vk_session_close(&session) == 0, "views: session closed");
	check(allocations == before, "views: nothing leaked");
}
