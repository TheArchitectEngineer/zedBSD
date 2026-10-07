/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws075-p007b increment b3: the host test of layered rendering in the i915
 * Vulkan executor.
 *
 * The executor is linked as the kernel builds it, with the services of
 * plan/ws031/tests/i915-vk-render-stubs.inc.  Checked: the framebuffer keeps
 * VkFramebufferCreateInfo.layers; a render pass begin clears every layer of
 * a layered attachment and vkCmdClearAttachments every layer of its
 * rectangle; a layered colour target's surface state has the render target
 * view extent and the surface array; a layered depth buffer's Depth and view
 * extent are the view's layers less one.
 */

#include "../../ws031/tests/i915-vk-render-stubs.inc"

#include "../../../src/drivers/gpu/i915/render/batch.h"
#include "../../../src/drivers/gpu/i915/render/heap.h"
#include "../../../src/drivers/gpu/i915/render/state.h"

#include "../../../src/drivers/gpu/i915/intel/genxml.h"

#define FIXTURE_QUEUE_SUBMIT			18U
#define FIXTURE_ALLOCATE_MEMORY			21U
#define FIXTURE_CREATE_FRAMEBUFFER		80U
#define FIXTURE_CREATE_COMMAND_POOL		85U
#define FIXTURE_ALLOCATE_COMMAND_BUFFERS	88U
#define FIXTURE_BEGIN_COMMAND_BUFFER		90U
#define FIXTURE_END_COMMAND_BUFFER		91U
#define FIXTURE_CMD_CLEAR_ATTACHMENTS		121U
#define FIXTURE_CMD_BEGIN_RENDER_PASS		133U
#define FIXTURE_CMD_END_RENDER_PASS		135U

#define FIXTURE_DEVICE		0xd0ULL
#define FIXTURE_QUEUE		0xd1ULL
#define FIXTURE_MEMORY		0x100ULL
#define FIXTURE_IMAGE		0x300ULL
#define FIXTURE_DEPTH		0x301ULL
#define FIXTURE_VIEW		0x400ULL
#define FIXTURE_DEPTH_VIEW	0x401ULL
#define FIXTURE_PASS		0x500ULL
#define FIXTURE_FRAMEBUFFER	0x600ULL
#define FIXTURE_FLAT		0x601ULL
#define FIXTURE_POOL		0x900ULL
#define FIXTURE_CMDBUF		0xa00ULL

#define FIXTURE_STORAGE_BYTES	262144U
#define FIXTURE_STORAGE_VA	0x200000000ULL
#define FIXTURE_IMAGE_OFFSET	0x1000U
#define FIXTURE_DEPTH_OFFSET	0x10000U

/* The memory's storage and the blob that is it. */
static uint8_t fixture_storage[FIXTURE_STORAGE_BYTES] __attribute__((aligned(4096)));
static struct i915_gem_object fixture_storage_object;
static struct stub_wire fixture_wire;

/* The pass the begins and the state name: one colour attachment that loads with a clear. */
static struct i915_gfx_pass fixture_pass;

static void fixture_objects(void);
static void fixture_framebuffer(uint64_t identity, uint32_t layers);
static void test_framebuffer(void);
static void test_clears(void);
static void test_target_state(void);
static void test_depth_state(void);

/* Runs the scenarios. */
int
main(void)
{
	unsigned long mark;

	/* A session with the storage blob and the objects. */
	mark = stub_allocation_mark();
	memset(&fixture_storage_object, 0, sizeof(fixture_storage_object));
	fixture_storage_object.slot = 7U;
	fixture_storage_object.bytes = sizeof(fixture_storage);
	fixture_storage_object.run.paddr = (hal_physaddr_t)(uintptr_t)fixture_storage;
	fixture_storage_object.va = FIXTURE_STORAGE_VA;
	stub_session_open(&fixture_storage_object);
	fixture_objects();

	/* The scenarios. */
	test_framebuffer();
	test_clears();
	test_target_state();
	test_depth_state();

	/* Nothing is left after the close. */
	drv_i915_object_remove(stub_session, I915_VK_OBJ_RENDER_PASS, FIXTURE_PASS);
	stub_session_close();
	assert(stub_live_since(mark) == 0U);
	printf("ws075 p007b b3 layered host test PASS\n");
	return 0;
}

/* Makes the memory, a two-layer colour image and a two-layer depth image with views of both layers, and the pass. */
static void
fixture_objects(void)
{
	struct i915_gfx_image *image;
	struct i915_gfx_view *view;
	size_t reply_bytes;
	unsigned index;
	int error;

	/* vkAllocateMemory of the storage, whose blob is the fixture's storage. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_ALLOCATE_MEMORY);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 5U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_STORAGE_BYTES);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_MEMORY);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 24U);
	error = drv_i915_render_blob_attach(stub_vk, &stub_gpu, FIXTURE_MEMORY, &fixture_storage_object);
	assert(error == 0);

	/* The colour image: 16x16 R8G8B8A8 in two layers of 16 rows, and the D32 image likewise. */
	for (index = 0U; index < 2U; index++) {
		image = kern_calloc(1U, sizeof(*image));
		assert(image != NULL);
		image->format = VK_FORMAT_R8G8B8A8_UNORM;
		if (index == 1U)
			image->format = VK_FORMAT_D32_SFLOAT;
		image->width = 16U;
		image->height = 16U;
		image->pitch = 64U;
		image->levels = 1U;
		image->type = VK_IMAGE_TYPE_2D;
		image->layers = 2U;
		image->slice_rows = 16U;
		image->bytes = 64U * 16U * 2U;
		image->samples = 1U;
		image->sample_width = 16U;
		image->sample_height = 16U;
		image->memory = drv_i915_object_lookup(stub_session, I915_VK_OBJ_MEMORY, FIXTURE_MEMORY);
		image->offset = FIXTURE_IMAGE_OFFSET;
		if (index == 1U)
			image->offset = FIXTURE_DEPTH_OFFSET;
		error = drv_i915_object_insert(stub_session, I915_VK_OBJ_IMAGE, index == 0U ? FIXTURE_IMAGE : FIXTURE_DEPTH, image);
		assert(error == 0);

		/* The view of both layers. */
		view = kern_calloc(1U, sizeof(*view));
		assert(view != NULL);
		view->image = image;
		view->format = image->format;
		view->level_count = 1U;
		view->view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		view->layer_count = 2U;
		error = drv_i915_object_insert(stub_session, I915_VK_OBJ_IMAGE_VIEW, index == 0U ? FIXTURE_VIEW : FIXTURE_DEPTH_VIEW, view);
		assert(error == 0);
	}

	/* The pass: attachment 0 is the colour target, which loads with a clear. */
	memset(&fixture_pass, 0, sizeof(fixture_pass));
	fixture_pass.attachment_count = 1U;
	fixture_pass.attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	fixture_pass.attachments[0].load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	fixture_pass.color_attachment = 0U;
	fixture_pass.depth_attachment = VK_ATTACHMENT_UNUSED;
	fixture_pass.color_count = 1U;
	fixture_pass.color_attachments[0] = 0U;
	error = drv_i915_object_insert(stub_session, I915_VK_OBJ_RENDER_PASS, FIXTURE_PASS, &fixture_pass);
	assert(error == 0);

	/* A command pool and one primary command buffer. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_CREATE_COMMAND_POOL);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 39U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_POOL);
	stub_put32(&fixture_wire, FIXTURE_ALLOCATE_COMMAND_BUFFERS);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 40U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_POOL);
	stub_put32(&fixture_wire, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_CMDBUF);
	(void)stub_execute_ok(&fixture_wire);
}

/* vkCreateFramebuffer of the colour view, 16x16, with the layers given, as libvulkan's codec encodes it. */
static void
fixture_framebuffer(
	uint64_t identity,
	uint32_t layers)
{
	size_t reply_bytes;

	/* [80][reply][device][present][sType 37][no chain][flags][pass][count][1]{view}[16][16][layers][allocator][present][identity]. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_CREATE_FRAMEBUFFER);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 37U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_PASS);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_VIEW);
	stub_put32(&fixture_wire, 16U);
	stub_put32(&fixture_wire, 16U);
	stub_put32(&fixture_wire, layers);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, identity);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 24U);
	assert(stub_get32(stub_reply, 4U) == VK_SUCCESS);
}

/* The framebuffer keeps its layers; zero reads as one. */
static void
test_framebuffer(void)
{
	const struct i915_gfx_framebuffer *framebuffer;

	/* Two layers. */
	fixture_framebuffer(FIXTURE_FRAMEBUFFER, 2U);
	framebuffer = drv_i915_object_lookup(stub_session, I915_VK_OBJ_FRAMEBUFFER, FIXTURE_FRAMEBUFFER);
	assert(framebuffer != NULL);
	assert(framebuffer->layers == 2U);
	assert(framebuffer->views[0] == drv_i915_object_lookup(stub_session, I915_VK_OBJ_IMAGE_VIEW, FIXTURE_VIEW));

	/* Zero, which the API forbids, is kept as one. */
	fixture_framebuffer(FIXTURE_FLAT, 0U);
	framebuffer = drv_i915_object_lookup(stub_session, I915_VK_OBJ_FRAMEBUFFER, FIXTURE_FLAT);
	assert(framebuffer != NULL && framebuffer->layers == 1U);
}

/*
 * A begin on the two-layer framebuffer clears both layers, and a
 * vkCmdClearAttachments rectangle of layers 0 and 1 fills both; on the
 * one-layer framebuffer the begin clears layer 0 only.
 */
static void
test_clears(void)
{
	size_t reply_bytes;
	unsigned before;
	unsigned word;
	unsigned pass;
	uint64_t framebuffer;

	/* Each framebuffer in turn. */
	for (pass = 0U; pass < 2U; pass++) {
		framebuffer = FIXTURE_FRAMEBUFFER;
		if (pass == 1U)
			framebuffer = FIXTURE_FLAT;

		/* Records begin, a pass begin with one clear value, a clear of layers 0 and 1, the pass end and end. */
		stub_wire_begin(&fixture_wire);
		stub_put32(&fixture_wire, FIXTURE_BEGIN_COMMAND_BUFFER);
		stub_put32(&fixture_wire, 1U);
		stub_put64(&fixture_wire, FIXTURE_CMDBUF);
		stub_put64(&fixture_wire, 1U);
		stub_put32(&fixture_wire, 42U);
		stub_put64(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 0U);
		stub_put64(&fixture_wire, 0U);
		stub_put32(&fixture_wire, FIXTURE_CMD_BEGIN_RENDER_PASS);
		stub_put32(&fixture_wire, 0U);
		stub_put64(&fixture_wire, FIXTURE_CMDBUF);
		stub_put64(&fixture_wire, 1U);
		stub_put32(&fixture_wire, 43U);
		stub_put64(&fixture_wire, 0U);
		stub_put64(&fixture_wire, FIXTURE_PASS);
		stub_put64(&fixture_wire, framebuffer);
		stub_put32(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 16U);
		stub_put32(&fixture_wire, 16U);
		stub_put32(&fixture_wire, 1U);
		stub_put64(&fixture_wire, 1U);
		stub_put32(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 0U);
		stub_put64(&fixture_wire, 4U);
		for (word = 0U; word < 4U; word++)
			stub_put32(&fixture_wire, 0x3f800000U);
		stub_put32(&fixture_wire, 0U);
		stub_put32(&fixture_wire, FIXTURE_CMD_CLEAR_ATTACHMENTS);
		stub_put32(&fixture_wire, 0U);
		stub_put64(&fixture_wire, FIXTURE_CMDBUF);
		stub_put32(&fixture_wire, 1U);
		stub_put64(&fixture_wire, 1U);
		stub_put32(&fixture_wire, VK_IMAGE_ASPECT_COLOR_BIT);
		stub_put32(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 0U);
		stub_put64(&fixture_wire, 4U);
		for (word = 0U; word < 4U; word++)
			stub_put32(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 1U);
		stub_put64(&fixture_wire, 1U);
		stub_put32(&fixture_wire, 2U);
		stub_put32(&fixture_wire, 3U);
		stub_put32(&fixture_wire, 4U);
		stub_put32(&fixture_wire, 5U);
		stub_put32(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 2U);
		stub_put32(&fixture_wire, FIXTURE_CMD_END_RENDER_PASS);
		stub_put32(&fixture_wire, 0U);
		stub_put64(&fixture_wire, FIXTURE_CMDBUF);
		stub_put32(&fixture_wire, FIXTURE_END_COMMAND_BUFFER);
		stub_put32(&fixture_wire, 1U);
		stub_put64(&fixture_wire, FIXTURE_CMDBUF);
		reply_bytes = stub_execute_ok(&fixture_wire);
		assert(reply_bytes == 16U);
		assert(stub_get32(stub_reply, 12U) == VK_SUCCESS);

		/* vkQueueSubmit of the command buffer. */
		before = stub_rect_calls;
		stub_wire_begin(&fixture_wire);
		stub_put32(&fixture_wire, FIXTURE_QUEUE_SUBMIT);
		stub_put32(&fixture_wire, 1U);
		stub_put64(&fixture_wire, FIXTURE_QUEUE);
		stub_put32(&fixture_wire, 1U);
		stub_put64(&fixture_wire, 1U);
		stub_put32(&fixture_wire, 4U);
		stub_put64(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 0U);
		stub_put64(&fixture_wire, 0U);
		stub_put64(&fixture_wire, 0U);
		stub_put32(&fixture_wire, 1U);
		stub_put64(&fixture_wire, 1U);
		stub_put64(&fixture_wire, FIXTURE_CMDBUF);
		stub_put32(&fixture_wire, 0U);
		stub_put64(&fixture_wire, 0U);
		stub_put64(&fixture_wire, 0U);
		reply_bytes = stub_execute_ok(&fixture_wire);
		assert(reply_bytes == 8U && stub_get32(stub_reply, 4U) == VK_SUCCESS);

		/* The begin clears the framebuffer's layers; the rectangle clears its two layers. */
		if (pass == 0U) {
			assert(stub_rect_calls == before + 4U);
			assert(stub_rects[before].dst.va == FIXTURE_STORAGE_VA + FIXTURE_IMAGE_OFFSET);
			assert(stub_rects[before + 1U].dst.va == FIXTURE_STORAGE_VA + FIXTURE_IMAGE_OFFSET + 64U * 16U);
			assert(stub_rects[before + 2U].dst.va == FIXTURE_STORAGE_VA + FIXTURE_IMAGE_OFFSET);
			assert(stub_rects[before + 3U].dst.va == FIXTURE_STORAGE_VA + FIXTURE_IMAGE_OFFSET + 64U * 16U);
			assert(stub_rects[before + 2U].dst_rect.x == 2 && stub_rects[before + 2U].dst_rect.w == 4U);
			assert(stub_rects[before].dst_rect.w == 16U && stub_rects[before].dst_rect.h == 16U);
		} else {
			assert(stub_rect_calls == before + 3U);
			assert(stub_rects[before].dst.va == FIXTURE_STORAGE_VA + FIXTURE_IMAGE_OFFSET);
		}
	}
}

/* A draw into the two-layer colour view: the target's surface state is an array with view extent 1. */
static void
test_target_state(void)
{
	static uint8_t page[I915_GFX_SLOT_BYTES];
	static struct i915_gfx_draw_state state;
	static struct i915_gfx_kernels kernels;
	struct i915_gfx_pipeline pipeline;
	const uint32_t *rss;
	struct i915_gfx_image *image;
	int error;

	/* The pipeline's 16x16 viewport, no texture, drawing through the pass and framebuffer. */
	memset(&pipeline, 0, sizeof(pipeline));
	pipeline.viewport[2] = 0x41800000U;
	pipeline.viewport[3] = 0x41800000U;
	pipeline.viewport[5] = 0x3f800000U;
	pipeline.scissor.extent.width = 16U;
	pipeline.scissor.extent.height = 16U;
	memset(&state, 0, sizeof(state));
	state.pipeline = &pipeline;
	state.pass = &fixture_pass;
	state.framebuffer = drv_i915_object_lookup(stub_session, I915_VK_OBJ_FRAMEBUFFER, FIXTURE_FRAMEBUFFER);
	memset(&kernels, 0, sizeof(kernels));
	image = drv_i915_object_lookup(stub_session, I915_VK_OBJ_IMAGE, FIXTURE_IMAGE);

	/* Writes the state and reads the render target's surface state. */
	memset(page, 0, sizeof(page));
	error = drv_i915_gfx_write_state(page, &state, &kernels, image, 0x6U);
	assert(error == 0);
	rss = (const uint32_t *)(const void *)(page + I915_GFX_RSS_TARGET);
	assert((rss[0] & GEN12_RSS_SURFACE_ARRAY) != 0U);
	assert(((rss[3] >> GEN12_RSS_DEPTH_SHIFT) & GEN12_RSS_DEPTH_MASK) == 1U);
	assert(((rss[4] >> GEN12_RSS_VIEW_EXTENT_SHIFT) & GEN12_RSS_DEPTH_MASK) == 1U);
	assert(((rss[4] >> GEN12_RSS_MIN_ARRAY_ELEMENT_SHIFT) & GEN12_RSS_DEPTH_MASK) == 0U);
}

/* The two-layer depth view: 3DSTATE_DEPTH_BUFFER's Depth and view extent are 1, its first element 0. */
static void
test_depth_state(void)
{
	static uint32_t commands[1024];
	struct i915_gfx_framebuffer framebuffer;
	struct i915_gfx_pass pass;
	struct i915_gfx_draw_state state;
	struct i915_gfx_pipeline pipeline;
	struct i915_gfx_batch batch;
	struct i915_gfx_image *depth;
	unsigned index;
	int found;
	int error;

	/* A pass whose depth attachment is the two-layer depth view. */
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.width = 16U;
	framebuffer.height = 16U;
	framebuffer.layers = 2U;
	framebuffer.view_count = 1U;
	framebuffer.views[0] = drv_i915_object_lookup(stub_session, I915_VK_OBJ_IMAGE_VIEW, FIXTURE_DEPTH_VIEW);
	memset(&pass, 0, sizeof(pass));
	pass.attachment_count = 1U;
	pass.color_attachment = VK_ATTACHMENT_UNUSED;
	pass.depth_attachment = 0U;
	memset(&pipeline, 0, sizeof(pipeline));
	memset(&state, 0, sizeof(state));
	state.pass = &pass;
	state.framebuffer = &framebuffer;
	state.pipeline = &pipeline;
	depth = drv_i915_object_lookup(stub_session, I915_VK_OBJ_IMAGE, FIXTURE_DEPTH);

	/* Emits the depth state and finds 3DSTATE_DEPTH_BUFFER. */
	memset(&batch, 0, sizeof(batch));
	batch.cmds = commands;
	batch.capacity = 1024U;
	error = drv_i915_gfx_emit_depth(&batch, &state, depth, 0x1000U, 0x6U);
	assert(error == 0 && batch.overflow == 0);
	found = 0;
	for (index = 0U; index + 7U < batch.count; index++) {
		if (commands[index] == GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DEPTH_BUFFER, GEN12_3DSTATE_DEPTH_BUFFER_DWORDS)) {
			/* Dword 5: first element 0, Depth 1; dword 7: view extent 1 above the QPitch. */
			assert(((commands[index + 5U] >> 8) & GEN12_RSS_DEPTH_MASK) == 0U);
			assert(((commands[index + 5U] >> 20) & GEN12_RSS_DEPTH_MASK) == 1U);
			assert(((commands[index + 7U] >> 21) & GEN12_RSS_DEPTH_MASK) == 1U);
			assert((commands[index + 7U] & 0x7fffU) == 16U / 4U);
			found = 1;
			break;
		}
	}
	assert(found);
}
