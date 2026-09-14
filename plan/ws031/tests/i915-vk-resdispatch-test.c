/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the resource wire decode (p011 increment A).  Encodes the
 * libvulkan Venus streams for vkAllocateMemory/vkFreeMemory, vkCreateBuffer/
 * vkBindBufferMemory/vkDestroyBuffer and vkCreateImage/vkBindImageMemory/
 * vkDestroyImage exactly as userland/base/libvulkan does, drives them through
 * the real command router and resource dispatch against the WS029 GEM
 * allocator, and checks the object table and the reply framing (opcode echo,
 * VkResult, output identity).
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

/* Drives one encoded command through the router and returns the reply length. */
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

/*
 * Exercises the whole resource decode under a single device attach, so the one
 * device the fixture never detaches stays reachable at exit rather than leaking.
 */
static void
test_resources(void)
{
	struct i915_device *device;
	struct i915_vk_device vk;
	struct i915_vk_session session;
	void *gpu_session;
	uint8_t reply[64];
	const uint64_t solo = 0x123456789aULL;
	const uint64_t mem = 0x100ULL;
	const uint64_t buf = 0x200ULL;
	const uint64_t img = 0x300ULL;
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

	/* vkAllocateMemory, exactly as libvulkan memory.c encodes it. */
	wire_len = 0;
	w32(21U); w32(1U);		/* opcode, reply flag */
	w64(0xD0U);			/* device wire id */
	w64(1U);			/* pAllocateInfo present */
	w32(5U);			/* VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO */
	w64(0U);			/* extension present */
	w64(65536U);			/* allocationSize */
	w32(0U);			/* memoryTypeIndex */
	w64(0U);			/* pAllocator present */
	w64(1U);			/* pMemory present */
	w64(solo);			/* memory wire id */
	assert(run_command(&session, reply, sizeof(reply)) == 24U);
	assert(rd32(reply, 0U) == 21U);
	assert(rd32(reply, 4U) == 0U);
	assert(rd64(reply, 8U) == 1U);
	assert(rd64(reply, 16U) == solo);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_MEMORY, solo) != NULL);

	/* vkFreeMemory returns void: the reply is the echoed opcode alone. */
	wire_len = 0;
	w32(22U); w32(1U); w64(0xD0U); w64(solo); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	assert(rd32(reply, 0U) == 22U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_MEMORY, solo) == NULL);

	/* Memory to bind the buffer and image into. */
	wire_len = 0;
	w32(21U); w32(1U); w64(0xD0U); w64(1U); w32(5U); w64(0U);
	w64(65536U); w32(0U); w64(0U); w64(1U); w64(mem);
	assert(run_command(&session, reply, sizeof(reply)) == 24U);

	/* vkCreateBuffer with a VkBufferCreateInfo of size 4096, no queue families. */
	wire_len = 0;
	w32(50U); w32(1U);		/* opcode, reply flag */
	w64(0xD0U);			/* device */
	w64(1U);			/* pCreateInfo present */
	w32(12U);			/* VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO */
	w64(0U);			/* external absent */
	w32(0U);			/* flags */
	w64(4096U);			/* size */
	w32(0x80U);			/* usage (vertex buffer) */
	w32(0U);			/* sharingMode */
	w32(0U);			/* queueFamilyIndexCount */
	w64(0U);			/* payload count */
	w64(0U);			/* pAllocator present */
	w64(1U);			/* pBuffer present */
	w64(buf);			/* buffer wire id */
	assert(run_command(&session, reply, sizeof(reply)) == 24U);
	assert(rd32(reply, 0U) == 50U);
	assert(rd32(reply, 4U) == 0U);
	assert(rd64(reply, 16U) == buf);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_BUFFER, buf) != NULL);

	/* vkBindBufferMemory binds the buffer to the memory at offset 0. */
	wire_len = 0;
	w32(28U); w32(1U); w64(0xD0U); w64(buf); w64(mem); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 8U);
	assert(rd32(reply, 0U) == 28U);
	assert(rd32(reply, 4U) == 0U);	/* VK_SUCCESS */

	/* vkCreateImage with a 320x240 R8G8B8A8_UNORM linear image. */
	wire_len = 0;
	w32(54U); w32(1U);		/* opcode, reply flag */
	w64(0xD0U);			/* device */
	w64(1U);			/* pCreateInfo present */
	w32(14U);			/* VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO */
	w64(0U);			/* external absent */
	w32(0U);			/* flags */
	w32(1U);			/* imageType 2D */
	w32(199U);			/* format R8G8B8A8_UNORM */
	w32(320U); w32(240U); w32(1U);	/* extent w,h,d */
	w32(1U);			/* mipLevels */
	w32(1U);			/* arrayLayers */
	w32(1U);			/* samples */
	w32(0U);			/* tiling linear */
	w32(0x10U);			/* usage (color attachment) */
	w32(0U);			/* sharingMode */
	w32(0U);			/* queueFamilyIndexCount */
	w64(0U);			/* payload count */
	w32(0U);			/* initialLayout */
	w64(0U);			/* pAllocator present */
	w64(1U);			/* pImage present */
	w64(img);			/* image wire id */
	assert(run_command(&session, reply, sizeof(reply)) == 24U);
	assert(rd32(reply, 0U) == 54U);
	assert(rd64(reply, 16U) == img);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_IMAGE, img) != NULL);

	/* vkBindImageMemory binds the image to the memory at offset 0. */
	wire_len = 0;
	w32(29U); w32(1U); w64(0xD0U); w64(img); w64(mem); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 8U);
	assert(rd32(reply, 4U) == 0U);

	/* Destroy image and buffer, then free memory; each leaves the table. */
	wire_len = 0;
	w32(55U); w32(1U); w64(0xD0U); w64(img); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_IMAGE, img) == NULL);

	wire_len = 0;
	w32(51U); w32(1U); w64(0xD0U); w64(buf); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_BUFFER, buf) == NULL);

	wire_len = 0;
	w32(22U); w32(1U); w64(0xD0U); w64(mem); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_MEMORY, mem) == NULL);

	i915_vk_object_table_destroy(vk.objects);
	fixture_gpu_ops->close(device, gpu_session);
	printf("res-dispatch: memory/buffer/image create/bind/destroy ok\n");
}

int
main(void)
{
	test_resources();
	printf("WS031 res dispatch fixture PASS\n");
	return 0;
}
