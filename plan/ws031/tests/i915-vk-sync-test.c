/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the synchronization module (p009). Exercises fence arming
 * against the engine seqno, status, the polling wait and reset, plus semaphore
 * and query lifetime, against the WS029 device.
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
#include "../../../src/drivers/gpu/i915/vk/sync.c"

/* The other modules are not exercised here; routing never reaches them. */
int
i915_vk_res_dispatch(struct i915_vk_session *x, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)x; (void)o; (void)r; (void)w; return EINVAL; }
int
i915_vk_pipe_dispatch(struct i915_vk_session *x, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)x; (void)o; (void)r; (void)w; return EINVAL; }
int
i915_vk_cmdbuf_dispatch(struct i915_vk_session *x, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)x; (void)o; (void)r; (void)w; return EINVAL; }
int
i915_vk_wsi_dispatch(struct i915_vk_session *x, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)x; (void)o; (void)r; (void)w; return EINVAL; }

/* A little-endian encoder mirroring the libvulkan wire writer. */
static uint8_t wire[256];
static size_t wire_len;
static void w32(uint32_t v) { wire[wire_len++] = (uint8_t)v; wire[wire_len++] = (uint8_t)(v >> 8); wire[wire_len++] = (uint8_t)(v >> 16); wire[wire_len++] = (uint8_t)(v >> 24); }
static void w64(uint64_t v) { w32((uint32_t)v); w32((uint32_t)(v >> 32)); }
static uint32_t rd32(const uint8_t *b, size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }
static size_t run_command(struct i915_vk_session *session, uint8_t *reply, size_t reply_size)
{
	struct i915_vk_reader reader;
	struct i915_vk_writer writer;
	int error;
	reader.base = wire; reader.size = wire_len; reader.offset = 0U; reader.error = 0;
	writer.base = reply; writer.size = reply_size; writer.offset = 0U; writer.error = 0;
	error = i915_vk_cmd_dispatch(session, &reader, &writer);
	assert(error == 0);
	assert(writer.error == 0);
	return writer.offset;
}

static int fixture_pci_token;
#define fixture_pci_device	((struct drv_pci_device *)&fixture_pci_token)

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
	error = fixture_service->publish(fixture_pci_device, fixture_service_argument);
	assert(error == 0);
	return device;
}

static void
test_fence(void)
{
	struct i915_device *device;
	struct i915_vk_device vk;
	struct i915_vk_session session;
	struct i915_vk_fence *fence;
	struct i915_vk_semaphore *semaphore;
	struct i915_vk_query_pool *pool;
	void *gpu_session;
	uint8_t reply[64];
	const uint64_t fh = 0x55ULL;
	int error;

	fixture_reset();
	device = attach();
	error = fixture_gpu_ops->open(device, &gpu_session);
	assert(error == 0);
	memset(&vk, 0, sizeof(vk));
	vk.i915 = device;
	memset(&session, 0, sizeof(session));
	session.vk = &vk;
	session.gpu = gpu_session;

	/* A fence created signaled reports success immediately. */
	error = i915_vk_fence_create(&session, 1, &fence);
	assert(error == 0);
	assert(i915_vk_fence_status(fence) == 0);
	i915_vk_fence_destroy(fence);

	/* An unsignaled, unarmed fence is never ready. */
	error = i915_vk_fence_create(&session, 0, &fence);
	assert(error == 0);
	assert(i915_vk_fence_status(fence) == EBUSY);

	/* Armed for seqno 5, it stays busy until the engine passes that seqno. */
	error = i915_vk_fence_arm(fence, 0U, 5U);
	assert(error == 0);
	device->engines[0].completed_seqno = 3U;
	assert(i915_vk_fence_status(fence) == EBUSY);
	assert(i915_vk_fence_wait(fence, 0U) == ETIMEDOUT);

	/* Once the engine retires past seqno 5 the fence signals and a wait returns. */
	device->engines[0].completed_seqno = 5U;
	assert(i915_vk_fence_status(fence) == 0);
	assert(i915_vk_fence_wait(fence, 1000U) == 0);

	/* Reset returns it to the unsignaled, unarmed state. */
	error = i915_vk_fence_reset(fence);
	assert(error == 0);
	assert(i915_vk_fence_status(fence) == EBUSY);
	i915_vk_fence_destroy(fence);

	/* Semaphores and query pools have a simple lifetime. */
	error = i915_vk_semaphore_create(&session, &semaphore);
	assert(error == 0);
	i915_vk_semaphore_destroy(semaphore);
	error = i915_vk_query_pool_create(&session, 1U, 4U, &pool);
	assert(error == 0);
	i915_vk_query_pool_destroy(pool);

	/* The wire decode: create a fence, read its status, then destroy it. */
	error = i915_vk_object_table_create(&vk.objects);
	assert(error == 0);

	/* vkCreateFence, unsignaled: [device][present][sType 8][pNext][flags 0]
	 * [pAllocator][pFence][fence]. */
	wire_len = 0;
	w32(35U); w32(1U); w64(0xD0U); w64(1U); w32(8U); w64(0U); w32(0U);
	w64(0U); w64(1U); w64(fh);
	assert(run_command(&session, reply, sizeof(reply)) == 24U);
	assert(rd32(reply, 0U) == 35U);
	assert(rd32(reply, 4U) == 0U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_FENCE, fh) != NULL);

	/* vkGetFenceStatus on an unarmed fence reports VK_NOT_READY (1). */
	wire_len = 0;
	w32(38U); w32(1U); w64(0xD0U); w64(fh);
	assert(run_command(&session, reply, sizeof(reply)) == 8U);
	assert(rd32(reply, 0U) == 38U);
	assert(rd32(reply, 4U) == 1U);

	/* vkDestroyFence removes it from the table. */
	wire_len = 0;
	w32(36U); w32(1U); w64(0xD0U); w64(fh); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_FENCE, fh) == NULL);

	i915_vk_object_table_destroy(vk.objects);
	fixture_gpu_ops->close(device, gpu_session);
}

int
main(void)
{
	test_fence();
	printf("i915 vk sync host test PASS\n");
	return 0;
}
