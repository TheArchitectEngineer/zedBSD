/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for command buffer recording (p008) and the command-buffer
 * object lifecycle wire decode (p011 increment B).  test_recording checks the
 * recorded batch carries the pipeline state, the 3DPRIMITIVE and the
 * terminating MI_BATCH_BUFFER_END.  test_lifecycle drives vkCreateCommandPool,
 * vkAllocateCommandBuffers, vkBeginCommandBuffer, vkFreeCommandBuffers and
 * vkDestroyCommandPool through the router against the WS029 GEM allocator.
 */

#include "../../ws029/tests/i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"
#include "../../../src/drivers/gpu/i915/ggtt.c"
#include "../../../src/drivers/gpu/i915/ppgtt.c"
#include "../../../src/drivers/gpu/i915/gem.c"
#include "../../../src/drivers/gpu/i915/irq.c"
#include "../../../src/drivers/gpu/i915/engine.c"
#include "../../../src/drivers/gpu/i915/lrc.c"
#include "../../../src/drivers/gpu/i915/request.c"
#include "../../../src/drivers/gpu/i915/i915.c"
#include "../../../src/drivers/gpu/i915/vk/cmd.c"
#include "../../../src/drivers/gpu/i915/vk/pipe.c"
#include "../../../src/drivers/gpu/i915/vk/cmdbuf.c"

static int fixture_pci_token;
#define fixture_pci_device	((struct drv_pci_device *)&fixture_pci_token)

/* The modules outside pipe/cmdbuf are not exercised here. */
int
i915_vk_res_dispatch(struct i915_vk_session *s, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)s; (void)o; (void)r; (void)w; return EINVAL; }
int
i915_vk_sync_dispatch(struct i915_vk_session *s, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)s; (void)o; (void)r; (void)w; return EINVAL; }
int
i915_vk_wsi_dispatch(struct i915_vk_session *s, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)s; (void)o; (void)r; (void)w; return EINVAL; }

/* A little-endian encoder mirroring the libvulkan wire writer. */
static uint8_t wire[512];
static size_t wire_len;
static void w32(uint32_t v) { wire[wire_len++] = (uint8_t)v; wire[wire_len++] = (uint8_t)(v >> 8); wire[wire_len++] = (uint8_t)(v >> 16); wire[wire_len++] = (uint8_t)(v >> 24); }
static void w64(uint64_t v) { w32((uint32_t)v); w32((uint32_t)(v >> 32)); }
static uint32_t rd32(const uint8_t *b, size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }
static uint64_t rd64(const uint8_t *b, size_t o) { return (uint64_t)rd32(b, o) | ((uint64_t)rd32(b, o + 4) << 32); }

static int
find_command(const uint32_t *batch, uint32_t used, uint32_t opcode)
{
	uint32_t index;

	for (index = 0U; index < used; index++) {
		if ((batch[index] >> 16) == opcode)
			return (int)index;
	}
	return -1;
}

static struct i915_device *
attach(void)
{
	struct i915_device *device;
	int error;

	error = drv_i915_pci_driver_register();
	assert(error == 0);
	error = fixture_driver->attach(fixture_pci_device, &fixture_driver->ids[5]);
	assert(error == 0);
	device = fixture_driver_data;
	assert(device != NULL);
	error = fixture_service->publish(fixture_pci_device, fixture_service_argument);
	assert(error == 0);
	return device;
}

static size_t
run_command(struct i915_vk_session *session, uint8_t *reply, size_t reply_size)
{
	struct i915_vk_reader reader;
	struct i915_vk_writer writer;
	int error;

	reader.base = wire;
	reader.size = wire_len;
	reader.offset = 0U;
	reader.error = 0;
	writer.base = reply;
	writer.size = reply_size;
	writer.offset = 0U;
	writer.error = 0;
	error = i915_vk_cmd_dispatch(session, &reader, &writer);
	assert(error == 0);
	assert(writer.error == 0);
	return writer.offset;
}

/* Records a draw into a local batch and checks the emitted GEN command stream. */
static void
test_recording(void)
{
	uint32_t buffer[512];
	struct i915_vk_cmdbuf cmdbuf;
	struct i915_vk_shader_binary vs;
	struct i915_vk_shader_binary fs;
	struct i915_vk_pipeline_info info;
	struct i915_vk_pipeline *pipeline;
	int prim_at;
	int error;

	memset(buffer, 0, sizeof(buffer));
	memset(&vs, 0, sizeof(vs));
	memset(&fs, 0, sizeof(fs));
	vs.grf_used = 24U;
	fs.grf_used = 20U;
	memset(&info, 0, sizeof(info));
	info.vs = &vs;
	info.fs = &fs;
	info.vs_kernel = 0x00100000ULL;
	info.fs_kernel = 0x00200000ULL;
	error = i915_vk_pipeline_create(NULL, &info, &pipeline);
	assert(error == 0);

	memset(&cmdbuf, 0, sizeof(cmdbuf));
	cmdbuf.batch.map = buffer;
	cmdbuf.batch.capacity = 512U;

	error = i915_vk_cmdbuf_begin(&cmdbuf);
	assert(error == 0);

	/* A draw without a pipeline is refused. */
	error = i915_vk_cmd_draw(&cmdbuf, 3U, 1U, 0U, 0U);
	assert(error == EINVAL);

	error = i915_vk_cmd_bind_pipeline(&cmdbuf, pipeline);
	assert(error == 0);
	error = i915_vk_cmd_draw(&cmdbuf, 3U, 1U, 0U, 0U);
	assert(error == 0);
	error = i915_vk_cmdbuf_end(&cmdbuf);
	assert(error == 0);

	assert(find_command(buffer, cmdbuf.batch.cursor, GEN12_CMD_3DSTATE_VS) >= 0);
	assert(find_command(buffer, cmdbuf.batch.cursor, GEN12_CMD_3DSTATE_PS) >= 0);
	prim_at = find_command(buffer, cmdbuf.batch.cursor, GEN12_CMD_3DPRIMITIVE);
	assert(prim_at >= 0);
	assert(buffer[prim_at + 2] == 3U);	/* vertex count */
	assert(buffer[prim_at + 4] == 1U);	/* instance count */
	assert(buffer[cmdbuf.batch.cursor - 1U] == GEN12_MI_BATCH_BUFFER_END);

	i915_vk_pipeline_destroy(pipeline);
	printf("i915 vk cmdbuf recording PASS\n");
}

/* Drives the command-buffer object lifecycle through the wire router. */
static void
test_lifecycle(void)
{
	struct i915_device *device;
	struct i915_vk_device vk;
	struct i915_vk_session session;
	struct i915_vk_cmdbuf *cb;
	void *gpu_session;
	uint8_t reply[64];
	const uint64_t pool = 0x900ULL;
	const uint64_t cb0 = 0xA00ULL;
	const uint64_t cb1 = 0xA01ULL;
	int error;

	fixture_reset();
	device = attach();
	error = fixture_gpu_ops->open(device, &gpu_session);
	assert(error == 0);

	memset(&vk, 0, sizeof(vk));
	vk.i915 = device;
	error = i915_vk_object_table_create(&vk.objects);
	assert(error == 0);
	memset(&session, 0, sizeof(session));
	session.vk = &vk;
	session.gpu = gpu_session;

	/* vkCreateCommandPool. */
	wire_len = 0;
	w32(85U); w32(1U); w64(0xD0U); w64(1U); w32(39U); w64(0U); w32(0U); w32(0U);
	w64(0U); w64(1U); w64(pool);
	assert(run_command(&session, reply, sizeof(reply)) == 24U);
	assert(rd32(reply, 0U) == 85U);
	assert(rd32(reply, 4U) == 0U);
	assert(rd64(reply, 16U) == pool);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_COMMAND_POOL, pool) != NULL);

	/* vkAllocateCommandBuffers for two primary buffers. */
	wire_len = 0;
	w32(88U); w32(1U);		/* opcode, reply flag */
	w64(0xD0U);			/* device */
	w64(1U);			/* pAllocateInfo present */
	w32(40U);			/* VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO */
	w64(0U);			/* pNext present */
	w64(pool);			/* commandPool */
	w32(0U);			/* level PRIMARY */
	w32(2U);			/* commandBufferCount */
	w64(2U);			/* payload count */
	w64(cb0); w64(cb1);		/* buffer wire ids */
	assert(run_command(&session, reply, sizeof(reply)) == 32U);	/* op+res+count+2*id */
	assert(rd32(reply, 0U) == 88U);
	assert(rd32(reply, 4U) == 0U);
	assert(rd64(reply, 8U) == 2U);
	assert(rd64(reply, 16U) == cb0);
	assert(rd64(reply, 24U) == cb1);
	cb = i915_vk_obj_lookup(&vk, I915_VK_OBJ_COMMAND_BUFFER, cb0);
	assert(cb != NULL);
	assert(cb->batch.map != NULL && cb->batch.capacity > 0U);

	/* vkBeginCommandBuffer resets the recording state. */
	wire_len = 0;
	w32(90U); w32(1U); w64(cb0); w64(1U); w32(42U); w64(0U); w32(0U); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 8U);
	assert(rd32(reply, 0U) == 90U);
	assert(rd32(reply, 4U) == 0U);

	/* vkFreeCommandBuffers releases both buffers. */
	wire_len = 0;
	w32(89U); w32(1U); w64(0xD0U); w64(pool); w32(2U); w64(2U); w64(cb0); w64(cb1);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_COMMAND_BUFFER, cb0) == NULL);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_COMMAND_BUFFER, cb1) == NULL);

	/* vkDestroyCommandPool releases the pool. */
	wire_len = 0;
	w32(86U); w32(1U); w64(0xD0U); w64(pool); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_COMMAND_POOL, pool) == NULL);

	i915_vk_object_table_destroy(vk.objects);
	fixture_gpu_ops->close(device, gpu_session);
	printf("i915 vk cmdbuf lifecycle PASS\n");
}

int
main(void)
{
	test_recording();
	test_lifecycle();
	printf("i915 vk cmdbuf host test PASS\n");
	return 0;
}
