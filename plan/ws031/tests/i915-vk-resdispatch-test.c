/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the resource wire decode (p011 increment A0).  Encodes the
 * libvulkan Venus stream for vkAllocateMemory and vkFreeMemory exactly as
 * userland/base/libvulkan/memory.c does, drives it through the real command
 * router and resource dispatch against the WS029 GEM allocator, and checks the
 * object table and the reply framing (opcode echo, VkResult, output identity).
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
#include "../../../src/drivers/gpu/i915/vk/res.c"

static int fixture_pci_token;
#define fixture_pci_device	((struct drv_pci_device *)&fixture_pci_token)

/* The modules outside res are not exercised here; routing never reaches them. */
int
i915_vk_pipe_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)op; (void)r; (void)w; return EINVAL;
}

int
i915_vk_cmdbuf_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)op; (void)r; (void)w; return EINVAL;
}

int
i915_vk_sync_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)op; (void)r; (void)w; return EINVAL;
}

int
i915_vk_wsi_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *w)
{
	(void)s; (void)op; (void)r; (void)w; return EINVAL;
}

/* A little-endian encoder that mirrors the libvulkan wire writer. */
static uint8_t wire[512];
static size_t wire_len;

static void
w32(uint32_t value)
{
	wire[wire_len++] = (uint8_t)value;
	wire[wire_len++] = (uint8_t)(value >> 8);
	wire[wire_len++] = (uint8_t)(value >> 16);
	wire[wire_len++] = (uint8_t)(value >> 24);
}

static void
w64(uint64_t value)
{
	w32((uint32_t)value);
	w32((uint32_t)(value >> 32));
}

static uint32_t
rd32(const uint8_t *b, size_t off)
{
	return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
	       ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

static uint64_t
rd64(const uint8_t *b, size_t off)
{
	return (uint64_t)rd32(b, off) | ((uint64_t)rd32(b, off + 4) << 32);
}

/* Registers the driver, attaches the fake device and publishes the node. */
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

static void
test_allocate_free(void)
{
	struct i915_device *device;
	struct i915_vk_device vk;
	struct i915_vk_session session;
	struct i915_vk_reader reader;
	struct i915_vk_writer writer;
	void *gpu_session;
	uint8_t reply[64];
	const uint64_t handle = 0x123456789aULL;
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

	/* Encode vkAllocateMemory exactly as libvulkan memory.c does. */
	wire_len = 0;
	w32(21U);		/* opcode vkAllocateMemory */
	w32(1U);		/* reply-request flag */
	w64(0xD0U);		/* device wire id */
	w64(1U);		/* pAllocateInfo present */
	w32(5U);		/* VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO */
	w64(0U);		/* extension present */
	w64(65536U);		/* allocationSize */
	w32(0U);		/* memoryTypeIndex */
	w64(0U);		/* pAllocator present */
	w64(1U);		/* pMemory present */
	w64(handle);		/* memory wire id */

	reader.base = wire;
	reader.size = wire_len;
	reader.offset = 0U;
	reader.error = 0;
	writer.base = reply;
	writer.size = sizeof(reply);
	writer.offset = 0U;
	writer.error = 0;

	error = i915_vk_cmd_dispatch(&session, &reader, &writer);
	assert(error == 0);
	assert(writer.error == 0);

	/* Reply: [opcode 21][result 0][present 1][identifier handle] = 24 bytes. */
	assert(writer.offset == 24U);
	assert(rd32(reply, 0U) == 21U);
	assert(rd32(reply, 4U) == 0U);
	assert(rd64(reply, 8U) == 1U);
	assert(rd64(reply, 16U) == handle);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_MEMORY, handle) != NULL);

	/* Encode vkFreeMemory. */
	wire_len = 0;
	w32(22U);		/* opcode vkFreeMemory */
	w32(1U);		/* reply-request flag */
	w64(0xD0U);		/* device wire id */
	w64(handle);		/* memory wire id */
	w64(0U);		/* pAllocator present */

	reader.base = wire;
	reader.size = wire_len;
	reader.offset = 0U;
	reader.error = 0;
	writer.offset = 0U;
	writer.error = 0;

	error = i915_vk_cmd_dispatch(&session, &reader, &writer);
	assert(error == 0);
	assert(writer.error == 0);

	/* vkFreeMemory returns void: the reply is the echoed opcode alone. */
	assert(writer.offset == 4U);
	assert(rd32(reply, 0U) == 22U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_MEMORY, handle) == NULL);

	i915_vk_object_table_destroy(vk.objects);
	fixture_gpu_ops->close(device, gpu_session);
	printf("res-dispatch: allocate/free round-trip ok\n");
}

int
main(void)
{
	test_allocate_free();
	printf("WS031 res dispatch fixture PASS\n");
	return 0;
}
