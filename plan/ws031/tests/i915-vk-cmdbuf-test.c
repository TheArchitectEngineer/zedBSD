/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for command pools and buffers, recording and vkQueueSubmit
 * (render/command.c), and for the commands a draw ends its batch with
 * (render/state.c).
 *
 * A command buffer is recorded as a list of operations and runs at
 * vkQueueSubmit: clears and copies as GPU rectangles, draws as GPU draws.
 * The GPU runs are the stand-ins of i915-vk-render-stubs.inc, which record
 * what they were asked to run, so the fixture checks what the recording
 * handed to the GPU path and in which order.
 */

#include "i915-vk-render-stubs.inc"

#include "../../../src/drivers/gpu/i915/render/batch.h"
#include "../../../src/drivers/gpu/i915/render/state.h"

#include "../../../src/drivers/gpu/i915/data/i915-commands.inc"
#include "../../../src/drivers/gpu/i915/data/i915-3dstate-gen12.inc"

/* The wire opcodes the fixture sends, as libvulkan numbers them. */
#define FIXTURE_QUEUE_SUBMIT			18U
#define FIXTURE_ALLOCATE_MEMORY			21U
#define FIXTURE_FREE_MEMORY			22U
#define FIXTURE_BIND_BUFFER_MEMORY		28U
#define FIXTURE_BIND_IMAGE_MEMORY		29U
#define FIXTURE_CREATE_FENCE			35U
#define FIXTURE_DESTROY_FENCE			36U
#define FIXTURE_RESET_FENCES			37U
#define FIXTURE_GET_FENCE_STATUS		38U
#define FIXTURE_CREATE_BUFFER			50U
#define FIXTURE_DESTROY_BUFFER			51U
#define FIXTURE_CREATE_IMAGE			54U
#define FIXTURE_DESTROY_IMAGE			55U
#define FIXTURE_CREATE_COMMAND_POOL		85U
#define FIXTURE_DESTROY_COMMAND_POOL		86U
#define FIXTURE_RESET_COMMAND_POOL		87U
#define FIXTURE_ALLOCATE_COMMAND_BUFFERS	88U
#define FIXTURE_FREE_COMMAND_BUFFERS		89U
#define FIXTURE_BEGIN_COMMAND_BUFFER		90U
#define FIXTURE_END_COMMAND_BUFFER		91U
#define FIXTURE_CMD_BIND_PIPELINE		93U
#define FIXTURE_CMD_BIND_VERTEX_BUFFERS		105U
#define FIXTURE_CMD_DRAW			106U
#define FIXTURE_CMD_DRAW_INDEXED		107U
#define FIXTURE_CMD_CLEAR_COLOR_IMAGE		119U
#define FIXTURE_CMD_PUSH_CONSTANTS		132U

/* The wire identities the fixture gives its objects. */
#define FIXTURE_DEVICE		0xd0ULL
#define FIXTURE_QUEUE		0xd1ULL
#define FIXTURE_MEMORY		0x100ULL
#define FIXTURE_BUFFER		0x200ULL
#define FIXTURE_IMAGE		0x300ULL
#define FIXTURE_PIPELINE	0xb00ULL
#define FIXTURE_POOL		0x900ULL
#define FIXTURE_CB0		0xa00ULL
#define FIXTURE_CB1		0xa01ULL
#define FIXTURE_FENCE		0xf00ULL

/* The storage blob: its size and the GPU address it is bound at. */
#define FIXTURE_STORAGE_BYTES	65536U
#define FIXTURE_STORAGE_VA	0x200000000ULL

/* How many operations one command buffer records (render/command.c). */
#define FIXTURE_MAX_OPS		64U

/*
 * The storage libvulkan exports for the fixture's allocation, and the
 * session object that stands for that blob.
 */
static uint8_t fixture_storage[FIXTURE_STORAGE_BYTES] __attribute__((aligned(4096)));
static struct i915_gem_object fixture_storage_object;

/*
 * The pipeline the recordings bind.
 *
 * The draw is a stand-in, so the pipeline needs no kernels; it is published
 * under its identity for the whole lifecycle test and withdrawn at its end.
 */
static struct i915_gfx_pipeline fixture_pipeline;

/* The stream every command is built in. */
static struct stub_wire fixture_wire;

static void fixture_command_buffer(uint32_t opcode, uint64_t cmdbuf);
static void fixture_destroy(uint32_t opcode, uint64_t identity);
static void fixture_draw(uint64_t cmdbuf, uint32_t vertices, uint32_t instances);
static void fixture_submit(uint64_t cmdbuf, uint64_t fence);
static uint32_t fixture_fence_status(void);
static void fixture_resources(void);
static int fixture_find_command(const uint32_t *batch, unsigned used, uint32_t opcode);
static void test_emission(void);
static void test_lifecycle(void);
static void test_recording_limits(void);

/*
 * Runs the command buffer checks.
 */
int
main(void)
{
	/* Checks the end of a draw's batch, then recording and submission. */
	test_emission();
	test_lifecycle();
	test_recording_limits();

	/* Succeeded: every check held. */
	printf("i915 vk cmdbuf host test PASS\n");
	return 0;
}

/* Appends a command that names only a command buffer and asks for a reply: vkEndCommandBuffer. */
static void
fixture_command_buffer(
	uint32_t opcode,
	uint64_t cmdbuf)
{
	/* [opcode][reply][command buffer]. */
	stub_put32(&fixture_wire, opcode);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, cmdbuf);
}

/* Appends a generic destroy or free: [opcode][reply][device][identity][pAllocator]. */
static void
fixture_destroy(
	uint32_t opcode,
	uint64_t identity)
{
	/* The command has no reply body; the reply is its echoed opcode. */
	stub_put32(&fixture_wire, opcode);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, identity);
	stub_put64(&fixture_wire, 0U);
}

/* Appends vkCmdDraw with no first vertex or instance; a recording asks for no reply. */
static void
fixture_draw(
	uint64_t cmdbuf,
	uint32_t vertices,
	uint32_t instances)
{
	/* [106][no reply][command buffer][vertices][instances][first vertex][first instance]. */
	stub_put32(&fixture_wire, FIXTURE_CMD_DRAW);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, cmdbuf);
	stub_put32(&fixture_wire, vertices);
	stub_put32(&fixture_wire, instances);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
}

/* Appends vkQueueSubmit of one command buffer with no semaphores, signalling `fence`. */
static void
fixture_submit(
	uint64_t cmdbuf,
	uint64_t fence)
{
	/* The header, the queue and one VkSubmitInfo: sType 4, no chain. */
	stub_put32(&fixture_wire, FIXTURE_QUEUE_SUBMIT);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_QUEUE);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 4U);
	stub_put64(&fixture_wire, 0U);

	/* No wait semaphores and no stages. */
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);

	/* One command buffer. */
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, cmdbuf);

	/* No signal semaphores, then the fence. */
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, fence);
}

/* Asks vkGetFenceStatus of the fixture's fence and reports its VkResult. */
static uint32_t
fixture_fence_status(void)
{
	size_t reply_bytes;
	uint32_t status;

	/* [38][reply][device][fence] -> [38][VkResult]. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_GET_FENCE_STATUS);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, FIXTURE_FENCE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 8U);

	/* Reports the fence's result. */
	status = stub_get32(stub_reply, 4U);
	return status;
}

/* Creates the memory with its storage, a vertex buffer and a 16x16 image bound in it. */
static void
fixture_resources(void)
{
	size_t reply_bytes;
	int error;

	/* vkAllocateMemory of the whole storage, memory type 0. */
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

	/* vkCreateBuffer of 256 vertex-buffer bytes. */
	stub_put32(&fixture_wire, FIXTURE_CREATE_BUFFER);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 12U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 256U);
	stub_put32(&fixture_wire, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	stub_put32(&fixture_wire, VK_SHARING_MODE_EXCLUSIVE);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_BUFFER);

	/* vkCreateImage of a 16x16 R8G8B8A8_UNORM image. */
	stub_put32(&fixture_wire, FIXTURE_CREATE_IMAGE);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 14U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, VK_IMAGE_TYPE_2D);
	stub_put32(&fixture_wire, VK_FORMAT_R8G8B8A8_UNORM);
	stub_put32(&fixture_wire, 16U);
	stub_put32(&fixture_wire, 16U);
	stub_put32(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 1U);
	stub_put32(&fixture_wire, VK_SAMPLE_COUNT_1_BIT);
	stub_put32(&fixture_wire, VK_IMAGE_TILING_OPTIMAL);
	stub_put32(&fixture_wire, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	stub_put32(&fixture_wire, VK_SHARING_MODE_EXCLUSIVE);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, VK_IMAGE_LAYOUT_UNDEFINED);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_IMAGE);

	/* Binds the buffer at 0 and the image at 4096. */
	stub_put32(&fixture_wire, FIXTURE_BIND_BUFFER_MEMORY);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, FIXTURE_BUFFER);
	stub_put64(&fixture_wire, FIXTURE_MEMORY);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, FIXTURE_BIND_IMAGE_MEMORY);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, FIXTURE_IMAGE);
	stub_put64(&fixture_wire, FIXTURE_MEMORY);
	stub_put64(&fixture_wire, 4096U);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 3U * 24U + 2U * 8U);
	assert(stub_get32(stub_reply, 3U * 24U + 4U) == VK_SUCCESS);
	assert(stub_get32(stub_reply, 3U * 24U + 12U) == VK_SUCCESS);

	/* The blob libvulkan exports for the allocation becomes its storage. */
	error = drv_i915_render_blob_attach(stub_vk, FIXTURE_MEMORY, &fixture_storage_object);
	assert(error == 0);
}

/* Finds the first dword of a batch whose command opcode is `opcode`; -1 when none is. */
static int
fixture_find_command(
	const uint32_t *batch,
	unsigned used,
	uint32_t opcode)
{
	unsigned index;

	/* Looks at every dword's high half. */
	for (index = 0U; index < used; index++) {
		if ((batch[index] >> 16) == opcode)
			return (int)index;
	}

	/* No dword carries the opcode. */
	return -1;
}

/*
 * A draw ends its batch with the primitive: 3DPRIMITIVE with the vertex
 * and instance counts, a flush, and MI_BATCH_BUFFER_END.
 */
static void
test_emission(void)
{
	struct i915_gfx_batch batch;
	uint32_t commands[128];
	int primitive;

	/* Emits the primitive of a three-vertex, one-instance triangle list on a 64x32 target. */
	memset(commands, 0, sizeof(commands));
	batch.cmds = commands;
	batch.count = 0U;
	batch.capacity = 128U;
	batch.overflow = 0;
	drv_i915_gfx_emit_primitive(&batch, 64U, 32U, 4U, 3U, 0U, 1U, 0U);
	assert(batch.overflow == 0);

	/* The primitive carries the topology, the vertex count and the instance count. */
	primitive = fixture_find_command(commands, batch.count, GEN12_CMD_3DPRIMITIVE);
	assert(primitive >= 0);
	assert(commands[primitive + 1] == 4U);
	assert(commands[primitive + 2] == 3U);
	assert(commands[primitive + 4] == 1U);

	/* The drawing rectangle covers the target. */
	primitive = fixture_find_command(commands, batch.count, GEN12_CMD_3DSTATE_DRAWING_RECTANGLE);
	assert(primitive >= 0);
	assert(commands[primitive + 2] == ((64U - 1U) | ((32U - 1U) << 16)));

	/* The batch ends: MI_BATCH_BUFFER_END and one MI_NOOP behind it. */
	assert(commands[batch.count - 2U] == MI_BATCH_BUFFER_END);
	assert(commands[batch.count - 1U] == MI_NOOP);

	/*
	 * A batch too small for the primitive overflows instead of writing past
	 * its end, and still counts the dwords it would have needed.
	 */
	memset(commands, 0xee, sizeof(commands));
	batch.count = 0U;
	batch.capacity = 8U;
	batch.overflow = 0;
	drv_i915_gfx_emit_primitive(&batch, 64U, 32U, 4U, 3U, 0U, 1U, 0U);
	assert(batch.overflow != 0);
	assert(batch.count > 8U);
	assert(commands[8] == 0xeeeeeeeeU);
}

/*
 * A command buffer is allocated, recorded, submitted with a fence, and
 * freed through the wire; at submission its operations reach the GPU path
 * in recording order with what was bound.
 */
static void
test_lifecycle(void)
{
	struct i915_gfx_buffer *buffer;
	size_t reply_bytes;
	unsigned draws;
	unsigned rects;
	int error;

	/* Opens a session with the storage blob and makes the resources. */
	memset(&fixture_storage_object, 0, sizeof(fixture_storage_object));
	fixture_storage_object.slot = 7U;
	fixture_storage_object.bytes = sizeof(fixture_storage);
	fixture_storage_object.run.paddr = (hal_physaddr_t)(uintptr_t)fixture_storage;
	fixture_storage_object.va = FIXTURE_STORAGE_VA;
	stub_session_open(&fixture_storage_object);
	fixture_resources();
	buffer = drv_i915_object_lookup(stub_vk, I915_VK_OBJ_BUFFER, FIXTURE_BUFFER);
	assert(buffer != NULL);

	/* Publishes the stand-in pipeline the recording binds. */
	memset(&fixture_pipeline, 0, sizeof(fixture_pipeline));
	fixture_pipeline.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	error = drv_i915_object_insert(stub_vk, I915_VK_OBJ_PIPELINE, FIXTURE_PIPELINE, &fixture_pipeline);
	assert(error == 0);

	/* vkCreateCommandPool: [85][VK_SUCCESS][present][identity]. */
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
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 24U);
	assert(stub_get32(stub_reply, 4U) == VK_SUCCESS);
	assert(stub_get64(stub_reply, 16U) == FIXTURE_POOL);
	assert(drv_i915_object_lookup(stub_vk, I915_VK_OBJ_COMMAND_POOL, FIXTURE_POOL) != NULL);

	/* vkAllocateCommandBuffers of two primary buffers: [88][VK_SUCCESS][count][identities]. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_ALLOCATE_COMMAND_BUFFERS);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 40U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_POOL);
	stub_put32(&fixture_wire, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	stub_put32(&fixture_wire, 2U);
	stub_put64(&fixture_wire, 2U);
	stub_put64(&fixture_wire, FIXTURE_CB0);
	stub_put64(&fixture_wire, FIXTURE_CB1);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 32U);
	assert(stub_get32(stub_reply, 4U) == VK_SUCCESS);
	assert(stub_get64(stub_reply, 8U) == 2U);
	assert(stub_get64(stub_reply, 16U) == FIXTURE_CB0);
	assert(stub_get64(stub_reply, 24U) == FIXTURE_CB1);
	assert(drv_i915_object_lookup(stub_vk, I915_VK_OBJ_COMMAND_BUFFER, FIXTURE_CB0) != NULL);
	assert(drv_i915_object_lookup(stub_vk, I915_VK_OBJ_COMMAND_BUFFER, FIXTURE_CB1) != NULL);

	/* vkBeginCommandBuffer of cb0: [90][VK_SUCCESS]. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_BEGIN_COMMAND_BUFFER);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_CB0);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 42U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 8U);
	assert(stub_get32(stub_reply, 4U) == VK_SUCCESS);

	/*
	 * Records, with no replies: a clear of the image to four words, the
	 * pipeline, the vertex buffer at offset 64, eight bytes of push
	 * constants, then a three-vertex draw.
	 */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_CMD_CLEAR_COLOR_IMAGE);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_CB0);
	stub_put64(&fixture_wire, FIXTURE_IMAGE);
	stub_put32(&fixture_wire, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 4U);
	stub_put32(&fixture_wire, 0x11111111U);
	stub_put32(&fixture_wire, 0x22222222U);
	stub_put32(&fixture_wire, 0x33333333U);
	stub_put32(&fixture_wire, 0x44444444U);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, VK_IMAGE_ASPECT_COLOR_BIT);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 1U);
	stub_put32(&fixture_wire, FIXTURE_CMD_BIND_PIPELINE);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_CB0);
	stub_put32(&fixture_wire, VK_PIPELINE_BIND_POINT_GRAPHICS);
	stub_put64(&fixture_wire, FIXTURE_PIPELINE);
	stub_put32(&fixture_wire, FIXTURE_CMD_BIND_VERTEX_BUFFERS);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_CB0);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_BUFFER);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 64U);
	stub_put32(&fixture_wire, FIXTURE_CMD_PUSH_CONSTANTS);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_CB0);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, VK_SHADER_STAGE_VERTEX_BIT);
	stub_put32(&fixture_wire, 16U);
	stub_put32(&fixture_wire, 8U);
	stub_put64(&fixture_wire, 8U);
	stub_put32(&fixture_wire, 0x04030201U);
	stub_put32(&fixture_wire, 0x08070605U);
	fixture_draw(FIXTURE_CB0, 3U, 1U);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 0U);

	/* Recording runs nothing on the GPU. */
	assert(stub_draw_calls == 0U);
	assert(stub_rect_calls == 0U);

	/* vkEndCommandBuffer of cb0: [91][VK_SUCCESS]. */
	stub_wire_begin(&fixture_wire);
	fixture_command_buffer(FIXTURE_END_COMMAND_BUFFER, FIXTURE_CB0);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 8U);
	assert(stub_get32(stub_reply, 0U) == FIXTURE_END_COMMAND_BUFFER);
	assert(stub_get32(stub_reply, 4U) == VK_SUCCESS);

	/* vkCreateFence, unsignaled. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_CREATE_FENCE);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 8U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_FENCE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 24U);
	assert(fixture_fence_status() == VK_NOT_READY);

	/* vkQueueSubmit runs cb0 to its end before it replies: [18][VK_SUCCESS]. */
	stub_wire_begin(&fixture_wire);
	fixture_submit(FIXTURE_CB0, FIXTURE_FENCE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 8U);
	assert(stub_get32(stub_reply, 0U) == FIXTURE_QUEUE_SUBMIT);
	assert(stub_get32(stub_reply, 4U) == VK_SUCCESS);

	/* The clear ran as one GPU fill of the whole image, at the image's address in the storage. */
	assert(stub_rect_calls == 1U);
	assert(stub_last_rect.copy == 0);
	assert(stub_last_rect.dst.va == FIXTURE_STORAGE_VA + 4096U);
	assert(stub_last_rect.dst.width == 16U);
	assert(stub_last_rect.dst.pitch == 64U);
	assert(stub_last_rect.dst_rect.w == 16U);
	assert(stub_last_rect.dst_rect.h == 16U);
	assert(stub_last_rect.clear[0] == 0x11111111U);
	assert(stub_last_rect.clear[3] == 0x44444444U);

	/* The draw ran with the pipeline, the vertex buffer and the push constants that were bound. */
	assert(stub_draw_calls == 1U);
	assert(stub_last_draw.state.pipeline == &fixture_pipeline);
	assert(stub_last_draw.state.vertex[0].buffer == buffer);
	assert(stub_last_draw.state.vertex[0].offset == 64U);
	assert(stub_last_draw.state.push[16] == 0x01U);
	assert(stub_last_draw.state.push[23] == 0x08U);
	assert(stub_last_draw.vertex_count == 3U);
	assert(stub_last_draw.instance_count == 1U);
	assert(stub_last_draw.first_vertex == 0U);

	/* Everything the submission asked for is done, so its fence is signalled. */
	assert(fixture_fence_status() == VK_SUCCESS);

	/* vkResetFences returns the fence to unsignaled: [37][VK_SUCCESS]. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_RESET_FENCES);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_FENCE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 8U);
	assert(fixture_fence_status() == VK_NOT_READY);

	/*
	 * A draw that fails on the GPU stops the command buffer: the submission
	 * reports a lost device and the fence stays unsignaled.
	 */
	stub_draw_result = EIO;
	stub_wire_begin(&fixture_wire);
	fixture_submit(FIXTURE_CB0, FIXTURE_FENCE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	stub_draw_result = 0;
	assert(reply_bytes == 8U);
	assert(stub_get32(stub_reply, 4U) == (uint32_t)VK_ERROR_DEVICE_LOST);
	assert(strstr(stub_log, "command buffer stopped at operation 5 of 5") != NULL);
	assert(stub_draw_calls == 2U);
	assert(stub_rect_calls == 2U);
	assert(fixture_fence_status() == VK_NOT_READY);

	/* vkResetCommandPool empties every recording: a submission then runs nothing. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_RESET_COMMAND_POOL);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, FIXTURE_POOL);
	stub_put32(&fixture_wire, 0U);
	fixture_submit(FIXTURE_CB0, FIXTURE_FENCE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 16U);
	assert(stub_get32(stub_reply, 4U) == VK_SUCCESS);
	assert(stub_get32(stub_reply, 12U) == VK_SUCCESS);
	draws = stub_draw_calls;
	rects = stub_rect_calls;
	assert(draws == 2U);
	assert(rects == 2U);
	assert(fixture_fence_status() == VK_SUCCESS);

	/* vkFreeCommandBuffers releases both buffers. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_FREE_COMMAND_BUFFERS);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, FIXTURE_POOL);
	stub_put32(&fixture_wire, 2U);
	stub_put64(&fixture_wire, 2U);
	stub_put64(&fixture_wire, FIXTURE_CB0);
	stub_put64(&fixture_wire, FIXTURE_CB1);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 4U);
	assert(drv_i915_object_lookup(stub_vk, I915_VK_OBJ_COMMAND_BUFFER, FIXTURE_CB0) == NULL);
	assert(drv_i915_object_lookup(stub_vk, I915_VK_OBJ_COMMAND_BUFFER, FIXTURE_CB1) == NULL);

	/* Destroys the pool, the fence and the resources, and withdraws the pipeline. */
	stub_wire_begin(&fixture_wire);
	fixture_destroy(FIXTURE_DESTROY_COMMAND_POOL, FIXTURE_POOL);
	fixture_destroy(FIXTURE_DESTROY_FENCE, FIXTURE_FENCE);
	fixture_destroy(FIXTURE_DESTROY_IMAGE, FIXTURE_IMAGE);
	fixture_destroy(FIXTURE_DESTROY_BUFFER, FIXTURE_BUFFER);
	fixture_destroy(FIXTURE_FREE_MEMORY, FIXTURE_MEMORY);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 5U * 4U);
	assert(drv_i915_object_lookup(stub_vk, I915_VK_OBJ_COMMAND_POOL, FIXTURE_POOL) == NULL);
	drv_i915_object_remove(stub_vk, I915_VK_OBJ_PIPELINE, FIXTURE_PIPELINE);

	/* Closes the session: the draw state is released once and nothing stays allocated. */
	stub_session_close();
	assert(stub_session_closes == 1U);
	assert(stub_live == 0U);
}

/*
 * A recording that does not fit fails its end; an unimplemented vkCmd*
 * fails the stream; destroying a pool frees the buffers still in it.
 */
static void
test_recording_limits(void)
{
	size_t reply_bytes;
	unsigned index;
	int error;

	/* Opens a session with a pool and one command buffer. */
	stub_session_open(NULL);
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
	stub_put64(&fixture_wire, FIXTURE_CB0);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 24U + 24U);

	/* A secondary command buffer is refused by name as a missing feature. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_ALLOCATE_COMMAND_BUFFERS);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 40U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_POOL);
	stub_put32(&fixture_wire, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_CB1);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 8U);
	assert(stub_get32(stub_reply, 4U) == (uint32_t)VK_ERROR_INITIALIZATION_FAILED);
	assert(strstr(stub_log, "secondary command buffers are not implemented") != NULL);
	assert(drv_i915_object_lookup(stub_vk, I915_VK_OBJ_COMMAND_BUFFER, FIXTURE_CB1) == NULL);

	/* One operation more than a buffer holds: the end reports that the recording did not fit. */
	stub_wire_begin(&fixture_wire);
	for (index = 0U; index < FIXTURE_MAX_OPS + 1U; index++)
		fixture_draw(FIXTURE_CB0, 3U, 1U);
	fixture_command_buffer(FIXTURE_END_COMMAND_BUFFER, FIXTURE_CB0);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 8U);
	assert(stub_get32(stub_reply, 4U) == (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY);
	assert(strstr(stub_log, "holds more than 64 operations") != NULL);

	/* An unimplemented recording fails the stream: nothing after it runs and no reply is published. */
	stub_wire_begin(&fixture_wire);
	stub_put32(&fixture_wire, FIXTURE_CMD_DRAW_INDEXED);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, FIXTURE_CB0);
	stub_put32(&fixture_wire, 3U);
	stub_put32(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	fixture_command_buffer(FIXTURE_END_COMMAND_BUFFER, FIXTURE_CB0);
	error = stub_execute(&fixture_wire, &reply_bytes);
	assert(error == ENOTSUP);
	assert(reply_bytes == STUB_REPLY_BYTES);
	assert(strcmp(stub_log, "i915: vk: XXX unimplemented opcode 107 (recording)\n") == 0);

	/* vkDestroyCommandPool frees the buffer still allocated from it. */
	stub_wire_begin(&fixture_wire);
	fixture_destroy(FIXTURE_DESTROY_COMMAND_POOL, FIXTURE_POOL);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 4U);
	assert(drv_i915_object_lookup(stub_vk, I915_VK_OBJ_COMMAND_BUFFER, FIXTURE_CB0) == NULL);

	/* Closes the session; nothing stays allocated. */
	stub_session_close();
	assert(stub_live == 0U);
}
